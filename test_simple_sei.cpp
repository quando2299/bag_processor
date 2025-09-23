#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include "sei_generator.h"

int main() {
    // Test the simple SEI format
    uint64_t test_timestamp = 1751959747173000; // microseconds from rosbag

    std::cout << "Creating simple SEI with timestamp: " << test_timestamp << " microseconds" << std::endl;
    std::cout << "That's " << (test_timestamp / 1000000.0) << " seconds" << std::endl;

    std::vector<uint8_t> sei_nal = SEIGenerator::createSimpleTimestampSEI(test_timestamp);

    std::cout << "Generated SEI NAL unit size: " << sei_nal.size() << " bytes" << std::endl;
    std::cout << "Raw SEI data: ";
    for (size_t i = 0; i < sei_nal.size(); i++) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)sei_nal[i] << " ";
    }
    std::cout << std::endl;

    // Write a test H264 file with this SEI
    std::ofstream test_file("test_simple_sei.h264", std::ios::binary);

    // Write start code + SEI
    uint8_t start_code[] = {0x00, 0x00, 0x00, 0x01};
    test_file.write(reinterpret_cast<char*>(start_code), 4);
    test_file.write(reinterpret_cast<char*>(sei_nal.data()), sei_nal.size());

    // Write a simple frame NAL (minimal)
    test_file.write(reinterpret_cast<char*>(start_code), 4);
    uint8_t frame_nal[] = {0x41, 0x9A, 0x24, 0x4D, 0x00, 0x28, 0x88, 0x09, 0x11, 0x00}; // Dummy IDR frame
    test_file.write(reinterpret_cast<char*>(frame_nal), sizeof(frame_nal));

    test_file.close();

    std::cout << "Created test_simple_sei.h264 file" << std::endl;

    return 0;
}