#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <boost/filesystem.hpp>
#include "sei_generator.h"

namespace fs = boost::filesystem;

// Extract timestamp from JPG filename like "image_0123_1751959747.173.jpg"
uint64_t extractTimestampFromJpgFilename(const std::string& filename) {
    // Find the last underscore before .jpg
    size_t last_underscore = filename.rfind('_');
    size_t dot_pos = filename.rfind(".jpg");

    if (last_underscore != std::string::npos && dot_pos != std::string::npos) {
        std::string timestamp_str = filename.substr(last_underscore + 1, dot_pos - last_underscore - 1);
        try {
            // Convert to microseconds (multiply by 1000 if it's in milliseconds)
            double timestamp_seconds = std::stod(timestamp_str);
            return static_cast<uint64_t>(timestamp_seconds * 1000000); // Convert to microseconds
        } catch (...) {
            std::cerr << "Failed to parse timestamp from: " << filename << std::endl;
        }
    }
    return 0;
}

// Extract frame number from JPG filename like "image_0123_timestamp.jpg"
int extractFrameNumberFromJpg(const std::string& filename) {
    size_t first_underscore = filename.find('_');
    size_t second_underscore = filename.find('_', first_underscore + 1);

    if (first_underscore != std::string::npos && second_underscore != std::string::npos) {
        std::string frame_str = filename.substr(first_underscore + 1, second_underscore - first_underscore - 1);
        try {
            return std::stoi(frame_str);
        } catch (...) {
            // Fallback: try to extract 4-digit number
            for (size_t i = 0; i < filename.length() - 3; i++) {
                if (std::isdigit(filename[i]) && std::isdigit(filename[i+1]) &&
                    std::isdigit(filename[i+2]) && std::isdigit(filename[i+3])) {
                    return std::stoi(filename.substr(i, 4));
                }
            }
        }
    }
    return -1;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <images_directory> <h264_input_directory> <h264_output_directory>" << std::endl;
        std::cerr << "  images_directory: Directory with timestamped JPG files" << std::endl;
        std::cerr << "  h264_input_directory: Directory with H264 files to process" << std::endl;
        std::cerr << "  h264_output_directory: Output directory for H264 files with real timestamps" << std::endl;
        return 1;
    }

    std::string images_dir = argv[1];
    std::string h264_input_dir = argv[2];
    std::string h264_output_dir = argv[3];

    // Create output directory
    fs::create_directories(h264_output_dir);

    // Step 1: Extract timestamps from JPG files
    std::map<int, uint64_t> frame_timestamps; // frame_number -> timestamp_us

    std::cout << "Extracting timestamps from JPG files..." << std::endl;

    for (fs::directory_iterator iter(images_dir); iter != fs::directory_iterator(); ++iter) {
        if (iter->path().extension() == ".jpg") {
            std::string filename = iter->path().filename().string();
            int frame_number = extractFrameNumberFromJpg(filename);
            uint64_t timestamp = extractTimestampFromJpgFilename(filename);

            if (frame_number >= 0 && timestamp > 0) {
                frame_timestamps[frame_number] = timestamp;
                std::cout << "  Frame " << frame_number << " -> " << timestamp << " us" << std::endl;
            }
        }
    }

    std::cout << "Found " << frame_timestamps.size() << " timestamped frames" << std::endl;

    // Step 2: Process H264 files and inject corresponding timestamps
    std::cout << "Processing H264 files and injecting real timestamps..." << std::endl;

    int processed_count = 0;
    int h264_frame_index = 0;

    // Process sample-0.h264, sample-1.h264, etc.
    for (fs::directory_iterator entry(h264_input_dir); entry != fs::directory_iterator(); ++entry) {
        if (entry->path().extension() == ".h264") {
            std::string input_file = entry->path().string();
            std::string filename = entry->path().filename().string();

            // Extract sample number from "sample-123.h264"
            size_t dash_pos = filename.find('-');
            size_t dot_pos = filename.find(".h264");
            if (dash_pos != std::string::npos && dot_pos != std::string::npos) {
                int sample_number = std::stoi(filename.substr(dash_pos + 1, dot_pos - dash_pos - 1));

                // Find corresponding timestamp (sample-0 -> frame 0, sample-1 -> frame 1, etc.)
                auto timestamp_it = frame_timestamps.find(sample_number);
                if (timestamp_it != frame_timestamps.end()) {
                    uint64_t real_timestamp = timestamp_it->second;

                    // std::cout << "  Processing " << filename << " with timestamp " << real_timestamp << " us" << std::endl;

                    // Read input H264 file
                    std::ifstream input(input_file, std::ios::binary | std::ios::ate);
                    if (!input) {
                        std::cerr << "Failed to open: " << input_file << std::endl;
                        continue;
                    }

                    size_t size = input.tellg();
                    input.seekg(0, std::ios::beg);
                    std::vector<uint8_t> data(size);
                    input.read(reinterpret_cast<char*>(data.data()), size);
                    input.close();

                    // Create output file with real timestamp SEI
                    std::string output_file = h264_output_dir + "/" + filename;
                    std::ofstream output(output_file, std::ios::binary);
                    if (!output) {
                        std::cerr << "Failed to create: " << output_file << std::endl;
                        continue;
                    }

                    // Create SEI with real timestamp
                    std::vector<uint8_t> sei_nal = SEIGenerator::createSimpleTimestampSEI(real_timestamp);

                    // Write SEI as length-prefixed (big-endian)
                    uint32_t sei_length = sei_nal.size();
                    uint8_t length_bytes[4];
                    length_bytes[0] = (sei_length >> 24) & 0xFF;
                    length_bytes[1] = (sei_length >> 16) & 0xFF;
                    length_bytes[2] = (sei_length >> 8) & 0xFF;
                    length_bytes[3] = sei_length & 0xFF;
                    output.write(reinterpret_cast<char*>(length_bytes), 4);
                    output.write(reinterpret_cast<char*>(sei_nal.data()), sei_nal.size());

                    // Copy original file content (skip any existing SEI)
                    size_t pos = 0;
                    while (pos < data.size() - 4) {
                        uint32_t length = (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3];
                        size_t nal_start = pos + 4;
                        size_t nal_end = nal_start + length;

                        if (nal_end > data.size()) break;

                        if (nal_start < data.size()) {
                            uint8_t nal_type = data[nal_start] & 0x1F;

                            // Skip existing SEI units, copy everything else
                            if (nal_type != 6) {
                                // Copy as length-prefixed
                                output.write(reinterpret_cast<char*>(&data[pos]), 4); // Length
                                output.write(reinterpret_cast<char*>(&data[nal_start]), length); // NAL
                            }
                        }
                        pos = nal_end;
                    }

                    output.close();
                    processed_count++;
                } else {
                    std::cout << "  ⚠️  No timestamp found for " << filename << " (frame " << sample_number << ")" << std::endl;
                }
            }
        }
    }

    std::cout << "Output directory: " << h264_output_dir << std::endl;

    return 0;
}