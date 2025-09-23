#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <cstring>
#include "sei_generator.h"

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

    std::cout << "Analyzing H264 file: " << file_path << " (" << size << " bytes)" << std::endl;

    // Parse NAL units (length-prefixed format)
    size_t pos = 0;
    int sei_count = 0;
    int frame_count = 0;

    while (pos < data.size() - 4) {
        // Read 4-byte length (big-endian)
        uint32_t length = (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3];
        size_t nal_start = pos + 4;
        size_t nal_end = nal_start + length;

        if (nal_end > data.size()) {
            std::cerr << "Invalid NAL unit length at position " << pos << std::endl;
            break;
        }

        if (nal_start < data.size()) {
            uint8_t nal_type = data[nal_start] & 0x1F;

            std::cout << "NAL unit at offset " << nal_start << ": type=" << (int)nal_type
                     << " size=" << length << std::endl;

            if (nal_type == 6) { // SEI
                sei_count++;
                std::vector<uint8_t> sei_nalu(data.begin() + nal_start, data.begin() + nal_end);

                // Check if it's our timestamp SEI (complex format)
                uint64_t timestamp = SEIGenerator::extractTimestampFromSEI(sei_nalu);
                uint64_t simple_timestamp = SEIGenerator::extractSimpleTimestampFromSEI(sei_nalu);

                if (timestamp != 0) {
                    std::cout << "  ✅ Found complex timestamp SEI: " << timestamp << " microseconds ("
                             << (timestamp / 1000000.0) << " seconds)" << std::endl;
                } else if (simple_timestamp != 0) {
                    std::cout << "  ✅ Found simple timestamp SEI: " << simple_timestamp << " microseconds ("
                             << (simple_timestamp / 1000000.0) << " seconds)" << std::endl;

                    // Print raw SEI payload for debugging
                    std::cout << "  Raw SEI data (first 32 bytes): ";
                    for (size_t i = 0; i < std::min(length, (uint32_t)32); i++) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0')
                                 << (int)data[nal_start + i] << " ";
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "  ❌ SEI found but not timestamp SEI" << std::endl;

                    // Print raw SEI for debugging
                    std::cout << "  Raw SEI data (first 32 bytes): ";
                    for (size_t i = 0; i < std::min(length, (uint32_t)32); i++) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0')
                                 << (int)data[nal_start + i] << " ";
                    }
                    std::cout << std::endl;
                }
            } else if (nal_type == 1 || nal_type == 5) { // Frame
                frame_count++;
            }
        }

        pos = nal_end;
    }

    std::cout << std::endl;
    std::cout << "Summary:" << std::endl;
    std::cout << "  Total SEI NAL units: " << sei_count << std::endl;
    std::cout << "  Total frame NAL units: " << frame_count << std::endl;

    return 0;
}