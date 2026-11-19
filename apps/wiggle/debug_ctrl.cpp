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
//  wiggle 通用调试器原语层实现（寄存器事实见 debug_ctrl.h 注释）
//********************************************************************************************************************

#include "debug_ctrl.h"

#include "tas_utils_client.h"  // tasutil_userpins_set_high/low
#include "tas_am15_am14.h"     // TAS_UP_BRKIN

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace wiggle_dbg {

static void sleepMs(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

uint32_t csaAddrFromLink(uint32_t link)
{
    uint32_t seg = (link >> 16) & 0xFu;
    uint32_t off = link & 0xFFFFu;
    if (seg == 0 && off == 0) return 0;  // null 链接
    return (seg << 28) | (off << 6);
}

bool isHalted(CTasClientRw& client, int cpu, uint32_t& dbgsr)
{
    if (client.read32(csfr(cpu, OFF_DBGSR), &dbgsr) != TAS_ERR_NONE) return false;
    return (dbgsr & DBGSR_HALT_MASK) == DBGSR_HALTED;
}

HaltResult haltCpu(CTasClientRw& client, int cpu, std::string& diag)
{
    uint32_t dbgsr = 0;
    if (client.read32(csfr(cpu, OFF_DBGSR), &dbgsr) != TAS_ERR_NONE) {
        diag = "DBGSR read failed (CPU SFR space inaccessible)";
        return HaltResult::Failed;
    }
    if ((dbgsr & DBGSR_HALT_MASK) == DBGSR_HALTED) return HaltResult::AlreadyHalted;

    // Phase 1: BRKIN 硬件断入（需物理连线；先配置 EXEVT，reset 后默认 Disabled）
    if (client.write32(csfr(cpu, OFF_EXEVT), EXEVT_HALT) == TAS_ERR_NONE &&
        tasutil_userpins_set_high(&client, TAS_UP_BRKIN) == TAS_ERR_NONE) {
        sleepMs(HALT_POLL_MS);
        if (isHalted(client, cpu, dbgsr)) {
            tasutil_userpins_set_low(&client, TAS_UP_BRKIN);  // 释放信号线，halt 已锁存
            return HaltResult::HaltedBrkin;
        }
        tasutil_userpins_set_low(&client, TAS_UP_BRKIN);
    }

    // Phase 2: DBGSR.HALT=11B 软件 halt（不复位）
    if (client.write32(csfr(cpu, OFF_DBGSR), DBGSR_SET_HALT) == TAS_ERR_NONE) {
        sleepMs(HALT_POLL_MS);
        if (isHalted(client, cpu, dbgsr)) return HaltResult::HaltedDbgsr;
    }

    // Phase 3: 降级 RESET_AND_HALT（丢失运行上下文，必须警告）
    diag = "BRKIN/DBGSR halt unavailable, fell back to reset-and-halt (running context lost)";
    if (client.device_connect(TAS_CLNT_DCO_RESET_AND_HALT) != TAS_ERR_NONE) {
        diag = "reset-and-halt fallback failed: " + std::string(client.get_error_info());
        return HaltResult::Failed;
    }
    if (isHalted(client, cpu, dbgsr)) return HaltResult::HaltedReset;
    diag = "CPU not halted after reset-and-halt";
    return HaltResult::Failed;
}

ResumeResult resumeCpu(CTasClientRw& client, int cpu, std::string& diag)
{
    uint32_t dbgsr = 0;
    if (client.read32(csfr(cpu, OFF_DBGSR), &dbgsr) != TAS_ERR_NONE) {
        diag = "DBGSR read failed";
        return ResumeResult::Failed;
    }
    if ((dbgsr & DBGSR_HALT_MASK) != DBGSR_HALTED) return ResumeResult::AlreadyRunning;

    tasutil_userpins_set_low(&client, TAS_UP_BRKIN);  // 无 BRKIN 连线时忽略错误
    if (client.write32(csfr(cpu, OFF_DBGSR), DBGSR_CLR_HALT) != TAS_ERR_NONE) {
        diag = "DBGSR write failed";
        return ResumeResult::Failed;
    }
    sleepMs(HALT_POLL_MS);
    if (client.read32(csfr(cpu, OFF_DBGSR), &dbgsr) != TAS_ERR_NONE) {
        diag = "DBGSR readback failed";
        return ResumeResult::Failed;
    }
    if ((dbgsr & DBGSR_HALT_MASK) != DBGSR_HALTED) return ResumeResult::Running;

    // 已知平台限制（TriBoard TC3XX V2.0 + TasServer V2.0 实测）：CLR_HALT 写入无效
    diag = "DBGSR.CLR_HALT write ineffective on this platform - non-destructive resume "
           "not supported; use 'wiggle reset' to restart execution";
    return ResumeResult::NotSupported;
}

bool readCoreRegs(CTasClientRw& client, int cpu, CoreRegs& out)
{
    if (client.read32(csfr(cpu, OFF_PC), &out.pc) != TAS_ERR_NONE) return false;
    if (client.read32(csfr(cpu, OFF_PSW), &out.psw) != TAS_ERR_NONE) return false;
    if (client.read32(csfr(cpu, OFF_PCXI), &out.pcxi) != TAS_ERR_NONE) return false;
    for (uint32_t i = 0; i < 16; i++) {
        if (client.read32(csfr(cpu, OFF_A0 + i * 4), &out.a[i]) != TAS_ERR_NONE) return false;
        if (client.read32(csfr(cpu, OFF_D0 + i * 4), &out.d[i]) != TAS_ERR_NONE) return false;
    }
    return true;
}

bool bpReadAll(CTasClientRw& client, int cpu, BpSlot slots[BP_COUNT])
{
    for (uint32_t i = 0; i < BP_COUNT; i++) {
        if (client.read32(csfr(cpu, OFF_TR0EVT + i * BP_STRIDE), &slots[i].evt) != TAS_ERR_NONE)
            return false;
        if (client.read32(csfr(cpu, OFF_TR0ADR + i * BP_STRIDE), &slots[i].adr) != TAS_ERR_NONE)
            return false;
    }
    return true;
}

int bpAdd(CTasClientRw& client, int cpu, uint32_t addr)
{
    BpSlot slots[BP_COUNT];
    if (!bpReadAll(client, cpu, slots)) return -1;
    for (uint32_t i = 0; i < BP_COUNT; i++) {
        if ((slots[i].evt & TRIEVT_EVTA_MASK) != 0) continue;  // 槽位占用
        if (client.write32(csfr(cpu, OFF_TR0ADR + i * BP_STRIDE), addr) != TAS_ERR_NONE)
            return -1;
        if (client.write32(csfr(cpu, OFF_TR0EVT + i * BP_STRIDE), TRIEVT_INSTR_HALT) != TAS_ERR_NONE)
            return -1;
        uint32_t rb = 0;
        if (client.read32(csfr(cpu, OFF_TR0EVT + i * BP_STRIDE), &rb) != TAS_ERR_NONE ||
            rb != TRIEVT_INSTR_HALT)
            return -1;  // 写后读回校验
        return static_cast<int>(i);
    }
    return -1;  // 8 槽全满
}

bool bpDel(CTasClientRw& client, int cpu, int slot)
{
    if (slot < 0 || slot >= static_cast<int>(BP_COUNT)) return false;
    uint32_t off = static_cast<uint32_t>(slot) * BP_STRIDE;
    if (client.write32(csfr(cpu, OFF_TR0EVT + off), 0) != TAS_ERR_NONE) return false;
    return client.write32(csfr(cpu, OFF_TR0ADR + off), 0) == TAS_ERR_NONE;
}

void bpClear(CTasClientRw& client, int cpu)
{
    for (uint32_t i = 0; i < BP_COUNT; i++) bpDel(client, cpu, static_cast<int>(i));
}

bool stepOnce(CTasClientRw& client, int cpu, uint32_t& newPc, std::string& diag)
{
    uint32_t dbgsr = 0;
    if (!isHalted(client, cpu, dbgsr)) {
        diag = "CPU must be halted before step";
        return false;
    }
    uint32_t pc = 0;
    if (client.read32(csfr(cpu, OFF_PC), &pc) != TAS_ERR_NONE) {
        diag = "PC read failed";
        return false;
    }

    // 不译码指令长度：PC+2/PC+4 双临时断点覆盖 16/32 位两种情况
    int s2 = bpAdd(client, cpu, pc + 2);
    int s4 = (s2 >= 0) ? bpAdd(client, cpu, pc + 4) : -1;
    if (s2 < 0 || s4 < 0) {
        if (s2 >= 0) bpDel(client, cpu, s2);
        diag = "need 2 free breakpoint slots for step";
        return false;
    }

    std::string rdiag;
    ResumeResult rr = resumeCpu(client, cpu, rdiag);
    if (rr != ResumeResult::Running) {
        bpDel(client, cpu, s2);
        bpDel(client, cpu, s4);
        diag = "step requires resume: " + rdiag;
        newPc = pc;
        return false;
    }

    auto t0 = std::chrono::steady_clock::now();
    bool halted = false;
    while (true) {
        if (isHalted(client, cpu, dbgsr)) { halted = true; break; }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (ms >= STEP_TIMEOUT_MS) break;
        sleepMs(HALT_POLL_MS);
    }

    bpDel(client, cpu, s2);
    bpDel(client, cpu, s4);

    if (!halted) {
        // 分支跳走等场景：临时断点未命中，强制 halt 收回控制权
        std::string hdiag;
        haltCpu(client, cpu, hdiag);
        diag = "step did not hit next-instruction breakpoint within timeout "
               "(branch taken?); CPU re-halted";
    }
    if (client.read32(csfr(cpu, OFF_PC), &newPc) != TAS_ERR_NONE) {
        diag = "PC read failed after step";
        return false;
    }
    return halted;
}

bool backtrace(CTasClientRw& client, int cpu, std::vector<BtFrame>& frames)
{
    frames.clear();
    CoreRegs regs;
    if (!readCoreRegs(client, cpu, regs)) return false;

    frames.push_back({regs.pc, 0, true});
    uint32_t ra = regs.a[11];      // 当前函数返回地址 → 调用者帧 PC
    uint32_t link = regs.pcxi;     // 指向调用者已保存上下文 CSA
    std::vector<uint32_t> visited;

    while (frames.size() < BT_MAX_FRAMES) {
        uint32_t ea = csaAddrFromLink(link);
        if (ea == 0) break;  // 链尾 null
        if (std::find(visited.begin(), visited.end(), ea) != visited.end()) break;  // 循环检测
        visited.push_back(ea);

        uint32_t nextLink = 0, nextRa = 0;
        if (client.read32(ea, &nextLink) != TAS_ERR_NONE) break;
        bool upper = (link & PCXI_UL) != 0;
        uint32_t raOff = upper ? CSA_UPPER_RA_OFF : CSA_LOWER_RA_OFF;
        if (client.read32(ea + raOff, &nextRa) != TAS_ERR_NONE) break;

        frames.push_back({ra, ea, upper});
        ra = nextRa;
        link = nextLink;
    }
    return true;
}

}  // namespace wiggle_dbg