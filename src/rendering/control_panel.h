#pragma once

#include <opencv2/opencv.hpp>

// ============================================================================
// Custom Control Panel (Dark UI)
// ============================================================================

/**
 * Draw dark-mode control panel (custom sliders).
 */
void DrawControlPanel(cv::Mat&image);

/**
 * Handle mouse events for the control panel sliders.
 * @return true if the event was consumed by the control panel.
 */
bool HandleControlPanelMouse(int event, int x, int y, int flags);
