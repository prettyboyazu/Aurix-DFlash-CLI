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
//  TriCore 调试寄存器/CSA 布局纯定义（无 IO），MCD 与 TAS 后端共享
//  寄存器偏移与位域经 TC3xx RegisterDefs JSON 与 TriCore Vol1 §12 三方交叉验证。
//  平台约束：TriBoard 等DBGSR.CLR_HALT 写入无效的平台，resume 返回 NotSupported，
//  由调用方明确报错，禁止静默复位掩盖能力缺失。
//********************************************************************************************************************

#ifndef WIGGLE_TRICORE_DEFS_H
#define WIGGLE_TRICORE_DEFS_H

#include <cstdint>


namespace wiggle_dbg {

// CPU SFR 基址表（SRI 总线，TC3xx Part1；索引=核号）。
// 注意：下方寄存器偏移相对 SFR 基址（含 0x10000 CSFR 段），与 SVD peripheral baseAddress 一致；
// 勿误用 CSFR 基址（SFR+0x10000）再叠加完整偏移。
constexpr uint32_t CPU_CSFR_BASE[6] = {
    0xF8800000u, 0xF8820000u, 0xF8840000u,
    0xF8860000u, 0xF8880000u, 0xF88C0000u
};
constexpr int CPU_COUNT = 6;

// 寄存器偏移（相对 SFR 基址；halt 态架构状态映射到 FPI 地址空间，Vol1 §12.4.4）
constexpr uint32_t OFF_TR0EVT   = 0x1F000u;  // 触发事件寄存器 i=0..7，间距8
constexpr uint32_t OFF_TR0ADR   = 0x1F004u;  // 触发地址寄存器
constexpr uint32_t OFF_DBGSR    = 0x1FD00u;  // 调试状态
constexpr uint32_t OFF_EXEVT    = 0x1FD08u;  // BRKIN 外部事件动作配置
constexpr uint32_t OFF_TRIG_ACC = 0x1FD30u;  // 触发激活历史（读清零）
constexpr uint32_t OFF_DBGTCR   = 0x1FD48u;  // 调试陷阱控制
constexpr uint32_t OFF_PCXI     = 0x1FE00u;  // 前上下文链接字
constexpr uint32_t OFF_PSW      = 0x1FE04u;
constexpr uint32_t OFF_PC       = 0x1FE08u;
constexpr uint32_t OFF_D0       = 0x1FF00u;  // D[0..15] 间距4
constexpr uint32_t OFF_A0       = 0x1FF80u;  // A[0..15] 间距4

// DBGSR.HALT[2:1] 读写规则（Vol1 §12.9）：读01B=halted；写11B=halt；写10B=resume
constexpr uint32_t DBGSR_HALT_MASK = 0x6u;
constexpr uint32_t DBGSR_HALTED    = 0x2u;
constexpr uint32_t DBGSR_SET_HALT  = 0x6u;
constexpr uint32_t DBGSR_CLR_HALT  = 0x4u;
constexpr uint32_t DBGSR_DE        = 0x1u;

// EXEVT.EVTA=010B：BRKIN 触发 halt（reset 后默认 Disabled，必须先配置）
constexpr uint32_t EXEVT_HALT = 0x2u;

constexpr uint32_t BP_COUNT  = 8u;  // TC3xx 硬件断点槽数
constexpr uint32_t BP_STRIDE = 8u;
// 指令断点：TYP=1(bit12, PC触发) + EVTA=010B(halt)。
// TYP 位经 TC33x/36x/37x/39x SVD 与硬件写后读回实测确认为 bit12（Vol1 表 bit26 不适用）。
constexpr uint32_t TRIEVT_INSTR_HALT = (1u << 12) | 0x2u;
constexpr uint32_t TRIEVT_EVTA_MASK  = 0x7u;

// PCXI 链接字：PCXS[19:16]=段，PCXO[15:0]=偏移；EA=(段<<28)|(偏移<<6)（Vol1 Fig13）
constexpr uint32_t PCXI_UL = (1u << 20);  // 1=upper context, 0=lower context

// CSA upper 布局 word 偏移（Vol1 Fig12）：A11(RA) 在 +0xC；lower 布局 A11 在 +0x4
constexpr uint32_t CSA_UPPER_RA_OFF = 0xCu;
constexpr uint32_t CSA_LOWER_RA_OFF = 0x4u;

constexpr uint32_t HALT_POLL_MS   = 10u;    // halt 生效等待轮询间隔 ms
constexpr uint32_t STEP_TIMEOUT_MS = 2000u; // 单步临时断点等待上限 ms
constexpr uint32_t BT_MAX_FRAMES  = 64u;    // 回溯深度上限（防 CSA 链异常）

inline uint64_t csfr(int cpu, uint32_t off)
{
    return static_cast<uint64_t>(CPU_CSFR_BASE[cpu]) + off;
}

// 链接字 → CSA 有效地址；null 链接返回 0
inline uint32_t csaAddrFromLink(uint32_t link)
{
    uint32_t seg = (link >> 16) & 0xFu;
    uint32_t off = link & 0xFFFFu;
    if (seg == 0 && off == 0) return 0;
    return (seg << 28) | (off << 6);
}

}  // namespace wiggle_dbg

#endif  // WIGGLE_TRICORE_DEFS_H

