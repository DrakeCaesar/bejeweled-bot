#include "application.h"
#include "config.h"
#include "hook/memory_hook.h"
#include "window_helpers.h"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <vector>

namespace {

// Convert grid coordinates to game-window client coordinates and post a mouse
// message (move + optional button down/up).
void SendMouseClickToGame(HWND gameWindow, int x, int y, bool isDown) {
  if (!gameWindow || !IsWindow(gameWindow))
    return;

  int gameX = x + Config::BOARD_X;
  int gameY = y + Config::BOARD_Y;

  LPARAM lParam = MAKELPARAM(gameX, gameY);
  PostMessage(gameWindow, WM_MOUSEMOVE, 0, lParam);

  if (isDown) {
    PostMessage(gameWindow, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
  } else {
    PostMessage(gameWindow, WM_LBUTTONUP, 0, lParam);
  }
}

// Map the game's internal color code (0..6) to a GemType, in the actual sprite
// order confirmed visually: 0=RED 1=WHITE 2=GREEN 3=YELLOW 4=PURPLE 5=ORANGE
// 6=BLUE.
GemType HookColorToGemType(int v) {
  if (v == -1) {
    return GemType::HYPERCUBE; // hypercube is encoded as -1 (0xFFFFFFFF)
  }
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

// Map the game's special-gem state code (0x228) to a GemState.
// Verified in-game (natural specials compared against editor-written ones):
//   0 = normal, 1 = flame, 2 = hypercube, 4 = star, 5 = supernova
// Hypercubes are identified by the gem TYPE (+0x220 == -1), so their state (2)
// does not map to a GemState here.
GemState HookSpecialToGemState(int v) {
  switch (v) {
  case MemoryHook::kStateFlame:
    return GemState::FLAME;
  case MemoryHook::kStateStar:
    return GemState::STAR;
  case MemoryHook::kStateSupernova:
    return GemState::SUPERNOVA;
  default:
    return GemState::NORMAL;
  }
}

// Compact 8-line rendering of a raw memory board (0..6, -1) using the same
// letters as BoardState::toString(), for predicted-vs-actual diffing.
std::string MemBoardToString(const int board[64]) {
  static const char map[7] = {'r', 'w', 'g', 'y', 'p', 'o', 'b'};
  std::string s;
  s.reserve(72);
  for (int i = 0; i < 64; i++) {
    int v = board[i];
    s += (v == -1) ? 'H' : (v >= 0 && v <= 6 ? map[v] : '?');
    if (i % 8 == 7 && i != 63)
      s += '\n';
  }
  return s;
}

// ---------------------------------------------------------------------------
// Board editor palette
//
// Two swatch rows at the top of the overlay: colours (row 0) and states
// (row 1). The brush is the (selected colour, selected state) pair; the HC
// state ignores the colour and yields a hypercube. Staged ("pending") cells are
// shown on the board with a white outline and are only written to the game when
// the user presses Enter.
// ---------------------------------------------------------------------------
constexpr int kPaletteRowH = 32;
constexpr int kPaletteStatusH = 28;
constexpr int kPaletteHeight = kPaletteRowH * 2 + kPaletteStatusH;

struct BrushColorDef {
  const char *label;
  GemType type;
};
constexpr int kColorChoices = 7;
const BrushColorDef kBrushColors[kColorChoices] = {
    {"R", GemType::RED},   {"O", GemType::ORANGE}, {"Y", GemType::YELLOW},
    {"G", GemType::GREEN}, {"B", GemType::BLUE},   {"P", GemType::PURPLE},
    {"W", GemType::WHITE},
};

struct BrushStateDef {
  const char *label;
  GemState state;
  bool hypercube;
};
constexpr int kStateChoices = 5;
const BrushStateDef kBrushStates[kStateChoices] = {
    {"N", GemState::NORMAL, false},  {"FL", GemState::FLAME, false},
    {"ST", GemState::STAR, false},   {"SN", GemState::SUPERNOVA, false},
    {"HC", GemState::NORMAL, true},
};

// Map GemType/GemState back to the game's internal codes (see HookColorToGemType
// / HookSpecialToGemState above).
int GemTypeToHookCode(GemType t) {
  switch (t) {
  case GemType::RED:
    return 0;
  case GemType::WHITE:
    return 1;
  case GemType::GREEN:
    return 2;
  case GemType::YELLOW:
    return 3;
  case GemType::PURPLE:
    return 4;
  case GemType::ORANGE:
    return 5;
  case GemType::BLUE:
    return 6;
  case GemType::HYPERCUBE:
    return -1;
  default:
    return 0;
  }
}

// Inverse of the mapping above: GemState -> the game's +0x228 code. Hypercubes
// are a GemType (not a GemState) and are handled separately (type -1 + state 2).
int GemStateToHookCode(GemState s) {
  switch (s) {
  case GemState::FLAME:
    return MemoryHook::kStateFlame;
  case GemState::STAR:
    return MemoryHook::kStateStar;
  case GemState::SUPERNOVA:
    return MemoryHook::kStateSupernova;
  default:
    return MemoryHook::kStateNormal;
  }
}

/// Editor state handed to the renderer.
struct EditorView {
  const uint8_t *pendingMask;
  const GemType *pendingType;
  const GemState *pendingState;
  int brushColorIdx;
  int brushStateIdx;
};

// Palette hit-testing (shared by the renderer and the mouse handler).
int PaletteColorIndexAt(int x, int y, int cols) {
  if (y < 0 || y >= kPaletteRowH || cols <= 0) {
    return -1;
  }
  int w = cols / kColorChoices;
  if (w <= 0) {
    return -1;
  }
  int i = x / w;
  return (i >= 0 && i < kColorChoices) ? i : -1;
}

int PaletteStateIndexAt(int x, int y, int cols) {
  if (y < kPaletteRowH || y >= kPaletteRowH * 2 || cols <= 0) {
    return -1;
  }
  int w = cols / kStateChoices;
  if (w <= 0) {
    return -1;
  }
  int i = x / w;
  return (i >= 0 && i < kStateChoices) ? i : -1;
}

void DrawPalette(cv::Mat &image, const EditorView &ed) {
  const int cols = image.cols;
  const int wC = std::max(1, cols / kColorChoices);
  const int wS = std::max(1, cols / kStateChoices);

  // Row 0: colours.
  for (int i = 0; i < kColorChoices; ++i) {
    cv::Rect r(i * wC, 0, wC, kPaletteRowH);
    cv::rectangle(image, r, GemTypeToColor(kBrushColors[i].type), cv::FILLED);
    bool sel = (i == ed.brushColorIdx);
    cv::rectangle(image, r, sel ? cv::Scalar(0, 255, 255) : cv::Scalar(20, 20, 20),
                  sel ? 3 : 1);
    cv::putText(image, kBrushColors[i].label,
                cv::Point(r.x + wC / 2 - 6, kPaletteRowH - 9),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 2);
  }

  // Row 1: states, previewed in the selected colour.
  for (int i = 0; i < kStateChoices; ++i) {
    cv::Rect r(i * wS, kPaletteRowH, wS, kPaletteRowH);
    cv::Scalar fill = kBrushStates[i].hypercube
                          ? cv::Scalar(150, 150, 150)
                          : GemTypeToColor(kBrushColors[ed.brushColorIdx].type);
    cv::rectangle(image, r, fill, cv::FILLED);
    bool sel = (i == ed.brushStateIdx);
    cv::rectangle(image, r, sel ? cv::Scalar(0, 255, 255) : cv::Scalar(20, 20, 20),
                  sel ? 3 : 1);
    cv::putText(image, kBrushStates[i].label,
                cv::Point(r.x + wS / 2 - 14, kPaletteRowH * 2 - 9),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 2);
  }
}

// Draw the hook-read board as a colored 8x8 grid into `image`. `board` is the
// raw 64 color codes (0..6); `analysis` supplies the best-move highlight;
// `ed` supplies the palette and the staged (pending) cells.
void DrawHookBoard(cv::Mat &image, const int *board, const int *states,
                   const BoardAnalysis *analysis, bool autoPlay,
                   const EditorView &ed) {
  const int n = Config::GRID_SIZE;
  const int cell = image.cols / n;
  const int top = kPaletteHeight;

  DrawPalette(image, ed);

  // Status line under the palette.
  cv::Rect statusRect(0, kPaletteRowH * 2, image.cols, kPaletteStatusH);
  image(statusRect).setTo(cv::Scalar(40, 40, 40));
  std::string status = "Auto-play: " + std::string(autoPlay ? "ON" : "OFF");
  if (analysis && analysis->boardValid && analysis->bestSwapIndex >= 0) {
    const auto &s = analysis->scoredSwaps[analysis->bestSwapIndex];
    status += " Best:(" + std::to_string(s.swap.a.x) + "," +
              std::to_string(s.swap.a.y) + ")<->(" + std::to_string(s.swap.b.x) +
              "," + std::to_string(s.swap.b.y) + ")";
  }
  int staged = 0;
  for (int i = 0; i < 64; ++i) {
    if (ed.pendingMask[i]) {
      ++staged;
    }
  }
  status += " | staged:" + std::to_string(staged) +
            " (L=stamp R=erase Enter=write Bksp=clear)";
  cv::putText(image, status, cv::Point(8, kPaletteRowH * 2 + 19),
              cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(0, 255, 0), 1);

  if (!board) {
    cv::putText(image, "Board not available", cv::Point(10, top + 40),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
    return;
  }

  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      int i = r * n + c;
      bool isStaged = ed.pendingMask[i] != 0;
      GemType type = isStaged ? ed.pendingType[i] : HookColorToGemType(board[i]);
      cv::Rect rect(c * cell, top + r * cell, cell, cell);
      cv::rectangle(image, rect, GemTypeToColor(type), cv::FILLED);
      cv::rectangle(image, rect, cv::Scalar(30, 30, 30), 1);

      int code = (type == GemType::HYPERCUBE) ? -1 : GemTypeToHookCode(type);
      std::string label = (code < 0) ? "H" : std::to_string(code);
      if (type != GemType::HYPERCUBE) {
        GemState st =
            isStaged ? ed.pendingState[i]
                     : HookSpecialToGemState(states ? states[i] : 0);
        if (st == GemState::FLAME) {
          label += "F";
        } else if (st == GemState::SUPERNOVA) {
          label += "N";
        } else if (st == GemState::STAR) {
          label += "S";
        }
      }
      cv::putText(image, label,
                  cv::Point(c * cell + cell / 2 - 8,
                            top + r * cell + cell / 2 + 8),
                  cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 2);

      // Staged cells get a white outline so they read as "pending".
      if (isStaged) {
        cv::rectangle(image, rect, cv::Scalar(255, 255, 255), 3);
      }
    }
  }

  // Mark gems that are part of an already-committed (lingering) match: the bot
  // treats these as locked — they will clear and cannot be added to.
  if (analysis && analysis->boardValid) {
    for (int r = 0; r < n; r++) {
      for (int c = 0; c < n; c++) {
        if (!analysis->board.cells[r][c].pending)
          continue;
        cv::Rect rect(c * cell, top + r * cell, cell, cell);
        cv::rectangle(image, rect, cv::Scalar(0, 0, 255), 3);
        cv::line(image, cv::Point(rect.x, rect.y),
                 cv::Point(rect.x + rect.width, rect.y + rect.height),
                 cv::Scalar(0, 0, 255), 2);
        cv::line(image, cv::Point(rect.x + rect.width, rect.y),
                 cv::Point(rect.x, rect.y + rect.height),
                 cv::Scalar(0, 0, 255), 2);
      }
    }
  }

  // Highlight the best swap.
  if (analysis && analysis->boardValid && analysis->bestSwapIndex >= 0) {
    const auto &s = analysis->scoredSwaps[analysis->bestSwapIndex];
    auto hl = [&](cv::Point p) {
      cv::Rect rect(p.x * cell, top + p.y * cell, cell, cell);
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

Application::~Application() { Shutdown(); }

bool Application::Initialize() {
  std::cout << "Bejeweled 3 Bot - Memory Hook" << std::endl;
  std::cout << "===============================" << std::endl;
  std::cout << std::endl;

  if (!FindAndSetupGameWindow()) {
    return false;
  }

  // Determine resolution (needed to translate grid coords to click coords).
  RECT windowRect;
  GetClientRect(gameWindow_, &windowRect);
  int height = windowRect.bottom - windowRect.top;
  if (height != 600 && height != 768 && height != 1200) {
    std::cerr << "Error: Unsupported window height: " << height << std::endl;
    std::cerr << "Expected 600, 768, or 1200" << std::endl;
    return false;
  }
  auto resConfig = Config::GetResolutionConfig(height);
  Config::BOARD_X = resConfig.boardX;
  Config::BOARD_Y = resConfig.boardY;
  Config::CELL_SIZE = resConfig.cellSize;
  Config::BOARD_WIDTH = Config::CELL_SIZE * Config::GRID_SIZE;
  Config::BOARD_HEIGHT = Config::CELL_SIZE * Config::GRID_SIZE;
  Config::BANNER_HEIGHT = resConfig.bannerHeight;

  std::cout << "Detected resolution " << height << "p" << std::endl;
  std::cout << "  Board position: (" << Config::BOARD_X << ", "
            << Config::BOARD_Y << ")" << std::endl;
  std::cout << "  Cell size: " << Config::CELL_SIZE << std::endl;
  std::cout << std::endl;

  std::cout << "Controls:" << std::endl;
  std::cout << "  'q' - Quit" << std::endl;
  std::cout << "  'b' - Toggle auto-play" << std::endl;
  std::cout << "  'p' - Print board analysis" << std::endl;
  std::cout << "  'm' - Dump board memory" << std::endl;
  std::cout << "  'c' - Write checkerboard test board" << std::endl;
  std::cout << std::endl;

  return true;
}

bool Application::FindAndSetupGameWindow() {
  std::cout << "Searching for window: " << Config::WINDOW_TITLE << std::endl;
  gameWindow_ = FindGameWindow(Config::WINDOW_TITLE);

  if (!gameWindow_) {
    std::cerr << "Error: Could not find game window!" << std::endl;
    std::cerr << "Please make sure Bejeweled 3 is running." << std::endl;
    return false;
  }

  std::cout << "Game window found!" << std::endl;

  if (MemoryHook::Attach(gameWindow_)) {
    std::cout << "Memory hook attached (base 0x" << std::hex
              << MemoryHook::ModuleBase() << std::dec << ")" << std::endl;
  } else {
    std::cout << "Memory hook could not attach" << std::endl;
  }

  SetWindowAlwaysOnTop(gameWindow_, true);
  std::cout << "Set game window to always on top" << std::endl;

  return true;
}

void Application::Run() { RunHookMode(); }

void Application::RunHookMode() {
  running_ = true;

  constexpr int kActivationIntervalMs = 1000;
  auto lastActivation = std::chrono::steady_clock::now();

  const int cell = 80;
  cv::Mat display(Config::GRID_SIZE * cell + kPaletteHeight,
                  Config::GRID_SIZE * cell, CV_8UC3);
  cv::namedWindow(Config::DISPLAY_WINDOW_NAME, cv::WINDOW_AUTOSIZE);
  cv::setMouseCallback(Config::DISPLAY_WINDOW_NAME, &Application::OnMouse,
                       this);
  displayCols_ = display.cols;
  displayCellPx_ = display.cols / Config::GRID_SIZE;
  paletteHeightPx_ = kPaletteHeight;

  std::cout << "Memory hook mode active. 'b'=auto-play 'p'=print 'm'=dump "
               "'c'=checkerboard 'q'=quit\n";
  std::cout << "Board editor: click a palette swatch, left-click cells to "
               "stage, Enter=write to game, Backspace=clear staged\n";

  // Self-serve RNG trace: enable auto-play and log the full MT19937 state +
  // index + gem board every frame to rng_state.bin; auto-quit afterwards.
  bool rngTrace = rngAutoTraceMs_ > 0;
  FILE *rngBin = nullptr;
  std::chrono::steady_clock::time_point rngStart;
  if (rngTrace) {
    autoPlayEnabled_ = true;
    rngBin = std::fopen("rng_state.bin", "wb");
    rngStart = std::chrono::steady_clock::now();
    std::cout << "RNG autoplay trace -> rng_state.bin for " << rngAutoTraceMs_
              << " ms\n";
  }

  // Self-serve board/gem trace: enable auto-play and log the per-slot gem
  // pointer, raw dwords +0x08..+0x24, type and special state every frame to
  // board_trace.bin; auto-quit afterwards.
  bool boardTrace = boardTraceMs_ > 0;
  FILE *trcBin = nullptr;
  std::chrono::steady_clock::time_point trcStart;
  if (boardTrace) {
    autoPlayEnabled_ = true;
    trcBin = std::fopen("board_trace.bin", "wb");
    trcStart = std::chrono::steady_clock::now();
    std::cout << "Board trace -> board_trace.bin for " << boardTraceMs_
              << " ms\n";
  }

  while (running_) {
    // Read the board directly from game memory.
    int board[64];
    bool haveBoard = MemoryHook::ReadBoard(board);
    int states[64] = {};
    if (haveBoard) {
      MemoryHook::ReadSpecialState(states);
    }

    if (rngTrace && rngBin) {
      uint32_t st[MemoryHook::kMtStateCount];
      int ix = -1;
      MemoryHook::ReadMtState(st, ix);
      int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - rngStart)
                       .count();
      int8_t bd[64];
      for (int i = 0; i < 64; i++) {
        bd[i] = haveBoard ? (int8_t)board[i] : (int8_t)-2;
      }
      uint32_t rptr[64];
      int rtype[64];
      MemoryHook::ReadBoardRaw(rptr, rtype);
      std::fwrite(&ms, sizeof(ms), 1, rngBin);
      std::fwrite(&ix, sizeof(ix), 1, rngBin);
      std::fwrite(st, sizeof(uint32_t), MemoryHook::kMtStateCount, rngBin);
      std::fwrite(rptr, sizeof(uint32_t), 64, rngBin);
      for (int i = 0; i < 64; i++) {
        int8_t t = (int8_t)rtype[i];
        std::fwrite(&t, 1, 1, rngBin);
      }
      std::fflush(rngBin);
      if (ms > rngAutoTraceMs_) {
        running_ = false;
      }
    }

    if (boardTrace && trcBin) {
      uint32_t ptr[64];
      uint32_t dw[64][8];
      int ttype[64];
      int tstate[64];
      MemoryHook::ReadGemDwords(ptr, dw, ttype, tstate);
      int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - trcStart)
                       .count();
      std::fwrite(&ms, sizeof(ms), 1, trcBin);
      std::fwrite(ptr, sizeof(uint32_t), 64, trcBin);
      std::fwrite(dw, sizeof(uint32_t), 64 * 8, trcBin);
      std::fwrite(ttype, sizeof(int), 64, trcBin);
      std::fwrite(tstate, sizeof(int), 64, trcBin);
      std::fflush(trcBin);
      if (ms > boardTraceMs_) {
        // Capture the final overlay so the rendering can be inspected offline.
        cv::imwrite("overlay_last.png", display);
        std::cout << "Saved overlay_last.png\n";
        running_ = false;
      }
    }

    if (haveBoard) {
      std::vector<std::vector<Gem>> grid(
          Config::GRID_SIZE, std::vector<Gem>(Config::GRID_SIZE));
      for (int r = 0; r < Config::GRID_SIZE; r++) {
        for (int c = 0; c < Config::GRID_SIZE; c++) {
          int v = board[r * Config::GRID_SIZE + c];
          grid[r][c].type = HookColorToGemType(v);
          grid[r][c].state = (v == -1)
                                 ? GemState::NORMAL
                                 : HookSpecialToGemState(
                                       states[r * Config::GRID_SIZE + c]);
          grid[r][c].gridPos = cv::Point(c, r);
        }
      }
      analysis_.analyze(grid);

      // Cascade validation: record distinct live boards until the board
      // settles, then print the predicted-vs-actual comparison.
      if (observingCascade_) {
        std::string s = MemBoardToString(board);
        if (observedStates_.empty() || observedStates_.back() != s) {
          observedStates_.push_back(s);
          observedBoards_.push_back(analysis_.board);
        }
        if (!analysis_.settling &&
            analysis_.stableFrameCount >= Config::BOT_STABLE_FRAMES_REQUIRED) {
          PrintCascadeComparison();
          observingCascade_ = false;
        }
      }
    }

    UpdateAutoPlay();

    // Periodically keep the game window active (not paused) when unfocused.
    auto nowSteady = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            nowSteady - lastActivation)
            .count() >= kActivationIntervalMs) {
      KeepGameActive(gameWindow_);
      lastActivation = nowSteady;
    }

    EditorView ed{pendingMask_.data(), pendingType_.data(),
                  pendingState_.data(), brushColorIndex_, brushStateIndex_};
    DrawHookBoard(display, haveBoard ? board : nullptr,
                  haveBoard ? states : nullptr, &analysis_, autoPlayEnabled_,
                  ed);
    cv::imshow(Config::DISPLAY_WINDOW_NAME, display);

    int key = cv::waitKey(1);

    if (key != -1) {
      HandleKeyboardInput(key);
    }

    if (cv::getWindowProperty(Config::DISPLAY_WINDOW_NAME,
                              cv::WND_PROP_VISIBLE) <= 0) {
      running_ = false;
    }
  }

  if (rngBin) {
    std::fclose(rngBin);
  }
}

void Application::HandleKeyboardInput(int key) {
  if (key == 'q' || key == 'Q') {
    std::cout << "Exiting..." << std::endl;
    running_ = false;
  } else if (key == 'p' || key == 'P') {
    std::cout << "\nBoard analysis:" << std::endl;
    if (!analysis_.boardValid) {
      std::cout << "  (no valid board yet)" << std::endl;
    } else {
      std::cout << "  valid swaps: " << analysis_.scoredSwaps.size()
                << std::endl;
      if (analysis_.bestSwapIndex >= 0) {
        const auto &s = analysis_.scoredSwaps[analysis_.bestSwapIndex];
        std::cout << "  best: (" << s.swap.a.x << "," << s.swap.a.y << ") <-> ("
                  << s.swap.b.x << "," << s.swap.b.y << ")  score="
                  << s.totalScore << "  clears=" << s.immediateClears
                  << std::endl;
      }
    }
  } else if (key == 'm' || key == 'M') {
    std::cout << "Dumping board memory..." << std::endl;
    MemoryHook::DumpBoardState();
  } else if (key == 'c' || key == 'C') {
    // TEST HOOK: overwrite the board with a 2-colour checkerboard.
    int n = MemoryHook::SetBoardCheckerboard(0, 2, (float)Config::CELL_SIZE);
    std::cout << "Checkerboard test board written to " << n << "/64 gems"
              << std::endl;
  } else if (key == 13 || key == 10) {
    // Enter: write all staged (pending) cells to the game.
    FlushPendingBoard();
  } else if (key == 8 || key == 127) {
    // Backspace: discard staged cells.
    ClearPendingBoard();
    std::cout << "Staged cells cleared." << std::endl;
  } else if (key == 'b' || key == 'B') {
    autoPlayEnabled_ = !autoPlayEnabled_;
    if (autoPlayEnabled_) {
      std::cout << "Auto-play ENABLED";
      if (Config::BOT_STOP_AFTER_SUPERNOVA_FOUND) {
        std::cout << " (will stop when supernova move is found)";
      }
      std::cout << std::endl;
      awaitingBoardChange_ = false;
    } else {
      std::cout << "Auto-play DISABLED" << std::endl;
    }
  }
}

void Application::Shutdown() {
  MemoryHook::Detach();
  cv::destroyAllWindows();
}

// ---------------------------------------------------------------------------
// Board editor
// ---------------------------------------------------------------------------
void Application::OnMouse(int event, int x, int y, int flags, void *userdata) {
  (void)flags;
  if (!userdata) {
    return;
  }
  static_cast<Application *>(userdata)->HandleEditorClick(event, x, y);
}

void Application::HandleEditorClick(int event, int x, int y) {
  if (event != cv::EVENT_LBUTTONDOWN && event != cv::EVENT_RBUTTONDOWN) {
    return;
  }

  // Palette (colour row, then state row).
  int ci = PaletteColorIndexAt(x, y, displayCols_);
  if (ci >= 0) {
    brushColorIndex_ = ci;
    brushStateIndex_ = 0; // picking a colour resets to "normal"
    return;
  }
  int si = PaletteStateIndexAt(x, y, displayCols_);
  if (si >= 0) {
    brushStateIndex_ = si;
    return;
  }

  // Board cell.
  if (y < paletteHeightPx_) {
    return;
  }
  int cell = (displayCellPx_ > 0) ? displayCellPx_ : 1;
  int c = x / cell;
  int r = (y - paletteHeightPx_) / cell;
  if (r < 0 || r >= Config::GRID_SIZE || c < 0 || c >= Config::GRID_SIZE) {
    return;
  }
  int i = r * Config::GRID_SIZE + c;

  if (event == cv::EVENT_RBUTTONDOWN) {
    pendingMask_[i] = 0; // erase
    return;
  }

  // Stage the current brush.
  const BrushStateDef &bs = kBrushStates[brushStateIndex_];
  pendingType_[i] =
      bs.hypercube ? GemType::HYPERCUBE : kBrushColors[brushColorIndex_].type;
  pendingState_[i] = bs.state;
  pendingMask_[i] = 1;
}

void Application::FlushPendingBoard() {
  uint8_t mask[64] = {};
  int color[64] = {};
  int state[64] = {};
  int n = 0;
  for (int i = 0; i < 64; ++i) {
    if (!pendingMask_[i]) {
      continue;
    }
    mask[i] = 1;
    color[i] = GemTypeToHookCode(pendingType_[i]);
    // A hypercube is BOTH type -1 and state 2 - writing only one of them leaves
    // the game with a gem it cannot render.
    state[i] = (pendingType_[i] == GemType::HYPERCUBE)
                   ? MemoryHook::kStateHypercube
                   : GemStateToHookCode(pendingState_[i]);
    ++n;
  }
  if (n == 0) {
    std::cout << "No staged cells to write." << std::endl;
    return;
  }
  int written = MemoryHook::WriteBoardCells(mask, color, state,
                                            (float)Config::CELL_SIZE);
  std::cout << "Wrote " << written << "/" << n << " staged cells to the board"
            << std::endl;
  pendingMask_.fill(0);
}

void Application::ClearPendingBoard() { pendingMask_.fill(0); }

void Application::ExecuteBestSwap() {
  if (analysis_.scoredSwaps.empty() || analysis_.bestSwapIndex < 0) {
    return;
  }

  const ScoredSwap &bestSwap = analysis_.scoredSwaps[analysis_.bestSwapIndex];
  const MatchableSwap &swap = bestSwap.swap;

  int x1 = swap.a.x * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  int y1 = swap.a.y * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  int x2 = swap.b.x * Config::CELL_SIZE + Config::CELL_SIZE / 2;
  int y2 = swap.b.y * Config::CELL_SIZE + Config::CELL_SIZE / 2;

  SendMouseClickToGame(gameWindow_, x1, y1, true);
  SendMouseClickToGame(gameWindow_, x1, y1, false);
  SendMouseClickToGame(gameWindow_, x2, y2, true);
  SendMouseClickToGame(gameWindow_, x2, y2, false);

  // Event-driven gating: from here the bot waits until the board in memory
  // reflects this swap before doing anything else. No fixed click delay.
  awaitingBoardChange_ = true;
  moveDispatchedAt_ = std::chrono::steady_clock::now();
  analysis_.stableFrameCount = 0;

  // Cascade validation: predict this move's result via simulation and start
  // observing the live board to compare against it.
  preMoveBoard_ = analysis_.board;
  lastSwap_ = swap;
  havePreMove_ = true;
  BoardState predicted;
  predictedClears_ = analysis_.board.simulateMove(swap, predicted);
  predictedBoard_ = predicted.toString();
  observedStates_.clear();
  observedBoards_.clear();
  observingCascade_ = true;
}

void Application::PrintCascadeComparison() {
  std::cout << "\n=== Cascade validation ===\n";
  std::cout << "Predicted clears: " << predictedClears_ << "\n";
  std::cout << "Predicted final board:\n" << predictedBoard_ << "\n";

  const std::string &actual = observedStates_.back();
  std::cout << "Actual distinct states: " << observedStates_.size() << "\n";
  std::cout << "Actual final board:\n" << actual << "\n";

  // Diff predicted vs actual final board (ignoring newlines).
  auto diffBoards = [](const std::string &pred, const std::string &act) {
    int diff = 0;
    size_t j = 0;
    for (char pc : pred) {
      if (pc == '\n')
        continue;
      while (j < act.size() && act[j] == '\n')
        ++j;
      if (j >= act.size())
        break;
      if (pc != act[j])
        ++diff;
      ++j;
    }
    return diff;
  };
  std::cout << "Cells differing from prediction (no refill): "
            << diffBoards(predictedBoard_, actual) << "/64\n";

  // Option B: re-run the simulation with the live refill injected, so the
  // prediction continues through the gems the game actually spawned instead of
  // stopping at the refill. The residual diff then measures only real model
  // error (cascade/clear ordering), not the RNG.
  if (havePreMove_ && !observedBoards_.empty()) {
    const BoardState &refill = observedBoards_.back();
    BoardState seeded;
    int seededClears =
        preMoveBoard_.simulateMoveSeeded(lastSwap_, refill, seeded);
    std::string seededStr = seeded.toString();
    std::cout << "Seeded clears (with live refill): " << seededClears << "\n";
    std::cout << "Seeded final board:\n" << seededStr << "\n";
    std::cout << "Cells differing from prediction (seeded): "
              << diffBoards(seededStr, actual) << "/64\n";
  }
  std::cout << "============================\n";
}

void Application::UpdateAutoPlay() {
  if (!autoPlayEnabled_) {
    return;
  }

  // A move is in flight: wait until the board in memory actually reflects it.
  // This replaces the old fixed click delay — we react to the real transition.
  if (awaitingBoardChange_) {
    bool stuck =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - moveDispatchedAt_)
            .count() > Config::BOT_MAX_WAIT_FOR_CHANGE_MS;
    if (!stuck && (!analysis_.boardValid ||
                   analysis_.board.equals(preMoveBoard_))) {
      return; // swap not applied yet — keep waiting
    }
    awaitingBoardChange_ = false; // swap registered (or we gave up)
  }

  if (!analysis_.boardValid) {
    return;
  }

  // When waiting for settle, require a board with no pending match and a couple
  // of identical reads (a debounce, not a frame-count heuristic).
  if (Config::BOT_WAIT_FOR_SETTLE) {
    if (analysis_.settling) {
      return;
    }
    if (analysis_.stableFrameCount < Config::BOT_STABLE_FRAMES_REQUIRED) {
      return;
    }
  }

  if (Config::BOT_STOP_AFTER_SUPERNOVA_FOUND && analysis_.bestSwapIndex >= 0) {
    const auto &bestSwap = analysis_.scoredSwaps[analysis_.bestSwapIndex];
    if (bestSwap.specialType == GemState::SUPERNOVA) {
      autoPlayEnabled_ = false;
      std::cout << "\nAuto-play STOPPED: Supernova move detected!" << std::endl;
      return;
    }
  }

  ExecuteBestSwap();
}
