# Bejeweled 3 Bot

A computer vision-based bot for playing Bejeweled 3, built with C++ and OpenCV.

## Prerequisites

- **CMake** (3.20 or higher)
- **vcpkg** (for package management)
- **Visual Studio 2019 or later** (with C++ development tools)
- **Bejeweled 3** game installed

## Setup

### 1. Install vcpkg

If you don't have vcpkg installed:

```bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

### 2. Install Dependencies

Navigate to the project directory and install OpenCV:

```bash
vcpkg install
```

Or manually:

```bash
vcpkg install opencv4:x64-windows
```

### 3. Configure CMake with vcpkg

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
```

Replace `[path-to-vcpkg]` with the actual path to your vcpkg installation.

### 4. Build the Project

```bash
cmake --build build --config Release
```

## Configuration

Before running, you need to configure the constants in `src/main.cpp`:

### Required Configuration

1. **WINDOW_TITLE**: The exact or partial title of the Bejeweled 3 window
   ```cpp
   const std::string WINDOW_TITLE = "Bejeweled 3";
   ```

2. **Game Board Crop Rectangle**: Calibrate these values to match your setup
   ```cpp
   const int BOARD_X = 100;        // X offset from left edge of window
   const int BOARD_Y = 100;        // Y offset from top edge of window
   const int BOARD_WIDTH = 800;    // Width of the game board area
   const int BOARD_HEIGHT = 800;   // Height of the game board area
   ```

### How to Calibrate Crop Rectangle

1. Run the program for the first time (it will show the full window if crop fails)
2. Press `i` to see the current window dimensions
3. Note the window size and visually estimate the game board position
4. Update the `BOARD_X`, `BOARD_Y`, `BOARD_WIDTH`, and `BOARD_HEIGHT` constants
5. Rebuild and test until the crop rectangle perfectly frames the game board

## Usage

1. **Launch Bejeweled 3** and start a game
2. **Run the bot**:
   ```bash
   .\build\Release\bejeweled-bot.exe
   ```
3. A window will appear showing the cropped game board in real-time
4. **Controls**:
   - Press `q` to quit
   - Press `i` to print window information

## Project Structure

```
bejeweled-bot/
├── src/
│   └── main.cpp          # Main program with window capture logic
├── CMakeLists.txt        # CMake build configuration
├── vcpkg.json           # Dependency manifest
└── README.md            # This file
```

## Current Features

- ✅ Window detection and handle acquisition
- ✅ Real-time window capture
- ✅ Configurable crop rectangle for game board
- ✅ Live display of game board
- ✅ Adjustable FPS

## Next Steps

Future enhancements will include:
- Gem detection and classification
- Grid detection and alignment
- Move validation and suggestion
- Automated gameplay

## Troubleshooting

**Window not found**: 
- Make sure Bejeweled 3 is running
- Check the exact window title in Task Manager
- Update `WINDOW_TITLE` constant to match

**Blank or black capture**:
- Some games use DirectX/OpenGL rendering which may not capture with GDI
- Try running the game in windowed mode
- Ensure the window is not minimized

**Crop rectangle out of bounds**:
- Press `i` while running to see actual window dimensions
- Adjust `BOARD_X`, `BOARD_Y`, `BOARD_WIDTH`, `BOARD_HEIGHT` accordingly
- Rebuild the project after making changes

## License

MIT License - feel free to modify and distribute.
