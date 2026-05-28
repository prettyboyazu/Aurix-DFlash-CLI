#include "device_config_loader.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <climits>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

// Helper: convert hex string (e.g. "0xAF000000") to uint32_t.
// NOTE: Known limitation - values exceeding UINT32_MAX are truncated with a WARNING.
// This is acceptable because all AURIX DFlash addresses fit in 32 bits.
static uint32_t hexStringToUint32(const std::string& s) {
    if (s.empty()) return 0;
    try {
        unsigned long long val = std::stoull(s, nullptr, 0);
        if (val > UINT32_MAX) {
            fprintf(stderr, "WARNING: hex value '%s' exceeds UINT32_MAX, truncated\n", s.c_str());
        }
        return static_cast<uint32_t>(val);
    } catch (const std::exception& e) {
        fprintf(stderr, "WARNING: Failed to parse hex value '%s': %s\n", s.c_str(), e.what());
        return 0;
    }
}

// Helper: case-insensitive string comparison
static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Helper: case-insensitive substring search
static bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.size() > haystack.size()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

// Helper: extract short name from filename, e.g. "TC27x_D_step.json" -> "TC27x"
static std::string extractShortName(const std::string& filename) {
    // Look for pattern like "TC##x" at the beginning
    // Filename examples: "TC27x_D_step.json", "TC37x_A_step.json"
    std::string stem = fs::path(filename).stem().string();
    
    // Find "TC" prefix and extract up to (and including) the 'x'
    size_t tcPos = stem.find("TC");
    if (tcPos == std::string::npos) {
        // Fallback: return stem up to first underscore
        size_t us = stem.find('_');
        return (us != std::string::npos) ? stem.substr(0, us) : stem;
    }
    
    // From "TC", find the next 'x' or 'X'
    size_t xPos = stem.find_first_of("xX", tcPos + 2);
    if (xPos != std::string::npos) {
        return stem.substr(tcPos, xPos - tcPos + 1);
    }
    
    // Fallback: take until underscore or end
    size_t endPos = stem.find('_', tcPos);
    return (endPos != std::string::npos) ? stem.substr(tcPos, endPos - tcPos) : stem.substr(tcPos);
}

bool DeviceConfigLoader::loadFromDirectory(const std::string& dirPath) {
    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        std::cerr << "[DeviceConfigLoader] Directory does not exist: " << dirPath << std::endl;
        return false;
    }

    int loadedCount = 0;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;

        const std::string filename = entry.path().filename().string();
        
        // Only process .json files
        if (entry.path().extension() != ".json") continue;

        // Skip "devices.json" - it's an index file not used by this loader
        // (loadFromDirectory parses each device JSON individually)
        if (iequals(filename, "devices.json")) continue;

        // Skip TC4x config files - TC4x uses different DFlash base addresses
        // (0xAE000000/0xAC000000) and erase sequences; not supported in this tool.
        // Runtime detection via tas_df_check_if_tc4x() provides a second safety check.
        if (filename.size() >= 3 && filename.substr(0, 3) == "TC4") continue;

        if (parseDeviceJson(entry.path().string())) {
            loadedCount++;
        }
    }

    std::cout << "[DeviceConfigLoader] Loaded " << loadedCount << " device config(s) from: " << dirPath << std::endl;
    return loadedCount > 0;
}

bool DeviceConfigLoader::parseDeviceJson(const std::string& filePath) {
    // Read file content
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        std::cerr << "[DeviceConfigLoader] Cannot open file: " << filePath << std::endl;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    // Parse JSON
    json root;
    try {
        root = json::parse(content);
    } catch (const json::parse_error& e) {
        std::cerr << "[DeviceConfigLoader] JSON parse error in " << filePath << ": " << e.what() << std::endl;
        return false;
    }

    // Extract deviceName
    if (!root.contains("deviceName") || !root["deviceName"].is_string()) {
        std::cerr << "[DeviceConfigLoader] Missing 'deviceName' in: " << filePath << std::endl;
        return false;
    }
    std::string deviceName = root["deviceName"].get<std::string>();

    // Extract JTAG IDs from registers array
    std::vector<uint32_t> jtagIds;
    if (root.contains("registers") && root["registers"].is_array()) {
        for (const auto& reg : root["registers"]) {
            if (!reg.contains("registerName") || !reg["registerName"].is_string()) continue;
            if (reg["registerName"].get<std::string>() != "JTAGID") continue;

            if (reg.contains("registerValue") && reg["registerValue"].is_array()) {
                for (const auto& val : reg["registerValue"]) {
                    if (val.is_string()) {
                        uint32_t id = hexStringToUint32(val.get<std::string>());
                        if (id != 0) {
                            jtagIds.push_back(id);
                        }
                    }
                }
            }
            break; // Found JTAGID entry, no need to continue
        }
    }

    // Extract DataFlash memory segment
    uint32_t baseAddress = 0;
    uint32_t totalSize = 0;
    uint32_t sectorSize = 0;
    uint32_t ucbBaseAddress = 0;
    uint32_t ucbSectorSize = 0;
    uint32_t ucbNumSectors = 0;
    bool foundDFlash = false;

    if (root.contains("memory") && root["memory"].is_array()) {
        for (const auto& mem : root["memory"]) {
            if (!mem.contains("memoryName") || !mem["memoryName"].is_string()) continue;
            if (mem["memoryName"].get<std::string>() != "DataFlash") continue;

            // Extract base address
            if (mem.contains("memoryStartAddress") && mem["memoryStartAddress"].is_string()) {
                baseAddress = hexStringToUint32(mem["memoryStartAddress"].get<std::string>());
            }

            // Extract total size
            if (mem.contains("memorySize") && mem["memorySize"].is_string()) {
                totalSize = hexStringToUint32(mem["memorySize"].get<std::string>());
            }

            // Extract sector size from first logical sector
            if (mem.contains("logicalSectors") && mem["logicalSectors"].is_array()) {
                const auto& sectors = mem["logicalSectors"];
                if (!sectors.empty() && sectors[0].contains("sectorSize")) {
                    const auto& ss = sectors[0]["sectorSize"];
                    if (ss.is_string()) {
                        sectorSize = hexStringToUint32(ss.get<std::string>());
                    } else if (ss.is_number_unsigned()) {
                        sectorSize = ss.get<uint32_t>();
                    }
                }
            }

            // Parse UCB array if present (inside the DataFlash memory entry)
            if (mem.contains("UCB") && mem["UCB"].is_array()) {
                const auto& ucbArray = mem["UCB"];
                if (!ucbArray.empty()) {
                    // Extract baseAddress from first sector's sectorStartAddress
                    if (ucbArray[0].contains("sectorStartAddress") && ucbArray[0]["sectorStartAddress"].is_string()) {
                        ucbBaseAddress = hexStringToUint32(ucbArray[0]["sectorStartAddress"].get<std::string>());
                    }
                    // Extract sectorSize from first sector
                    if (ucbArray[0].contains("sectorSize")) {
                        const auto& ss = ucbArray[0]["sectorSize"];
                        if (ss.is_string()) {
                            ucbSectorSize = hexStringToUint32(ss.get<std::string>());
                        } else if (ss.is_number_unsigned()) {
                            ucbSectorSize = ss.get<uint32_t>();
                        }
                    }
                    ucbNumSectors = static_cast<uint32_t>(ucbArray.size());
                }
            }

            foundDFlash = true;
            break;
        }
    }

    if (!foundDFlash || baseAddress == 0 || totalSize == 0 || sectorSize == 0) {
        std::cerr << "[DeviceConfigLoader] Missing or invalid DataFlash config in: " << filePath << std::endl;
        return false;
    }

    // Determine family from filename
    std::string filename = fs::path(filePath).filename().string();
    std::string shortName = extractShortName(filename);
    std::string family;
    bool isTc3x = false;

    if (filename.find("TC2") != std::string::npos || shortName.find("TC2") != std::string::npos) {
        family = "TC2x";
        isTc3x = false;
    } else if (filename.find("TC3") != std::string::npos || shortName.find("TC3") != std::string::npos) {
        family = "TC3x";
        isTc3x = true;
    } else {
        // Infer from base address
        if (baseAddress == 0xAF000000) {
            family = "TC2x";  // Default assumption
            isTc3x = false;
        } else {
            family = "Unknown";
            isTc3x = false;
        }
    }

    // Build device entry
    DeviceEntry entry;
    entry.dflash.deviceName = deviceName;
    entry.dflash.shortName = shortName;
    entry.dflash.family = family;
    entry.dflash.isTc3x = isTc3x;
    entry.dflash.baseAddress = baseAddress;
    entry.dflash.totalSize = totalSize;
    entry.dflash.sectorSize = sectorSize;
    // NOTE: Assumes uniform sector size across all DFlash sectors.
    // This holds true for all current TC2x/TC3x DataFlash configurations.
    // ProgramFlash may have non-uniform sectors, but DataFlash does not.
    entry.dflash.numSectors = totalSize / sectorSize;
    // Populate UCB config (remains default zeros if JSON has no UCB array)
    entry.dflash.ucb.baseAddress = ucbBaseAddress;
    entry.dflash.ucb.sectorSize = ucbSectorSize;
    entry.dflash.ucb.numSectors = ucbNumSectors;
    entry.dflash.ucb.totalSize = ucbNumSectors * ucbSectorSize;
    entry.jtagIds = jtagIds;

    devices_.push_back(std::move(entry));
    return true;
}

bool DeviceConfigLoader::findByJtagId(uint32_t jtagId, DFlashConfig& cfg) const {
    const uint32_t maskedInput = jtagId & 0x0FFFFFFF;

    for (const auto& entry : devices_) {
        for (uint32_t id : entry.jtagIds) {
            if ((id & 0x0FFFFFFF) == maskedInput) {
                cfg = entry.dflash;
                return true;
            }
        }
    }
    return false;
}

bool DeviceConfigLoader::findByName(const std::string& name, DFlashConfig& cfg) const {
    // First try exact match on shortName (case-insensitive)
    for (const auto& entry : devices_) {
        if (iequals(entry.dflash.shortName, name)) {
            cfg = entry.dflash;
            return true;
        }
    }

    // Then try substring match on deviceName (case-insensitive)
    for (const auto& entry : devices_) {
        if (icontains(entry.dflash.deviceName, name)) {
            cfg = entry.dflash;
            return true;
        }
    }

    return false;
}

std::vector<std::string> DeviceConfigLoader::getSupportedDeviceNames() const {
    std::vector<std::string> names;
    for (const auto& entry : devices_) {
        names.push_back(entry.dflash.shortName);
    }

    // Sort and deduplicate
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}
