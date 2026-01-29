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
    cv::Mat captureBuffer[2];
    cv::Mat gameBoardBuffer[2];
    int captureIndex = 0;
    int processIndex = 1;

    std::mutex mutex;
    std::condition_variable cv;

    bool shouldExit = false;
    bool captureReady = false;

    // Display data
    cv::Mat displayReady;
    bool hasDisplayReady = false;
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

        // Capture window
        auto captureStart = std::chrono::high_resolution_clock::now();
        cv::Mat fullWindow = CaptureWindow(gameWindow);
        auto captureEnd = std::chrono::high_resolution_clock::now();

        if (fullWindow.empty())
        {
            std::cerr << "Warning: Failed to capture window" << std::endl;
            continue;
        }

        // Crop to game board
        auto cropStart = std::chrono::high_resolution_clock::now();
        cv::Mat gameBoard = CropGameBoard(fullWindow);
        auto cropEnd = std::chrono::high_resolution_clock::now();

        {
            std::lock_guard<std::mutex> lock(frameData.mutex);

            if (frameData.shouldExit)
                break;

            frameData.captureBuffer[frameData.captureIndex] = fullWindow;
            frameData.gameBoardBuffer[frameData.captureIndex] = gameBoard;

            // Update timing
            timingInfo.captureMs = std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();
            timingInfo.cropMs = std::chrono::duration<double, std::milli>(cropEnd - cropStart).count();

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
    cv::Mat prevGameBoard;
    bool firstFrame = true;

    while (true)
    {
        std::unique_lock<std::mutex> lock(frameData.mutex);
        frameData.cv.wait(lock, [&]
                          { return frameData.captureReady || frameData.shouldExit; });

        if (frameData.shouldExit && !frameData.captureReady)
            break;

        int processIdx = frameData.processIndex;
        cv::Mat gameBoard = frameData.gameBoardBuffer[processIdx].clone();

        // Swap indices
        frameData.captureIndex = 1 - frameData.captureIndex;
        frameData.processIndex = 1 - frameData.processIndex;
        frameData.captureReady = false;

        lock.unlock();

        if (!gameBoard.empty())
        {
            // Detect gems
            auto detectStart = std::chrono::high_resolution_clock::now();
            std::vector<std::vector<Gem>> gemGrid = DetectGems(gameBoard, prevGameBoard);
            auto detectEnd = std::chrono::high_resolution_clock::now();
            timingInfo.detectMs = std::chrono::duration<double, std::milli>(detectEnd - detectStart).count();

            // Create display image
            auto drawStart = std::chrono::high_resolution_clock::now();
            cv::Mat displayImage(gameBoard.rows + Config::BANNER_HEIGHT, gameBoard.cols, gameBoard.type(), cv::Scalar(20, 20, 20));

            gameBoard.copyTo(displayImage(cv::Rect(0, Config::BANNER_HEIGHT, gameBoard.cols, gameBoard.rows)));
            DrawBanner(displayImage, currentFps, timingInfo.captureMs, timingInfo.detectMs);

            if (Config::SHOW_DEBUG_OVERLAY)
            {
                DrawDebugOverlay(displayImage, gemGrid);
            }

            auto drawEnd = std::chrono::high_resolution_clock::now();
            timingInfo.drawMs = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();

            // Store for main thread to display
            {
                std::lock_guard<std::mutex> displayLock(frameData.mutex);
                frameData.displayReady = displayImage;
                frameData.hasDisplayReady = true;
            }

            prevGameBoard = gameBoard;
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
    while (running)
    {
        // Non-blocking display update from process thread
        {
            std::lock_guard<std::mutex> lock(frameData.mutex);
            if (frameData.hasDisplayReady)
            {
                auto displayStart = std::chrono::high_resolution_clock::now();
                cv::imshow(Config::DISPLAY_WINDOW_NAME, frameData.displayReady);
                auto displayEnd = std::chrono::high_resolution_clock::now();
                timingInfo.displayMs = std::chrono::duration<double, std::milli>(displayEnd - displayStart).count();
                frameData.hasDisplayReady = false;
            }
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
                if (!frameData.captureBuffer[frameData.processIndex].empty())
                {
                    std::cout << "  Full window size: " << frameData.captureBuffer[frameData.processIndex].cols
                              << "x" << frameData.captureBuffer[frameData.processIndex].rows << std::endl;
                }
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
