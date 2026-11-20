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
//  MCD 执行控制后端实现（动态加载 mcdxdas.dll）
//********************************************************************************************************************

#include "mcd_ctrl.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace wiggle_mcd {

namespace {

struct Api {
    mcd_initialize_fn     initialize = nullptr;
    mcd_exit_fn           exit = nullptr;
    mcd_open_server_fn    open_server = nullptr;
    mcd_close_server_fn   close_server = nullptr;
    mcd_qry_servers_fn    qry_servers = nullptr;
    mcd_qry_cores_fn      qry_cores = nullptr;
    mcd_open_core_fn      open_core = nullptr;
    mcd_close_core_fn     close_core = nullptr;
    mcd_run_fn            run = nullptr;
    mcd_stop_fn           stop = nullptr;
    mcd_step_fn           step = nullptr;
    mcd_qry_state_fn      qry_state = nullptr;
    mcd_qry_error_info_fn qry_error_info = nullptr;
    mcd_execute_txlist_fn execute_txlist = nullptr;
    mcd_create_trig_fn    create_trig = nullptr;
    mcd_remove_trig_fn    remove_trig = nullptr;
    mcd_activate_trig_set_fn activate_trig_set = nullptr;
    mcd_qry_trig_state_fn qry_trig_state = nullptr;
    mcd_qry_reg_groups_fn qry_reg_groups = nullptr;
    mcd_qry_reg_map_fn    qry_reg_map = nullptr;
    mcd_qry_mem_spaces_fn qry_mem_spaces = nullptr;
    mcd_qry_rst_classes_fn qry_rst_classes = nullptr;
    mcd_qry_rst_class_info_fn qry_rst_class_info = nullptr;
    mcd_rst_fn            rst = nullptr;
};

Api g_api;
bool g_loaded = false;
bool g_initialized = false;
mcd_server_st* g_server = nullptr;

constexpr uint32_t POLL_MS = 10;
constexpr uint32_t WAIT_TIMEOUT_MS = 2000;
constexpr uint32_t QUERY_MAX_ATTEMPTS = 5;
constexpr uint32_t QUERY_RETRY_TIMEOUT_MS = 200;

// 只用于状态查询；事务读可能读清寄存器，不能套用自动重试。
template<typename Query>
mcd_return_et queryWithRetry(Query query)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(QUERY_RETRY_TIMEOUT_MS);
    mcd_return_et rc = MCD_RET_ACT_AGAIN;
    for (uint32_t attempt = 0; attempt < QUERY_MAX_ATTEMPTS; ++attempt) {
        if (attempt > 0 && std::chrono::steady_clock::now() >= deadline) break;
        rc = query();
        if (rc != MCD_RET_ACT_AGAIN || attempt + 1 == QUERY_MAX_ATTEMPTS) break;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) break;
        const auto delay = remaining < POLL_MS ? remaining : POLL_MS;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return rc;
}

void sleepMs(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string errDetail(const mcd_core_st* core, mcd_return_et rc)
{
    char buf[320];
    if (g_api.qry_error_info) {
        mcd_error_info_st ei;
        memset(&ei, 0, sizeof(ei));
        g_api.qry_error_info(core, &ei);
        snprintf(buf, sizeof(buf), "MCD rc=0x%X error=0x%X: %s",
                 (unsigned)rc, (unsigned)ei.error_code, ei.error_str);
        return buf;
    }
    snprintf(buf, sizeof(buf), "MCD rc=0x%X", (unsigned)rc);
    return buf;
}

bool loadDll(std::string& err)
{
    if (g_loaded) return true;

    std::vector<std::string> paths;
    // 独立部署优先：exe 同目录的 mcdxdas.dll（随包携带），其次 DAS 安装路径
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH)) {
        std::string ep(exePath);
        size_t slash = ep.find_last_of("\\/");
        if (slash != std::string::npos) paths.push_back(ep.substr(0, slash) + "\\mcdxdas.dll");
    }
    const char* dasHome = getenv("DAS_HOME");
    if (dasHome) paths.push_back(std::string(dasHome) + "/clients/mcdxdas.dll");
    paths.push_back("C:/Program Files/DAS64/clients/mcdxdas.dll");
    paths.push_back("C:/Infineon/DAS64/clients/mcdxdas.dll");
    paths.push_back("C:/Infineon/AURIX-Studio-1.10.32/plugins/"
                    "com.rt.hightec.tcf.agent.launcher_1.0.56/res/mcdxdas.dll");
    paths.push_back("mcdxdas.dll");

#ifdef _WIN32
    HMODULE h = nullptr;
    for (const auto& p : paths) {
        h = LoadLibraryA(p.c_str());
        if (h) break;
    }
    if (!h) { err = "mcdxdas.dll not found (install DAS or set DAS_HOME)"; return false; }
    auto sym = [&](const char* n) { return (void*)GetProcAddress(h, n); };
#else
    void* h = nullptr;
    for (const auto& p : paths) {
        h = dlopen(p.c_str(), RTLD_NOW);
        if (h) break;
    }
    if (!h) { err = "mcdxdas.so not found"; return false; }
    auto sym = [&](const char* n) { return dlsym(h, n); };
#endif

    g_api.initialize     = (mcd_initialize_fn)sym("mcd_initialize_f");
    g_api.exit           = (mcd_exit_fn)sym("mcd_exit_f");
    g_api.open_server    = (mcd_open_server_fn)sym("mcd_open_server_f");
    g_api.close_server   = (mcd_close_server_fn)sym("mcd_close_server_f");
    g_api.qry_servers    = (mcd_qry_servers_fn)sym("mcd_qry_servers_f");
    g_api.qry_cores      = (mcd_qry_cores_fn)sym("mcd_qry_cores_f");
    g_api.open_core      = (mcd_open_core_fn)sym("mcd_open_core_f");
    g_api.close_core     = (mcd_close_core_fn)sym("mcd_close_core_f");
    g_api.run            = (mcd_run_fn)sym("mcd_run_f");
    g_api.stop           = (mcd_stop_fn)sym("mcd_stop_f");
    g_api.step           = (mcd_step_fn)sym("mcd_step_f");
    g_api.qry_state      = (mcd_qry_state_fn)sym("mcd_qry_state_f");
    g_api.qry_error_info = (mcd_qry_error_info_fn)sym("mcd_qry_error_info_f");
    g_api.execute_txlist = (mcd_execute_txlist_fn)sym("mcd_execute_txlist_f");
    g_api.create_trig    = (mcd_create_trig_fn)sym("mcd_create_trig_f");
    g_api.remove_trig    = (mcd_remove_trig_fn)sym("mcd_remove_trig_f");
    g_api.activate_trig_set = (mcd_activate_trig_set_fn)sym("mcd_activate_trig_set_f");
    g_api.qry_trig_state = (mcd_qry_trig_state_fn)sym("mcd_qry_trig_state_f");
    g_api.qry_reg_groups = (mcd_qry_reg_groups_fn)sym("mcd_qry_reg_groups_f");
    g_api.qry_reg_map    = (mcd_qry_reg_map_fn)sym("mcd_qry_reg_map_f");
    g_api.qry_mem_spaces = (mcd_qry_mem_spaces_fn)sym("mcd_qry_mem_spaces_f");
    g_api.qry_rst_classes = (mcd_qry_rst_classes_fn)sym("mcd_qry_rst_classes_f");
    g_api.qry_rst_class_info = (mcd_qry_rst_class_info_fn)sym("mcd_qry_rst_class_info_f");
    g_api.rst            = (mcd_rst_fn)sym("mcd_rst_f");

    if (!g_api.initialize || !g_api.open_server || !g_api.qry_cores || !g_api.open_core ||
        !g_api.close_core || !g_api.run || !g_api.stop || !g_api.step || !g_api.qry_state) {
        err = "mcdxdas.dll missing required MCD exports";
        return false;
    }
    g_loaded = true;
    return true;
}

bool waitState(const Session& s, uint32_t want, std::string& err)
{
    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        mcd_core_state_st st;
        memset(&st, 0, sizeof(st));
        const mcd_return_et rc = queryWithRetry([&]() {
            memset(&st, 0, sizeof(st));
            return g_api.qry_state(s.core, &st);
        });
        if (rc != MCD_RET_ACT_NONE) {
            err = rc == MCD_RET_ACT_AGAIN ? "qry_state: AGAIN retry budget exhausted" :
                  "qry_state: " + errDetail(s.core, rc);
            return false;
        }
        if (st.state == MCD_CORE_STATE_UNKNOWN) {
            err = "qry_state: core state unknown";
            return false;
        }
        if (st.state == want) return true;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (ms >= WAIT_TIMEOUT_MS) {
            err = "timeout waiting for core state " + std::to_string(want);
            return false;
        }
        sleepMs(POLL_MS);
    }
}

}  // namespace

bool available(std::string& err)
{
    if (!loadDll(err)) return false;
    if (g_initialized) return true;
    mcd_api_version_st req;
    memset(&req, 0, sizeof(req));
    req.v_api_major = 1;
    req.v_api_minor = 1;
    // DAS 实现校验 author 必须为 SPRINT Release（MCD API 版本标识）
    snprintf(req.author, sizeof(req.author), "SPRINT Release");
    mcd_impl_version_info_st impl;
    memset(&impl, 0, sizeof(impl));
    mcd_return_et rc = g_api.initialize(&req, &impl);
    if (rc != MCD_RET_ACT_NONE) {
        err = errDetail(nullptr, rc);
        return false;
    }
    g_initialized = true;
    return true;
}

bool open(int cpu, Session& s, std::string& err)
{
    s.core = nullptr;
    s.cpu = cpu;
    if (!available(err)) return false;

    if (!g_server) {
        // 真实硬件需按 access HW 打开：枚举运行中服务器，取 acc_hw 名配置连接
        mcd_return_et rc = MCD_RET_ACT_HANDLE_ERROR;
        if (g_api.qry_servers) {
            uint32_t ns = 0;
            if (g_api.qry_servers("localhost", 1, 0, &ns, nullptr) == MCD_RET_ACT_NONE && ns > 0) {
                std::vector<mcd_server_info_st> si(ns);
                memset(si.data(), 0, sizeof(mcd_server_info_st) * ns);
                uint32_t got = ns;
                if (g_api.qry_servers("localhost", 1, 0, &got, si.data()) == MCD_RET_ACT_NONE) {
                    for (uint32_t i = 0; i < got && !g_server; i++) {
                        if (!si[i].acc_hw[0]) continue;
                        char cfg[256];
                        snprintf(cfg, sizeof(cfg), "McdAccHw=\"%s\"", si[i].acc_hw);
                        rc = g_api.open_server("", cfg, &g_server);
                    }
                }
            }
        }
        if (!g_server) rc = g_api.open_server("", "", &g_server);
        if (rc != MCD_RET_ACT_NONE && !g_server) {
            err = "open_server: " + errDetail(nullptr, rc);
            return false;
        }
    }

    mcd_core_con_info_st ci;
    memset(&ci, 0, sizeof(ci));
    uint32_t count = 0;
    mcd_return_et rc = g_api.qry_cores(&ci, 0, &count, nullptr);
    if (rc != MCD_RET_ACT_NONE || count == 0) {
        err = "qry_cores: " + errDetail(nullptr, rc);
        return false;
    }
    std::vector<mcd_core_con_info_st> infos(count);
    memset(infos.data(), 0, sizeof(mcd_core_con_info_st) * count);
    uint32_t got = count;
    rc = g_api.qry_cores(&ci, 0, &got, infos.data());
    if (rc != MCD_RET_ACT_NONE) {
        err = "qry_cores(list): " + errDetail(nullptr, rc);
        return false;
    }

    // 核名形如 "Core 0: CPU0 TriCore 1.6P"，按 CPU<n> 子串匹配，否则按索引
    int sel = -1;
    char needle[16];
    snprintf(needle, sizeof(needle), "CPU%d", cpu);
    for (uint32_t i = 0; i < got; i++) {
        if (strstr(infos[i].core, needle)) { sel = (int)i; break; }
    }
    if (sel < 0) {
        if (cpu < 0 || (uint32_t)cpu >= got) {
            err = "core " + std::to_string(cpu) + " not available";
            return false;
        }
        sel = cpu;
    }

    rc = g_api.open_core(&infos[sel], &s.core);
    if (rc != MCD_RET_ACT_NONE || !s.core) {
        err = "open_core: " + errDetail(nullptr, rc);
        return false;
    }
    return true;
}

void close(Session& s)
{
    if (s.core && g_api.close_core) {
        g_api.close_core(s.core);
        s.core = nullptr;
    }
    // 释放 access HW 独占权，后续 TAS 连接才能接入
    if (g_server && g_api.close_server) {
        g_api.close_server(g_server);
        g_server = nullptr;
    }
}

bool state(const Session& s, uint32_t& st, std::string& info)
{
    uint32_t trigId = 0;
    return stateFull(s, st, trigId, info);
}

bool stop(const Session& s, bool& applied, std::string& err)
{
    applied = false;
    uint32_t st = 0;
    std::string info;
    if (state(s, st, info) && (st == MCD_CORE_STATE_DEBUG || st == MCD_CORE_STATE_HALTED))
        return true;
    // 预查询失败不能阻止收回控制权，但 stop 本身只下发一次。
    mcd_return_et rc = g_api.stop(s.core, 0);
    if (rc != MCD_RET_ACT_NONE) {
        err = "stop: " + errDetail(s.core, rc);
        return false;
    }
    applied = true;
    return waitState(s, MCD_CORE_STATE_DEBUG, err);
}

bool run(const Session& s, std::string& err)
{
    // 发出即成功：watch 活跃时核可能立即再命中，等待 RUNNING 会假超时
    mcd_return_et rc = g_api.run(s.core, 0);
    if (rc != MCD_RET_ACT_NONE) {
        err = "run: " + errDetail(s.core, rc);
        return false;
    }
    return true;
}

bool stopGlobal(const Session& s, bool& applied, std::string& err)
{
    applied = false;
    mcd_return_et rc = g_api.stop(s.core, 1);
    if (rc != MCD_RET_ACT_NONE) { err = "stop(global): " + errDetail(s.core, rc); return false; }
    applied = true;
    return waitState(s, MCD_CORE_STATE_DEBUG, err);
}

bool runGlobal(const Session& s, std::string& err)
{
    mcd_return_et rc = g_api.run(s.core, 1);
    if (rc != MCD_RET_ACT_NONE) { err = "run(global): " + errDetail(s.core, rc); return false; }
    return true;
}

bool stateFull(const Session& s, uint32_t& st, uint32_t& trigId, std::string& info)
{
    st = MCD_CORE_STATE_UNKNOWN;
    trigId = 0;
    if (!s.core) { info = "MCD core unavailable"; return false; }
    mcd_core_state_st cs;
    const mcd_return_et rc = queryWithRetry([&]() {
        memset(&cs, 0, sizeof(cs));
        return g_api.qry_state(s.core, &cs);
    });
    if (rc != MCD_RET_ACT_NONE) {
        info = rc == MCD_RET_ACT_AGAIN ? "qry_state: AGAIN retry budget exhausted" :
               "qry_state: " + errDetail(s.core, rc);
        return false;
    }
    if (cs.state == MCD_CORE_STATE_UNKNOWN) {
        info = "qry_state: core state unknown";
        return false;
    }
    st = cs.state;
    trigId = cs.trig_id;
    info = cs.stop_str[0] ? cs.stop_str : cs.info_str;
    return true;
}

bool readMem(const Session& s, uint64_t addr, uint8_t* buf, uint32_t n, std::string& err)
{
    if (!s.core || !g_api.execute_txlist) { err = "MCD txlist unavailable"; return false; }
    mcd_tx_st tx;
    memset(&tx, 0, sizeof(tx));
    tx.addr.address = addr;
    tx.access_type = MCD_TX_AT_R;
    tx.access_width = 1;
    tx.data = buf;
    tx.num_bytes = n;
    mcd_txlist_st tl;
    tl.tx = &tx;
    tl.num_tx = 1;
    tl.num_tx_ok = 0;
    mcd_return_et rc = g_api.execute_txlist(s.core, &tl);
    if (rc != MCD_RET_ACT_NONE || tx.num_bytes_ok != n) {
        const std::string detail = errDetail(s.core, rc);
        char position[160];
        snprintf(position, sizeof(position),
                 "readMem addr=0x%llX length=%u bytes_ok=%u: ",
                 static_cast<unsigned long long>(addr), n, tx.num_bytes_ok);
        err = position + detail;
        return false;
    }
    return true;
}

bool writeMem(const Session& s, uint64_t addr, const uint8_t* buf, uint32_t n, std::string& err)
{
    if (!s.core || !g_api.execute_txlist) { err = "MCD txlist unavailable"; return false; }
    mcd_tx_st tx;
    memset(&tx, 0, sizeof(tx));
    tx.addr.address = addr;
    tx.access_type = MCD_TX_AT_W;
    tx.access_width = 1;
    tx.data = const_cast<uint8_t*>(buf);
    tx.num_bytes = n;
    mcd_txlist_st tl;
    tl.tx = &tx;
    tl.num_tx = 1;
    tl.num_tx_ok = 0;
    mcd_return_et rc = g_api.execute_txlist(s.core, &tl);
    if (rc != MCD_RET_ACT_NONE || tx.num_bytes_ok != n) {
        err = "writeMem: " + errDetail(s.core, rc);
        return false;
    }
    return true;
}

bool createTrig(const Session& s, uint32_t type, uint64_t addr, uint64_t range,
                uint32_t& id, std::string& err)
{
    if (!s.core || !g_api.create_trig) { err = "MCD trig unavailable"; return false; }
    mcd_trig_simple_core_st tr;
    memset(&tr, 0, sizeof(tr));
    tr.struct_size = sizeof(tr);
    tr.type = type;
    tr.action = MCD_TRIG_ACTION_DBG_DEBUG;
    tr.addr_start.address = addr;
    tr.addr_range = range;
    mcd_return_et rc = g_api.create_trig(s.core, &tr, &id);
    if (rc != MCD_RET_ACT_NONE) {
        err = "create_trig: " + errDetail(s.core, rc);
        return false;
    }
    rc = g_api.activate_trig_set(s.core);
    if (rc != MCD_RET_ACT_NONE) {
        err = "activate_trig_set: " + errDetail(s.core, rc);
        return false;
    }
    return true;
}

bool removeTrig(const Session& s, uint32_t id, std::string& err)
{
    if (!s.core || !g_api.remove_trig) { err = "MCD trig unavailable"; return false; }
    mcd_return_et rc = g_api.remove_trig(s.core, id);
    if (rc != MCD_RET_ACT_NONE) {
        err = "remove_trig: " + errDetail(s.core, rc);
        return false;
    }
    // 删除后必须重新上传触发集，否则硬件仍保留旧 watch
    rc = g_api.activate_trig_set(s.core);
    if (rc != MCD_RET_ACT_NONE) {
        err = "activate_trig_set: " + errDetail(s.core, rc);
        return false;
    }
    return true;
}

bool trigCaptured(const Session& s, uint32_t id, bool& captured, bool& valid, std::string& err)
{
    valid = false;
    if (!s.core || !g_api.qry_trig_state) { err = "MCD trig state unavailable"; return false; }
    mcd_trig_state_st ts;
    const mcd_return_et rc = queryWithRetry([&]() {
        memset(&ts, 0, sizeof(ts));
        return g_api.qry_trig_state(s.core, id, &ts);
    });
    if (rc != MCD_RET_ACT_NONE) {
        err = rc == MCD_RET_ACT_AGAIN ? "qry_trig_state: AGAIN retry budget exhausted" :
              "qry_trig_state: " + errDetail(s.core, rc);
        return false;
    }
    valid = ts.captured_valid != 0;
    if (valid) captured = ts.captured != 0;
    return true;
}

bool step(const Session& s, uint32_t n, bool& applied, std::string& err)
{
    applied = false;
    mcd_return_et rc = g_api.step(s.core, 0, MCD_CORE_STEP_TYPE_INSTR, n);
    if (rc != MCD_RET_ACT_NONE) {
        err = "step: " + errDetail(s.core, rc);
        return false;
    }
    applied = true;
    return waitState(s, MCD_CORE_STATE_DEBUG, err);
}

bool regMap(const Session& s, std::vector<RegInfo>& out, std::string& err)
{
    out.clear();
    if (!s.core || !g_api.qry_reg_groups || !g_api.qry_reg_map) {
        err = "MCD reg map unavailable";
        return false;
    }
    uint32_t ng = 0;
    mcd_return_et rc = g_api.qry_reg_groups(s.core, 0, &ng, nullptr);
    if (rc != MCD_RET_ACT_NONE) {
        err = "qry_reg_groups: " + errDetail(s.core, rc);
        return false;
    }
    std::vector<mcd_register_group_st> groups(ng);
    uint32_t got = ng;
    rc = g_api.qry_reg_groups(s.core, 0, &got, groups.data());
    if (rc != MCD_RET_ACT_NONE) {
        err = "qry_reg_groups(list): " + errDetail(s.core, rc);
        return false;
    }
    for (uint32_t i = 0; i < got; i++) {
        uint32_t nr = 0;
        rc = g_api.qry_reg_map(s.core, groups[i].reg_group_id, 0, &nr, nullptr);
        if (rc != MCD_RET_ACT_NONE) continue;
        std::vector<mcd_register_info_st> regs(nr);
        uint32_t rg = nr;
        rc = g_api.qry_reg_map(s.core, groups[i].reg_group_id, 0, &rg, regs.data());
        if (rc != MCD_RET_ACT_NONE) continue;
        for (uint32_t k = 0; k < rg; k++) {
            RegInfo e;
            e.name = regs[k].regname;
            e.addr = regs[k].addr.address;
            e.space = regs[k].addr.mem_space_id;
            e.bits = regs[k].regsize;
            out.push_back(std::move(e));
        }
    }
    return true;
}

bool readRegVal(const Session& s, const RegInfo& r, uint64_t& val, std::string& err)
{
    uint32_t bytes = (r.bits + 7) / 8;
    if (bytes == 0 || bytes > 8) { err = "unsupported register width"; return false; }
    uint8_t buf[8] = {0};
    mcd_tx_st tx;
    memset(&tx, 0, sizeof(tx));
    tx.addr.address = r.addr;
    tx.addr.mem_space_id = r.space;
    tx.access_type = MCD_TX_AT_R;
    tx.access_width = 1;
    tx.data = buf;
    tx.num_bytes = bytes;
    mcd_txlist_st tl;
    tl.tx = &tx;
    tl.num_tx = 1;
    tl.num_tx_ok = 0;
    mcd_return_et rc = g_api.execute_txlist(s.core, &tl);
    if (rc != MCD_RET_ACT_NONE || tx.num_bytes_ok != bytes) {
        err = "readReg: " + errDetail(s.core, rc);
        return false;
    }
    val = 0;
    for (uint32_t i = 0; i < bytes; i++) val |= static_cast<uint64_t>(buf[i]) << (8 * i);
    return true;
}

bool writeRegVal(const Session& s, const RegInfo& r, uint64_t val, std::string& err)
{
    uint32_t bytes = (r.bits + 7) / 8;
    if (bytes == 0 || bytes > 8) { err = "unsupported register width"; return false; }
    uint8_t buf[8];
    for (uint32_t i = 0; i < bytes; i++) buf[i] = static_cast<uint8_t>(val >> (8 * i));
    mcd_tx_st tx;
    memset(&tx, 0, sizeof(tx));
    tx.addr.address = r.addr;
    tx.addr.mem_space_id = r.space;
    tx.access_type = MCD_TX_AT_W;
    tx.access_width = 1;
    tx.data = buf;
    tx.num_bytes = bytes;
    mcd_txlist_st tl;
    tl.tx = &tx;
    tl.num_tx = 1;
    tl.num_tx_ok = 0;
    mcd_return_et rc = g_api.execute_txlist(s.core, &tl);
    if (rc != MCD_RET_ACT_NONE || tx.num_bytes_ok != bytes) {
        err = "writeReg: " + errDetail(s.core, rc);
        return false;
    }
    return true;
}

bool reset(const Session& s, uint32_t classBit, bool andHalt, std::string& err)
{
    if (!s.core || !g_api.rst) { err = "MCD reset unavailable"; return false; }
    mcd_return_et rc = g_api.rst(s.core, 1u << classBit, andHalt ? 1 : 0);
    if (rc != MCD_RET_ACT_NONE) {
        err = "rst: " + errDetail(s.core, rc);
        return false;
    }
    return true;
}

bool rstClasses(const Session& s, std::vector<std::pair<uint32_t, std::string>>& out, std::string& err)
{
    out.clear();
    if (!s.core || !g_api.qry_rst_classes) { err = "MCD rst classes unavailable"; return false; }
    uint32_t vec = 0;
    mcd_return_et rc = g_api.qry_rst_classes(s.core, &vec);
    if (rc != MCD_RET_ACT_NONE) {
        err = "qry_rst_classes: " + errDetail(s.core, rc);
        return false;
    }
    for (uint32_t b = 0; b < 32; b++) {
        if (!(vec & (1u << b))) continue;
        mcd_rst_info_st info;
        memset(&info, 0, sizeof(info));
        if (g_api.qry_rst_class_info &&
            g_api.qry_rst_class_info(s.core, static_cast<uint8_t>(b), &info) == MCD_RET_ACT_NONE)
            out.emplace_back(b, info.info_str);
        else
            out.emplace_back(b, "");
    }
    return true;
}

bool memSpaces(const Session& s, std::vector<MemSpaceInfo>& out, std::string& err)
{
    out.clear();
    if (!s.core || !g_api.qry_mem_spaces) { err = "MCD mem spaces unavailable"; return false; }
    uint32_t ns = 0;
    mcd_return_et rc = g_api.qry_mem_spaces(s.core, 0, &ns, nullptr);
    if (rc != MCD_RET_ACT_NONE) {
        err = "qry_mem_spaces: " + errDetail(s.core, rc);
        return false;
    }
    std::vector<mcd_memspace_st> sp(ns);
    uint32_t got = ns;
    rc = g_api.qry_mem_spaces(s.core, 0, &got, sp.data());
    if (rc != MCD_RET_ACT_NONE) {
        err = "qry_mem_spaces(list): " + errDetail(s.core, rc);
        return false;
    }
    for (uint32_t i = 0; i < got; i++) {
        MemSpaceInfo e;
        e.id = sp[i].mem_space_id;
        e.name = sp[i].mem_space_name;
        e.min = sp[i].min_addr;
        e.max = sp[i].max_addr;
        out.push_back(std::move(e));
    }
    return true;
}

}  // namespace wiggle_mcd
