#include "config.h"
#include "window_capture.h"
#include "gem_detector.h"
#include <iostream>
#include <iomanip>
#include <chrono>

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

    // Calculate frame delay for target FPS
    int frameDelay = 1000 / Config::TARGET_FPS;

    // Store previous frame for motion detection
    cv::Mat prevGameBoard;

    // FPS tracking
    auto lastFpsUpdateTime = std::chrono::high_resolution_clock::now();
    int frameCount = 0;
    float currentFps = 0.0f;

    // Timing tracking
    auto lastTimingPrintTime = std::chrono::high_resolution_clock::now();
    struct TimingInfo
    {
        double captureMs = 0;
        double cropMs = 0;
        double detectMs = 0;
        double drawMs = 0;
        double displayMs = 0;
        double totalMs = 0;
    } timingInfo;

    // Tracking for periodic game activation
    auto lastActivationTime = std::chrono::high_resolution_clock::now();
    const int ACTIVATION_INTERVAL_MS = 1000; // Send activation message every 1 second

    // Main capture loop
    while (true)
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        // Periodically send activation messages to keep game rendering
        auto timeSinceActivation = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       startTime - lastActivationTime)
                                       .count();
        if (timeSinceActivation >= ACTIVATION_INTERVAL_MS)
        {
            KeepGameActive(gameWindow);
            lastActivationTime = startTime;
        }

        // Capture the window
        auto captureStart = std::chrono::high_resolution_clock::now();
        cv::Mat fullWindow = CaptureWindow(gameWindow);
        auto captureEnd = std::chrono::high_resolution_clock::now();
        timingInfo.captureMs = std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();

        if (fullWindow.empty())
        {
            std::cerr << "Warning: Failed to capture window" << std::endl;
            cv::waitKey(frameDelay);
            continue;
        }

        // Crop to game board
        auto cropStart = std::chrono::high_resolution_clock::now();
        cv::Mat gameBoard = CropGameBoard(fullWindow);
        auto cropEnd = std::chrono::high_resolution_clock::now();
        timingInfo.cropMs = std::chrono::duration<double, std::milli>(cropEnd - cropStart).count();

        if (gameBoard.empty())
        {
            std::cerr << "Warning: Failed to crop game board" << std::endl;
            // Display full window instead
            cv::imshow(Config::DISPLAY_WINDOW_NAME, fullWindow);
        }
        else
        {
            // Detect gems in the grid
            auto detectStart = std::chrono::high_resolution_clock::now();
            std::vector<std::vector<Gem>> gemGrid = DetectGems(gameBoard, prevGameBoard);
            auto detectEnd = std::chrono::high_resolution_clock::now();
            timingInfo.detectMs = std::chrono::duration<double, std::milli>(detectEnd - detectStart).count();

            // Create display image with banner space at the top
            auto drawStart = std::chrono::high_resolution_clock::now();
            cv::Mat displayImage(gameBoard.rows + Config::BANNER_HEIGHT, gameBoard.cols, gameBoard.type(), cv::Scalar(20, 20, 20));

            // Copy the game board to below the banner
            gameBoard.copyTo(displayImage(cv::Rect(0, Config::BANNER_HEIGHT, gameBoard.cols, gameBoard.rows)));

            // Draw banner with FPS and timing info
            DrawBanner(displayImage, currentFps, timingInfo.captureMs, timingInfo.detectMs);

            // Draw debug overlay if enabled
            if (Config::SHOW_DEBUG_OVERLAY)
            {
                DrawDebugOverlay(displayImage, gemGrid);
            }

            auto drawEnd = std::chrono::high_resolution_clock::now();
            timingInfo.drawMs = std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();

            // Display the image
            auto displayStart = std::chrono::high_resolution_clock::now();
            cv::imshow(Config::DISPLAY_WINDOW_NAME, displayImage);
            auto displayEnd = std::chrono::high_resolution_clock::now();
            timingInfo.displayMs = std::chrono::duration<double, std::milli>(displayEnd - displayStart).count();

            // Store current frame for next iteration
            prevGameBoard = gameBoard.clone();
        }

        // Update FPS counter
        frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        auto fpsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsUpdateTime).count();
        if (fpsElapsed >= 500) // Update FPS every 500ms
        {
            currentFps = (frameCount * 1000.0f) / fpsElapsed;
            frameCount = 0;
            lastFpsUpdateTime = now;

            // Print timing information
            timingInfo.totalMs = std::chrono::duration<double, std::milli>(now - startTime).count();
            std::cout << "Timing (ms): Capture=" << std::fixed << std::setprecision(2) << timingInfo.captureMs
                      << " Crop=" << timingInfo.cropMs
                      << " Detect=" << timingInfo.detectMs
                      << " Draw=" << timingInfo.drawMs
                      << " Display=" << timingInfo.displayMs
                      << " Total=" << timingInfo.totalMs
                      << " FPS=" << currentFps << std::endl;
        }

        // Handle keyboard input
        int key = cv::waitKey(1);
        if (key == 'q' || key == 'Q')
        {
            std::cout << "Exiting..." << std::endl;
            break;
        }
        else if (key == 'i' || key == 'I')
        {
            std::cout << "Window info:" << std::endl;
            std::cout << "  Full window size: " << fullWindow.cols << "x" << fullWindow.rows << std::endl;
            if (!gameBoard.empty())
            {
                std::cout << "  Game board size: " << gameBoard.cols << "x" << gameBoard.rows << std::endl;
            }
        }
        else if (key == 'd' || key == 'D')
        {
            // Print current gem grid
            if (!gameBoard.empty() && !prevGameBoard.empty())
            {
                std::vector<std::vector<Gem>> gemGrid = DetectGems(gameBoard, prevGameBoard);
                PrintGemGrid(gemGrid);
            }
        }
        else if (key == 't' || key == 'T')
        {
            // Print detailed timing info
            timingInfo.totalMs = std::chrono::duration<double, std::milli>(now - startTime).count();
            std::cout << "\nDetailed Timing Information:" << std::endl;
            std::cout << "  Capture:  " << std::fixed << std::setprecision(2) << timingInfo.captureMs << " ms" << std::endl;
            std::cout << "  Crop:     " << timingInfo.cropMs << " ms" << std::endl;
            std::cout << "  Detect:   " << timingInfo.detectMs << " ms" << std::endl;
            std::cout << "  Draw:     " << timingInfo.drawMs << " ms" << std::endl;
            std::cout << "  Display:  " << timingInfo.displayMs << " ms" << std::endl;
            std::cout << "  Total:    " << timingInfo.totalMs << " ms" << std::endl;
            std::cout << "  FPS:      " << currentFps << std::endl
                      << std::endl;
        }

        // Check if window was closed
        if (cv::getWindowProperty(Config::DISPLAY_WINDOW_NAME, cv::WND_PROP_VISIBLE) <= 0)
        {
            std::cout << "Window closed. Exiting..." << std::endl;
            break;
        }
    }

    cv::destroyAllWindows();
    return 0;
}
