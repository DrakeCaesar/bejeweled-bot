#pragma once

#include <opencv2/opencv.hpp>
#include <mutex>
#include <condition_variable>
#include "window_capture.h"
#include "config.h"

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

  // Classification mode toggle
  Config::ClassificationMode classificationMode = Config::ClassificationMode::COLOR;
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
