#include "config.h"
#include "window_capture.h"
#include "gem_detector.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// ============================================================================
// Helper Functions
// ============================================================================

// Keep the game window active by sending activation messages
void KeepGameActive(HWND gameWindow)
{
    // Send WM_ACTIVATEAPP to make the game think it's active
    SendMessage(gameWindow, WM_ACTIVATEAPP, TRUE, 0);

    // Send WM_ACTIVATE with WA_ACTIVE
    SendMessage(gameWindow, WM_ACTIVATE, WA_ACTIVE, 0);

    // Optional: Send a fake mouse move to show "activity"
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(gameWindow, &pt);
    SendMessage(gameWindow, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
}

// Set window to always on top
void SetWindowAlwaysOnTop(HWND hwnd, bool onTop)
{
    SetWindowPos(hwnd,
                 onTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ============================================================================
// Timing Structure
// ============================================================================

struct TimingInfo
{
    double captureMs = 0;
    double cropMs = 0;
    double detectMs = 0;
    double drawMs = 0;
    double displayMs = 0;
    double totalMs = 0;
};

// ============================================================================
// Shared State for Threading
// ============================================================================

struct SharedFrameData
{
    // Double buffers for game board
    cv::Mat gameBoardBuffer[2];
    int captureIndex = 0;
    int processIndex = 1;

    std::mutex mutex;
    std::condition_variable cv;

    bool shouldExit = false;
    bool captureReady = false;

    // Display buffer
    cv::Mat displayReady;
    bool hasDisplayReady = false;

    // Previous frame for motion detection
    cv::Mat prevGameBoard;

    // Main thread display copy
    cv::Mat displayCopy;
};

// ============================================================================
// Capture Thread
// ============================================================================

void CaptureThreadFunc(HWND gameWindow, SharedFrameData &frameData, TimingInfo &timingInfo)
{
    const int ACTIVATION_INTERVAL_MS = 1000;
    auto lastActivation = std::chrono::high_resolution_clock::now();

    while (true)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();

        // Periodically keep game active
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivation).count() >= ACTIVATION_INTERVAL_MS)
        {
            KeepGameActive(gameWindow);
            lastActivation = now;
        }

        // Capture only the game board region directly (zero allocation, zero crop)
        auto captureStart = std::chrono::high_resolution_clock::now();
        bool captureSuccess = CaptureWindowRegionInto(gameWindow, Config::BOARD_X, Config::BOARD_Y,
                                                      Config::BOARD_WIDTH, Config::BOARD_HEIGHT,
                                                      frameData.gameBoardBuffer[frameData.captureIndex]);
        auto captureEnd = std::chrono::high_resolution_clock::now();

        if (!captureSuccess)
        {
            std::cerr << "Warning: Failed to capture window region" << std::endl;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(frameData.mutex);

            if (frameData.shouldExit)
                break;

            // Update timing (crop is now part of capture)
            timingInfo.captureMs = std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();
            timingInfo.cropMs = 0.0; // No separate crop needed

            frameData.captureReady = true;
        }
        frameData.cv.notify_one();
    }
}

// ============================================================================
// Process/Display Thread
// ============================================================================

void ProcessThreadFunc(SharedFrameData &frameData, TimingInfo &timingInfo, float &currentFps, int &frameCount,
                       std::chrono::high_resolution_clock::time_point &lastFpsUpdateTime)
{
    bool firstFrame = true;

    while (true)
    {
        std::unique_lock<std::mutex> lock(frameData.mutex);
        frameData.cv.wait(lock, [&]
                          { return frameData.captureReady || frameData.shouldExit; });

        if (frameData.shouldExit && !frameData.captureReady)
            break;

        // Swap indices first
        frameData.captureIndex = 1 - frameData.captureIndex;
        frameData.processIndex = 1 - frameData.processIndex;
        frameData.captureReady = false;

        lock.unlock();

        // Use direct reference to buffer (no copy needed - buffer won't change until next frame)
        cv::Mat &currentFrame = frameData.gameBoardBuffer[1 - frameData.processIndex]; // The buffer we just finished capturing

        if (!currentFrame.empty())
        {
            // Detect gems
            auto detectStart = std::chrono::high_resolution_clock::now();
            std::vector<std::vector<Gem>> gemGrid = DetectGems(currentFrame, frameData.prevGameBoard);
            auto detectEnd = std::chrono::high_resolution_clock::now();
            timingInfo.detectMs = std::chrono::duration<double, std::milli>(detectEnd - detectStart).count();

            // Draw into pre-allocated display buffer (protected by mutex)
            auto drawStart = std::chrono::high_resolution_clock::now();

            {
                std::lock_guard<std::mutex> displayLock(frameData.mutex);

                frameData.displayReady.setTo(cv::Scalar(20, 20, 20));
                currentFrame.copyTo(frameData.displayReady(cv::Rect(0, Config::BANNER_HEIGHT,
                                                                    currentFrame.cols,
                                                                    currentFrame.rows)));
                DrawBanner(frameData.displayReady, currentFps, timingInfo.captureMs, timingInfo.detectMs);

                if (Config::SHOW_DEBUG_OVERLAY)
                {
                    DrawDebugOverlay(frameData.displayReady, gemGrid);
                }

                frameData.hasDisplayReady = true;
            }

            auto drawEnd = std::chrono::high_resolution_clock::now();
            timingInfo.drawMs = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();

            // Update previous frame (only copy needed for motion detection)
            currentFrame.copyTo(frameData.prevGameBoard);
        }

        // Update FPS
        frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        auto fpsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsUpdateTime).count();
        if (fpsElapsed >= 500)
        {
            currentFps = (frameCount * 1000.0f) / fpsElapsed;
            frameCount = 0;
            lastFpsUpdateTime = now;

            std::cout << "Timing (ms): Capture=" << std::fixed << std::setprecision(2) << timingInfo.captureMs
                      << " Crop=" << timingInfo.cropMs
                      << " Detect=" << timingInfo.detectMs
                      << " Draw=" << timingInfo.drawMs
                      << " Display=" << timingInfo.displayMs
                      << " FPS=" << currentFps << std::endl;
        }
    }
}

// ============================================================================
// Main Program
// ============================================================================

int main()
{
    std::cout << "Bejeweled 3 Bot - Window Capture Module" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << std::endl;

    // Find the game window
    std::cout << "Searching for window: " << Config::WINDOW_TITLE << std::endl;
    HWND gameWindow = FindGameWindow(Config::WINDOW_TITLE);

    if (!gameWindow)
    {
        std::cerr << "Error: Could not find game window!" << std::endl;
        std::cerr << "Please make sure Bejeweled 3 is running and update WINDOW_TITLE constant in config.h." << std::endl;
        return -1;
    }

    std::cout << "Game window found!" << std::endl;
    std::cout << std::endl;

    // Set game window to always on top to keep it rendering
    SetWindowAlwaysOnTop(gameWindow, true);
    std::cout << "Set game window to always on top" << std::endl;

    // Get initial window size for info
    RECT windowRect;
    GetClientRect(gameWindow, &windowRect);
    std::cout << "Window size: " << (windowRect.right - windowRect.left)
              << "x" << (windowRect.bottom - windowRect.top) << std::endl;
    std::cout << "Crop region: (" << Config::BOARD_X << ", " << Config::BOARD_Y << ") "
              << Config::BOARD_WIDTH << "x" << Config::BOARD_HEIGHT << std::endl;
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  'q' - Quit" << std::endl;
    std::cout << "  'i' - Print window info" << std::endl;
    std::cout << "  'd' - Dump current gem grid to console" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: HSV color thresholds need calibration!" << std::endl;
    std::cout << "      Adjust color constants in config.h if gems aren't detected correctly." << std::endl;
    std::cout << std::endl;

    // Create display window
    cv::namedWindow(Config::DISPLAY_WINDOW_NAME, cv::WINDOW_NORMAL);

    // Shared frame data for threading
    SharedFrameData frameData;

    // Pre-allocate all buffers with proper sizes
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    // Capture a dummy frame to get exact format
    cv::Mat dummyCapture = CaptureWindow(gameWindow);
    if (dummyCapture.empty())
    {
        std::cerr << "Error: Failed to capture initial frame for buffer allocation" << std::endl;
        return -1;
    }
    cv::Mat dummyBoard = CropGameBoard(dummyCapture);

    // Pre-allocate all buffers with exact size and type
    int captureType = dummyCapture.type();
    int boardType = dummyBoard.type();

    for (int i = 0; i < 2; i++)
    {
        frameData.gameBoardBuffer[i] = cv::Mat(dummyBoard.size(), boardType);
    }
    frameData.prevGameBoard = cv::Mat(dummyBoard.size(), boardType);
    frameData.prevGameBoard.setTo(cv::Scalar(0, 0, 0, 0)); // Initialize to black
    // Double buffer for display (eliminate copy in main thread)
    frameData.displayReady = cv::Mat(dummyBoard.rows + Config::BANNER_HEIGHT, dummyBoard.cols, boardType);
    frameData.displayCopy = cv::Mat(dummyBoard.rows + Config::BANNER_HEIGHT, dummyBoard.cols, boardType);

    std::cout << "Pre-allocated buffers: Board=" << dummyBoard.size() << " type=" << boardType << std::endl;

    // Timing tracking
    TimingInfo timingInfo;

    // FPS tracking
    auto lastFpsUpdateTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    float currentFps = 0.0f;

    // Start capture thread
    std::thread captureThread(CaptureThreadFunc, gameWindow, std::ref(frameData), std::ref(timingInfo));

    // Start process thread
    std::thread processThread(ProcessThreadFunc, std::ref(frameData), std::ref(timingInfo), std::ref(currentFps),
                              std::ref(frameCount), std::ref(lastFpsUpdateTime));

    // Main thread: handle keyboard input and display
    bool running = true;
    bool shouldDisplay = false;
    while (running)
    {
        // Quick copy while holding mutex, then display without mutex
        {
            std::lock_guard<std::mutex> lock(frameData.mutex);
            if (frameData.hasDisplayReady)
            {
                frameData.displayReady.copyTo(frameData.displayCopy);
                frameData.hasDisplayReady = false;
                shouldDisplay = true;
            }
        }

        // Display outside of mutex so process thread isn't blocked
        if (shouldDisplay)
        {
            auto displayStart = std::chrono::high_resolution_clock::now();
            cv::imshow(Config::DISPLAY_WINDOW_NAME, frameData.displayCopy);
            auto displayEnd = std::chrono::high_resolution_clock::now();
            timingInfo.displayMs = std::chrono::duration<double, std::milli>(displayEnd - displayStart).count();
            shouldDisplay = false;
        }

        int key = cv::waitKey(1);

        if (key == 'q' || key == 'Q')
        {
            std::cout << "Exiting..." << std::endl;
            {
                std::lock_guard<std::mutex> lock(frameData.mutex);
                frameData.shouldExit = true;
            }
            frameData.cv.notify_one();
            running = false;
        }
        else if (key == 'i' || key == 'I')
        {
            std::cout << "Window info:" << std::endl;
            {
                std::lock_guard<std::mutex> lock(frameData.mutex);
                if (!frameData.gameBoardBuffer[frameData.processIndex].empty())
                {
                    std::cout << "  Game board size: " << frameData.gameBoardBuffer[frameData.processIndex].cols
                              << "x" << frameData.gameBoardBuffer[frameData.processIndex].rows << std::endl;
                }
            }
        }

        // Check if window was closed
        if (cv::getWindowProperty(Config::DISPLAY_WINDOW_NAME, cv::WND_PROP_VISIBLE) <= 0)
        {
            std::cout << "Window closed. Exiting..." << std::endl;
            {
                std::lock_guard<std::mutex> lock(frameData.mutex);
                frameData.shouldExit = true;
            }
            frameData.cv.notify_one();
            running = false;
        }
    }

    // Wait for threads to finish
    captureThread.join();
    processThread.join();

    cv::destroyAllWindows();
    return 0;
}
