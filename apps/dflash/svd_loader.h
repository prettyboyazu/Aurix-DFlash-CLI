#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

struct SvdEnumVal {
    std::string name;
    std::string desc;
    uint32_t value = 0;
};

struct SvdField {
    std::string name;
    std::string desc;
    uint32_t lsb = 0;
    uint32_t msb = 0;
    std::string access;
    std::vector<SvdEnumVal> values;
};

struct SvdRegister {
    std::string name;        // "STATUS"
    std::string fullName;    // "HF.STATUS" or "DMU_HF_STATUS"
    std::string peripheral;  // "DMU"
    std::string cluster;     // "HF" (empty if not in cluster)
    std::string desc;
    uint64_t address = 0;    // absolute address
    uint32_t size = 32;      // bits
    std::string access;      // "read-only" / "read-write"
    uint32_t resetValue = 0;
    std::vector<SvdField> fields;
};

class SvdLoader {
public:
    bool loadFromFile(const std::string& jsonPath);

    // Lookup by full name "HF.STATUS" or "DMU_HF_STATUS" (case-insensitive)
    const SvdRegister* findRegister(const std::string& name) const;

    // Lookup by address
    const SvdRegister* findByAddress(uint64_t addr) const;

    // List all peripheral names
    std::vector<std::string> getPeripheralNames() const;

    // List all registers in a peripheral
    std::vector<const SvdRegister*> getRegisters(const std::string& peripheral) const;

    // Get Flash status register name based on device (TC3x: "HF.STATUS", TC2x: "FSR")
    std::string getFlashStatusRegName() const;

    // Get Flash error status register name
    std::string getFlashErrorRegName() const;

    // Get device name from SVD
    const std::string& getDeviceName() const { return deviceName_; }

    // Get total register count
    size_t getRegisterCount() const { return registers_.size(); }

    bool isLoaded() const { return !registers_.empty(); }

private:
    std::string deviceName_;
    std::vector<SvdRegister> registers_;
    std::map<std::string, size_t> nameIndex_;    // normalized name -> index
    std::map<uint64_t, size_t> addrIndex_;       // address -> index
    std::vector<std::string> peripheralNames_;

    static std::string normalizeName(const std::string& name);
    void buildIndex();
};
