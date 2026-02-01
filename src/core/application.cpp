#include "application.h"
#include "../capture/capture_thread.h"
#include "../config.h"
#include "../detection/gem_detector.h"
#include "../detection/mask_loader.h"
#include "../detection/process_thread.h"
#include "../detection/shape_classifier.h"
#include "../rendering/overlay.h"
#include "../rendering/control_panel.h"
#include <iostream>

namespace {
  // Recording UI controls (trackbars)
  int g_recordColTrack = 0;
  int g_recordRowTrack = 0;

  void OnMouse(int event, int x, int y, int flags, void*) {
    if (HandleControlPanelMouse(event, x, y, flags)) return;
    if (event != cv::EVENT_LBUTTONDOWN) return;
    if (y < Config::BANNER_HEIGHT) return;

    int col = x / Config::CELL_SIZE;
    int row = (y - Config::BANNER_HEIGHT) / Config::CELL_SIZE;

    if (col < 0 || col >= Config::GRID_SIZE || row < 0 || row >= Config::GRID_SIZE) {
      return;
    }

    g_recordColTrack = col;
    g_recordRowTrack = row;
    SetSelectedCell(col, row);
  }

}

// ============================================================================
// Application Implementation
// ============================================================================

Application::Application() = default;

Application::~Application() { Shutdown(); }

bool Application::Initialize() {
  std::cout << "Bejeweled 3 Bot - Window Capture Module" << std::endl;
  std::cout << "=========================================" << std::endl;
  std::cout << std::endl;

  if (!FindAndSetupGameWindow()) {
    return false;
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

  // Mouse click selection for recording cell
  cv::setMouseCallback(Config::DISPLAY_WINDOW_NAME, OnMouse, nullptr);

  // Recording controls

  // Load gem masks for shape classification
  // Now running from project root, so use simple path
  bool masksLoaded = MaskLoader::LoadAllMasks("assets");

  if (!masksLoaded) {
    std::cerr << "Warning: Failed to load gem masks from 'assets' directory. "
        "Shape classification will not work."
        << std::endl;
  }

  // Print controls
  std::cout << "Controls:" << std::endl;
  std::cout << "  'q' - Quit" << std::endl;
  std::cout << "  'c' - Toggle classification mode (Color/Shape)" << std::endl;
  std::cout << "  'i' - Print window info" << std::endl;
  std::cout << "  'd' - Dump current gem grid to console" << std::endl;
  std::cout << "  'r' - Start/stop recording frames (uses Rec Col/Row/Frames)" << std::endl;
  std::cout << "  's' - Show recording status" << std::endl;
  std::cout << std::endl;
  std::cout << "Note: HSV color thresholds need calibration!" << std::endl;
  std::cout << "      Adjust color constants in config.h if gems aren't "
      "detected correctly."
      << std::endl;
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

bool Application::InitializeCapture() {
  // Detect which monitor the game window is on
  HMONITOR hMonitor = MonitorFromWindow(gameWindow_, MONITOR_DEFAULTTOPRIMARY);
  int monitorIndex = 0;

  struct MonitorEnumData {
    HMONITOR targetMonitor;
    int currentIndex;
    int foundIndex;
  };

  MonitorEnumData enumData = {hMonitor, 0, -1};

  EnumDisplayMonitors(
    NULL, NULL,
    [](HMONITOR hMon, HDC hdc, LPRECT rect, LPARAM data) -> BOOL {
      MonitorEnumData* pData = (MonitorEnumData *)data;
      if (hMon == pData->targetMonitor) {
        pData->foundIndex = pData->currentIndex;
        return FALSE;
      }
      pData->currentIndex++;
      return TRUE;
    },
    (LPARAM)&enumData);

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
    }
    else {
      std::cout << "Desktop Duplication not available - falling back to BitBlt"
          << std::endl;
    }
  }
  else {
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
  // Buffers are 1224px tall (200px banner + 1024px game)
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
    SetThreadPriority((HANDLE)captureThread_.native_handle(), THREAD_PRIORITY_HIGHEST);
    SetThreadPriority((HANDLE)processThread_.native_handle(),
                      THREAD_PRIORITY_ABOVE_NORMAL);
  }
}

void Application::Run() {
  StartThreads();
  running_ = true;

  while (running_) {
    DisplayFrame();

    int key = cv::waitKey(1);
    HandleKeyboardInput(key);

    // Check if window was closed
    if (cv::getWindowProperty(Config::DISPLAY_WINDOW_NAME,
                              cv::WND_PROP_VISIBLE) <= 0) {
      std::cout << "Window closed. Exiting..." << std::endl; {
        std::lock_guard<std::mutex> lock(frameData_.mutex);
        frameData_.shouldExit = true;
      }
      frameData_.cv.notify_one();
      running_ = false;
    }
  }
}

void Application::DisplayFrame() {
  int displayIndex = -1;
  cv::Mat displayLocal;
  bool shouldDisplay = false; {
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
        .count(); {
      std::lock_guard<std::mutex> lock(frameData_.mutex);
      frameData_.bufferState[displayIndex] = BufferState::FREE;
    }
    frameData_.cv.notify_all();
  }
}

void Application::HandleKeyboardInput(int key) {
  if (key == 'q' || key == 'Q') {
    std::cout << "Exiting..." << std::endl; {
      std::lock_guard<std::mutex> lock(frameData_.mutex);
      frameData_.shouldExit = true;
    }
    frameData_.cv.notify_one();
    running_ = false;
  }
  else if (key == 'c' || key == 'C') {
    std::lock_guard<std::mutex> lock(frameData_.mutex);
    if (frameData_.classificationMode == Config::ClassificationMode::COLOR) {
      frameData_.classificationMode = Config::ClassificationMode::SHAPE;
      std::cout << "Classification mode changed to: SHAPE" << std::endl;
    }
    else {
      frameData_.classificationMode = Config::ClassificationMode::COLOR;
      std::cout << "Classification mode changed to: COLOR" << std::endl;
    }
  }
  else if (key == 'i' || key == 'I') {
    std::cout << "Window info:" << std::endl;
    std::lock_guard<std::mutex> lock(frameData_.mutex);
    for (int i = 0; i < 3; i++) {
      if (!frameData_.frameBuffer[i].empty()) {
        std::cout << "  Game board size: " << frameData_.frameBuffer[i].cols
            << "x" << frameData_.frameBuffer[i].rows << std::endl;
        break;
      }
    }
  }
  else if (key == 'r' || key == 'R') {
    if (!IsRecording()) {
      StartRecording(g_recordColTrack, g_recordRowTrack, 1000, "recorded_frames");
      std::cout << "Press 'R' again to stop recording early" << std::endl;
    }
    else {
      StopRecording();
    }
  }
  else if (key == 's' || key == 'S') {
    if (IsRecording()) {
      std::cout << "Recording status: " << GetRecordedFrameCount()
          << " unique frames captured (checked " << GetTotalFramesChecked()
          << " total frames)" << std::endl;
    }
    else {
      std::cout << "Not currently recording. Press 'R' to start." << std::endl;
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

  cv::destroyAllWindows();
}
