#!/bin/bash

echo "Fixing H264 files to add Flutter-compatible SEI with REAL timestamps..."

# Get the current directory (bag_processor)
CURRENT_DIR=$(pwd)

# Find the latest extracted images directory
IMAGES_DIR=$(ls -1d "$CURRENT_DIR"/extracted_images_* 2>/dev/null | tail -1)
if [ ! -d "$IMAGES_DIR" ]; then
    echo "ERROR: No extracted_images directory found in $CURRENT_DIR!"
    exit 1
fi

echo "Using images from: $IMAGES_DIR"

# Extract datetime from the images directory name
DATETIME=$(basename "$IMAGES_DIR" | sed 's/extracted_images_//')

# Find the corresponding h264 directory
H264_BASE_DIR="$CURRENT_DIR/h264/$DATETIME"
if [ ! -d "$H264_BASE_DIR" ]; then
    echo "ERROR: No H264 directory found at: $H264_BASE_DIR"
    exit 1
fi

echo "Using H264 files from: $H264_BASE_DIR"

# Build the timestamp injection tool (using POSIX dirent for cross-platform compatibility)
echo "Building timestamp injection tool..."
g++ -std=c++14 inject_real_timestamps_to_h264.cpp sei_generator.cpp -o inject_real_timestamps_to_h264

if [ $? -ne 0 ]; then
    echo "ERROR: Failed to build inject_real_timestamps_to_h264!"
    exit 1
fi

# Process ALL camera directories
PROCESSED_COUNT=0
FAILED_COUNT=0

# Loop through each subdirectory in the h264 folder (each represents a camera)
for H264_DIR in "$H264_BASE_DIR"/*; do
    if [ -d "$H264_DIR" ]; then
        # Get the camera name from the directory
        CAMERA_NAME=$(basename "$H264_DIR")

        # Remove the _30fps suffix to match the image directory name
        CAMERA_NAME_WITHOUT_SUFFIX="${CAMERA_NAME%_30fps}"

        # Find corresponding images directory (without _30fps suffix)
        CAMERA_IMAGES_DIR="$IMAGES_DIR/$CAMERA_NAME_WITHOUT_SUFFIX"

        if [ -d "$CAMERA_IMAGES_DIR" ]; then
            echo "Processing camera: $CAMERA_NAME"
            echo "  Camera images: $CAMERA_IMAGES_DIR"
            echo "  H264 files: $H264_DIR"

            # Create output directory for this camera with SEI injected
            OUTPUT_DIR="$CURRENT_DIR/h264_with_sei/$CAMERA_NAME"
            mkdir -p "$OUTPUT_DIR"

            # Use the tool to inject real timestamps
            echo "  Injecting real timestamps..."
            ./inject_real_timestamps_to_h264 "$CAMERA_IMAGES_DIR" "$H264_DIR" "$OUTPUT_DIR"

            if [ $? -eq 0 ]; then
                echo "  ✓ SUCCESS for $CAMERA_NAME -> $OUTPUT_DIR"
                PROCESSED_COUNT=$((PROCESSED_COUNT + 1))
            else
                echo "  ✗ ERROR: Timestamp injection failed for $CAMERA_NAME!"
                FAILED_COUNT=$((FAILED_COUNT + 1))
            fi
            echo ""
        else
            echo "Warning: No matching images directory for $CAMERA_NAME_WITHOUT_SUFFIX at $CAMERA_IMAGES_DIR"
        fi
    fi
done

echo "========================================="
echo "Processing complete!"
echo "  Processed: $PROCESSED_COUNT camera(s)"
echo "  Failed: $FAILED_COUNT camera(s)"
echo "========================================="

if [ $PROCESSED_COUNT -eq 0 ]; then
    echo "ERROR: No cameras were processed successfully!"
    exit 1
else
    echo "SUCCESS! H264 files with SEI timestamps are injected !"
fi