#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct DFlashConfig {
    std::string deviceName;   // e.g. "TC27x D step"
    std::string shortName;    // e.g. "TC27x" (used for CLI matching)
    std::string family;       // "TC2x" / "TC3x"
    bool isTc3x;
    uint32_t baseAddress;     // 0xAF000000
    uint32_t totalSize;       // DFlash total bytes
    uint32_t sectorSize;      // logical sector size
    uint32_t numSectors;      // number of sectors
};

class DeviceConfigLoader {
public:
    // Load all JSON configs from directory
    bool loadFromDirectory(const std::string& dirPath);

    // Find config by JTAG ID (masks out version nibble)
    bool findByJtagId(uint32_t jtagId, DFlashConfig& cfg) const;

    // Find config by short name (case-insensitive, e.g. "TC27x")
    bool findByName(const std::string& name, DFlashConfig& cfg) const;

    // Get list of supported device short names
    std::vector<std::string> getSupportedDeviceNames() const;

    // Get number of loaded configs
    size_t getDeviceCount() const { return devices_.size(); }

private:
    struct DeviceEntry {
        DFlashConfig dflash;
        std::vector<uint32_t> jtagIds;  // from JSON registers
    };
    std::vector<DeviceEntry> devices_;

    bool parseDeviceJson(const std::string& filePath);
};
