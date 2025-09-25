#include <iostream>
#include <fstream>
#include <vector>
#include "sei_generator.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.h264> <output.h264>" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = argv[2];

    // Read input file (length-prefixed format)
    std::ifstream input(input_path, std::ios::binary | std::ios::ate);
    if (!input) {
        std::cerr << "Failed to open input: " << input_path << std::endl;
        return 1;
    }

    size_t size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    input.read(reinterpret_cast<char*>(data.data()), size);
    input.close();

    // Create output file
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        std::cerr << "Failed to create output: " << output_path << std::endl;
        return 1;
    }
    
    // Add a simple SEI timestamp at the beginning
    uint64_t test_timestamp = 1751959747173000; // Test timestamp in microseconds
    std::vector<uint8_t> sei_nal = SEIGenerator::createSimpleTimestampSEI(test_timestamp);

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
    std::cout << "Created " << output_path << " with simple SEI timestamp (length-prefixed)" << std::endl;
    return 0;
}