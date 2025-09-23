#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include "sei_generator.h"

class H264TimestampReader {
private:
    std::vector<uint8_t> data_;

    bool isStartCode(size_t pos) {
        if (pos + 3 >= data_.size()) return false;

        // Check for 3-byte start code (0x000001)
        if (data_[pos] == 0x00 && data_[pos + 1] == 0x00 && data_[pos + 2] == 0x01) {
            return true;
        }

        // Check for 4-byte start code (0x00000001)
        if (pos + 4 <= data_.size() &&
            data_[pos] == 0x00 && data_[pos + 1] == 0x00 &&
            data_[pos + 2] == 0x00 && data_[pos + 3] == 0x01) {
            return true;
        }

        return false;
    }

    size_t findNextStartCode(size_t start) {
        for (size_t i = start; i < data_.size() - 3; i++) {
            if (isStartCode(i)) {
                return i;
            }
        }
        return data_.size();
    }

public:
    bool loadFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "Failed to open file: " << path << std::endl;
            return false;
        }

        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        data_.resize(size);
        file.read(reinterpret_cast<char*>(data_.data()), size);
        file.close();

        std::cout << "Loaded H.264 file: " << path << " (" << size << " bytes)" << std::endl;
        return true;
    }

    void extractTimestamps() {
        std::cout << "\nSearching for SEI timestamps in H.264 stream...\n" << std::endl;

        size_t pos = 0;
        int nal_count = 0;
        int sei_count = 0;
        int timestamp_count = 0;
        std::vector<double> timestamps;

        while (pos < data_.size() - 4) {
            if (isStartCode(pos)) {
                // Skip start code
                if (data_[pos + 2] == 0x01) {
                    pos += 3;
                } else {
                    pos += 4;
                }

                // Find next start code or end of file
                size_t next_start = findNextStartCode(pos);
                size_t nal_size = next_start - pos;

                if (nal_size > 0 && pos < data_.size()) {
                    uint8_t nal_type = data_[pos] & 0x1F;
                    nal_count++;

                    // Check for SEI NAL unit
                    if (nal_type == 6) {  // SEI
                        sei_count++;

                        // Extract NAL unit data
                        std::vector<uint8_t> nal_data(data_.begin() + pos, data_.begin() + pos + nal_size);

                        // Try to extract timestamp
                        uint64_t timestamp_us = SEIGenerator::extractTimestampFromSEI(nal_data);

                        if (timestamp_us > 0) {
                            double timestamp_sec = timestamp_us / 1000000.0;
                            timestamps.push_back(timestamp_sec);
                            timestamp_count++;

                            std::cout << "Found timestamp SEI #" << timestamp_count
                                     << " at NAL " << nal_count
                                     << ": " << std::fixed << std::setprecision(6)
                                     << timestamp_sec << " seconds" << std::endl;
                        } else {
                            std::cout << "Found non-timestamp SEI at NAL " << nal_count << std::endl;
                        }
                    } else if (nal_type == 7) {  // SPS
                        std::cout << "Found SPS at NAL " << nal_count << std::endl;
                    } else if (nal_type == 8) {  // PPS
                        std::cout << "Found PPS at NAL " << nal_count << std::endl;
                    } else if (nal_type == 5) {  // IDR
                        std::cout << "Found IDR frame at NAL " << nal_count << std::endl;
                    }
                }

                pos = next_start;
            } else {
                pos++;
            }
        }

        std::cout << "\n=== Summary ===" << std::endl;
        std::cout << "Total NAL units: " << nal_count << std::endl;
        std::cout << "SEI NAL units: " << sei_count << std::endl;
        std::cout << "Timestamp SEI units: " << timestamp_count << std::endl;

        if (!timestamps.empty()) {
            std::cout << "\nExtracted timestamps:" << std::endl;
            for (size_t i = 0; i < timestamps.size(); i++) {
                std::cout << "  Frame " << i << ": "
                         << std::fixed << std::setprecision(6)
                         << timestamps[i] << " sec" << std::endl;

                if (i > 0) {
                    double delta = timestamps[i] - timestamps[i-1];
                    std::cout << "    Delta from previous: "
                             << std::fixed << std::setprecision(3)
                             << (delta * 1000) << " ms" << std::endl;
                }
            }

            // Calculate average frame rate
            if (timestamps.size() > 1) {
                double total_duration = timestamps.back() - timestamps.front();
                double avg_fps = (timestamps.size() - 1) / total_duration;
                std::cout << "\nAverage frame rate: "
                         << std::fixed << std::setprecision(2)
                         << avg_fps << " fps" << std::endl;
            }
        } else {
            std::cout << "\nNo timestamps found in the H.264 stream." << std::endl;
            std::cout << "The stream may not have been processed with timestamp injection." << std::endl;
        }
    }
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <h264_file>" << std::endl;
        std::cerr << "  h264_file: Path to H.264 file to read timestamps from" << std::endl;
        return 1;
    }

    H264TimestampReader reader;

    if (!reader.loadFile(argv[1])) {
        return 1;
    }

    reader.extractTimestamps();

    return 0;
}