# Frame Recording Instructions

## Overview
A frame recording system has been added to capture raw gem images from any grid position for asset creation and testing.

## How to Use

### Starting Recording
1. Run the application
2. Press **'R'** to start recording
   - Records from **column 3, row 2** (configurable)
   - Captures **100 frames** by default
   - Saves to `recorded_frames/` directory in project root
   - Files are named: `frame_0000.png`, `frame_0001.png`, etc.

### Stopping Recording
- Press **'R'** again to stop early
- Recording auto-stops after 100 frames

### Checking Status
- Press **'S'** to see how many frames have been captured

### Output
- Frames are saved as PNG files in `recorded_frames/` (in project root)
- Each frame is the raw gem region (128x128 pixels by default)
- Files are numbered sequentially with zero-padding

## Customization

To change recording parameters, edit `src/core/application.cpp` line where `StartRecording()` is called:

```cpp
StartRecording(col, row, maxFrames, outputDir);
```

**Parameters:**
- `col`: Grid column (0-7)
- `row`: Grid row (0-7) 
- `maxFrames`: Number of frames to capture
- `outputDir`: Directory path for output files

## Current Configuration
- **Position**: Column 3, Row 2
- **Max Frames**: 100
- **Output Directory**: `recorded_frames/` (in project root)

## Workflow
1. Start the bot and wait for the game to load
2. Press 'R' to start recording
3. Let the game run - frames will be captured automatically
4. After 100 frames (or press 'R' to stop early), recording completes
5. Check `recorded_frames/` folder in project root for your images
6. Clean up/organize the frames manually
7. Use them to create new sprite sheets or assets

## Notes
- **Frame processing is DISABLED during recording for maximum capture performance**
- Recording runs in the processing thread, so it won't affect display
- Frames are captured BEFORE classification, so they're raw game images
- The recording position is based on grid coordinates (not pixels)
- Files are overwritten if you record multiple times - move/rename them if needed
- While recording, all gems will show as UNKNOWN in the display (this is normal)
