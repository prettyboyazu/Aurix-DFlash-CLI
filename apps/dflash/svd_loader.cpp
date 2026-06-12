#include "svd_loader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <set>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

static uint64_t parseHexAddr(const std::string& s) {
    if (s.empty()) return 0;
    try {
        return std::stoull(s, nullptr, 0);
    } catch (...) {
        return 0;
    }
}

std::string SvdLoader::normalizeName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        if (c == '.' || c == '_') continue;
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

void SvdLoader::buildIndex() {
    nameIndex_.clear();
    addrIndex_.clear();

    for (size_t i = 0; i < registers_.size(); ++i) {
        const auto& reg = registers_[i];
        // Index by fullName (normalized)
        nameIndex_[normalizeName(reg.fullName)] = i;
        // Also index by "peripheral_name" (e.g., "dmu_hf_status")
        std::string alt = normalizeName(reg.peripheral + "_" + reg.fullName);
        if (alt != normalizeName(reg.fullName)) {
            nameIndex_[alt] = i;
        }
        // Index by address (only first register at each address)
        if (addrIndex_.find(reg.address) == addrIndex_.end()) {
            addrIndex_[reg.address] = i;
        }
    }
}

static void parseFields(const json& fieldsArr, std::vector<SvdField>& out) {
    for (const auto& fj : fieldsArr) {
        SvdField f;
        f.name = fj.value("name", "");
        f.desc = fj.value("desc", "");
        f.lsb = fj.value("lsb", 0u);
        f.msb = fj.value("msb", 0u);
        f.access = fj.value("access", "");

        if (fj.contains("values") && fj["values"].is_array()) {
            for (const auto& vj : fj["values"]) {
                SvdEnumVal ev;
                ev.name = vj.value("name", "");
                ev.desc = vj.value("desc", "");
                ev.value = vj.value("value", 0u);
                f.values.push_back(std::move(ev));
            }
        }
        out.push_back(std::move(f));
    }
}

static void parseRegister(const json& rj, const std::string& periphName,
                          const std::string& clusterName,
                          std::vector<SvdRegister>& out)
{
    SvdRegister reg;
    reg.name = rj.value("name", "");
    reg.fullName = rj.value("fullName", reg.name);
    reg.peripheral = periphName;
    reg.cluster = clusterName;
    reg.desc = rj.value("desc", "");
    reg.address = parseHexAddr(rj.value("address", ""));
    reg.size = rj.value("size", 32u);
    reg.access = rj.value("access", "");

    if (rj.contains("resetValue")) {
        const auto& rv = rj["resetValue"];
        if (rv.is_string()) {
            reg.resetValue = static_cast<uint32_t>(parseHexAddr(rv.get<std::string>()));
        } else if (rv.is_number_unsigned()) {
            reg.resetValue = rv.get<uint32_t>();
        }
    }

    if (rj.contains("fields") && rj["fields"].is_array()) {
        parseFields(rj["fields"], reg.fields);
    }

    out.push_back(std::move(reg));
}

bool SvdLoader::loadFromFile(const std::string& jsonPath) {
    std::ifstream ifs(jsonPath);
    if (!ifs.is_open()) {
        std::cerr << "[SvdLoader] Cannot open file: " << jsonPath << std::endl;
        return false;
    }

    json root;
    try {
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
        root = json::parse(content);
    } catch (const json::parse_error& e) {
        std::cerr << "[SvdLoader] JSON parse error: " << e.what() << std::endl;
        return false;
    }

    deviceName_ = root.value("device", "");
    registers_.clear();

    if (!root.contains("peripherals") || !root["peripherals"].is_array()) {
        std::cerr << "[SvdLoader] No 'peripherals' array found" << std::endl;
        return false;
    }

    std::set<std::string> periphSet;
    for (const auto& pj : root["peripherals"]) {
        std::string periphName = pj.value("name", "");
        if (periphName.empty()) continue;
        periphSet.insert(periphName);

        // Parse clusters
        if (pj.contains("clusters") && pj["clusters"].is_array()) {
            for (const auto& cj : pj["clusters"]) {
                std::string clusterName = cj.value("name", "");
                if (cj.contains("registers") && cj["registers"].is_array()) {
                    for (const auto& rj : cj["registers"]) {
                        parseRegister(rj, periphName, clusterName, registers_);
                    }
                }
            }
        }

        // Parse direct registers (not in cluster)
        if (pj.contains("registers") && pj["registers"].is_array()) {
            for (const auto& rj : pj["registers"]) {
                parseRegister(rj, periphName, "", registers_);
            }
        }
    }

    peripheralNames_.assign(periphSet.begin(), periphSet.end());
    buildIndex();

    std::cout << "[SvdLoader] Loaded " << registers_.size() << " registers from "
              << peripheralNames_.size() << " peripherals (" << deviceName_ << ")" << std::endl;
    return true;
}

const SvdRegister* SvdLoader::findRegister(const std::string& name) const {
    std::string key = normalizeName(name);
    auto it = nameIndex_.find(key);
    if (it != nameIndex_.end()) {
        return &registers_[it->second];
    }
    return nullptr;
}

const SvdRegister* SvdLoader::findByAddress(uint64_t addr) const {
    auto it = addrIndex_.find(addr);
    if (it != addrIndex_.end()) {
        return &registers_[it->second];
    }
    return nullptr;
}

std::vector<std::string> SvdLoader::getPeripheralNames() const {
    return peripheralNames_;
}

std::vector<const SvdRegister*> SvdLoader::getRegisters(const std::string& peripheral) const {
    std::vector<const SvdRegister*> result;
    std::string norm = normalizeName(peripheral);
    for (const auto& reg : registers_) {
        if (normalizeName(reg.peripheral) == norm) {
            result.push_back(&reg);
        }
    }
    return result;
}

std::string SvdLoader::getFlashStatusRegName() const {
    // TC3x: DMU.HF.STATUS
    if (findRegister("HF.STATUS") != nullptr) {
        return "HF.STATUS";
    }
    // TC2x: FLASH0.FSR
    if (findRegister("FSR") != nullptr) {
        return "FSR";
    }
    return "";
}

std::string SvdLoader::getFlashErrorRegName() const {
    // TC3x: DMU.HF.ERRSR
    if (findRegister("HF.ERRSR") != nullptr) {
        return "HF.ERRSR";
    }
    // TC2x: FLASH0.FSR (combined status+error)
    if (findRegister("FSR") != nullptr) {
        return "FSR";
    }
    return "";
}
