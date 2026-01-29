#define NOMINMAX
#include <opencv2/opencv.hpp>
#include <Windows.h>
#include <iostream>
#include <string>

// ============================================================================
// CONFIGURATION CONSTANTS - Fill these out based on your setup
// ============================================================================

// Window title of the Bejeweled 3 game (or part of it)
const std::string WINDOW_TITLE = "Bejeweled 3";

// Game board crop rectangle (relative to the captured window)
// These values need to be calibrated based on your screen resolution and window size
const int BOARD_X = 100;      // X offset from left edge of window
const int BOARD_Y = 100;      // Y offset from top edge of window
const int BOARD_WIDTH = 800;  // Width of the game board area
const int BOARD_HEIGHT = 800; // Height of the game board area

// Display settings
const std::string DISPLAY_WINDOW_NAME = "Bejeweled 3 - Game Board";
const int TARGET_FPS = 30; // Target frames per second for capture

// ============================================================================
// Window Capture Functions
// ============================================================================

HWND FindGameWindow(const std::string &windowTitle)
{
    HWND hwnd = FindWindowA(NULL, windowTitle.c_str());

    if (hwnd == NULL)
    {
        std::cout << "Searching for window containing: " << windowTitle << std::endl;
        // If exact match fails, search for partial match
        hwnd = FindWindowA(NULL, NULL);
        while (hwnd != NULL)
        {
            char title[256];
            GetWindowTextA(hwnd, title, sizeof(title));
            std::string titleStr(title);

            if (titleStr.find(windowTitle) != std::string::npos && IsWindowVisible(hwnd))
            {
                std::cout << "Found window: " << titleStr << std::endl;
                return hwnd;
            }
            hwnd = GetWindow(hwnd, GW_HWNDNEXT);
        }
    }

    return hwnd;
}

cv::Mat CaptureWindow(HWND hwnd)
{
    if (!hwnd)
    {
        return cv::Mat();
    }

    // Get window dimensions
    RECT windowRect;
    GetClientRect(hwnd, &windowRect);
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;

    if (width <= 0 || height <= 0)
    {
        return cv::Mat();
    }

    // Get device context
    HDC hdcWindow = GetDC(hwnd);
    HDC hdcMemory = CreateCompatibleDC(hdcWindow);

    // Create bitmap
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
    HGDIOBJ oldBitmap = SelectObject(hdcMemory, hBitmap);

    // Copy window content to bitmap
    BitBlt(hdcMemory, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

    // Convert to OpenCV Mat
    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height; // Negative to flip image
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    cv::Mat mat(height, width, CV_8UC4);
    GetDIBits(hdcMemory, hBitmap, 0, height, mat.data, (BITMAPINFO *)&bi, DIB_RGB_COLORS);

    // Convert BGRA to BGR
    cv::Mat result;
    cv::cvtColor(mat, result, cv::COLOR_BGRA2BGR);

    // Cleanup
    SelectObject(hdcMemory, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMemory);
    ReleaseDC(hwnd, hdcWindow);

    return result;
}

cv::Mat CropGameBoard(const cv::Mat &fullWindow)
{
    if (fullWindow.empty())
    {
        return cv::Mat();
    }

    // Validate crop rectangle
    int maxX = std::min(BOARD_X + BOARD_WIDTH, fullWindow.cols);
    int maxY = std::min(BOARD_Y + BOARD_HEIGHT, fullWindow.rows);

    if (BOARD_X >= fullWindow.cols || BOARD_Y >= fullWindow.rows)
    {
        std::cerr << "Crop rectangle is outside window bounds!" << std::endl;
        return cv::Mat();
    }

    cv::Rect cropRect(
        BOARD_X,
        BOARD_Y,
        maxX - BOARD_X,
        maxY - BOARD_Y);

    return fullWindow(cropRect).clone();
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
    std::cout << "Searching for window: " << WINDOW_TITLE << std::endl;
    HWND gameWindow = FindGameWindow(WINDOW_TITLE);

    if (!gameWindow)
    {
        std::cerr << "Error: Could not find game window!" << std::endl;
        std::cerr << "Please make sure Bejeweled 3 is running and update WINDOW_TITLE constant." << std::endl;
        return -1;
    }

    std::cout << "Game window found!" << std::endl;
    std::cout << std::endl;

    // Get initial window size for info
    RECT windowRect;
    GetClientRect(gameWindow, &windowRect);
    std::cout << "Window size: " << (windowRect.right - windowRect.left)
              << "x" << (windowRect.bottom - windowRect.top) << std::endl;
    std::cout << "Crop region: (" << BOARD_X << ", " << BOARD_Y << ") "
              << BOARD_WIDTH << "x" << BOARD_HEIGHT << std::endl;
    std::cout << std::endl;
    std::cout << "Press 'q' to quit, 'i' to print window info" << std::endl;
    std::cout << std::endl;

    // Create display window
    cv::namedWindow(DISPLAY_WINDOW_NAME, cv::WINDOW_NORMAL);

    // Calculate frame delay for target FPS
    int frameDelay = 1000 / TARGET_FPS;

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
            cv::imshow(DISPLAY_WINDOW_NAME, fullWindow);
        }
        else
        {
            // Display cropped game board
            cv::imshow(DISPLAY_WINDOW_NAME, gameBoard);
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
