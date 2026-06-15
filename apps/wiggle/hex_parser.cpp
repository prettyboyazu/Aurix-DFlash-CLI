#include "hex_parser.h"

#include <fstream>
#include <sstream>
#include <algorithm>

// Parse a single hex character to value (0-15), returns -1 on error
static int hexCharToVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Parse two hex characters to a byte
static bool parseByte(const std::string& line, size_t offset, uint8_t& out)
{
    if (line.size() < 2 || offset > line.size() - 2) return false;
    int hi = hexCharToVal(line[offset]);
    int lo = hexCharToVal(line[offset + 1]);
    if (hi < 0 || lo < 0) return false;
    out = static_cast<uint8_t>((hi << 4) | lo);
    return true;
}

HexParseResult parseIntelHex(const std::string& filePath)
{
    HexParseResult result;
    result.success = false;
    result.entryPoint = 0;

    std::ifstream ifs(filePath);
    if (!ifs) {
        result.errorMsg = "Cannot open file: " + filePath;
        return result;
    }

    uint32_t extLinearAddr = 0;  // Upper 16 bits from type 04 records
    bool forceNewSegment = false;
    int lineNum = 0;
    std::string line;

    while (std::getline(ifs, line)) {
        lineNum++;

        // Strip trailing whitespace / CR
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();

        if (line.empty()) continue;

        // Each line must start with ':'
        if (line[0] != ':') {
            result.errorMsg = "Line " + std::to_string(lineNum) + ": missing ':' prefix";
            return result;
        }

        // Minimum record: :LLAAAATTCC = 1 + 2 + 4 + 2 + 2 = 11 chars
        if (line.size() < 11) {
            result.errorMsg = "Line " + std::to_string(lineNum) + ": record too short";
            return result;
        }

        // Parse fields
        uint8_t byteCount, addrHi, addrLo, recordType;
        if (!parseByte(line, 1, byteCount) ||
            !parseByte(line, 3, addrHi) ||
            !parseByte(line, 5, addrLo) ||
            !parseByte(line, 7, recordType)) {
            result.errorMsg = "Line " + std::to_string(lineNum) + ": invalid hex characters";
            return result;
        }

        uint16_t recordAddr = (static_cast<uint16_t>(addrHi) << 8) | addrLo;

        // Expected length: 1(':') + 2*(byteCount + 4 + 1) hex chars
        size_t expectedLen = 1 + 2 * (static_cast<size_t>(byteCount) + 4 + 1);
        if (line.size() < expectedLen) {
            result.errorMsg = "Line " + std::to_string(lineNum) + ": incomplete record";
            return result;
        }

        // Parse data bytes and checksum
        std::vector<uint8_t> dataBytes(byteCount);
        for (uint8_t i = 0; i < byteCount; i++) {
            if (!parseByte(line, 9 + i * 2, dataBytes[i])) {
                result.errorMsg = "Line " + std::to_string(lineNum) + ": invalid data byte";
                return result;
            }
        }

        uint8_t checksum;
        if (!parseByte(line, 9 + byteCount * 2, checksum)) {
            result.errorMsg = "Line " + std::to_string(lineNum) + ": invalid checksum byte";
            return result;
        }

        // Validate checksum: sum of all bytes (LL + AA + AA + TT + data + CC) mod 256 == 0
        uint32_t sum = byteCount + addrHi + addrLo + recordType;
        for (uint8_t i = 0; i < byteCount; i++) sum += dataBytes[i];
        sum += checksum;
        if ((sum & 0xFF) != 0) {
            result.errorMsg = "Line " + std::to_string(lineNum) + ": checksum error";
            return result;
        }

        // Process record by type
        switch (recordType) {
            case 0x00: {
                // Data record
                uint32_t fullAddr = extLinearAddr + recordAddr;

                // Try to merge into last segment if addresses are contiguous
                if (!result.segments.empty() && !forceNewSegment) {
                    HexSegment& last = result.segments.back();
                    uint32_t lastEnd = last.baseAddress + static_cast<uint32_t>(last.data.size());
                    if (fullAddr == lastEnd) {
                        last.data.insert(last.data.end(), dataBytes.begin(), dataBytes.end());
                        forceNewSegment = false;
                        break;
                    }
                }

                // Start a new segment
                HexSegment seg;
                seg.baseAddress = fullAddr;
                seg.data = std::move(dataBytes);
                result.segments.push_back(std::move(seg));
                forceNewSegment = false;
                break;
            }
            case 0x01:
                // EOF record
                result.success = true;
                return result;

            case 0x04:
                // Extended Linear Address record
                if (byteCount != 2) {
                    result.errorMsg = "Line " + std::to_string(lineNum) + ": type 04 record must have 2 data bytes";
                    return result;
                }
                extLinearAddr = (static_cast<uint32_t>(dataBytes[0]) << 24) |
                                (static_cast<uint32_t>(dataBytes[1]) << 16);
                forceNewSegment = true;
                break;

            case 0x05:
                // Start Linear Address (entry point)
                if (byteCount == 4) {
                    result.entryPoint = (static_cast<uint32_t>(dataBytes[0]) << 24) |
                                        (static_cast<uint32_t>(dataBytes[1]) << 16) |
                                        (static_cast<uint32_t>(dataBytes[2]) << 8) |
                                        static_cast<uint32_t>(dataBytes[3]);
                }
                break;

            case 0x02:
                // Extended Segment Address (legacy 16-bit)
                if (byteCount == 2) {
                    extLinearAddr = ((static_cast<uint32_t>(dataBytes[0]) << 8) |
                                     static_cast<uint32_t>(dataBytes[1])) << 4;
                }
                forceNewSegment = true;
                break;

            case 0x03:
                // Start Segment Address (legacy) - ignore
                break;

            default:
                result.errorMsg = "Line " + std::to_string(lineNum) + ": unknown record type 0x" +
                                  std::to_string(recordType);
                return result;
        }
    }

    // If we reach here without an EOF record, it's still valid data but warn
    if (result.segments.empty()) {
        result.errorMsg = "No data records found";
        return result;
    }

    // No EOF record found but we have data - treat as success with warning
    result.success = true;
    result.warningMsg = "HEX file missing EOF record (data may be incomplete)";
    return result;
}

HexParseResult loadBinaryFile(const std::string& filePath, uint32_t baseAddress)
{
    HexParseResult result;
    result.success = false;
    result.entryPoint = 0;

    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs) {
        result.errorMsg = "Cannot open file: " + filePath;
        return result;
    }

    auto pos = ifs.tellg();
    if (pos == std::streampos(-1)) {
        result.errorMsg = "Failed to determine file size: " + filePath;
        return result;
    }
    size_t fileSize = static_cast<size_t>(pos);
    if (fileSize == 0) {
        result.errorMsg = "File is empty: " + filePath;
        return result;
    }

    ifs.seekg(0);
    HexSegment seg;
    seg.baseAddress = baseAddress;
    seg.data.resize(static_cast<size_t>(fileSize));
    ifs.read(reinterpret_cast<char*>(seg.data.data()), fileSize);

    if (!ifs) {
        result.errorMsg = "Read error: " + filePath;
        return result;
    }

    result.segments.push_back(std::move(seg));
    result.success = true;
    return result;
}
