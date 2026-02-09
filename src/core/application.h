#pragma once

#include "capture/window_capture.h"
#include "types/frame_data.h"
#include "types/timing.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <windows.h>

// ============================================================================
// Application - Main application lifecycle management
// ============================================================================

class Application {
public:
  Application();

  ~Application();

  /**
   * Initialize the application.
   * Finds game window, sets up capture, allocates buffers.
   * @return true if initialization succeeded
   */
  bool Initialize();

  /**
   * Run the main display loop.
   * Handles keyboard input and displays processed frames.
   */
  void Run();

  /**
   * Cleanup and shutdown.
   */
  void Shutdown();

private:
  // Setup helpers
  bool FindAndSetupGameWindow();

  bool InitializeCapture();

  bool AllocateBuffers();

  void StartThreads();

  // Display loop helpers
  void HandleKeyboardInput(int key);

  void DisplayFrame();

  // State
  HWND gameWindow_ = nullptr;
  bool running_ = false;

  // Shared data
  SharedFrameData frameData_;
  TimingInfo timingInfo_;

  // FPS tracking
  std::chrono::high_resolution_clock::time_point lastFpsUpdateTime_;
  int frameCount_ = 0;
  float currentFps_ = 0.0f;

  // Threads
  std::thread captureThread_;
  std::thread processThread_;
};