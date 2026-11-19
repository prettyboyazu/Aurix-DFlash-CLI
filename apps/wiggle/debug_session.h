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
//  调试后端会话层：唯一调试入口，全部走 MCD（非破坏、状态持久）。
//  职责：会话生命周期 + 调试命令实现（控制/寄存器/内存/断点/观察点/复位/符号/搜索/比对）。
//  不依赖 TAS；TAS 仅承担 Flash 与连接级诊断（见 wiggle_main）。
//  命令以 nljson req/resp 交互，CLI 与 serve 共用同一实现（分工：本层=逻辑，main=解析/输出）。
//********************************************************************************************************************

#ifndef WIGGLE_DEBUG_SESSION_H
#define WIGGLE_DEBUG_SESSION_H

#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "mcd_ctrl.h"
#include "elf_symbols.h"

namespace wiggle_dbg {

using nljson = nlohmann::json;
using wiggle_mcd::RegInfo;
using wiggle_mcd::MemSpaceInfo;

struct TrigRec {
    uint32_t id = 0;
    uint32_t type = 0;
    uint64_t addr = 0;
    uint64_t range = 0;
};

class DebugSession {
public:
    // 建立/复用 MCD 会话（跨命令持久）；cpu 变化时重连
    bool ensure(int cpu, std::string& err);
    void close();
    bool isOpen() const { return open_; }

    // 原生调试命令分发；resp 含 status。返回是否为本层处理的命令
    bool dispatch(const nljson& req, nljson& resp);

    // 符号表（serve/CLI 会话内持久）
    ElfSymbolTable& symbols() { return elf_; }

    // 供 main 层 Flash/诊断命令复用的 MCD 内存 IO（需先 ensure）
    bool readMem(uint64_t a, uint8_t* b, uint32_t n, std::string& e) { return wiggle_mcd::readMem(mcd_, a, b, n, e); }
    bool writeMem(uint64_t a, const uint8_t* b, uint32_t n, std::string& e) { return wiggle_mcd::writeMem(mcd_, a, b, n, e); }

private:
    bool readU32(uint64_t addr, uint32_t& val, std::string& err);
    const ElfSymbol* symFindCode(uint32_t addr, uint32_t& off);
    std::string symAnnotate(uint32_t addr);
    bool regLookup(int cpu, const std::string& name, RegInfo& out, std::string& err);

    void cmdState(const nljson& req, nljson& resp);
    void cmdHaltGoStep(const nljson& req, nljson& resp);
    void cmdTrig(const nljson& req, nljson& resp);
    void cmdRead(const nljson& req, nljson& resp);
    void cmdWrite(const nljson& req, nljson& resp);
    void cmdPoke(const nljson& req, nljson& resp);
    void cmdReg(const nljson& req, nljson& resp);
    void cmdVar(const nljson& req, nljson& resp);
    void cmdBt(const nljson& req, nljson& resp);
    void cmdDisasm(const nljson& req, nljson& resp);
    void cmdStepOver(const nljson& req, nljson& resp);
    void cmdStepOut(const nljson& req, nljson& resp);
    void cmdSample(const nljson& req, nljson& resp);
    void cmdSym(const nljson& req, nljson& resp);
    void cmdReset(const nljson& req, nljson& resp);
    void cmdSearch(const nljson& req, nljson& resp);
    void cmdCompare(const nljson& req, nljson& resp);
    void cmdInfo(const nljson& req, nljson& resp);

    wiggle_mcd::Session mcd_;
    bool open_ = false;
    int cpu_ = 0;
    std::vector<TrigRec> trigs_;
    std::vector<RegInfo> regmap_;
    bool regmapLoaded_ = false;
    ElfSymbolTable elf_;
};

// 全局单例：CLI 一次性进程或 serve 持久进程共用
extern DebugSession g_debug;

}  // namespace wiggle_dbg

#endif  // WIGGLE_DEBUG_SESSION_H