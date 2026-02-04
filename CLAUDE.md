# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++ computer vision bot for playing Bejeweled 3, using OpenCV 4 for real-time window capture and gem detection. The project is Windows-specific and uses GDI/Desktop Duplication API for screen capture.

## Build System

### Dependencies
- **CMake 3.20+**
- **vcpkg** for package management
- **OpenCV 4** with contrib and freetype features

### Setup Commands
```bash
# Install dependencies via vcpkg
vcpkg install

# Configure CMake with vcpkg toolchain (multiple build directory options)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
# OR for VS Code tasks configuration:
cmake -B builds/msbuild-vcpkg -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake

# Build the project
cmake --build build --config Release
# OR for VS Code tasks:
cmake --build builds/msbuild-vcpkg --config Release
```

### Development Commands (from .vscode/tasks.json)
- **Rebuild and Run**: Debug build and execution (kills existing process, builds, runs)
- **Rebuild and Run (Release)**: Release build and execution (kills existing process, builds, runs)

### Build Output
- Executable: `bejeweled-bot.exe` (placed in project root)
- PDB files generated alongside executable
- Supports MSVC, MinGW, Clang, and GNU toolchains
- Performance optimizations: AVX2, LTO, parallel builds
- Auto-detects processor count for parallel builds

## Architecture

### Multi-threaded Pipeline
The application uses a three-buffer state machine with three threads:
1. **Capture Thread**: Captures game window using GDI or Desktop Duplication API
2. **Processing Thread**: Detects and classifies gems in the 8x8 grid
3. **Main Thread**: Handles display, UI, and user input

### Buffer State Machine
Three buffers flow through sequential states:
- `FREE` → `CAPTURED` → `PROCESSED` → `DISPLAYING`
- Thread-safe synchronization with mutexes and condition variables
- Defined in `src/types/frame_data.h`

### Static Thread Pool
- 8 persistent worker threads for parallel gem detection (`src/detection/thread_pool.h`)
- Processes 8x8 grid cells in parallel
- Reduces thread creation overhead

### Key Components
- **`src/core/application.{h,cpp}`**: Main application lifecycle
- **`src/capture/window_capture.{h,cpp}`**: Window capture system (GDI/Desktop Duplication)
- **`src/capture/capture_thread.{h,cpp}`**: Capture thread implementation
- **`src/detection/gem_detector.{h,cpp}`**: Grid-based gem detection
- **`src/detection/shape_classifier.{h,cpp}`**: Color-based gem classification
- **`src/detection/gem_types.{h,cpp}`**: Gem type definitions and constants
- **`src/detection/mask_loader.{h,cpp}`**: Asset loading and management
- **`src/detection/process_thread.{h,cpp}`**: Processing thread implementation
- **`src/detection/thread_pool.h`**: Static thread pool for parallel processing
- **`src/rendering/overlay.{h,cpp}`**: Debug overlay rendering
- **`src/rendering/control_panel.{h,cpp}`**: UI control panel
- **`src/rendering/banner.{h,cpp}`**: Information banner display
- **`src/types/frame_data.h`**: Buffer state machine and synchronization
- **`src/types/timing.h`**: Performance tracking with rolling averages

### Configuration
Centralized in `src/config.h` namespace `Config`:
- Window title and crop rectangle calibration
- Grid settings (8x8, 128x128 cells)
- Display and performance settings
- Gem matching thresholds
- Recording parameters
- Thread priorities and timing

### Gem Detection Pipeline
1. **Grid Extraction**: Divides 1024x1024 board into 8x8 grid (128x128 cells)
2. **Gem Classification**: Uses `ClassifyGemByShape()` for color matching
3. **Asset Matching**: Compares against pre-loaded sprite sheets in Lab color space
4. **Temporal Coherence**: Uses previous frame results for consistency
5. **Parallel Processing**: 8x8 grid cells processed in parallel via thread pool

### Gem Types and States
Defined in `src/detection/gem_types.{h,cpp}`:
- Standard gems: RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, WHITE
- Special: HYPERCUBE (60 frames vs 20 for standard gems)
- States: NORMAL, FLAME, LIGHTNING, STAR (for future enhancements)

## Asset System

### Directory Structure
- `assets/color/`: Color reference images for gem matching (PNG sprite sheets)
- `assets/mask/`: Alpha mask images for gem isolation
- Other asset categories: `normal/`, `other/`, `rotating/`, `shadow/`, `static_color/`, `static_mask/`

### Sprite Sheets
- Standard gems: 5x4 grids of 20 animated frames (128x128 each)
- Hypercube special: 10x6 grid of 60 frames
- Pre-converted to Lab color space for performance
- Loaded at startup by `src/detection/mask_loader.{h,cpp}`

### Frame Recording System
- Press 'R' to record frames from specific grid positions
- Outputs to `recorded_frames/` directory
- Used for creating new sprite sheets and assets
- See `RECORDING_INSTRUCTIONS.md` for details
- Recording disables gem processing for maximum capture performance

### Asset Creation Workflow
1. Record frames from game using 'R' key (default: column 3, row 2, 100 frames)
2. Use `stitch_animation.sh` to create sprite sheets from recorded frames
3. Place new assets in appropriate `assets/` subdirectories
4. Update gem type definitions in `src/detection/gem_types.{h,cpp}` if needed

## Development Workflow

### Configuration Calibration
1. Run the bot once to see window dimensions (press 'i')
2. Adjust `BOARD_X`, `BOARD_Y`, `BOARD_WIDTH`, `BOARD_HEIGHT` in `config.h`
3. Rebuild and test until crop rectangle frames the game board perfectly

### Runtime Controls
- **q**: Quit application
- **i**: Print window information (dimensions, crop)
- **R**: Start/stop frame recording
- **S**: Show recording status

### Performance Tuning
- `USE_DESKTOP_DUPLICATION`: Switch between GDI and Desktop Duplication API
- `CAPTURE_INTERVAL_MS`: Control capture frequency (default: 10ms)
- `BOOST_THREAD_PRIORITY`: Reduce FPS drop when window loses focus
- `GEM_MATCH_SEARCH_THRESHOLD_PERCENT`: Early exit threshold for gem matching
- `ENABLE_AVX2`: Enable AVX2 SIMD instructions (MSVC only)
- `ENABLE_LTO`: Enable Link Time Optimization (Release builds)

## Important Notes

### Main Entry Point
- `src/main.cpp`: Entry point with high-resolution timer using `winmm.lib`
- Initializes application and runs main loop
- Handles keyboard input and window messages

### Windows-Specific Features
- Uses Windows API for window capture and mouse input
- Requires `winmm.lib` for high-resolution timing
- Desktop Duplication API requires DirectX 11 compatible hardware
- Mouse click forwarding to game window with coordinate translation

### CMake Configuration Details
- Auto-detects processor count for parallel builds
- Supports MSVC, MinGW, Clang, and GNU toolchains
- Sets `NOMINMAX` to prevent Windows min/max macro conflicts
- Exports compile commands for clangd LSP support
- Places executable and PDB files in project root

### Timing and Performance Monitoring
- Rolling averages over 10 frames for performance metrics (`src/types/timing.h`)
- Tracks capture, detection, draw, and display times
- Real-time FPS calculation and display
- Console output for performance debugging

### Testing Approach
- No formal test framework; manual runtime verification
- Debug overlays and visualization for testing
- Console output for gem detection debugging
- Frame recording system for asset validation