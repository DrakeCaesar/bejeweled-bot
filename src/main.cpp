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
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

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

struct TimerResolution
{
    explicit TimerResolution(UINT periodMs)
        : period(periodMs)
    {
        timeBeginPeriod(period);
    }

    ~TimerResolution()
    {
        timeEndPeriod(period);
    }

    UINT period;
};

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
    // Triple buffers for game board (circular rotation: capture -> process -> previous)
    cv::Mat gameBoardBuffer[3];
    int captureIndex = 0;
    int processIndex = 1;
    int prevIndex = 2;

    std::mutex mutex;
    std::condition_variable cv;

    bool shouldExit = false;
    bool captureReady = false;

    // Double buffers for display (zero-copy display)
    cv::Mat displayBuffer[2];
    int displayWriteIndex = 0;
    int displayReadIndex = 1;
    bool hasDisplayReady = false;

    // Desktop Duplication context
    DuplicationContext *dupContext = nullptr;
};

// ============================================================================
// Capture Thread
// ============================================================================

void CaptureThreadFunc(HWND gameWindow, SharedFrameData &frameData, TimingInfo &timingInfo)
{
    const int ACTIVATION_INTERVAL_MS = 1000;
    auto lastActivation = std::chrono::high_resolution_clock::now();

    const auto captureInterval = std::chrono::milliseconds(Config::CAPTURE_INTERVAL_MS);
    auto nextCaptureTime = std::chrono::steady_clock::now();

    // Use Desktop Duplication if available, fallback to BitBlt
    bool useDuplication = (frameData.dupContext != nullptr);

    const auto spinThreshold = std::chrono::microseconds(300);

    while (true)
    {
        // Pace capture to fixed interval (e.g., every 10 ms)
        auto nowSteady = std::chrono::steady_clock::now();
        if (nextCaptureTime < nowSteady)
        {
            nextCaptureTime = nowSteady;
        }

        auto timeToWait = nextCaptureTime - nowSteady;
        auto sleepUntil = (timeToWait > spinThreshold) ? (nextCaptureTime - spinThreshold) : nextCaptureTime;
        if (sleepUntil > nowSteady)
        {
            std::this_thread::sleep_until(sleepUntil);
        }

        while (std::chrono::steady_clock::now() < nextCaptureTime)
        {
            std::this_thread::yield();
        }

        nextCaptureTime += captureInterval;

        auto frameStart = std::chrono::high_resolution_clock::now();

        // Periodically keep game active
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActivation).count() >= ACTIVATION_INTERVAL_MS)
        {
            KeepGameActive(gameWindow);
            lastActivation = now;
        }

        bool captureSuccess = false;
        auto captureStart = std::chrono::high_resolution_clock::now();

        if (useDuplication)
        {
            // Convert window-relative coordinates to screen-absolute coordinates
            RECT windowRect;
            GetWindowRect(gameWindow, &windowRect);
            POINT clientOffset = {0, 0};
            ClientToScreen(gameWindow, &clientOffset);

            int screenX = clientOffset.x + Config::BOARD_X;
            int screenY = clientOffset.y + Config::BOARD_Y;

            // Desktop Duplication - blocks until new frame arrives (perfect sync!)
            captureSuccess = CaptureRegionFromDuplication(
                frameData.dupContext,
                screenX, screenY,
                Config::BOARD_WIDTH, Config::BOARD_HEIGHT,
                frameData.gameBoardBuffer[frameData.captureIndex],
                16 // 16ms timeout = ~60 FPS minimum
            );
        }
        else
        {
            // Fallback to BitBlt
            captureSuccess = CaptureWindowRegionInto(
                gameWindow,
                Config::BOARD_X, Config::BOARD_Y,
                Config::BOARD_WIDTH, Config::BOARD_HEIGHT,
                frameData.gameBoardBuffer[frameData.captureIndex]);
        }

        auto captureEnd = std::chrono::high_resolution_clock::now();

        if (!captureSuccess)
        {
            if (!useDuplication)
            {
                std::cerr << "Warning: Failed to capture window region" << std::endl;
            }
            // For duplication, timeout is normal, just continue
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(frameData.mutex);

            if (frameData.shouldExit)
                break;

            // Update timing
            timingInfo.captureMs = std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();
            timingInfo.cropMs = 0.0;

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

        // Rotate triple buffer indices: prev <- process <- capture <- next
        int oldPrev = frameData.prevIndex;
        frameData.prevIndex = frameData.processIndex;
        frameData.processIndex = frameData.captureIndex;
        frameData.captureIndex = oldPrev;
        frameData.captureReady = false;

        lock.unlock();

        // Use direct reference to buffers (no copy needed!)
        cv::Mat &currentFrame = frameData.gameBoardBuffer[frameData.processIndex];
        cv::Mat &prevFrame = frameData.gameBoardBuffer[frameData.prevIndex];

        if (!currentFrame.empty())
        {
            // Detect gems (using previous frame for motion detection)
            auto detectStart = std::chrono::high_resolution_clock::now();
            std::vector<std::vector<Gem>> gemGrid = DetectGems(currentFrame, prevFrame);
            auto detectEnd = std::chrono::high_resolution_clock::now();
            timingInfo.detectMs = std::chrono::duration<double, std::milli>(detectEnd - detectStart).count();

            // Draw into pre-allocated display buffer (protected by mutex)
            auto drawStart = std::chrono::high_resolution_clock::now();

            {
                std::lock_guard<std::mutex> displayLock(frameData.mutex);

                cv::Mat &displayBuf = frameData.displayBuffer[frameData.displayWriteIndex];
                displayBuf.setTo(cv::Scalar(20, 20, 20));
                currentFrame.copyTo(displayBuf(cv::Rect(0, Config::BANNER_HEIGHT,
                                                        currentFrame.cols,
                                                        currentFrame.rows)));
                DrawBanner(displayBuf, currentFps, timingInfo.captureMs, timingInfo.detectMs);

                if (Config::SHOW_DEBUG_OVERLAY)
                {
                    DrawDebugOverlay(displayBuf, gemGrid);
                }

                // Swap display buffer indices
                frameData.displayWriteIndex = 1 - frameData.displayWriteIndex;
                frameData.displayReadIndex = 1 - frameData.displayReadIndex;
                frameData.hasDisplayReady = true;
            }

            auto drawEnd = std::chrono::high_resolution_clock::now();
            timingInfo.drawMs = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();

            // No copy needed! Previous frame is already in gameBoardBuffer[prevIndex]
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
    TimerResolution timerResolution(1);

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

    // Detect which monitor the game window is on
    HMONITOR hMonitor = MonitorFromWindow(gameWindow, MONITOR_DEFAULTTOPRIMARY);
    int monitorIndex = 0;

    // Enumerate monitors to find the index
    struct MonitorEnumData
    {
        HMONITOR targetMonitor;
        int currentIndex;
        int foundIndex;
    };

    MonitorEnumData enumData = {hMonitor, 0, -1};

    EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMon, HDC hdc, LPRECT rect, LPARAM data) -> BOOL
                        {
                            MonitorEnumData *pData = (MonitorEnumData *)data;
                            if (hMon == pData->targetMonitor)
                            {
                                pData->foundIndex = pData->currentIndex;
                                return FALSE; // Stop enumeration
                            }
                            pData->currentIndex++;
                            return TRUE; // Continue enumeration
                        },
                        (LPARAM)&enumData);

    if (enumData.foundIndex >= 0)
    {
        monitorIndex = enumData.foundIndex;
        std::cout << "Game window detected on monitor " << monitorIndex << std::endl;
    }

    // Try to initialize Desktop Duplication for perfect frame sync (if enabled)
    if (Config::USE_DESKTOP_DUPLICATION)
    {
        std::cout << "Initializing Desktop Duplication API..." << std::endl;
        frameData.dupContext = InitializeDesktopDuplication(monitorIndex);
        if (frameData.dupContext)
        {
            std::cout << "Desktop Duplication enabled - capturing synced to display refresh!" << std::endl;
        }
        else
        {
            std::cout << "Desktop Duplication not available - falling back to BitBlt (may need admin rights)" << std::endl;
        }
    }
    else
    {
        std::cout << "Desktop Duplication disabled in config - using BitBlt" << std::endl;
    }

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

    // Triple buffer for game board (circular rotation eliminates prevGameBoard copy!)
    for (int i = 0; i < 3; i++)
    {
        frameData.gameBoardBuffer[i] = cv::Mat(dummyBoard.size(), boardType);
    }
    // Double buffer for display (zero-copy display)
    for (int i = 0; i < 2; i++)
    {
        frameData.displayBuffer[i] = cv::Mat(dummyBoard.rows + Config::BANNER_HEIGHT, dummyBoard.cols, boardType);
    }

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

    if (Config::BOOST_THREAD_PRIORITY)
    {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        SetThreadPriority(captureThread.native_handle(), THREAD_PRIORITY_HIGHEST);
        SetThreadPriority(processThread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
    }

    // Main thread: handle keyboard input and display
    bool running = true;
    while (running)
    {
        // Check if display is ready (no copy - direct display!)
        bool shouldDisplay = false;
        int displayIdx = 0;
        {
            std::lock_guard<std::mutex> lock(frameData.mutex);
            if (frameData.hasDisplayReady)
            {
                displayIdx = frameData.displayReadIndex;
                frameData.hasDisplayReady = false;
                shouldDisplay = true;
            }
        }

        // Display outside of mutex so process thread isn't blocked
        if (shouldDisplay)
        {
            auto displayStart = std::chrono::high_resolution_clock::now();
            cv::imshow(Config::DISPLAY_WINDOW_NAME, frameData.displayBuffer[displayIdx]);
            auto displayEnd = std::chrono::high_resolution_clock::now();
            timingInfo.displayMs = std::chrono::duration<double, std::milli>(displayEnd - displayStart).count();
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

    // Cleanup Desktop Duplication if it was initialized
    if (frameData.dupContext)
    {
        CleanupDesktopDuplication(frameData.dupContext);
    }

    cv::destroyAllWindows();
    return 0;
}
