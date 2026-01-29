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
const int BOARD_X = 523;       // X offset from left edge of window
const int BOARD_Y = 75;        // Y offset from top edge of window
const int BOARD_WIDTH = 1024;  // Width of the game board area (1024x1024)
const int BOARD_HEIGHT = 1024; // Height of the game board area

// Grid configuration
const int GRID_SIZE = 8;   // 8x8 grid
const int CELL_SIZE = 128; // Each cell is 128x128 pixels

// Gem detection settings
const int GEM_CENTER_SIZE = 80;      // Size of center region to sample (80x80 from center of 128x128 cell)
const float MOTION_THRESHOLD = 5.0f; // Pixel difference threshold to detect motion

// Color thresholds for gem classification (HSV color space)
// These need to be calibrated based on actual game colors
const cv::Scalar RED_GEM_HSV_MIN = cv::Scalar(0, 100, 100);
const cv::Scalar RED_GEM_HSV_MAX = cv::Scalar(10, 255, 255);

const cv::Scalar ORANGE_GEM_HSV_MIN = cv::Scalar(10, 100, 100);
const cv::Scalar ORANGE_GEM_HSV_MAX = cv::Scalar(25, 255, 255);

const cv::Scalar YELLOW_GEM_HSV_MIN = cv::Scalar(25, 100, 100);
const cv::Scalar YELLOW_GEM_HSV_MAX = cv::Scalar(35, 255, 255);

const cv::Scalar GREEN_GEM_HSV_MIN = cv::Scalar(35, 100, 100);
const cv::Scalar GREEN_GEM_HSV_MAX = cv::Scalar(85, 255, 255);

const cv::Scalar BLUE_GEM_HSV_MIN = cv::Scalar(85, 100, 100);
const cv::Scalar BLUE_GEM_HSV_MAX = cv::Scalar(130, 255, 255);

const cv::Scalar PURPLE_GEM_HSV_MIN = cv::Scalar(130, 100, 100);
const cv::Scalar PURPLE_GEM_HSV_MAX = cv::Scalar(160, 255, 255);

const cv::Scalar WHITE_GEM_HSV_MIN = cv::Scalar(0, 0, 200);
const cv::Scalar WHITE_GEM_HSV_MAX = cv::Scalar(180, 30, 255);

// Display settings
const std::string DISPLAY_WINDOW_NAME = "Bejeweled 3 - Game Board";
const int TARGET_FPS = 30;            // Target frames per second for capture
const bool SHOW_DEBUG_OVERLAY = true; // Show grid and detection overlay

// ============================================================================
// Gem Classification
// ============================================================================

enum class GemType
{
    EMPTY,
    RED,
    ORANGE,
    YELLOW,
    GREEN,
    BLUE,
    PURPLE,
    WHITE,
    UNKNOWN
};

enum class GemState
{
    NORMAL,
    FLAME,     // For future: flame gem detection
    LIGHTNING, // For future: lightning gem detection
    STAR,      // For future: star gem detection
    HYPERCUBE  // For future: hypercube detection
};

struct Gem
{
    GemType type;
    GemState state;
    bool isMoving;
    cv::Point gridPos;   // Grid position (0-7, 0-7)
    cv::Rect roi;        // Region of interest in image
    cv::Scalar avgColor; // Average color for debugging
};

std::string GemTypeToString(GemType type)
{
    switch (type)
    {
    case GemType::RED:
        return "RED";
    case GemType::ORANGE:
        return "ORANGE";
    case GemType::YELLOW:
        return "YELLOW";
    case GemType::GREEN:
        return "GREEN";
    case GemType::BLUE:
        return "BLUE";
    case GemType::PURPLE:
        return "PURPLE";
    case GemType::WHITE:
        return "WHITE";
    case GemType::EMPTY:
        return "EMPTY";
    default:
        return "UNKNOWN";
    }
}

cv::Scalar GemTypeToColor(GemType type)
{
    switch (type)
    {
    case GemType::RED:
        return cv::Scalar(0, 0, 255);
    case GemType::ORANGE:
        return cv::Scalar(0, 165, 255);
    case GemType::YELLOW:
        return cv::Scalar(0, 255, 255);
    case GemType::GREEN:
        return cv::Scalar(0, 255, 0);
    case GemType::BLUE:
        return cv::Scalar(255, 0, 0);
    case GemType::PURPLE:
        return cv::Scalar(255, 0, 255);
    case GemType::WHITE:
        return cv::Scalar(255, 255, 255);
    default:
        return cv::Scalar(128, 128, 128);
    }
}

GemType ClassifyGemByColor(const cv::Mat &gemRegion)
{
    if (gemRegion.empty())
    {
        return GemType::UNKNOWN;
    }

    // Convert to HSV for better color classification
    cv::Mat hsv;
    cv::cvtColor(gemRegion, hsv, cv::COLOR_BGR2HSV);

    // Calculate the percentage of pixels matching each color range
    auto calculateColorMatch = [&hsv](const cv::Scalar &minHSV, const cv::Scalar &maxHSV) -> float
    {
        cv::Mat mask;
        cv::inRange(hsv, minHSV, maxHSV, mask);
        int matchingPixels = cv::countNonZero(mask);
        return (float)matchingPixels / (hsv.rows * hsv.cols);
    };

    // Check each gem color
    float redMatch = calculateColorMatch(RED_GEM_HSV_MIN, RED_GEM_HSV_MAX);
    float orangeMatch = calculateColorMatch(ORANGE_GEM_HSV_MIN, ORANGE_GEM_HSV_MAX);
    float yellowMatch = calculateColorMatch(YELLOW_GEM_HSV_MIN, YELLOW_GEM_HSV_MAX);
    float greenMatch = calculateColorMatch(GREEN_GEM_HSV_MIN, GREEN_GEM_HSV_MAX);
    float blueMatch = calculateColorMatch(BLUE_GEM_HSV_MIN, BLUE_GEM_HSV_MAX);
    float purpleMatch = calculateColorMatch(PURPLE_GEM_HSV_MIN, PURPLE_GEM_HSV_MAX);
    float whiteMatch = calculateColorMatch(WHITE_GEM_HSV_MIN, WHITE_GEM_HSV_MAX);

    // Find the best match
    float maxMatch = 0.1f; // Minimum threshold
    GemType bestType = GemType::UNKNOWN;

    if (redMatch > maxMatch)
    {
        maxMatch = redMatch;
        bestType = GemType::RED;
    }
    if (orangeMatch > maxMatch)
    {
        maxMatch = orangeMatch;
        bestType = GemType::ORANGE;
    }
    if (yellowMatch > maxMatch)
    {
        maxMatch = yellowMatch;
        bestType = GemType::YELLOW;
    }
    if (greenMatch > maxMatch)
    {
        maxMatch = greenMatch;
        bestType = GemType::GREEN;
    }
    if (blueMatch > maxMatch)
    {
        maxMatch = blueMatch;
        bestType = GemType::BLUE;
    }
    if (purpleMatch > maxMatch)
    {
        maxMatch = purpleMatch;
        bestType = GemType::PURPLE;
    }
    if (whiteMatch > maxMatch)
    {
        maxMatch = whiteMatch;
        bestType = GemType::WHITE;
    }

    return bestType;
}

bool IsGemMoving(const cv::Mat &prevFrame, const cv::Mat &currFrame, const cv::Rect &roi)
{
    if (prevFrame.empty() || currFrame.empty())
    {
        return false;
    }

    // Ensure ROI is within bounds
    cv::Rect safeRoi = roi & cv::Rect(0, 0, currFrame.cols, currFrame.rows);
    if (safeRoi.width <= 0 || safeRoi.height <= 0)
    {
        return false;
    }

    cv::Mat prevRegion = prevFrame(safeRoi);
    cv::Mat currRegion = currFrame(safeRoi);

    // Calculate absolute difference
    cv::Mat diff;
    cv::absdiff(prevRegion, currRegion, diff);

    // Convert to grayscale and calculate mean difference
    cv::Mat grayDiff;
    cv::cvtColor(diff, grayDiff, cv::COLOR_BGR2GRAY);

    cv::Scalar meanDiff = cv::mean(grayDiff);

    return meanDiff[0] > MOTION_THRESHOLD;
}

std::vector<std::vector<Gem>> DetectGems(const cv::Mat &gameBoard, const cv::Mat &prevBoard)
{
    std::vector<std::vector<Gem>> grid(GRID_SIZE, std::vector<Gem>(GRID_SIZE));

    for (int row = 0; row < GRID_SIZE; row++)
    {
        for (int col = 0; col < GRID_SIZE; col++)
        {
            Gem &gem = grid[row][col];
            gem.gridPos = cv::Point(col, row);
            gem.state = GemState::NORMAL;

            // Calculate cell center
            int cellX = col * CELL_SIZE;
            int cellY = row * CELL_SIZE;
            int centerX = cellX + (CELL_SIZE - GEM_CENTER_SIZE) / 2;
            int centerY = cellY + (CELL_SIZE - GEM_CENTER_SIZE) / 2;

            // Define ROI for gem center
            gem.roi = cv::Rect(centerX, centerY, GEM_CENTER_SIZE, GEM_CENTER_SIZE);

            // Check if ROI is valid
            if (gem.roi.x + gem.roi.width > gameBoard.cols ||
                gem.roi.y + gem.roi.height > gameBoard.rows)
            {
                gem.type = GemType::UNKNOWN;
                gem.isMoving = false;
                continue;
            }

            // Check for motion
            gem.isMoving = IsGemMoving(prevBoard, gameBoard, gem.roi);

            // Only classify if gem is not moving
            if (!gem.isMoving)
            {
                cv::Mat gemRegion = gameBoard(gem.roi);
                gem.type = ClassifyGemByColor(gemRegion);
                gem.avgColor = cv::mean(gemRegion);
            }
            else
            {
                gem.type = GemType::UNKNOWN;
            }
        }
    }

    return grid;
}

void DrawDebugOverlay(cv::Mat &image, const std::vector<std::vector<Gem>> &grid)
{
    // Draw grid lines
    for (int i = 0; i <= GRID_SIZE; i++)
    {
        int pos = i * CELL_SIZE;
        cv::line(image, cv::Point(pos, 0), cv::Point(pos, image.rows),
                 cv::Scalar(100, 100, 100), 1);
        cv::line(image, cv::Point(0, pos), cv::Point(image.cols, pos),
                 cv::Scalar(100, 100, 100), 1);
    }

    // Draw gem information
    for (int row = 0; row < GRID_SIZE; row++)
    {
        for (int col = 0; col < GRID_SIZE; col++)
        {
            const Gem &gem = grid[row][col];

            // Draw center ROI
            cv::rectangle(image, gem.roi,
                          gem.isMoving ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0),
                          2);

            // Draw gem type label
            if (!gem.isMoving && gem.type != GemType::UNKNOWN)
            {
                cv::Point labelPos(col * CELL_SIZE + 5, row * CELL_SIZE + 20);
                cv::putText(image, GemTypeToString(gem.type), labelPos,
                            cv::FONT_HERSHEY_SIMPLEX, 0.4,
                            GemTypeToColor(gem.type), 1);
            }
            else if (gem.isMoving)
            {
                cv::Point labelPos(col * CELL_SIZE + 5, row * CELL_SIZE + 20);
                cv::putText(image, "MOVING", labelPos,
                            cv::FONT_HERSHEY_SIMPLEX, 0.4,
                            cv::Scalar(0, 0, 255), 1);
            }
        }
    }
}

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
    std::cout << "Controls:" << std::endl;
    std::cout << "  'q' - Quit" << std::endl;
    std::cout << "  'i' - Print window info" << std::endl;
    std::cout << "  'd' - Dump current gem grid to console" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: HSV color thresholds need calibration!" << std::endl;
    std::cout << "      Adjust color constants in main.cpp if gems aren't detected correctly." << std::endl;
    std::cout << std::endl;

    // Create display window
    cv::namedWindow(DISPLAY_WINDOW_NAME, cv::WINDOW_NORMAL);

    // Calculate frame delay for target FPS
    int frameDelay = 1000 / TARGET_FPS;

    // Store previous frame for motion detection
    cv::Mat prevGameBoard;

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
            // Detect gems in the grid
            std::vector<std::vector<Gem>> gemGrid = DetectGems(gameBoard, prevGameBoard);

            // Create display image
            cv::Mat displayImage = gameBoard.clone();

            // Draw debug overlay if enabled
            if (SHOW_DEBUG_OVERLAY)
            {
                DrawDebugOverlay(displayImage, gemGrid);
            }

            // Display the image
            cv::imshow(DISPLAY_WINDOW_NAME, displayImage);

            // Store current frame for next iteration
            prevGameBoard = gameBoard.clone();
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
                std::cout << "\nCurrent Gem Grid:" << std::endl;
                std::vector<std::vector<Gem>> gemGrid = DetectGems(gameBoard, prevGameBoard);
                for (int row = 0; row < GRID_SIZE; row++)
                {
                    for (int col = 0; col < GRID_SIZE; col++)
                    {
                        const Gem &gem = gemGrid[row][col];
                        if (gem.isMoving)
                        {
                            std::cout << "  [M]";
                        }
                        else
                        {
                            std::cout << "  [" << GemTypeToString(gem.type).substr(0, 1) << "]";
                        }
                    }
                    std::cout << std::endl;
                }
                std::cout << std::endl;
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
