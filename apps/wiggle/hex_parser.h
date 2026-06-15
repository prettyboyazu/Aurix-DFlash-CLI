#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A contiguous segment of data from a HEX file
struct HexSegment {
    uint32_t baseAddress;          // Start address of this segment
    std::vector<uint8_t> data;     // Raw data bytes
};

// Parse result
struct HexParseResult {
    bool success;
    std::string errorMsg;
    std::string warningMsg;                  // Non-fatal warnings (e.g. missing EOF)
    std::vector<HexSegment> segments;  // Possibly non-contiguous segments
    uint32_t entryPoint;               // Start address record (if present), 0 otherwise
};

// Parse an Intel HEX file. Supports:
// - :10 data records (type 00)
// - :02000004 extended linear address records (type 04)
// - :00000001FF EOF record (type 01)
// - Checksum validation
HexParseResult parseIntelHex(const std::string& filePath);

// Load a raw binary file as a single segment at given base address
HexParseResult loadBinaryFile(const std::string& filePath, uint32_t baseAddress);
