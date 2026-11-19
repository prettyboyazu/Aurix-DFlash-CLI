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
//  MCD 执行控制后端：动态加载 mcdxdas.dll（DAS 安装目录），提供非破坏性 stop/run/step。
//  DLL 缺失时 available()=false，调用方降级到 TAS 寄存器探测路径，保持 exe 零依赖。
//********************************************************************************************************************

#ifndef WIGGLE_MCD_CTRL_H
#define WIGGLE_MCD_CTRL_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "mcd_abi.h"

namespace wiggle_mcd {

struct Session {
    mcd_core_st* core = nullptr;
    int cpu = 0;
};

// 加载 DLL + 解析符号 + mcd_initialize；err 输出失败原因
bool available(std::string& err);

// 打开服务器连接并按核号打开 core；失败 err 说明
bool open(int cpu, Session& s, std::string& err);
void close(Session& s);

// 查询核状态（MCD_CORE_STATE_*）；info 为状态描述串
bool state(const Session& s, uint32_t& st, std::string& info);
// 含触发 ID 的完整状态（watch/break 命中归因）
bool stateFull(const Session& s, uint32_t& st, uint32_t& trigId, std::string& info);

// 非破坏性 halt：stop + 轮询至 DEBUG/HALT（2s 超时）
bool stop(const Session& s, std::string& err);
// resume：run 发出即成功（watch 活跃时可能立即再命中，属正常调试行为）
bool run(const Session& s, std::string& err);
// 硬件单步 n 条指令 + 轮询至 DEBUG
bool step(const Session& s, uint32_t n, std::string& err);

// 内存/寄存器读写（txlist，默认地址空间=设备内存）
bool readMem(const Session& s, uint64_t addr, uint8_t* buf, uint32_t n, std::string& err);
bool writeMem(const Session& s, uint64_t addr, const uint8_t* buf, uint32_t n, std::string& err);

// 触发器：type=MCD_TRIG_TYPE_IP/READ/WRITE/RW；创建/删除后自动 activate 上传硬件
bool createTrig(const Session& s, uint32_t type, uint64_t addr, uint64_t range,
                uint32_t& id, std::string& err);
bool removeTrig(const Session& s, uint32_t id, std::string& err);
bool trigCaptured(const Session& s, uint32_t id, bool& captured, std::string& err);

// MCD 命名寄存器表（CPU Registers 组：PC/PSW/A0-A15/D0-D15/CSFR）
struct RegInfo {
    std::string name;
    uint64_t addr = 0;
    uint32_t space = 0;
    uint32_t bits = 32;
};
bool regMap(const Session& s, std::vector<RegInfo>& out, std::string& err);
bool readRegVal(const Session& s, const RegInfo& r, uint64_t& val, std::string& err);
bool writeRegVal(const Session& s, const RegInfo& r, uint64_t val, std::string& err);

// MCD 复位：classBit 0=Power-On 1=System 2=Application；rst_and_halt 可选
bool reset(const Session& s, uint32_t classBit, bool andHalt, std::string& err);
bool rstClasses(const Session& s, std::vector<std::pair<uint32_t, std::string>>& out, std::string& err);

// 地址空间清单（实测 TC3xx 单一 "Code and Data" 空间）
struct MemSpaceInfo {
    uint32_t id = 0;
    std::string name;
    uint64_t min = 0;
    uint64_t max = 0;
};
bool memSpaces(const Session& s, std::vector<MemSpaceInfo>& out, std::string& err);

}  // namespace wiggle_mcd

#endif  // WIGGLE_MCD_CTRL_H