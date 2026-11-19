/*
 *  Copyright (c) 2026 Infineon Technologies AG.
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
//  TriCore TC1.6 指令译码（C++ 原生，无外部依赖）。
//  首要保证指令长度(16/32)正确——step-over/反汇编依赖；长度判别规则:
//  32-bit 指令首半字 opcode[12:9] ∈ {5,7,B,D,F}（其余为 16-bit）。
//  助记符为常见子集，未覆盖者显示 .hword/.word 原始值（不误导）。
//********************************************************************************************************************

#ifndef WIGGLE_TRICORE_DISASM_H
#define WIGGLE_TRICORE_DISASM_H

#include <cstdint>
#include <string>

namespace wiggle_dbg {

struct DisasmInsn {
    uint32_t addr = 0;
    uint32_t length = 2;      // 2 或 4 字节
    uint32_t raw = 0;         // 首半字(16)或全字(32)
    bool is32 = false;
    bool is_call = false;
    bool is_ret = false;
    bool is_branch = false;
    std::string mnemonic;
};

// 判别 16/32 位长度（首半字）
inline bool tricoreIs32(uint16_t h)
{
    uint32_t low = (h >> 9) & 0xFu;
    return low == 5 || low == 7 || low == 0xB || low == 0xD || low == 0xF;
}

// 译码一条指令；buf 至少 4 字节可读（不足 4 时按 2 处理）
DisasmInsn tricoreDecode(uint32_t addr, const uint8_t* buf, uint32_t avail);

}  // namespace wiggle_dbg

#endif  // WIGGLE_TRICORE_DISASM_H