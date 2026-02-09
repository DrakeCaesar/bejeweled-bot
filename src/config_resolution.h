#pragma once

namespace Config {

// Resolution-specific configuration
struct ResolutionConfig {
  int boardX;                // X offset from left edge of window
  int boardY;                // Y offset from top edge of window
  int cellSize;              // Grid cell size in pixels
  int bannerHeight;          // Height of info banner
  int frameDiffRegionWidth;  // Frame diff region width
  int frameDiffRegionHeight; // Frame diff region height
};

// Resolution configurations for supported resolutions
inline ResolutionConfig GetResolutionConfig(int height) {
  switch (height) {
  case 600:
    return {
        261, // boardX
        37,  // boardY
        64,  // cellSize (8x8 grid of 64x64 cells)
        150, // bannerHeight (scaled down)
        25,  // frameDiffRegionWidth
        45   // frameDiffRegionHeight
    };
  case 768:
    return {
        334, // boardX
        47,  // boardY
        82,  // cellSize (8x8 grid of 82x82 cells)
        175, // bannerHeight (scaled down)
        25,  // frameDiffRegionWidth
        45   // frameDiffRegionHeight
    };
  case 1200:
  default:
    return {
        523, // boardX
        75,  // boardY
        128, // cellSize (8x8 grid of 128x128 cells)
        200, // bannerHeight
        25,  // frameDiffRegionWidth
        45   // frameDiffRegionHeight
    };
  }
}

} // namespace Config
