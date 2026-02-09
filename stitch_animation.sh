#!/bin/bash

# Usage: ./stitch_animation.sh <output_filename> <starting_frame_index>
# Example: ./stitch_animation.sh red_gem 0
# Creates: assets/color/red_gem.png and assets/mask/red_gem.png

if [ $# -ne 2 ]; then
    echo "Usage: $0 <output_filename> <starting_frame_index>"
    echo "Example: $0 Red 0"
    exit 1
fi

OUTPUT_NAME=$1
START_FRAME=$2
RECORDED_DIR="recorded_frames"
COLOR_OUT_DIR="assets/color"
MASK_OUT_DIR="assets/mask"

# Ensure output directories exist
mkdir -p "$COLOR_OUT_DIR"
mkdir -p "$MASK_OUT_DIR"

# Count total frames
TOTAL_FRAMES=$(ls "$RECORDED_DIR"/frame_*.png 2>/dev/null | wc -l)

if [ $TOTAL_FRAMES -eq 0 ]; then
    echo "Error: No frames found in $RECORDED_DIR"
    exit 1
fi

echo "Found $TOTAL_FRAMES total frames"

# Calculate grid dimensions based on frame count
calculate_grid() {
    local count=$1
    if [ $count -eq 60 ]; then
        echo "10 6"
        elif [ $count -eq 20 ]; then
        echo "5 4"
    else
        # Smart grid: try to keep aspect ratio close to 1
        local sqrt=$(echo "sqrt($count)" | bc)
        local cols=$sqrt
        local rows=$(( (count + cols - 1) / cols ))
        # Adjust if rows is close to cols (prefer wider)
        if [ $rows -lt $cols ]; then
            local temp=$cols
            cols=$rows
            rows=$temp
        fi
        echo "$cols $rows"
    fi
}

read COLS ROWS <<< $(calculate_grid $TOTAL_FRAMES)
echo "Using grid: ${COLS}x${ROWS}"

# Build file list starting from START_FRAME (loop back to 0000)
FRAME_LIST=""
MASK_LIST=""
FRAME_COUNT=0
TARGET_FRAMES=$((COLS * ROWS))

for ((j = 0; j < TARGET_FRAMES; j++)); do
    i=$(( (START_FRAME + j) % TOTAL_FRAMES ))
    FRAME_NUM=$(printf "%04d" $i)
    COLOR_FILE="$RECORDED_DIR/frame_${FRAME_NUM}.png"
    MASK_FILE="$RECORDED_DIR/mask_${FRAME_NUM}.png"

    if [ -f "$COLOR_FILE" ] && [ -f "$MASK_FILE" ]; then
        FRAME_LIST="$FRAME_LIST $COLOR_FILE"
        MASK_LIST="$MASK_LIST $MASK_FILE"
        FRAME_COUNT=$((FRAME_COUNT + 1))
    else
        echo "Warning: Missing frame pair for index $i"
    fi
done

if [ $FRAME_COUNT -eq 0 ]; then
    echo "Error: No valid frame pairs found starting from frame $START_FRAME"
    exit 1
fi

echo "Stitching $FRAME_COUNT frames into grid..."

# Use ImageMagick montage to create grid
magick montage $FRAME_LIST -tile ${COLS}x${ROWS} -geometry +0+0 "$COLOR_OUT_DIR/$OUTPUT_NAME.png"
magick montage $MASK_LIST -tile ${COLS}x${ROWS} -geometry +0+0 "$MASK_OUT_DIR/$OUTPUT_NAME.png"

echo "Done!"
echo "Color grid saved to: $COLOR_OUT_DIR/$OUTPUT_NAME.png"
echo "Mask grid saved to: $MASK_OUT_DIR/$OUTPUT_NAME.png"
