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
//  TriCore 译码实现：长度判别 + 常见助记符子集（未覆盖显示原始值，不误导）
//********************************************************************************************************************

#include "tricore_disasm.h"

#include <cstdio>

namespace wiggle_dbg {

DisasmInsn tricoreDecode(uint32_t addr, const uint8_t* buf, uint32_t avail)
{
    DisasmInsn d;
    d.addr = addr;
    uint16_t h = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
    d.is32 = tricoreIs32(h);
    d.length = d.is32 ? 4 : 2;
    if (d.is32 && avail >= 4) {
        d.raw = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
                (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
    } else {
        d.raw = h;
        if (d.is32) d.length = 4;  // 仍标 32，但 raw 仅首半字
    }

    uint32_t op = (h >> 9) & 0x7Fu;
    char m[24];
    if (!d.is32) {
        switch (op) {
            case 0x00: snprintf(m, sizeof(m), "ret"); d.is_ret = true; break;
            case 0x06: snprintf(m, sizeof(m), "rfe"); d.is_ret = true; break;
            case 0x2E: snprintf(m, sizeof(m), "call"); d.is_call = true; break;
            case 0x5C: snprintf(m, sizeof(m), "call"); d.is_call = true; break;
            case 0x42: snprintf(m, sizeof(m), "add"); break;
            case 0x82: snprintf(m, sizeof(m), "add"); break;
            case 0x0A: snprintf(m, sizeof(m), "svlcx"); break;
            case 0x0E: snprintf(m, sizeof(m), "rslcx"); break;
            case 0x1E: snprintf(m, sizeof(m), "b"); d.is_branch = true; break;
            case 0x3E: snprintf(m, sizeof(m), "b"); d.is_branch = true; break;
            case 0x5E: snprintf(m, sizeof(m), "b"); d.is_branch = true; break;
            case 0x7E: snprintf(m, sizeof(m), "b"); d.is_branch = true; break;
            case 0xDC: snprintf(m, sizeof(m), "mov"); break;
            case 0xFC: snprintf(m, sizeof(m), "mov"); break;
            case 0x6C: snprintf(m, sizeof(m), "ld.w"); break;
            case 0x8C: snprintf(m, sizeof(m), "st.w"); break;
            case 0x0C: snprintf(m, sizeof(m), "nop"); break;
            default: snprintf(m, sizeof(m), ".hword 0x%04X", h); break;
        }
    } else {
        uint32_t op32 = (h >> 9) & 0x7Fu;
        switch (op32) {
            case 0x1D: snprintf(m, sizeof(m), "call"); d.is_call = true; break;
            case 0x0B: snprintf(m, sizeof(m), "add"); break;
            case 0x3B: snprintf(m, sizeof(m), "mov"); break;
            case 0x09: snprintf(m, sizeof(m), "ld.w"); break;
            case 0x29: snprintf(m, sizeof(m), "st.w"); break;
            case 0x05: snprintf(m, sizeof(m), "movh.a"); break;
            case 0x0D: snprintf(m, sizeof(m), "lea"); break;
            case 0x1B: snprintf(m, sizeof(m), "j"); d.is_branch = true; break;
            case 0x3D: snprintf(m, sizeof(m), "jl"); d.is_call = true; break;
            case 0x5D: snprintf(m, sizeof(m), "jla"); d.is_call = true; break;
            case 0x7D: snprintf(m, sizeof(m), "jf"); d.is_branch = true; break;
            default: snprintf(m, sizeof(m), ".word 0x%08X", d.raw); break;
        }
    }
    d.mnemonic = m;
    return d;
}

}  // namespace wiggle_dbg