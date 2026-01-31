#include "window_capture.h"
#include "../config.h"
#include <iostream>
#include <mmsystem.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

// ============================================================================
// Window Finding
// ============================================================================

HWND FindGameWindow(const std::string &windowTitle) {
  HWND hwnd = FindWindowA(NULL, windowTitle.c_str());

  if (hwnd == NULL) {
    std::cout << "Searching for window containing: " << windowTitle
              << std::endl;
    // If exact match fails, search for partial match
    hwnd = FindWindowA(NULL, NULL);
    while (hwnd != NULL) {
      char title[256];
      GetWindowTextA(hwnd, title, sizeof(title));
      std::string titleStr(title);

      if (titleStr.find(windowTitle) != std::string::npos &&
          IsWindowVisible(hwnd)) {
        std::cout << "Found window: " << titleStr << std::endl;
        return hwnd;
      }
      hwnd = GetWindow(hwnd, GW_HWNDNEXT);
    }
  }

  return hwnd;
}

// ============================================================================
// Basic Window Capture (BitBlt)
// ============================================================================

cv::Mat CaptureWindow(HWND hwnd) {
  if (!hwnd) {
    return cv::Mat();
  }

  RECT windowRect;
  GetClientRect(hwnd, &windowRect);
  int width = windowRect.right - windowRect.left;
  int height = windowRect.bottom - windowRect.top;

  if (width <= 0 || height <= 0) {
    return cv::Mat();
  }

  HDC hdcWindow = GetDC(hwnd);
  HDC hdcMemory = CreateCompatibleDC(hdcWindow);

  HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
  HGDIOBJ oldBitmap = SelectObject(hdcMemory, hBitmap);

  BitBlt(hdcMemory, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

  BITMAPINFOHEADER bi;
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = width;
  bi.biHeight = -height;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = 0;
  bi.biXPelsPerMeter = 0;
  bi.biYPelsPerMeter = 0;
  bi.biClrUsed = 0;
  bi.biClrImportant = 0;

  cv::Mat mat(height, width, CV_8UC4);
  GetDIBits(hdcMemory, hBitmap, 0, height, mat.data, (BITMAPINFO *)&bi,
            DIB_RGB_COLORS);

  cv::Mat result;
  cv::cvtColor(mat, result, cv::COLOR_BGRA2BGR);

  SelectObject(hdcMemory, oldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(hdcMemory);
  ReleaseDC(hwnd, hdcWindow);

  return result;
}

bool CaptureWindowInto(HWND hwnd, cv::Mat &outputBuffer) {
  if (!hwnd || outputBuffer.empty()) {
    return false;
  }

  RECT windowRect;
  GetClientRect(hwnd, &windowRect);
  int width = windowRect.right - windowRect.left;
  int height = windowRect.bottom - windowRect.top;

  if (width <= 0 || height <= 0) {
    return false;
  }

  if (outputBuffer.rows != height || outputBuffer.cols != width ||
      outputBuffer.type() != CV_8UC3) {
    return false;
  }

  HDC hdcWindow = GetDC(hwnd);
  HDC hdcMemory = CreateCompatibleDC(hdcWindow);

  HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
  HGDIOBJ oldBitmap = SelectObject(hdcMemory, hBitmap);

  BitBlt(hdcMemory, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

  BITMAPINFOHEADER bi;
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = width;
  bi.biHeight = -height;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = 0;
  bi.biXPelsPerMeter = 0;
  bi.biYPelsPerMeter = 0;
  bi.biClrUsed = 0;
  bi.biClrImportant = 0;

  // Reuse static buffer to avoid repeated allocations
  thread_local cv::Mat temp;
  if (temp.rows != height || temp.cols != width || temp.type() != CV_8UC4) {
    temp.create(height, width, CV_8UC4);
  }
  GetDIBits(hdcMemory, hBitmap, 0, height, temp.data, (BITMAPINFO *)&bi,
            DIB_RGB_COLORS);

  cv::cvtColor(temp, outputBuffer, cv::COLOR_BGRA2BGR);

  SelectObject(hdcMemory, oldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(hdcMemory);
  ReleaseDC(hwnd, hdcWindow);

  return true;
}

bool CaptureWindowRegionInto(HWND hwnd, int x, int y, int width, int height,
                             cv::Mat &outputBuffer) {
  if (!hwnd || outputBuffer.empty()) {
    return false;
  }

  if (outputBuffer.rows != height || outputBuffer.cols != width ||
      outputBuffer.type() != CV_8UC3) {
    return false;
  }

  HDC hdcWindow = GetDC(hwnd);
  HDC hdcMemory = CreateCompatibleDC(hdcWindow);

  HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
  HGDIOBJ oldBitmap = SelectObject(hdcMemory, hBitmap);

  BitBlt(hdcMemory, 0, 0, width, height, hdcWindow, x, y, SRCCOPY);

  BITMAPINFOHEADER bi;
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = width;
  bi.biHeight = -height;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = 0;
  bi.biXPelsPerMeter = 0;
  bi.biYPelsPerMeter = 0;
  bi.biClrUsed = 0;
  bi.biClrImportant = 0;

  // Reuse static buffer to avoid repeated allocations
  thread_local cv::Mat temp;
  if (temp.rows != height || temp.cols != width || temp.type() != CV_8UC4) {
    temp.create(height, width, CV_8UC4);
  }
  GetDIBits(hdcMemory, hBitmap, 0, height, temp.data, (BITMAPINFO *)&bi,
            DIB_RGB_COLORS);

  cv::cvtColor(temp, outputBuffer, cv::COLOR_BGRA2BGR);

  SelectObject(hdcMemory, oldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(hdcMemory);
  ReleaseDC(hwnd, hdcWindow);

  return true;
}

cv::Mat CropGameBoard(const cv::Mat &fullWindow) {
  if (fullWindow.empty()) {
    return cv::Mat();
  }

  int maxX = std::min(Config::BOARD_X + Config::BOARD_WIDTH, fullWindow.cols);
  int maxY = std::min(Config::BOARD_Y + Config::BOARD_HEIGHT, fullWindow.rows);

  if (Config::BOARD_X >= fullWindow.cols ||
      Config::BOARD_Y >= fullWindow.rows) {
    std::cerr << "Crop rectangle is outside window bounds!" << std::endl;
    return cv::Mat();
  }

  cv::Rect cropRect(Config::BOARD_X, Config::BOARD_Y, maxX - Config::BOARD_X,
                    maxY - Config::BOARD_Y);

  return fullWindow(cropRect).clone();
}

// ============================================================================
// Desktop Duplication API (DXGI)
// ============================================================================

DuplicationContext *InitializeDesktopDuplication(int monitorIndex) {
  DuplicationContext *ctx = new DuplicationContext();

  D3D_FEATURE_LEVEL featureLevel;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                 nullptr, 0, D3D11_SDK_VERSION, &ctx->device,
                                 &featureLevel, &ctx->context);

  if (FAILED(hr)) {
    std::cerr << "Failed to create D3D11 device" << std::endl;
    delete ctx;
    return nullptr;
  }

  IDXGIDevice *dxgiDevice = nullptr;
  hr = ctx->device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDevice);
  if (FAILED(hr)) {
    std::cerr << "Failed to get DXGI device" << std::endl;
    ctx->device->Release();
    delete ctx;
    return nullptr;
  }

  IDXGIAdapter *dxgiAdapter = nullptr;
  hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&dxgiAdapter);
  dxgiDevice->Release();
  if (FAILED(hr)) {
    std::cerr << "Failed to get DXGI adapter" << std::endl;
    ctx->device->Release();
    delete ctx;
    return nullptr;
  }

  IDXGIOutput *dxgiOutput = nullptr;
  hr = dxgiAdapter->EnumOutputs(monitorIndex, &dxgiOutput);
  dxgiAdapter->Release();
  if (FAILED(hr)) {
    std::cerr << "Failed to get DXGI output" << std::endl;
    ctx->device->Release();
    delete ctx;
    return nullptr;
  }

  IDXGIOutput1 *dxgiOutput1 = nullptr;
  hr =
      dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void **)&dxgiOutput1);
  dxgiOutput->Release();
  if (FAILED(hr)) {
    std::cerr << "Failed to get DXGI output1" << std::endl;
    ctx->device->Release();
    delete ctx;
    return nullptr;
  }

  hr = dxgiOutput1->DuplicateOutput(ctx->device, &ctx->duplication);
  dxgiOutput1->Release();
  if (FAILED(hr)) {
    std::cerr << "Failed to create desktop duplication" << std::endl;
    ctx->device->Release();
    delete ctx;
    return nullptr;
  }

  DXGI_OUTDUPL_DESC dupDesc;
  ctx->duplication->GetDesc(&dupDesc);
  ctx->width = dupDesc.ModeDesc.Width;
  ctx->height = dupDesc.ModeDesc.Height;

  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = ctx->width;
  texDesc.Height = ctx->height;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_STAGING;
  texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  hr = ctx->device->CreateTexture2D(&texDesc, nullptr, &ctx->stagingTexture);
  if (FAILED(hr)) {
    std::cerr << "Failed to create staging texture" << std::endl;
    ctx->duplication->Release();
    ctx->device->Release();
    delete ctx;
    return nullptr;
  }

  std::cout << "Desktop Duplication initialized: " << ctx->width << "x"
            << ctx->height << std::endl;
  return ctx;
}

bool CaptureNextDuplicationFrame(DuplicationContext *ctx, cv::Mat &outputBuffer,
                                 int timeoutMs) {
  if (!ctx || !ctx->duplication)
    return false;

  IDXGIResource *frameResource = nullptr;
  DXGI_OUTDUPL_FRAME_INFO frameInfo;

  HRESULT hr =
      ctx->duplication->AcquireNextFrame(timeoutMs, &frameInfo, &frameResource);

  if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
    return false;
  }

  if (FAILED(hr)) {
    if (hr == DXGI_ERROR_ACCESS_LOST) {
      std::cerr << "Desktop duplication access lost" << std::endl;
    }
    return false;
  }

  ID3D11Texture2D *frameTexture = nullptr;
  hr = frameResource->QueryInterface(__uuidof(ID3D11Texture2D),
                                     (void **)&frameTexture);
  frameResource->Release();

  if (FAILED(hr)) {
    ctx->duplication->ReleaseFrame();
    return false;
  }

  ctx->context->CopyResource(ctx->stagingTexture, frameTexture);
  frameTexture->Release();

  D3D11_MAPPED_SUBRESOURCE mapped;
  hr = ctx->context->Map(ctx->stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);

  if (FAILED(hr)) {
    ctx->duplication->ReleaseFrame();
    return false;
  }

  if (outputBuffer.rows != ctx->height || outputBuffer.cols != ctx->width ||
      outputBuffer.type() != CV_8UC3) {
    std::cerr << "Output buffer size mismatch" << std::endl;
    ctx->context->Unmap(ctx->stagingTexture, 0);
    ctx->duplication->ReleaseFrame();
    return false;
  }

  for (int y = 0; y < ctx->height; y++) {
    const BYTE *src = (const BYTE *)mapped.pData + y * mapped.RowPitch;
    BYTE *dst = outputBuffer.ptr<BYTE>(y);

    for (int x = 0; x < ctx->width; x++) {
      dst[x * 3 + 0] = src[x * 4 + 0];
      dst[x * 3 + 1] = src[x * 4 + 1];
      dst[x * 3 + 2] = src[x * 4 + 2];
    }
  }

  ctx->context->Unmap(ctx->stagingTexture, 0);
  ctx->duplication->ReleaseFrame();

  return true;
}

bool CaptureRegionFromDuplication(DuplicationContext *ctx, int x, int y,
                                  int width, int height, cv::Mat &outputBuffer,
                                  int timeoutMs) {
  if (!ctx || !ctx->duplication)
    return false;

  IDXGIResource *frameResource = nullptr;
  DXGI_OUTDUPL_FRAME_INFO frameInfo;

  HRESULT hr =
      ctx->duplication->AcquireNextFrame(timeoutMs, &frameInfo, &frameResource);

  if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
    return false;
  }

  if (FAILED(hr)) {
    return false;
  }

  ID3D11Texture2D *frameTexture = nullptr;
  hr = frameResource->QueryInterface(__uuidof(ID3D11Texture2D),
                                     (void **)&frameTexture);
  frameResource->Release();

  if (FAILED(hr)) {
    ctx->duplication->ReleaseFrame();
    return false;
  }

  ctx->context->CopyResource(ctx->stagingTexture, frameTexture);
  frameTexture->Release();

  D3D11_MAPPED_SUBRESOURCE mapped;
  hr = ctx->context->Map(ctx->stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);

  if (FAILED(hr)) {
    ctx->duplication->ReleaseFrame();
    return false;
  }

  if (outputBuffer.rows != height || outputBuffer.cols != width ||
      outputBuffer.type() != CV_8UC3) {
    ctx->context->Unmap(ctx->stagingTexture, 0);
    ctx->duplication->ReleaseFrame();
    return false;
  }

  if (x + width > ctx->width || y + height > ctx->height) {
    ctx->context->Unmap(ctx->stagingTexture, 0);
    ctx->duplication->ReleaseFrame();
    return false;
  }

  for (int row = 0; row < height; row++) {
    const BYTE *src =
        (const BYTE *)mapped.pData + (y + row) * mapped.RowPitch + x * 4;
    BYTE *dst = outputBuffer.ptr<BYTE>(row);

    for (int col = 0; col < width; col++) {
      dst[col * 3 + 0] = src[col * 4 + 0];
      dst[col * 3 + 1] = src[col * 4 + 1];
      dst[col * 3 + 2] = src[col * 4 + 2];
    }
  }

  ctx->context->Unmap(ctx->stagingTexture, 0);
  ctx->duplication->ReleaseFrame();

  return true;
}

void CleanupDesktopDuplication(DuplicationContext *ctx) {
  if (!ctx)
    return;

  if (ctx->stagingTexture)
    ctx->stagingTexture->Release();
  if (ctx->duplication)
    ctx->duplication->Release();
  if (ctx->context)
    ctx->context->Release();
  if (ctx->device)
    ctx->device->Release();

  delete ctx;
}

// ============================================================================
// Window Helpers
// ============================================================================

void KeepGameActive(HWND gameWindow) {
  SendMessage(gameWindow, WM_ACTIVATEAPP, TRUE, 0);
  SendMessage(gameWindow, WM_ACTIVATE, WA_ACTIVE, 0);

  POINT pt;
  GetCursorPos(&pt);
  ScreenToClient(gameWindow, &pt);
  SendMessage(gameWindow, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
}

void SetWindowAlwaysOnTop(HWND hwnd, bool onTop) {
  SetWindowPos(hwnd, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
