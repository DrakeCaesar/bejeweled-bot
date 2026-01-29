#include "config.h"
#include "window_capture.h"
#include "gem_detector.h"
#include <iostream>
#include <chrono>

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

    // Main capture loop
    while (true)
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        // Capture the window
        cv::Mat fullWindow = CaptureWindow(gameWindow);

        if (fullWindow.empty())
        {
            std::cerr << "Warning: Failed to capture window" << std::endl;
            cv::waitKey(frameDelay);
            continue;
        }

        // Crop to game board
        cv::Mat gameBoard = CropGameBoard(fullWindow);

        if (gameBoard.empty())
        {
            std::cerr << "Warning: Failed to crop game board" << std::endl;
            // Display full window instead
            cv::imshow(Config::DISPLAY_WINDOW_NAME, fullWindow);
        }
        else
        {
            // Detect gems in the grid
            std::vector<std::vector<Gem>> gemGrid = DetectGems(gameBoard, prevGameBoard);

            // Create display image with banner space at the top
            cv::Mat displayImage(gameBoard.rows + Config::BANNER_HEIGHT, gameBoard.cols, gameBoard.type(), cv::Scalar(20, 20, 20));

            // Copy the game board to below the banner
            gameBoard.copyTo(displayImage(cv::Rect(0, Config::BANNER_HEIGHT, gameBoard.cols, gameBoard.rows)));

            // Draw banner with FPS
            DrawBanner(displayImage, currentFps);

            // Draw debug overlay if enabled
            if (Config::SHOW_DEBUG_OVERLAY)
            {
                DrawDebugOverlay(displayImage, gemGrid);
            }

            // Display the image
            cv::imshow(Config::DISPLAY_WINDOW_NAME, displayImage);

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

        // Check if window was closed
        if (cv::getWindowProperty(Config::DISPLAY_WINDOW_NAME, cv::WND_PROP_VISIBLE) <= 0)
        {
            std::cout << "Window closed. Exiting..." << std::endl;
            break;
        }

        // Maintain target FPS
        auto endTime = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        int waitTime = frameDelay - static_cast<int>(elapsed);
        if (waitTime > 0)
        {
            cv::waitKey(waitTime);
        }
    }

    cv::destroyAllWindows();
    return 0;
}
