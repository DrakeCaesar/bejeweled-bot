#include "application.h"
#include "assets/frame_processing.h"
#include "assets/game_asset_extractor.h"
#include "assets/loader.h"
#include "bot/board_analysis.h"
#include "bot/board_debug.h"
#include "capture/capture_thread.h"
#include "capture/window_capture.h"
#include "config.h"
#include "detection/color_similarity.h"
#include "detection/gem_detector.h"
#include "detection/gradient_classifier.h"
#include "detection/process_thread.h"
#include "hook/memory_hook.h"
#include "rendering/control_panel.h"
#include "rendering/overlay.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {
// Recording UI controls r(trackbars)
int g_recordColTrack = 0;
int g_recordRowTrack = 0;
HWND g_gameWindow = nullptr;
bool g_mouseDownSent = false;

void SendMouseClickToGame(HWND gameWindow, int x, int y, bool isDown) {
  if (!gameWindow || !IsWindow(gameWindow))
    return;

  // Convert overlay coordinates to game window coordinates
  // Remove banner offset and add game board offset within the window
  int gameX = x + Config::BOARD_X;
  int gameY = y + Config::BOARD_Y;

  // Send mouse move event
  LPARAM lParam = MAKELPARAM(gameX, gameY);
  PostMessage(gameWindow, WM_MOUSEMOVE, 0, lParam);

  // Send mouse down/up
  if (isDown) {
    PostMessage(gameWindow, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
  } else {
    PostMessage(gameWindow, WM_LBUTTONUP, 0, lParam);
  }
}

void OnMouse(int event, int x, int y, int flags, void *) {
  if (HandleControlPanelMouse(event, x, y, flags))
    return;
  if (y < Config::BANNER_HEIGHT)
    return;

  // Calculate grid position
  int col = x / Config::CELL_SIZE;
  int row = (y - Config::BANNER_HEIGHT) / Config::CELL_SIZE;

  // Handle left button down - select cell and pass click to game (only once)
  if (event == cv::EVENT_LBUTTONDOWN) {
    if (col >= 0 && col < Config::GRID_SIZE && row >= 0 &&
        row < Config::GRID_SIZE) {
      g_recordColTrack = col;
      g_recordRowTrack = row;
      SetSelectedCell(col, row);
    }

    // Pass click to game (use adjusted coordinates - remove banner offset)
    int gameY = y - Config::BANNER_HEIGHT;
    SendMouseClickToGame(g_gameWindow, x, gameY, true);
    g_mouseDownSent = true;
  }
  // Handle mouse move - pass drag to game (only send move, not down)
  else if (event == cv::EVENT_MOUSEMOVE && (flags & cv::EVENT_FLAG_LBUTTON)) {
    int gameY = y - Config::BANNER_HEIGHT;
    // Only send move event, not another down event
    if (g_gameWindow && IsWindow(g_gameWindow)) {
      int gameX = x + Config::BOARD_X;
      int adjustedGameY = gameY + Config::BOARD_Y;
      LPARAM lParam = MAKELPARAM(gameX, adjustedGameY);
      PostMessage(g_gameWindow, WM_MOUSEMOVE, MK_LBUTTON, lParam);
    }
  }
  // Handle left button up - release mouse on game
  else if (event == cv::EVENT_LBUTTONUP) {
    int gameY = y - Config::BANNER_HEIGHT;
    if (g_mouseDownSent) {
      SendMouseClickToGame(g_gameWindow, x, gameY, false);
      g_mouseDownSent = false;
    }
  }
}

// Map the game's internal color code (0..6) to a GemType, in the actual sprite
// order confirmed visually: 0=RED 1=WHITE 2=GREEN 3=YELLOW 4=PURPLE 5=ORANGE
// 6=BLUE.
GemType HookColorToGemType(int v) {
  static const GemType map[7] = {
      GemType::RED,    // 0
      GemType::WHITE,  // 1
      GemType::GREEN,  // 2
      GemType::YELLOW, // 3
      GemType::PURPLE, // 4
      GemType::ORANGE, // 5
      GemType::BLUE,   // 6
  };
  return (v >= 0 && v <= 6) ? map[v] : GemType::UNKNOWN;
}

// Draw the hook-read board as a colored 8x8 grid into `image`. `board` is the
// raw 64 color codes (0..6); `analysis` supplies the best-move highlight.
void DrawHookBoard(cv::Mat &image, const int *board,
                   const BoardAnalysis *analysis, bool autoPlay,
                   const std::string &timingStr) {
  const int n = Config::GRID_SIZE;
  const int banner = 48;
  const int cell = image.cols / n;

  // Banner
  image(cv::Rect(0, 0, image.cols, banner)).setTo(cv::Scalar(40, 40, 40));
  std::string status = "Auto-play: " + std::string(autoPlay ? "ON" : "OFF");
  if (analysis && analysis->boardValid && analysis->bestSwapIndex >= 0) {
    const auto &s = analysis->scoredSwaps[analysis->bestSwapIndex];
    status += "  Best: (" + std::to_string(s.swap.a.x) + "," +
              std::to_string(s.swap.a.y) + ")<->(" + std::to_string(s.swap.b.x) +
              "," + std::to_string(s.swap.b.y) + ")";
  }
  cv::putText(image, status, cv::Point(10, 16), cv::FONT_HERSHEY_SIMPLEX, 0.5,
              cv::Scalar(0, 255, 0), 1);
  cv::putText(image, timingStr, cv::Point(10, 38), cv::FONT_HERSHEY_SIMPLEX, 0.5,
              cv::Scalar(0, 255, 255), 1);

  if (!board) {
    cv::putText(image, "Board not available", cv::Point(10, banner + 40),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
    return;
  }

  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      int v = board[r * n + c];
      GemType type = HookColorToGemType(v);
      cv::Rect rect(c * cell, banner + r * cell, cell, cell);
      cv::rectangle(image, rect, GemTypeToColor(type), cv::FILLED);
      cv::rectangle(image, rect, cv::Scalar(30, 30, 30), 1);
      cv::putText(image, std::to_string(v),
                  cv::Point(c * cell + cell / 2 - 8,
                            banner + r * cell + cell / 2 + 8),
                  cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 2);
    }
  }

  // Highlight the best swap.
  if (analysis && analysis->boardValid && analysis->bestSwapIndex >= 0) {
    const auto &s = analysis->scoredSwaps[analysis->bestSwapIndex];
    auto hl = [&](cv::Point p) {
      cv::Rect rect(p.x * cell, banner + p.y * cell, cell, cell);
      cv::rectangle(image, rect, cv::Scalar(0, 255, 255), 3);
    };
    hl(s.swap.a);
    hl(s.swap.b);
  }
}

} // namespace

// ============================================================================
// Application Implementation
// ============================================================================

Application::Application() = default;

Application::~Application() { Shutdown(); }

bool Application::Initialize() {
  std::cout << "Bejeweled 3 Bot - Window Capture Module" << std::endl;
  std::cout << "=========================================" << std::endl;
  std::cout << std::endl;

  InitializeColorSimilarityLut();

  if (!FindAndSetupGameWindow()) {
    return false;
  }

  // Extract game assets from main.pak.bac archive
  std::string gameDirectory =
      AssetExtractor::GetGameExecutableDirectory(gameWindow_);

  // Determine resolution based on window height
  RECT windowRect;
  GetClientRect(gameWindow_, &windowRect);
  int height = windowRect.bottom - windowRect.top;

  if (height != 600 && height != 768 && height != 1200) {
    std::cerr << "Error: Unsupported window height: " << height << std::endl;
    std::cerr << "Expected 600, 768, or 1200" << std::endl;
    return false;
  }

  // Set resolution-specific configuration before any allocations
  auto resConfig = Config::GetResolutionConfig(height);
  Config::BOARD_X = resConfig.boardX;
  Config::BOARD_Y = resConfig.boardY;
  Config::CELL_SIZE = resConfig.cellSize;
  Config::BOARD_WIDTH =
      Config::CELL_SIZE * Config::GRID_SIZE; // Calculate from cell size
  Config::BOARD_HEIGHT =
      Config::CELL_SIZE * Config::GRID_SIZE; // Calculate from cell size
  Config::BANNER_HEIGHT = resConfig.bannerHeight;
  Config::FRAME_DIFF_REGION_WIDTH = resConfig.frameDiffRegionWidth;
  Config::FRAME_DIFF_REGION_HEIGHT = resConfig.frameDiffRegionHeight;
  MaskLoader::FRAME_WIDTH = resConfig.cellSize;
  MaskLoader::FRAME_HEIGHT = resConfig.cellSize;
  MaskLoader::HYPERCUBE_FRAME_WIDTH = resConfig.cellSize;
  MaskLoader::HYPERCUBE_FRAME_HEIGHT = resConfig.cellSize;

  // Memory-hook mode: skip all visual/capture/detection setup. The board is
  // read directly from game memory; only the hook + move logic run.
  if (Config::USE_MEMORY_HOOK) {
    std::cout << "Memory hook mode: visual processing disabled." << std::endl;
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  'q' - Quit" << std::endl;
    std::cout << "  'b' - Toggle auto-play" << std::endl;
    std::cout << "  'p' - Print board analysis" << std::endl;
    std::cout << "  'm' - Dump board memory" << std::endl;
    std::cout << std::endl;
    return true;
  }

  // Set half-texel offset values based on resolution
  // 768p needs negative offsets, 600p/1200p need positive
  if (height == 768) {
    FrameProcessing::TexelOffsetConfig::OFFSET_X = -0.5;
    FrameProcessing::TexelOffsetConfig::OFFSET_Y = -0.5;
  } else {
    FrameProcessing::TexelOffsetConfig::OFFSET_X = 0.5;
    FrameProcessing::TexelOffsetConfig::OFFSET_Y = 0.5;
  }

  std::cout << "Detected resolution " << height << "p:" << std::endl;
  std::cout << "  Board position: (" << Config::BOARD_X << ", "
            << Config::BOARD_Y << ")" << std::endl;
  std::cout << "  Board size: " << Config::BOARD_WIDTH << "x"
            << Config::BOARD_HEIGHT << std::endl;
  std::cout << "  Cell size: " << Config::CELL_SIZE << std::endl;
  std::cout << "  Gem frame size: " << MaskLoader::FRAME_WIDTH << "x"
            << MaskLoader::FRAME_HEIGHT << std::endl;
  std::cout << "  Hypercube frame size: " << MaskLoader::HYPERCUBE_FRAME_WIDTH
            << "x" << MaskLoader::HYPERCUBE_FRAME_HEIGHT << std::endl;
  std::cout << "  Texel offset: ("
            << FrameProcessing::TexelOffsetConfig::OFFSET_X << ", "
            << FrameProcessing::TexelOffsetConfig::OFFSET_Y << ")" << std::endl;
  std::cout << std::endl;

  if (!gameDirectory.empty()) {
    std::string resFolder = "images\\" + std::to_string(height) + "\\";
    std::string outputResFolder = "assets\\" + std::to_string(height) + "\\";
    std::string memoryPrefix = std::to_string(height) + "/";

    // List of all assets to extract
    std::vector<std::string> assetFiles = {"gemoutlines_.gif",
                                           "gems_shadowed_.gif",
                                           "gems_shadowed.jp2",
                                           "bombgems.jp2",
                                           "bombgems_.gif",
                                           "doomgem_.gif",
                                           "doomgem.jp2",
                                           "butterfly shadow_.gif",
                                           "butterfly body.jp2",
                                           "butterfly body_.gif",
                                           "butterfly shadow.gif",
                                           "hypercube\\colorglow.jp2",
                                           "hypercube\\frame.jp2",
                                           "hypercube\\frame_.gif",
                                           "Gems\\Blue.jp2",
                                           "Gems\\Blue_.gif",
                                           "Gems\\Green.jp2",
                                           "Gems\\Green_.gif",
                                           "Gems\\Orange.jp2",
                                           "Gems\\Orange_.gif",
                                           "Gems\\Purple.jp2",
                                           "Gems\\Purple_.gif",
                                           "Gems\\Red.jp2",
                                           "Gems\\Red_.gif",
                                           "Gems\\White.jp2",
                                           "Gems\\White_.gif",
                                           "Gems\\Yellow.jp2",
                                           "Gems\\Yellow_.gif",
                                           "sm_shards_outline_.gif",
                                           "sm_shards_.gif"};

    // Extract all assets in a single pass into memory
    std::vector<AssetExtractor::ExtractRequest> filesToExtract;
    filesToExtract.reserve(assetFiles.size());
    for (const auto &file : assetFiles) {
      std::string archivePath = resFolder + file;
      std::string outputPath = outputResFolder + file;
      std::string key = memoryPrefix + file;
      for (char &ch : key) {
        if (ch == '\\') {
          ch = '/';
        }
      }
      filesToExtract.push_back({archivePath, outputPath, key});
    }

    std::unordered_map<std::string, std::vector<uint8_t>> extractedAssets;
    int extractedCount = AssetExtractor::ExtractFilesToMemory(
        gameDirectory, filesToExtract, extractedAssets);

    if (Config::WRITE_EXTRACTED_ASSETS_TO_DISK && extractedCount > 0) {
      int writtenCount =
          AssetExtractor::WriteExtractedFiles(filesToExtract, extractedAssets);
      std::cout << "Wrote " << writtenCount << " of " << assetFiles.size()
                << " asset files to " << outputResFolder << std::endl;
    }

    if (extractedCount > 0) {
      MaskLoader::SetInMemoryAssets(height, std::move(extractedAssets));
    }

    std::cout << "Extracted " << extractedCount << " of " << assetFiles.size()
              << " asset files into memory" << std::endl;
  }

  if (!InitializeCapture()) {
    return false;
  }

  if (!AllocateBuffers()) {
    return false;
  }

  // Create display window
  cv::namedWindow(Config::DISPLAY_WINDOW_NAME,
                  cv::WINDOW_AUTOSIZE | cv::WINDOW_KEEPRATIO);

  // Create trackbars for gradient parameters
  GradientParams::CreateTrackbars(Config::DISPLAY_WINDOW_NAME);

  // Mouse click selection for recording cell
  g_gameWindow = gameWindow_;
  cv::setMouseCallback(Config::DISPLAY_WINDOW_NAME, OnMouse, nullptr);

  // Recording controls

  // Load gem masks for shape classification from resolution-specific folder
  // Only attempt to load if we successfully extracted assets
  bool masksLoaded = false;
  if (height > 0) {
    masksLoaded = MaskLoader::LoadAllImages("assets", height);
  }

  if (!masksLoaded) {
    if (height > 0) {
      std::cerr << "Warning: Failed to load gem masks from assets\\" << height
                << " directory. Shape classification will not work."
                << std::endl;
    } else {
      std::cerr << "Warning: Game assets not extracted. Shape classification "
                   "will not work."
                << " Please ensure Bejeweled 3 is running and the assets "
                   "folder exists."
                << std::endl;
    }
  }

  // Print controls
  std::cout << "Controls:" << std::endl;
  std::cout << "  'q' - Quit" << std::endl;
  std::cout << "  'i' - Print window info" << std::endl;
  std::cout << "  'r' - Start/stop recording frames (uses Rec Col/Row/Frames)"
            << std::endl;
  std::cout << "  's' - Show recording status" << std::endl;
  std::cout << std::endl;

  // Initialize FPS tracking
  lastFpsUpdateTime_ = std::chrono::high_resolution_clock::now();

  return true;
}

bool Application::FindAndSetupGameWindow() {
  std::cout << "Searching for window: " << Config::WINDOW_TITLE << std::endl;
  gameWindow_ = FindGameWindow(Config::WINDOW_TITLE);

  if (!gameWindow_) {
    std::cerr << "Error: Could not find game window!" << std::endl;
    std::cerr << "Please make sure Bejeweled 3 is running and update "
                 "WINDOW_TITLE constant in config.h."
              << std::endl;
    return false;
  }

  std::cout << "Game window found!" << std::endl;
  std::cout << std::endl;

  // Attach memory hook (for direct board-state reads)
  if (MemoryHook::Attach(gameWindow_)) {
    std::cout << "Memory hook attached (base 0x" << std::hex
              << MemoryHook::ModuleBase() << std::dec << ") - press 'M' to "
              << "dump board state" << std::endl;
  } else {
    std::cout << "Memory hook could not attach (board dump unavailable)"
              << std::endl;
  }

  // Set game window to always on top
  SetWindowAlwaysOnTop(gameWindow_, true);
  std::cout << "Set game window to always on top" << std::endl;

  // Get window size info
  RECT windowRect;
  GetClientRect(gameWindow_, &windowRect);
  std::cout << "Window size: " << (windowRect.right - windowRect.left) << "x"
            << (windowRect.bottom - windowRect.top) << std::endl;
  std::cout << "Crop region: (" << Config::BOARD_X << ", " << Config::BOARD_Y
            << ") " << Config::BOARD_WIDTH << "x" << Config::BOARD_HEIGHT
            << std::endl;
  std::cout << std::endl;

  return true;
}

struct MonitorEnumData {
  HMONITOR targetMonitor;
  int currentIndex;
  int foundIndex;
};
static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM data) {
  auto *pData = reinterpret_cast<MonitorEnumData *>(data);
  if (hMon == pData->targetMonitor) {
    pData->foundIndex = pData->currentIndex;
    return FALSE;
  }
  pData->currentIndex++;
  return TRUE;
}

bool Application::InitializeCapture() {
  // Detect which monitor the game window is on
  HMONITOR hMonitor = MonitorFromWindow(gameWindow_, MONITOR_DEFAULTTOPRIMARY);
  int monitorIndex = 0;

  MonitorEnumData enumData = {hMonitor, 0, -1};

  EnumDisplayMonitors(NULL, NULL, MonitorEnumProc,
                      reinterpret_cast<LPARAM>(&enumData));

  if (enumData.foundIndex >= 0) {
    monitorIndex = enumData.foundIndex;
    std::cout << "Game window detected on monitor " << monitorIndex
              << std::endl;
  }

  // Try to initialize Desktop Duplication
  if (Config::USE_DESKTOP_DUPLICATION) {
    std::cout << "Initializing Desktop Duplication API..." << std::endl;
    frameData_.dupContext = InitializeDesktopDuplication(monitorIndex);
    if (frameData_.dupContext) {
      std::cout << "Desktop Duplication enabled - capturing synced to display "
                   "refresh!"
                << std::endl;
    } else {
      std::cout << "Desktop Duplication not available - falling back to BitBlt"
                << std::endl;
    }
  } else {
    std::cout << "Desktop Duplication disabled in config - using BitBlt"
              << std::endl;
  }

  return true;
}

bool Application::AllocateBuffers() {
  // Capture a dummy frame to get exact format
  cv::Mat dummyCapture = CaptureWindow(gameWindow_);
  if (dummyCapture.empty()) {
    std::cerr << "Error: Failed to capture initial frame for buffer allocation"
              << std::endl;
    return false;
  }
  cv::Mat dummyBoard = CropGameBoard(dummyCapture);
  int boardType = dummyBoard.type();

  // Pre-allocate all buffers with exact size and type
  // Buffers are board height + banner height (resolution-dependent)
  for (int i = 0; i < 3; i++) {
    frameData_.frameBuffer[i] = cv::Mat(dummyBoard.rows + Config::BANNER_HEIGHT,
                                        dummyBoard.cols, boardType);
    frameData_.bufferState[i] = BufferState::FREE;
  }

  std::cout << "Pre-allocated buffers: Board=" << dummyBoard.size()
            << " type=" << boardType << std::endl;

  return true;
}

void Application::StartThreads() {
  captureThread_ = std::thread(CaptureThreadFunc, gameWindow_,
                               std::ref(frameData_), std::ref(timingInfo_));

  processThread_ =
      std::thread(ProcessThreadFunc, std::ref(frameData_),
                  std::ref(timingInfo_), std::ref(currentFps_),
                  std::ref(frameCount_), std::ref(lastFpsUpdateTime_));

  if (Config::BOOST_THREAD_PRIORITY) {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority((HANDLE)captureThread_.native_handle(),
                      THREAD_PRIORITY_HIGHEST);
    SetThreadPriority((HANDLE)processThread_.native_handle(),
                      THREAD_PRIORITY_ABOVE_NORMAL);
  }
}

void Application::Run() {
  if (Config::USE_MEMORY_HOOK) {
    RunHookMode();
    return;
  }

  StartThreads();
  running_ = true;

  while (running_) {
    DisplayFrame();
    UpdateAutoPlay();

    int key = cv::waitKey(1);
    HandleKeyboardInput(key);

    // Check if window was closed
    if (cv::getWindowProperty(Config::DISPLAY_WINDOW_NAME,
                              cv::WND_PROP_VISIBLE) <= 0) {
      std::cout << "Window closed. Exiting..." << std::endl;
      {
        std::lock_guard<std::mutex> lock(frameData_.mutex);
        frameData_.shouldExit = true;
      }
      frameData_.cv.notify_one();
      running_ = false;
    }
  }
}

void Application::RunHookMode() {
  running_ = true;

  static BoardAnalysis analysis;
  frameData_.currentAnalysis = &analysis;

  // Keep the game window "active" (not paused) even when it loses focus,
  // using the same helper the capture thread used.
  constexpr int kActivationIntervalMs = 1000;
  auto lastActivation = std::chrono::steady_clock::now();

  // Display window for the live (memory-read) board.
  const int cell = 80;
  const int banner = 48;
  cv::Mat display(Config::GRID_SIZE * cell + banner, Config::GRID_SIZE * cell,
                  CV_8UC3);
  cv::namedWindow(Config::DISPLAY_WINDOW_NAME, cv::WINDOW_AUTOSIZE);

  std::cout << "Memory hook mode active. 'b'=auto-play 'p'=print 'm'=dump "
               "'q'=quit\n";

  // Track the previous board and timing for diagnostics.
  int prevBoard[64] = {};
  bool havePrevBoard = false;
  bool prevAutoPlay = autoPlayEnabled_;
  long long loopIndex = 0;
  double readMs = 0, analyzeMs = 0, autoPlayMs = 0, keepMs = 0, drawMs = 0,
         waitMs = 0, totalMs = 0;
  auto lastTimingPrint = std::chrono::steady_clock::now();

  while (running_) {
    auto loopStart = std::chrono::high_resolution_clock::now();

    // Read the board directly from game memory.
    int board[64];
    bool haveBoard = MemoryHook::ReadBoard(board);
    auto tRead = std::chrono::high_resolution_clock::now();

    bool boardChanged = false;
    if (haveBoard) {
      boardChanged = !havePrevBoard ||
                     std::memcmp(board, prevBoard, sizeof(board)) != 0;
      if (boardChanged) {
        std::memcpy(prevBoard, board, sizeof(board));
        havePrevBoard = true;
      }
      // Analyze every frame so the stability counter advances even when the
      // board is unchanged (analyze() is cheap on unchanged boards).
      std::vector<std::vector<Gem>> grid(
          Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));
      for (int r = 0; r < Config::GRID_SIZE; r++) {
        for (int c = 0; c < Config::GRID_SIZE; c++) {
          int v = board[r * Config::GRID_SIZE + c];
          grid[r][c].type = HookColorToGemType(v);
          grid[r][c].state = GemState::NORMAL;
          grid[r][c].gridPos = cv::Point(c, r);
        }
      }
      analysis.analyze(grid);
    }
    auto tAnalyze = std::chrono::high_resolution_clock::now();

    UpdateAutoPlay();
    auto tAutoPlay = std::chrono::high_resolution_clock::now();

    // Periodically keep the game window active (not paused) when unfocused.
    auto nowSteady = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            nowSteady - lastActivation)
            .count() >= kActivationIntervalMs) {
      KeepGameActive(gameWindow_);
      lastActivation = nowSteady;
    }
    auto tKeep = std::chrono::high_resolution_clock::now();

    // Draw every frame so the timing line updates live.
    char timingBuf[256];
    std::snprintf(timingBuf, sizeof(timingBuf),
                  "L%lld read=%.2f analyze=%.2f autoplay=%.2f keep=%.2f "
                  "draw=%.2f wait=%.2f total=%.2f ms",
                  loopIndex, readMs, analyzeMs, autoPlayMs, keepMs, drawMs,
                  waitMs, totalMs);
    DrawHookBoard(display, haveBoard ? board : nullptr, &analysis,
                  autoPlayEnabled_, timingBuf);
    cv::imshow(Config::DISPLAY_WINDOW_NAME, display);
    auto tDraw = std::chrono::high_resolution_clock::now();

    int key = cv::waitKey(1);
    auto tWait = std::chrono::high_resolution_clock::now();

    readMs =
        std::chrono::duration<double, std::milli>(tRead - loopStart).count();
    analyzeMs =
        std::chrono::duration<double, std::milli>(tAnalyze - tRead).count();
    autoPlayMs =
        std::chrono::duration<double, std::milli>(tAutoPlay - tAnalyze).count();
    keepMs =
        std::chrono::duration<double, std::milli>(tKeep - tAutoPlay).count();
    drawMs = std::chrono::duration<double, std::milli>(tDraw - tKeep).count();
    waitMs = std::chrono::duration<double, std::milli>(tWait - tDraw).count();
    totalMs =
        std::chrono::duration<double, std::milli>(tWait - loopStart).count();
    loopIndex++;

    // Print timing to console once per second.
    auto nowPrint = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            nowPrint - lastTimingPrint)
            .count() >= 1000) {
      std::printf("loop=%lld read=%.2f analyze=%.2f autoplay=%.2f keep=%.2f "
                  "draw=%.2f wait=%.2f total=%.2f ms\n",
                  loopIndex, readMs, analyzeMs, autoPlayMs, keepMs, drawMs,
                  waitMs, totalMs);
      lastTimingPrint = nowPrint;
    }

    if (key != -1) {
      HandleKeyboardInput(key);
    }

    if (cv::getWindowProperty(Config::DISPLAY_WINDOW_NAME,
                              cv::WND_PROP_VISIBLE) <= 0) {
      running_ = false;
    }
  }
}

void Application::DisplayFrame() {
  int displayIndex = -1;
  cv::Mat displayLocal;
  bool shouldDisplay = false;
  {
    std::lock_guard<std::mutex> lock(frameData_.mutex);
    displayIndex = frameData_.findBuffer(BufferState::PROCESSED);
    if (displayIndex >= 0) {
      frameData_.bufferState[displayIndex] = BufferState::DISPLAYING;
      displayLocal = frameData_.frameBuffer[displayIndex];
      shouldDisplay = true;
    }
  }

  if (shouldDisplay) {
    auto displayStart = std::chrono::high_resolution_clock::now();
    cv::imshow(Config::DISPLAY_WINDOW_NAME, displayLocal);
    auto displayEnd = std::chrono::high_resolution_clock::now();
    timingInfo_.displayMs =
        std::chrono::duration<double, std::milli>(displayEnd - displayStart)
            .count();
    {
      std::lock_guard<std::mutex> lock(frameData_.mutex);
      frameData_.bufferState[displayIndex] = BufferState::FREE;
    }
    frameData_.cv.notify_all();
  }
}

void Application::HandleKeyboardInput(int key) {
  if (key == 'q' || key == 'Q') {
    std::cout << "Exiting..." << std::endl;
    {
      std::lock_guard<std::mutex> lock(frameData_.mutex);
      frameData_.shouldExit = true;
    }
    frameData_.cv.notify_one();
    running_ = false;
  } else if (!Config::USE_MEMORY_HOOK && (key == 'i' || key == 'I')) {
    std::cout << "Window info:" << std::endl;
    std::lock_guard<std::mutex> lock(frameData_.mutex);
    for (int i = 0; i < 3; i++) {
      if (!frameData_.frameBuffer[i].empty()) {
        std::cout << "  Game board size: " << frameData_.frameBuffer[i].cols
                  << "x" << frameData_.frameBuffer[i].rows << std::endl;
        break;
      }
    }
  } else if (!Config::USE_MEMORY_HOOK && (key == 'r' || key == 'R')) {
    if (!IsRecording()) {
      StartRecording(g_recordColTrack, g_recordRowTrack, 5000,
                     "recorded_frames");
      std::cout << "Press 'R' again to stop recording early" << std::endl;
    } else {
      StopRecording();
    }
  } else if (!Config::USE_MEMORY_HOOK && (key == 's' || key == 'S')) {
    if (IsRecording()) {
      std::cout << "Recording status: " << GetRecordedFrameCount()
                << " unique frames captured (checked "
                << GetTotalFramesChecked() << " total frames)" << std::endl;
    } else {
      std::cout << "Not currently recording. Press 'R' to start." << std::endl;
    }
  } else if (key == 'p' || key == 'P') {
    std::cout << "\n" << std::endl;
    if (frameData_.currentAnalysis) {
      BoardDebug::PrintFullAnalysis(*frameData_.currentAnalysis);
    } else {
      std::cout << "No board analysis available yet" << std::endl;
    }
  } else if (key == 'm' || key == 'M') {
    std::cout << "Dumping board memory..." << std::endl;
    MemoryHook::DumpBoardState();
  } else if (key == 'b' || key == 'B') {
    autoPlayEnabled_ = !autoPlayEnabled_;
    if (autoPlayEnabled_) {
      std::cout << "Auto-play ENABLED";
      if (Config::BOT_STOP_AFTER_SUPERNOVA_FOUND) {
        std::cout << " (will stop when supernova move is found)";
      }
      std::cout << std::endl;
      lastClickTime_ = std::chrono::high_resolution_clock::now();
      clicksInProgress_ = 0;
    } else {
      std::cout << "Auto-play DISABLED" << std::endl;
    }
  }
}

void Application::Shutdown() {
  // Signal threads to exit
  {
    std::lock_guard<std::mutex> lock(frameData_.mutex);
    frameData_.shouldExit = true;
  }
  frameData_.cv.notify_all();

  // Wait for threads
  if (captureThread_.joinable()) {
    captureThread_.join();
  }
  if (processThread_.joinable()) {
    processThread_.join();
  }

  // Cleanup Desktop Duplication
  if (frameData_.dupContext) {
    CleanupDesktopDuplication(frameData_.dupContext);
    frameData_.dupContext = nullptr;
  }

  // Detach memory hook
  MemoryHook::Detach();

  cv::destroyAllWindows();
}

void Application::ExecuteBestSwap() {
  if (!frameData_.currentAnalysis ||
      frameData_.currentAnalysis->scoredSwaps.empty()) {
    return;
  }

  const BoardAnalysis &analysis = *frameData_.currentAnalysis;
  if (analysis.bestSwapIndex < 0) {
    return;
  }

  const ScoredSwap &bestSwap = analysis.scoredSwaps[analysis.bestSwapIndex];
  const MatchableSwap &swap = bestSwap.swap;

  // Calculate screen coordinates for both gems (swap.a and swap.b are in (col,
  // row) format)
  int x1 = swap.a.x * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  int y1 = swap.a.y * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  int x2 = swap.b.x * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  int y2 = swap.b.y * Config::CELL_SIZE + Config::CELL_SIZE / 2;

  auto now = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - lastClickTime_)
                     .count();

  // Execute click sequence with timing
  if (clicksInProgress_ == 0 && elapsed >= Config::BOT_CLICK_DELAY_MS) {
    // Click both gems in sequence (no wait between clicks)
    SendMouseClickToGame(gameWindow_, x1, y1, true);
    SendMouseClickToGame(gameWindow_, x1, y1, false);
    SendMouseClickToGame(gameWindow_, x2, y2, true);
    SendMouseClickToGame(gameWindow_, x2, y2, false);

    lastClickTime_ = now;
    clicksInProgress_ = 2;

    // Reset stable frame counter when we make a move
    if (frameData_.currentAnalysis) {
      frameData_.currentAnalysis->stableFrameCount = 0;
    }

    const char *specialName = "";
    if (bestSwap.specialType == GemState::SUPERNOVA) {
      specialName = " [SUPERNOVA]";
    } else if (bestSwap.specialType == GemState::STAR) {
      specialName = " [STAR]";
    } else if (bestSwap.specialType == GemState::FLAME) {
      specialName = " [FLAME]";
    } else if (bestSwap.matchLength == 5) {
      specialName = " [HYPERCUBE]";
    }

    // std::cout << "Auto-play: Swapping (" << swap.a.x << "," << swap.a.y
    //           << ") <-> (" << swap.b.x << "," << swap.b.y
    //           << ") - Score: " << bestSwap.totalScore << specialName
    //           << std::endl;
  } else if (clicksInProgress_ == 2 && elapsed >= Config::BOT_CLICK_DELAY_MS) {
    // Wait period complete - ready for next move
    clicksInProgress_ = 0;
  }
}

void Application::UpdateAutoPlay() {
  if (!autoPlayEnabled_) {
    return;
  }

  // Only make moves when we have a stable board
  if (!frameData_.currentAnalysis ||
      frameData_.currentAnalysis->stableFrameCount <
          Config::BOT_STABLE_FRAMES_REQUIRED) {
    return;
  }

  // Check if best move creates a supernova and stop if configured
  if (Config::BOT_STOP_AFTER_SUPERNOVA_FOUND &&
      frameData_.currentAnalysis->bestSwapIndex >= 0) {
    const auto &bestSwap =
        frameData_.currentAnalysis
            ->scoredSwaps[frameData_.currentAnalysis->bestSwapIndex];
    if (bestSwap.specialType == GemState::SUPERNOVA) {
      autoPlayEnabled_ = false;
      std::cout << "\nAuto-play STOPPED: Supernova move detected!" << std::endl;
      std::cout << "Best move: (" << bestSwap.swap.a.x << ","
                << bestSwap.swap.a.y << ") <-> (" << bestSwap.swap.b.x << ","
                << bestSwap.swap.b.y << ") - Score: " << bestSwap.totalScore
                << " [SUPERNOVA]" << std::endl;
      return;
    }
  }

  // Only start new moves when previous click sequence is complete
  if (clicksInProgress_ == 0) {
    ExecuteBestSwap();
  } else {
    // Continue executing current click sequence
    ExecuteBestSwap();
  }
}
