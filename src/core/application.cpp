#include "application.h"
#include "config.h"
#include "hook/memory_hook.h"
#include "window_helpers.h"
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

// Map the game's special-gem state code (0x228) to a GemState. Observed
// values: 0 = normal, 1 = flame, 2 = supernova, 4 = star.
GemState HookSpecialToGemState(int v) {
  switch (v) {
  case 1:
    return GemState::FLAME;
  case 2:
    return GemState::SUPERNOVA;
  case 4:
    return GemState::STAR;
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

// Draw the hook-read board as a colored 8x8 grid into `image`. `board` is the
// raw 64 color codes (0..6); `analysis` supplies the best-move highlight.
void DrawHookBoard(cv::Mat &image, const int *board, const int *states,
                   const BoardAnalysis *analysis, bool autoPlay) {
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
      std::string label = (v == -1) ? "H" : std::to_string(v);
      if (v != -1 && states && states[r * n + c] != 0) {
        int s = states[r * n + c];
        label += (s == 1) ? "F"   // flame
                 : (s == 2) ? "N" // supernova
                 : (s == 4) ? "S" // star
                            : "";
      }
      cv::putText(image, label,
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
  const int banner = 48;
  cv::Mat display(Config::GRID_SIZE * cell + banner, Config::GRID_SIZE * cell,
                  CV_8UC3);
  cv::namedWindow(Config::DISPLAY_WINDOW_NAME, cv::WINDOW_AUTOSIZE);

  std::cout << "Memory hook mode active. 'b'=auto-play 'p'=print 'm'=dump "
               "'q'=quit\n";

  while (running_) {
    // Read the board directly from game memory.
    int board[64];
    bool haveBoard = MemoryHook::ReadBoard(board);
    int states[64] = {};
    if (haveBoard) {
      MemoryHook::ReadSpecialState(states);
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

    DrawHookBoard(display, haveBoard ? board : nullptr,
                  haveBoard ? states : nullptr, &analysis_, autoPlayEnabled_);
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
  MemoryHook::Detach();
  cv::destroyAllWindows();
}

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

  auto now = std::chrono::high_resolution_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - lastClickTime_)
                     .count();

  if (clicksInProgress_ == 0 && elapsed >= Config::BOT_CLICK_DELAY_MS) {
    SendMouseClickToGame(gameWindow_, x1, y1, true);
    SendMouseClickToGame(gameWindow_, x1, y1, false);
    SendMouseClickToGame(gameWindow_, x2, y2, true);
    SendMouseClickToGame(gameWindow_, x2, y2, false);

    lastClickTime_ = now;
    clicksInProgress_ = 2;

    // Reset the stability counter so we wait for the board to settle again,
    // and enforce a minimum settle delay so cascades finish before we act.
    analysis_.stableFrameCount = 0;
    settleUntil_ = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(Config::BOT_SETTLE_DELAY_MS);

    // Cascade validation: predict this move's result via simulation and start
    // observing the live board to compare against it.
    BoardState predicted;
    predictedClears_ = analysis_.board.simulateMove(swap, predicted);
    predictedBoard_ = predicted.toString();
    observedStates_.clear();
    observingCascade_ = true;
  } else if (clicksInProgress_ == 2 && elapsed >= Config::BOT_CLICK_DELAY_MS) {
    clicksInProgress_ = 0;
  }
}

void Application::PrintCascadeComparison() {
  std::cout << "\n=== Cascade validation ===\n";
  std::cout << "Predicted clears: " << predictedClears_ << "\n";
  std::cout << "Predicted final board:\n" << predictedBoard_ << "\n";

  const std::string &actual = observedStates_.back();
  std::cout << "Actual distinct states: " << observedStates_.size() << "\n";
  std::cout << "Actual final board:\n" << actual << "\n";

  // Diff predicted vs actual final board (ignoring newlines).
  int diff = 0;
  size_t j = 0;
  for (char pc : predictedBoard_) {
    if (pc == '\n')
      continue;
    while (j < actual.size() && actual[j] == '\n')
      ++j;
    if (j >= actual.size())
      break;
    if (pc != actual[j])
      ++diff;
    ++j;
  }
  std::cout << "Cells differing from prediction: " << diff << "/64\n";
  std::cout << "============================\n";
}

void Application::UpdateAutoPlay() {
  if (!autoPlayEnabled_) {
    return;
  }

  // Don't act until the post-move settle delay has elapsed.
  if (std::chrono::steady_clock::now() < settleUntil_) {
    return;
  }

  // Don't act if the board has a pending match (cascade still resolving).
  if (analysis_.settling) {
    return;
  }

  if (!analysis_.boardValid ||
      analysis_.stableFrameCount < Config::BOT_STABLE_FRAMES_REQUIRED) {
    return;
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
