#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct UCBConfig {
    uint32_t baseAddress = 0;   // 0xAF400000
    uint32_t sectorSize = 0;    // 0x200 (512B)
    uint32_t numSectors = 0;    // from JSON UCB array length
    uint32_t totalSize = 0;     // numSectors * sectorSize
};

struct MemoryRegion {
    std::string name;       // "ProgramFlash", "DataFlash", "CPU0 DSPR", "CPU0 PSPR"
    uint64_t startAddr = 0;
    uint64_t endAddr = 0;
    uint32_t size = 0;
    bool isSRAM = false;
};

struct DeviceConfig {
    std::string deviceName;   // e.g. "TC27x D step"
    std::string shortName;    // e.g. "TC27x" (used for CLI matching)
    std::string family;       // "TC2x" / "TC3x"
    bool isTc3x;
    uint32_t baseAddress;     // 0xAF000000
    uint32_t totalSize;       // DFlash total bytes
    uint32_t sectorSize;      // logical sector size
    uint32_t numSectors;      // number of sectors
    UCBConfig ucb;            // UCB (User Configuration Block) config
    std::string svdFilePath;  // relative path to register def JSON
    std::vector<MemoryRegion> memoryRegions;  // all memory regions (PFlash, DFlash, SRAM)
};

class DeviceConfigLoader {
public:
    // Load all JSON configs from directory
    bool loadFromDirectory(const std::string& dirPath);

    // Find config by JTAG ID (masks out version nibble)
    bool findByJtagId(uint32_t jtagId, DeviceConfig& cfg) const;

    // Find config by short name (case-insensitive, e.g. "TC27x")
    bool findByName(const std::string& name, DeviceConfig& cfg) const;

    // Get list of supported device short names
    std::vector<std::string> getSupportedDeviceNames() const;

    // Get number of loaded configs
    size_t getDeviceCount() const { return devices_.size(); }

private:
    struct DeviceEntry {
        DeviceConfig dflash;
        std::vector<uint32_t> jtagIds;  // from JSON registers
    };
    std::vector<DeviceEntry> devices_;

    bool parseDeviceJson(const std::string& filePath);
};
