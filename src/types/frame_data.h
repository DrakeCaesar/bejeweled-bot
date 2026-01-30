#pragma once

#include "config.h"
#include "window_capture.h"
#include <condition_variable>
#include <mutex>
#include <opencv2/opencv.hpp>

// ============================================================================
// Buffer State Machine - Each buffer flows through states sequentially
// ============================================================================

enum class BufferState {
  FREE,      // Available for capture
  CAPTURED,  // Raw data captured, ready for processing
  PROCESSED, // Overlays added, ready for display
  DISPLAYING // Currently being shown on screen
};

// ============================================================================
// Shared State for Threading
// ============================================================================

struct SharedFrameData {
  // Single buffer array - each buffer flows through all pipeline stages
  cv::Mat frameBuffer[3];
  BufferState bufferState[3] = {BufferState::FREE, BufferState::FREE,
                                BufferState::FREE};

  std::mutex mutex;
  std::condition_variable cv;

  bool shouldExit = false;

  // Desktop Duplication context
  DuplicationContext *dupContext = nullptr;

  // Classification mode toggle
  Config::ClassificationMode classificationMode =
      Config::ClassificationMode::COLOR;

  // Find first buffer in given state, returns -1 if none found
  int findBuffer(BufferState state) const {
    for (int i = 0; i < 3; i++) {
      if (bufferState[i] == state)
        return i;
    }
    return -1;
  }
};
