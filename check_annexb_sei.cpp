#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include "sei_generator.h"

bool isStartCode(const std::vector<uint8_t>& data, size_t pos) {
    if (pos + 3 >= data.size()) return false;

    // Check for 3-byte start code (0x000001)
    if (data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) {
        return true;
    }

    // Check for 4-byte start code (0x00000001)
    if (pos + 4 <= data.size() &&
        data[pos] == 0x00 && data[pos + 1] == 0x00 &&
        data[pos + 2] == 0x00 && data[pos + 3] == 0x01) {
        return true;
    }

    return false;
}

size_t findNextStartCode(const std::vector<uint8_t>& data, size_t start) {
    for (size_t i = start; i < data.size() - 3; i++) {
        if (isStartCode(data, i)) {
            return i;
        }
    }
    return data.size();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <h264_file>" << std::endl;
        return 1;
    }

    std::string file_path = argv[1];

    // Read the H264 file
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return 1;
    }

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();

    std::cout << "Analyzing Annex-B H264 file: " << file_path << " (" << size << " bytes)" << std::endl;

    size_t pos = 0;
    int sei_count = 0;
    int frame_count = 0;

    while (pos < data.size() - 4) {
        if (isStartCode(data, pos)) {
            // Skip start code
            size_t nal_start = pos;
            if (data[pos + 2] == 0x01) {
                pos += 3; // 3-byte start code
            } else {
                pos += 4; // 4-byte start code
            }

            // Find next start code
            size_t next_start = findNextStartCode(data, pos);
            size_t nal_size = next_start - pos;

            if (nal_size > 0 && pos < data.size()) {
                uint8_t nal_type = data[pos] & 0x1F;

                std::cout << "NAL unit at offset " << pos << ": type=" << (int)nal_type
                         << " size=" << nal_size << std::endl;

                if (nal_type == 6) { // SEI
                    sei_count++;
                    std::vector<uint8_t> sei_nalu(data.begin() + pos, data.begin() + pos + nal_size);

                    uint64_t simple_timestamp = SEIGenerator::extractSimpleTimestampFromSEI(sei_nalu);

                    if (simple_timestamp != 0) {
                        std::cout << "  ✅ Found simple timestamp SEI: " << simple_timestamp << " microseconds ("
                                 << (simple_timestamp / 1000000.0) << " seconds)" << std::endl;
                    } else {
                        std::cout << "  ❌ SEI found but not simple timestamp format" << std::endl;
                    }

                    // Print raw SEI for debugging
                    std::cout << "  Raw SEI data (first 32 bytes): ";
                    for (size_t i = 0; i < std::min(nal_size, (size_t)32); i++) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0')
                                 << (int)data[pos + i] << " ";
                    }
                    std::cout << std::endl;
                } else if (nal_type == 1 || nal_type == 5) { // Frame
                    frame_count++;
                }
            }

            pos = next_start;
        } else {
            pos++;
        }
    }

    std::cout << std::endl;
    std::cout << "Summary:" << std::endl;
    std::cout << "  Total SEI NAL units: " << sei_count << std::endl;
    std::cout << "  Total frame NAL units: " << frame_count << std::endl;

    return 0;
}