#include "config.h"
#include "window_capture.h"
#include "gem_detector.h"
#include "app/helpers.h"
#include "app/threads.h"
#include "types/timing.h"
#include "types/frame_data.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

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
