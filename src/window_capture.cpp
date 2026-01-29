#include "window_capture.h"
#include "config.h"
#include <iostream>

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
  int maxX = std::min(Config::BOARD_X + Config::BOARD_WIDTH, fullWindow.cols);
  int maxY = std::min(Config::BOARD_Y + Config::BOARD_HEIGHT, fullWindow.rows);

  if (Config::BOARD_X >= fullWindow.cols || Config::BOARD_Y >= fullWindow.rows)
  {
    std::cerr << "Crop rectangle is outside window bounds!" << std::endl;
    return cv::Mat();
  }

  cv::Rect cropRect(
      Config::BOARD_X,
      Config::BOARD_Y,
      maxX - Config::BOARD_X,
      maxY - Config::BOARD_Y);

  return fullWindow(cropRect).clone();
}
