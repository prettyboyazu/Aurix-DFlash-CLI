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

// 非破坏性 halt：stop + 轮询至 DEBUG/HALT（2s 超时）
bool stop(const Session& s, std::string& err);
// resume：run + 轮询至 RUNNING
bool run(const Session& s, std::string& err);
// 硬件单步 n 条指令 + 轮询至 DEBUG
bool step(const Session& s, uint32_t n, std::string& err);

}  // namespace wiggle_mcd

#endif  // WIGGLE_MCD_CTRL_H