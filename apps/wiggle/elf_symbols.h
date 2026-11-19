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
//  手写 ELF32 小端符号表解析（TriCore TASKING/GCC 产物），供 bt/cpu_reg/step 符号化。
//  只读 .symtab + 关联 .strtab，保留 FUNC/OBJECT 已定义符号；全部偏移做边界校验。
//********************************************************************************************************************

#ifndef WIGGLE_ELF_SYMBOLS_H
#define WIGGLE_ELF_SYMBOLS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wiggle_dbg {

struct ElfSymbol {
    std::string name;
    uint32_t value = 0;
    uint32_t size = 0;
    uint8_t type = 0;  // STT_FUNC=2 / STT_OBJECT=1
};

class ElfSymbolTable {
public:
    bool load(const std::string& path, std::string& err);
    bool isLoaded() const { return !syms_.empty(); }
    const std::string& sourcePath() const { return path_; }
    size_t count() const { return syms_.size(); }

    // 地址→符号：优先区间命中（value<=addr<value+size），否则最近前驱符号；offset 输出偏移
    const ElfSymbol* findByAddr(uint32_t addr, uint32_t& offset) const;
    const ElfSymbol* findByName(const std::string& name) const;
    std::vector<const ElfSymbol*> list(const std::string& filter, size_t maxResults) const;

private:
    std::vector<ElfSymbol> syms_;  // 按 value 升序
    std::map<std::string, size_t> nameIndex_;
    std::string path_;
};

}  // namespace wiggle_dbg

#endif  // WIGGLE_ELF_SYMBOLS_H