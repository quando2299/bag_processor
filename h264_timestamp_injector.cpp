#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <boost/filesystem.hpp>
#include "sei_generator.h"

class H264TimestampInjector {
private:
    // NAL unit types
    static constexpr uint8_t NAL_UNIT_TYPE_SEI = 6;
    static constexpr uint8_t NAL_UNIT_TYPE_SPS = 7;
    static constexpr uint8_t NAL_UNIT_TYPE_PPS = 8;
    static constexpr uint8_t NAL_UNIT_TYPE_IDR = 5;
    static constexpr uint8_t NAL_UNIT_TYPE_NON_IDR = 1;

    std::vector<uint8_t> input_data_;
    std::vector<double> frame_timestamps_;
    std::string output_path_;

    struct NALUnit {
        size_t offset;
        size_t size;
        uint8_t type;
        bool is_frame;
    };

    std::vector<NALUnit> nal_units_;

public:
    H264TimestampInjector(const std::string& input_h264_path,
                          const std::string& output_h264_path,
                          const std::vector<double>& timestamps)
        : output_path_(output_h264_path), frame_timestamps_(timestamps) {

        loadH264File(input_h264_path);
        parseNALUnits();
    }

    bool process() {
        std::cout << "Processing H.264 file with timestamp injection..." << std::endl;
        std::cout << "Found " << nal_units_.size() << " NAL units" << std::endl;

        // Count frames
        int frame_count = 0;
        for (const auto& nal : nal_units_) {
            if (nal.is_frame) frame_count++;
        }
        std::cout << "Found " << frame_count << " frames" << std::endl;

        if (frame_timestamps_.size() < static_cast<size_t>(frame_count)) {
            std::cerr << "Warning: Not enough timestamps (" << frame_timestamps_.size()
                     << ") for all frames (" << frame_count << ")" << std::endl;
        }

        std::ofstream output(output_path_, std::ios::binary);
        if (!output) {
            std::cerr << "Failed to open output file: " << output_path_ << std::endl;
            return false;
        }

        size_t timestamp_index = 0;
        bool last_was_sps = false;

        for (size_t i = 0; i < nal_units_.size(); i++) {
            const NALUnit& nal = nal_units_[i];

            // Write the original NAL unit with start code
            writeStartCode(output);
            output.write(reinterpret_cast<const char*>(&input_data_[nal.offset]), nal.size);

            // After SPS, inject timestamp SEI for next frame
            if (nal.type == NAL_UNIT_TYPE_SPS) {
                last_was_sps = true;
            } else if (last_was_sps && nal.type == NAL_UNIT_TYPE_PPS) {
                // After PPS (which follows SPS), inject SEI with timestamp
                if (timestamp_index < frame_timestamps_.size()) {
                    // Convert timestamp from seconds to microseconds
                    uint64_t timestamp_us = static_cast<uint64_t>(frame_timestamps_[timestamp_index] * 1000000);

                    std::cout << "Injecting SEI timestamp: " << std::fixed << std::setprecision(6)
                             << frame_timestamps_[timestamp_index] << " sec (frame "
                             << timestamp_index << ")" << std::endl;

                    // Create and write SEI NAL unit (simple format for Flutter)
                    std::vector<uint8_t> sei_nal = SEIGenerator::createSimpleTimestampSEI(timestamp_us);
                    writeStartCode(output);
                    output.write(reinterpret_cast<const char*>(sei_nal.data()), sei_nal.size());

                    timestamp_index++;
                }
                last_was_sps = false;
            } else if (nal.is_frame && nal.type != NAL_UNIT_TYPE_IDR) {
                // For P/B frames that don't have SPS/PPS, inject SEI before the frame
                if (timestamp_index < frame_timestamps_.size()) {
                    // We need to inject SEI BEFORE this frame, so we need to reorganize
                    // This is handled by injecting after each IDR group
                }
                last_was_sps = false;
            } else {
                last_was_sps = false;
            }
        }

        output.close();
        std::cout << "Successfully wrote H.264 with timestamps to: " << output_path_ << std::endl;
        std::cout << "Injected " << timestamp_index << " timestamps" << std::endl;
        return true;
    }

    // Alternative method: Inject timestamp before each frame
    bool processPerFrame() {
        std::cout << "Processing H.264 file with per-frame timestamp injection..." << std::endl;
        std::cout << "Found " << nal_units_.size() << " NAL units" << std::endl;

        std::ofstream output(output_path_, std::ios::binary);
        if (!output) {
            std::cerr << "Failed to open output file: " << output_path_ << std::endl;
            return false;
        }

        size_t timestamp_index = 0;

        for (size_t i = 0; i < nal_units_.size(); i++) {
            const NALUnit& nal = nal_units_[i];

            // Inject SEI before each frame
            if (nal.is_frame && timestamp_index < frame_timestamps_.size()) {
                // Convert timestamp from seconds to microseconds
                uint64_t timestamp_us = static_cast<uint64_t>(frame_timestamps_[timestamp_index] * 1000000);

                // std::cout << "Injecting SEI timestamp before frame " << timestamp_index
                //          << ": " << std::fixed << std::setprecision(6)
                //          << frame_timestamps_[timestamp_index] << " sec" << std::endl;

                // Create and write SEI NAL unit (simple format for Flutter)
                std::vector<uint8_t> sei_nal = SEIGenerator::createSimpleTimestampSEI(timestamp_us);
                writeStartCode(output);
                output.write(reinterpret_cast<const char*>(sei_nal.data()), sei_nal.size());

                timestamp_index++;
            }

            // Write the original NAL unit
            writeStartCode(output);
            output.write(reinterpret_cast<const char*>(&input_data_[nal.offset]), nal.size);
        }

        output.close();
        std::cout << "Successfully wrote H.264 with per-frame timestamps to: " << output_path_ << std::endl;
        std::cout << "Injected " << timestamp_index << " timestamps" << std::endl;
        return true;
    }

private:
    void loadH264File(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Failed to open H.264 file: " + path);
        }

        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        input_data_.resize(size);
        file.read(reinterpret_cast<char*>(input_data_.data()), size);
        file.close();

        std::cout << "Loaded H.264 file: " << path << " (" << size << " bytes)" << std::endl;
    }

    void parseNALUnits() {
        size_t pos = 0;

        while (pos < input_data_.size() - 4) {
            // Find start code
            if (isStartCode(pos)) {
                size_t start_offset = pos;

                // Skip start code
                if (input_data_[pos + 2] == 0x01) {
                    pos += 3;
                } else {
                    pos += 4;
                }

                // Find next start code or end of file
                size_t next_start = findNextStartCode(pos);
                size_t nal_size = next_start - pos;

                if (nal_size > 0 && pos < input_data_.size()) {
                    NALUnit nal;
                    nal.offset = pos;
                    nal.size = nal_size;
                    nal.type = input_data_[pos] & 0x1F;
                    nal.is_frame = (nal.type == NAL_UNIT_TYPE_IDR ||
                                   nal.type == NAL_UNIT_TYPE_NON_IDR);

                    nal_units_.push_back(nal);

                    // Debug output for important NAL units
                    if (nal.type == NAL_UNIT_TYPE_SPS) {
                        std::cout << "  SPS at offset " << pos << std::endl;
                    } else if (nal.type == NAL_UNIT_TYPE_PPS) {
                        std::cout << "  PPS at offset " << pos << std::endl;
                    } else if (nal.type == NAL_UNIT_TYPE_IDR) {
                        std::cout << "  IDR frame at offset " << pos << std::endl;
                    } else if (nal.type == NAL_UNIT_TYPE_SEI) {
                        std::cout << "  SEI at offset " << pos << std::endl;
                    }
                }

                pos = next_start;
            } else {
                pos++;
            }
        }
    }

    bool isStartCode(size_t pos) {
        if (pos + 3 >= input_data_.size()) return false;

        // Check for 3-byte start code (0x000001)
        if (input_data_[pos] == 0x00 &&
            input_data_[pos + 1] == 0x00 &&
            input_data_[pos + 2] == 0x01) {
            return true;
        }

        // Check for 4-byte start code (0x00000001)
        if (pos + 4 <= input_data_.size() &&
            input_data_[pos] == 0x00 &&
            input_data_[pos + 1] == 0x00 &&
            input_data_[pos + 2] == 0x00 &&
            input_data_[pos + 3] == 0x01) {
            return true;
        }

        return false;
    }

    size_t findNextStartCode(size_t start) {
        for (size_t i = start; i < input_data_.size() - 3; i++) {
            if (isStartCode(i)) {
                return i;
            }
        }
        return input_data_.size();
    }

    void writeStartCode(std::ofstream& output) {
        // Write 4-byte start code
        const uint8_t start_code[] = {0x00, 0x00, 0x00, 0x01};
        output.write(reinterpret_cast<const char*>(start_code), 4);
    }
};

// Function to extract timestamps from image filenames
std::vector<double> extractTimestampsFromImages(const std::string& images_dir) {
    std::vector<double> timestamps;

    std::vector<std::string> image_files;
    for (auto& file : boost::filesystem::directory_iterator(images_dir)) {
        if (file.path().extension() == ".jpg" || file.path().extension() == ".png") {
            image_files.push_back(file.path().string());
        }
    }

    // Sort files by name (they should be in order like image_0000_timestamp.jpg)
    std::sort(image_files.begin(), image_files.end());

    for (const auto& filepath : image_files) {
        std::string filename = boost::filesystem::path(filepath).stem().string();

        // Extract timestamp from filename (format: image_XXXX_timestamp.jpg)
        size_t last_underscore = filename.rfind('_');
        if (last_underscore != std::string::npos) {
            std::string timestamp_str = filename.substr(last_underscore + 1);
            try {
                double timestamp = std::stod(timestamp_str);
                timestamps.push_back(timestamp);
            } catch (...) {
                std::cerr << "Failed to parse timestamp from: " << filename << std::endl;
            }
        }
    }

    std::cout << "Extracted " << timestamps.size() << " timestamps from image files" << std::endl;
    return timestamps;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_h264> <output_h264> [images_directory]" << std::endl;
        std::cerr << "  input_h264: Path to input H.264 file" << std::endl;
        std::cerr << "  output_h264: Path to output H.264 file with timestamps" << std::endl;
        std::cerr << "  images_directory: Optional directory with timestamped images" << std::endl;
        return 1;
    }

    std::string input_h264 = argv[1];
    std::string output_h264 = argv[2];

    std::vector<double> timestamps;

    if (argc >= 4) {
        // Extract timestamps from image directory
        std::string images_dir = argv[3];
        timestamps = extractTimestampsFromImages(images_dir);
    } else {
        // Use dummy timestamps for testing (30fps)
        std::cout << "No image directory provided, using test timestamps at 30fps" << std::endl;
        for (int i = 0; i < 300; i++) {  // 10 seconds at 30fps
            timestamps.push_back(i / 30.0);
        }
    }

    try {
        H264TimestampInjector injector(input_h264, output_h264, timestamps);

        // Use per-frame injection method
        if (injector.processPerFrame()) {
            std::cout << "✅ Successfully injected timestamps into H.264 stream" << std::endl;
            return 0;
        } else {
            std::cerr << "❌ Failed to inject timestamps" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}