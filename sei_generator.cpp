#include "sei_generator.h"
#include <algorithm>
#include <cstring>

// Definition of static constexpr member (required for C++14)
constexpr std::array<uint8_t, 16> SEIGenerator::TIMESTAMP_UUID;

std::vector<uint8_t> SEIGenerator::createTimestampSEI(uint64_t timestamp_us) {
    std::vector<uint8_t> timestamp_bytes = timestampToBytes(timestamp_us);
    return createUserDataSEI(TIMESTAMP_UUID, timestamp_bytes);
}

std::vector<uint8_t> SEIGenerator::createSimpleTimestampSEI(uint64_t timestamp_us) {
    std::vector<uint8_t> sei_nal;

    // NAL header (type 6 = SEI)
    sei_nal.push_back(NAL_UNIT_TYPE_SEI);

    // Simple SEI format expected by Flutter:
    // payload_type (we'll use type 1 for simple timestamp)
    sei_nal.push_back(0x01);

    // payload_size (8 bytes for timestamp)
    sei_nal.push_back(0x08);

    // 8-byte timestamp in big-endian format
    std::vector<uint8_t> timestamp_bytes = timestampToBytes(timestamp_us);
    sei_nal.insert(sei_nal.end(), timestamp_bytes.begin(), timestamp_bytes.end());

    // RBSP trailing bits (stop bit)
    sei_nal.push_back(0x80);

    return sei_nal;
}

std::vector<uint8_t> SEIGenerator::createUserDataSEI(
    const std::array<uint8_t, 16>& uuid,
    const std::vector<uint8_t>& data) {

    std::vector<uint8_t> sei_payload;

    // Payload type (user_data_unregistered)
    sei_payload.push_back(SEI_TYPE_USER_DATA_UNREGISTERED);

    // Calculate payload size (16 bytes UUID + data size)
    size_t payload_size = 16 + data.size();

    // Write payload size (using variable length encoding as per H.264 spec)
    // For simplicity, we'll use single byte for sizes < 255
    if (payload_size < 255) {
        sei_payload.push_back(static_cast<uint8_t>(payload_size));
    } else {
        // For larger sizes, use 0xFF bytes followed by actual size
        while (payload_size >= 255) {
            sei_payload.push_back(0xFF);
            payload_size -= 255;
        }
        sei_payload.push_back(static_cast<uint8_t>(payload_size));
        payload_size = 16 + data.size(); // Restore original size
    }

    // Add UUID
    sei_payload.insert(sei_payload.end(), uuid.begin(), uuid.end());

    // Add custom data
    sei_payload.insert(sei_payload.end(), data.begin(), data.end());

    // Create complete NAL unit
    std::vector<uint8_t> nal_unit;

    // NAL header (forbidden_zero_bit = 0, nal_ref_idc = 0, nal_unit_type = 6)
    nal_unit.push_back(NAL_UNIT_TYPE_SEI);

    // Add SEI payload with RBSP encoding
    std::vector<uint8_t> rbsp_data = writeRBSP(sei_payload);
    nal_unit.insert(nal_unit.end(), rbsp_data.begin(), rbsp_data.end());

    // Add RBSP trailing bits (stop bit + alignment)
    nal_unit.push_back(0x80);

    return nal_unit;
}

uint64_t SEIGenerator::extractTimestampFromSEI(const std::vector<uint8_t>& sei_nalu) {
    if (!isTimestampSEI(sei_nalu)) {
        return 0;
    }

    // Skip NAL header
    size_t pos = 1;

    // Remove RBSP encoding
    std::vector<uint8_t> payload_end(sei_nalu.begin() + pos, sei_nalu.end());
    std::vector<uint8_t> payload = readRBSP(payload_end);

    pos = 0;

    // Skip payload type
    if (payload[pos] != SEI_TYPE_USER_DATA_UNREGISTERED) {
        return 0;
    }
    pos++;

    // Read payload size
    size_t payload_size = 0;
    while (pos < payload.size() && payload[pos] == 0xFF) {
        payload_size += 255;
        pos++;
    }
    if (pos < payload.size()) {
        payload_size += payload[pos];
        pos++;
    }

    // Check if we have enough data
    if (pos + 16 + 8 > payload.size()) {
        return 0;
    }

    // Check UUID
    bool uuid_match = true;
    for (size_t i = 0; i < 16; i++) {
        if (payload[pos + i] != TIMESTAMP_UUID[i]) {
            uuid_match = false;
            break;
        }
    }

    if (!uuid_match) {
        return 0;
    }

    pos += 16;

    // Extract timestamp bytes
    std::vector<uint8_t> timestamp_bytes(payload.begin() + pos, payload.begin() + pos + 8);
    return bytesToTimestamp(timestamp_bytes);
}

bool SEIGenerator::isTimestampSEI(const std::vector<uint8_t>& nalu) {
    if (nalu.empty()) {
        return false;
    }

    // Check NAL unit type
    uint8_t nal_type = nalu[0] & 0x1F;
    if (nal_type != NAL_UNIT_TYPE_SEI) {
        return false;
    }

    // Try to extract timestamp, if successful it's a timestamp SEI
    return extractTimestampFromSEI(nalu) != 0 || extractSimpleTimestampFromSEI(nalu) != 0;
}

std::vector<uint8_t> SEIGenerator::timestampToBytes(uint64_t timestamp_us) {
    std::vector<uint8_t> bytes(8);

    // Convert to big-endian
    for (int i = 7; i >= 0; i--) {
        bytes[7 - i] = (timestamp_us >> (i * 8)) & 0xFF;
    }

    return bytes;
}

uint64_t SEIGenerator::bytesToTimestamp(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 8) {
        return 0;
    }

    uint64_t timestamp = 0;

    // Convert from big-endian
    for (int i = 0; i < 8; i++) {
        timestamp = (timestamp << 8) | bytes[i];
    }

    return timestamp;
}

uint64_t SEIGenerator::extractSimpleTimestampFromSEI(const std::vector<uint8_t>& sei_nalu) {
    if (sei_nalu.size() < 12) { // Minimum: NAL header + payload_type + payload_size + 8 bytes timestamp + RBSP trailing
        return 0;
    }

    // Check NAL unit type
    uint8_t nal_type = sei_nalu[0] & 0x1F;
    if (nal_type != NAL_UNIT_TYPE_SEI) {
        return 0;
    }

    // Check if it's our simple format
    if (sei_nalu[1] != 0x01 || sei_nalu[2] != 0x08) { // payload_type=1, payload_size=8
        return 0;
    }

    // Extract 8-byte timestamp starting at offset 3
    uint64_t timestamp = 0;
    for (int i = 0; i < 8; i++) {
        timestamp = (timestamp << 8) | sei_nalu[3 + i];
    }

    return timestamp;
}

std::vector<uint8_t> SEIGenerator::writeRBSP(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> rbsp;

    for (size_t i = 0; i < data.size(); i++) {
        // Check for emulation prevention: 0x00 0x00 0x00/0x01/0x02/0x03
        if (i >= 2 && data[i - 2] == 0x00 && data[i - 1] == 0x00 &&
            data[i] <= 0x03) {
            // Insert emulation prevention byte 0x03
            rbsp.push_back(0x03);
        }
        rbsp.push_back(data[i]);
    }

    return rbsp;
}

std::vector<uint8_t> SEIGenerator::readRBSP(const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> data;

    for (size_t i = 0; i < rbsp.size(); i++) {
        // Skip emulation prevention bytes (0x03) after 0x00 0x00
        if (i >= 2 && rbsp[i - 2] == 0x00 && rbsp[i - 1] == 0x00 &&
            rbsp[i] == 0x03) {
            // Skip this byte
            continue;
        }

        // Skip trailing RBSP bits
        if (i == rbsp.size() - 1 && rbsp[i] == 0x80) {
            break;
        }

        data.push_back(rbsp[i]);
    }

    return data;
}