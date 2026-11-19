/*
 *  Copyright (c) 2026 Infineon Technologies AG.
 *
 *  This file is part of TAS Client, an API for device access for Infineon's
 *  automotive MCUs.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *  **************************************************************************************************************** */

//********************************************************************************************************************
//  手写 ELF32 小端符号表解析实现
//********************************************************************************************************************

#include "elf_symbols.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace wiggle_dbg {

namespace {

constexpr size_t ELF_MAX_FILE = 64u * 1024u * 1024u;  // 符号文件上限 64MB
constexpr uint16_t EM_TRICORE = 44;
constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint8_t STT_FUNC = 2;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool ElfSymbolTable::load(const std::string& path, std::string& err)
{
    syms_.clear();
    nameIndex_.clear();
    path_.clear();

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "cannot open file: " + path; return false; }
    std::streamsize sz = f.tellg();
    if (sz < 52 || static_cast<size_t>(sz) > ELF_MAX_FILE) {
        err = "invalid ELF size";
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    if (!f) { err = "file read failed"; return false; }

    if (std::memcmp(buf.data(), "\x7f" "ELF", 4) != 0 || buf[4] != 1 || buf[5] != 1) {
        err = "not a 32-bit little-endian ELF";
        return false;
    }
    uint16_t machine = le16(buf.data() + 18);
    if (machine != EM_TRICORE) {
        err = "not a TriCore ELF (e_machine=" + std::to_string(machine) + ")";
        return false;
    }

    uint32_t shoff = le32(buf.data() + 32);
    uint16_t shentsize = le16(buf.data() + 46);
    uint16_t shnum = le16(buf.data() + 48);
    if (shentsize < 40 || shoff == 0 || shnum == 0 ||
        static_cast<uint64_t>(shoff) + static_cast<uint64_t>(shnum) * shentsize > buf.size()) {
        err = "missing or corrupt section headers";
        return false;
    }

    auto shAt = [&](uint32_t i) { return buf.data() + shoff + static_cast<size_t>(i) * shentsize; };

    for (uint16_t i = 0; i < shnum; i++) {
        const uint8_t* sh = shAt(i);
        if (le32(sh + 4) != SHT_SYMTAB) continue;

        uint32_t symOff = le32(sh + 16), symSize = le32(sh + 20);
        uint32_t link = le32(sh + 24), entsize = le32(sh + 36);
        if (entsize < 16) entsize = 16;
        if (link >= shnum ||
            static_cast<uint64_t>(symOff) + symSize > buf.size()) {
            err = "corrupt symtab section";
            return false;
        }
        const uint8_t* strSh = shAt(link);
        uint32_t strOff = le32(strSh + 16), strSize = le32(strSh + 20);
        if (static_cast<uint64_t>(strOff) + strSize > buf.size()) {
            err = "corrupt strtab section";
            return false;
        }
        const char* strtab = reinterpret_cast<const char*>(buf.data() + strOff);

        uint32_t n = symSize / entsize;
        for (uint32_t s = 0; s < n; s++) {
            const uint8_t* sym = buf.data() + symOff + static_cast<size_t>(s) * entsize;
            uint32_t st_name = le32(sym);
            uint8_t type = sym[12] & 0xFu;
            uint16_t st_shndx = le16(sym + 14);
            if ((type != STT_FUNC && type != STT_OBJECT) || st_shndx == 0 || st_name >= strSize)
                continue;
            // strtab 内按 null 终结截取，边界钳制
            size_t maxLen = strSize - st_name;
            size_t len = strnlen(strtab + st_name, maxLen);
            if (len == 0) continue;
            ElfSymbol e;
            e.name.assign(strtab + st_name, len);
            e.value = le32(sym + 4);
            e.size = le32(sym + 8);
            e.type = type;
            syms_.push_back(std::move(e));
        }
    }

    if (syms_.empty()) { err = "no FUNC/OBJECT symbols in .symtab"; return false; }
    std::stable_sort(syms_.begin(), syms_.end(),
                     [](const ElfSymbol& a, const ElfSymbol& b) { return a.value < b.value; });
    for (size_t i = 0; i < syms_.size(); i++)
        nameIndex_.emplace(syms_[i].name, i);  // 重名取首个
    path_ = path;
    return true;
}

const ElfSymbol* ElfSymbolTable::findByAddr(uint32_t addr, uint32_t& offset) const
{
    if (syms_.empty()) return nullptr;
    // 二分找最后一个 value <= addr
    size_t lo = 0, hi = syms_.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (syms_[mid].value <= addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return nullptr;
    size_t idx = lo - 1;
    // 优先区间命中（向前最多回溯 8 个，覆盖 size 重叠的别名符号）
    for (size_t k = idx;; --k) {
        const ElfSymbol& s = syms_[k];
        if (s.size > 0 && addr < s.value + s.size) {
            offset = addr - s.value;
            return &s;
        }
        if (k == 0 || idx - k >= 8) break;
    }
    offset = addr - syms_[idx].value;
    return &syms_[idx];
}

const ElfSymbol* ElfSymbolTable::findByName(const std::string& name) const
{
    auto it = nameIndex_.find(name);
    if (it == nameIndex_.end()) return nullptr;
    return &syms_[it->second];
}

std::vector<const ElfSymbol*> ElfSymbolTable::list(const std::string& filter, size_t maxResults) const
{
    std::vector<const ElfSymbol*> out;
    for (const auto& s : syms_) {
        if (!filter.empty() && s.name.find(filter) == std::string::npos) continue;
        out.push_back(&s);
        if (out.size() >= maxResults) break;
    }
    return out;
}

}  // namespace wiggle_dbg