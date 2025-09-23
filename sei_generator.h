#ifndef SEI_GENERATOR_H
#define SEI_GENERATOR_H

#include <vector>
#include <cstdint>
#include <array>

// NAL unit type for SEI
constexpr uint8_t NAL_UNIT_TYPE_SEI = 6;

// SEI payload types
constexpr uint8_t SEI_TYPE_USER_DATA_UNREGISTERED = 5;
constexpr uint8_t SEI_TYPE_USER_DATA_REGISTERED = 4;

class SEIGenerator {
private:
    // UUID for custom timestamp SEI (randomly generated but fixed for this application)
    // "ROSBAG-TIMESTAMP" identifier
    static constexpr std::array<uint8_t, 16> TIMESTAMP_UUID = {
        0x52, 0x4F, 0x53, 0x42,  // "ROSB"
        0x41, 0x47, 0x2D, 0x54,  // "AG-T"
        0x49, 0x4D, 0x45, 0x53,  // "IMES"
        0x54, 0x41, 0x4D, 0x50   // "TAMP"
    };

public:
    /**
     * Create a SEI NAL unit containing a timestamp
     * @param timestamp_us Timestamp in microseconds
     * @return SEI NAL unit data (without start code)
     */
    static std::vector<uint8_t> createTimestampSEI(uint64_t timestamp_us);

    /**
     * Create a simple SEI NAL unit compatible with Flutter decoder
     * @param timestamp_us Timestamp in microseconds
     * @return SEI NAL unit data (without start code)
     */
    static std::vector<uint8_t> createSimpleTimestampSEI(uint64_t timestamp_us);

    /**
     * Create a SEI NAL unit with custom user data
     * @param uuid 16-byte UUID identifier
     * @param data Custom data to embed
     * @return SEI NAL unit data (without start code)
     */
    static std::vector<uint8_t> createUserDataSEI(
        const std::array<uint8_t, 16>& uuid,
        const std::vector<uint8_t>& data
    );

    /**
     * Extract timestamp from a SEI NAL unit
     * @param sei_nalu SEI NAL unit data (without start code)
     * @return Timestamp in microseconds, or 0 if not a timestamp SEI
     */
    static uint64_t extractTimestampFromSEI(const std::vector<uint8_t>& sei_nalu);

    /**
     * Extract timestamp from a simple SEI NAL unit
     * @param sei_nalu SEI NAL unit data (without start code)
     * @return Timestamp in microseconds, or 0 if not a simple timestamp SEI
     */
    static uint64_t extractSimpleTimestampFromSEI(const std::vector<uint8_t>& sei_nalu);

    /**
     * Check if a NAL unit is a timestamp SEI
     * @param nalu NAL unit data (without start code)
     * @return true if it's a timestamp SEI with our UUID
     */
    static bool isTimestampSEI(const std::vector<uint8_t>& nalu);

    /**
     * Convert timestamp to 8-byte big-endian format
     * @param timestamp_us Timestamp in microseconds
     * @return 8-byte vector in big-endian format
     */
    static std::vector<uint8_t> timestampToBytes(uint64_t timestamp_us);

    /**
     * Convert 8-byte big-endian format to timestamp
     * @param bytes 8-byte vector in big-endian format
     * @return Timestamp in microseconds
     */
    static uint64_t bytesToTimestamp(const std::vector<uint8_t>& bytes);

private:
    /**
     * Write RBSP (Raw Byte Sequence Payload) with emulation prevention
     * @param data Raw data to write
     * @return RBSP encoded data
     */
    static std::vector<uint8_t> writeRBSP(const std::vector<uint8_t>& data);

    /**
     * Read RBSP and remove emulation prevention bytes
     * @param rbsp RBSP encoded data
     * @return Raw data
     */
    static std::vector<uint8_t> readRBSP(const std::vector<uint8_t>& rbsp);
};

#endif // SEI_GENERATOR_H