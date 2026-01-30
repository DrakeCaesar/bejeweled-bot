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
    // Rolling average over 100 frames
    static const int HISTORY_SIZE = 100;
    double captureHistory[HISTORY_SIZE] = {0};
    double detectHistory[HISTORY_SIZE] = {0};
    double drawHistory[HISTORY_SIZE] = {0};
    double displayHistory[HISTORY_SIZE] = {0};
    double fpsHistory[HISTORY_SIZE] = {0};
    int historyIndex = 0;
    int validFrameCount = 0; // Track how many frames we've actually collected

    // Current frame timings (for updating history)
    double captureMs = 0;
    double detectMs = 0;
    double drawMs = 0;
    double displayMs = 0;
    float currentFps = 0;

    // Computed averages
    double avgCaptureMs = 0;
    double avgDetectMs = 0;
    double avgDrawMs = 0;
    double avgDisplayMs = 0;
    float avgFps = 0;

    // Max widths for padding
    int maxFpsWidth = 5;     // "100.0" = 5 chars
    int maxCaptureWidth = 4; // "0.00" = 4 chars
    int maxDetectWidth = 4;
    int maxDrawWidth = 4;
    int maxDisplayWidth = 4;

    void updateAverages()
    {
        captureHistory[historyIndex] = captureMs;
        detectHistory[historyIndex] = detectMs;
        drawHistory[historyIndex] = drawMs;
        displayHistory[historyIndex] = displayMs;
        fpsHistory[historyIndex] = currentFps;

        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
        if (validFrameCount < HISTORY_SIZE)
            validFrameCount++;

        // Calculate averages over valid frames only
        int count = validFrameCount;
        avgCaptureMs = 0;
        avgDetectMs = 0;
        avgDrawMs = 0;
        avgDisplayMs = 0;
        avgFps = 0;

        for (int i = 0; i < count; i++)
        {
            avgCaptureMs += captureHistory[i];
            avgDetectMs += detectHistory[i];
            avgDrawMs += drawHistory[i];
            avgDisplayMs += displayHistory[i];
            avgFps += fpsHistory[i];
        }

        avgCaptureMs /= count;
        avgDetectMs /= count;
        avgDrawMs /= count;
        avgDisplayMs /= count;
        avgFps /= count;

        // Update max widths
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << avgFps;
        maxFpsWidth = std::max(maxFpsWidth, (int)oss.str().length());

        oss.str("");
        oss << std::setprecision(2) << avgCaptureMs;
        maxCaptureWidth = std::max(maxCaptureWidth, (int)oss.str().length());

        oss.str("");
        oss << avgDetectMs;
        maxDetectWidth = std::max(maxDetectWidth, (int)oss.str().length());

        oss.str("");
        oss << avgDrawMs;
        maxDrawWidth = std::max(maxDrawWidth, (int)oss.str().length());

        oss.str("");
        oss << avgDisplayMs;
        maxDisplayWidth = std::max(maxDisplayWidth, (int)oss.str().length());
    }
};

// ============================================================================
// Shared State for Threading
// ============================================================================

struct SharedFrameData
{
    // Triple buffers sized for display (banner + game board)
    // Capture writes to bottom region, banner drawn on top - zero copy display!
    cv::Mat gameBoardBuffer[3];
    cv::Mat rawBoardBuffer[3]; // Raw capture frames (no overlays)
    cv::Mat displayBuffer;     // Dedicated buffer for GUI display (avoids tearing)
    int captureIndex = 0;
    int processIndex = 1;
    int displayIndex = 2;
    int prevIndex = 2;

    std::mutex mutex;
    std::condition_variable cv;

    bool shouldExit = false;
    bool captureDone = false;
    bool processDone = false;
    bool displayDone = true; // wait for first processed frame
    bool hasDisplayReady = false;
    bool hasProcessedFrame = false;

    // Desktop Duplication context
    DuplicationContext *dupContext = nullptr;
};

static void TryRotateBuffers(SharedFrameData &frameData)
{
    if (frameData.captureDone && frameData.processDone && frameData.displayDone && frameData.hasProcessedFrame)
    {
        int oldDisplay = frameData.displayIndex;
        frameData.displayIndex = frameData.processIndex;
        frameData.processIndex = frameData.captureIndex;
        frameData.captureIndex = oldDisplay;
        frameData.prevIndex = frameData.displayIndex;

        frameData.captureDone = false;
        frameData.processDone = false;
        frameData.displayDone = false;
        frameData.hasDisplayReady = true;
    }
}

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

        int localCaptureIndex = 0;
        {
            std::unique_lock<std::mutex> lock(frameData.mutex);
            frameData.cv.wait(lock, [&]
                              { return frameData.shouldExit || !frameData.captureDone; });
            if (frameData.shouldExit)
                break;
            localCaptureIndex = frameData.captureIndex;
        }

        // Get ROI for game board region (below banner)
        cv::Mat gameBoardROI = frameData.rawBoardBuffer[localCaptureIndex](
            cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));

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
                gameBoardROI,
                16 // 16ms timeout = ~60 FPS minimum
            );
        }
        else
        {
            // Fallback to BitBlt - capture into ROI
            captureSuccess = CaptureWindowRegionInto(
                gameWindow,
                Config::BOARD_X, Config::BOARD_Y,
                Config::BOARD_WIDTH, Config::BOARD_HEIGHT,
                gameBoardROI);
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

            frameData.captureDone = true;
            TryRotateBuffers(frameData);
        }
        frameData.cv.notify_all();
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
        int localProcessIndex = 0;
        int localPrevIndex = 0;
        {
            std::unique_lock<std::mutex> lock(frameData.mutex);
            frameData.cv.wait(lock, [&]
                              { return frameData.shouldExit || (!frameData.processDone && (frameData.hasProcessedFrame || frameData.captureDone)); });

            if (frameData.shouldExit)
                break;

            if (!frameData.hasProcessedFrame)
            {
                // First frame: process the freshly captured buffer
                frameData.processIndex = frameData.captureIndex;
                localProcessIndex = frameData.processIndex;
                localPrevIndex = frameData.processIndex;
            }
            else
            {
                localProcessIndex = frameData.processIndex;
                localPrevIndex = frameData.prevIndex;
            }
        }

        // Get ROI references for game board region (excluding banner)
        cv::Mat currentFrame = frameData.rawBoardBuffer[localProcessIndex](
            cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));
        cv::Mat prevFrame = frameData.rawBoardBuffer[localPrevIndex](
            cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT));

        if (!currentFrame.empty())
        {
            // Detect gems (using previous frame for motion detection)
            auto detectStart = std::chrono::high_resolution_clock::now();
            std::vector<std::vector<Gem>> gemGrid = DetectGems(currentFrame, prevFrame);
            auto detectEnd = std::chrono::high_resolution_clock::now();
            timingInfo.detectMs = std::chrono::duration<double, std::milli>(detectEnd - detectStart).count();

            // Draw banner and overlay directly onto the buffer (zero-copy!)
            auto drawStart = std::chrono::high_resolution_clock::now();

            {
                std::lock_guard<std::mutex> displayLock(frameData.mutex);

                cv::Mat &fullBuffer = frameData.gameBoardBuffer[localProcessIndex];
                cv::Mat &rawFullBuffer = frameData.rawBoardBuffer[localProcessIndex];

                // Copy latest raw capture into the display buffer
                rawFullBuffer(cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT))
                    .copyTo(fullBuffer(cv::Rect(0, Config::BANNER_HEIGHT, Config::BOARD_WIDTH, Config::BOARD_HEIGHT)));

                // Clear banner area
                fullBuffer(cv::Rect(0, 0, fullBuffer.cols, Config::BANNER_HEIGHT)).setTo(cv::Scalar(20, 20, 20));

                // Draw banner and overlay directly on buffer
                DrawBanner(fullBuffer, timingInfo.avgFps, timingInfo.avgCaptureMs, timingInfo.avgDetectMs,
                           timingInfo.avgDrawMs, timingInfo.avgDisplayMs,
                           timingInfo.maxFpsWidth, timingInfo.maxCaptureWidth, timingInfo.maxDetectWidth,
                           timingInfo.maxDrawWidth, timingInfo.maxDisplayWidth);

                if (Config::SHOW_DEBUG_OVERLAY)
                {
                    DrawDebugOverlay(fullBuffer, gemGrid);
                }
            }

            auto drawEnd = std::chrono::high_resolution_clock::now();
            timingInfo.drawMs = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();

            // No copy needed! Previous frame is already in gameBoardBuffer[prevIndex]
        }

        {
            std::lock_guard<std::mutex> lock(frameData.mutex);
            frameData.processDone = true;
            frameData.hasProcessedFrame = true;
            TryRotateBuffers(frameData);
        }
        frameData.cv.notify_all();

        // Update rolling averages every frame
        frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        auto fpsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsUpdateTime).count();

        // Calculate instantaneous FPS for this frame
        timingInfo.currentFps = (frameCount * 1000.0f) / fpsElapsed;
        timingInfo.updateAverages();

        // Print to console every 500ms
        if (fpsElapsed >= 500)
        {
            currentFps = timingInfo.avgFps; // Use smoothed FPS
            frameCount = 0;
            lastFpsUpdateTime = now;

            std::cout << "FPS: " << std::fixed << std::setprecision(1) << std::setw(timingInfo.maxFpsWidth) << timingInfo.avgFps
                      << " | Capture: " << std::setprecision(2) << std::setw(timingInfo.maxCaptureWidth) << timingInfo.avgCaptureMs << "ms"
                      << " | Detect: " << std::setw(timingInfo.maxDetectWidth) << timingInfo.avgDetectMs << "ms"
                      << " | Draw: " << std::setw(timingInfo.maxDrawWidth) << timingInfo.avgDrawMs << "ms"
                      << " | Display: " << std::setw(timingInfo.maxDisplayWidth) << timingInfo.avgDisplayMs << "ms"
                      << std::endl;
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

    // Triple buffer for game board WITH banner space (zero-copy display!)
    // Buffers are 1064px tall (40px banner + 1024px game)
    for (int i = 0; i < 3; i++)
    {
        frameData.gameBoardBuffer[i] = cv::Mat(dummyBoard.rows + Config::BANNER_HEIGHT, dummyBoard.cols, boardType);
        frameData.rawBoardBuffer[i] = cv::Mat(dummyBoard.rows + Config::BANNER_HEIGHT, dummyBoard.cols, boardType);
    }
    frameData.displayBuffer = cv::Mat(dummyBoard.rows + Config::BANNER_HEIGHT, dummyBoard.cols, boardType);

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
        // Check if display is ready (zero-copy - direct display!)
        bool shouldDisplay = false;
        cv::Mat displayLocal;
        {
            std::lock_guard<std::mutex> lock(frameData.mutex);
            if (frameData.hasDisplayReady && !frameData.displayDone)
            {
                // Copy the completed frame into a dedicated display buffer
                frameData.gameBoardBuffer[frameData.displayIndex].copyTo(frameData.displayBuffer);
                // Clone for display to avoid GUI using a buffer that might be overwritten
                displayLocal = frameData.displayBuffer.clone();
                frameData.displayDone = true;
                frameData.hasDisplayReady = false;
                TryRotateBuffers(frameData);
                shouldDisplay = true;
            }
        }

        // Display outside of mutex so process thread isn't blocked
        if (shouldDisplay)
        {
            auto displayStart = std::chrono::high_resolution_clock::now();
            cv::imshow(Config::DISPLAY_WINDOW_NAME, displayLocal);
            auto displayEnd = std::chrono::high_resolution_clock::now();
            timingInfo.displayMs = std::chrono::duration<double, std::milli>(displayEnd - displayStart).count();
            frameData.cv.notify_all();
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
