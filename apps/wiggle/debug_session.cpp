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
//  调试后端会话层实现（全 MCD）
//********************************************************************************************************************

#include "debug_session.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>

#include "tricore_defs.h"
#include "hex_parser.h"
#include "tricore_disasm.h"

namespace wiggle_dbg {

DebugSession g_debug;

// 地址视图归一（TC2xx/TC3xx 实战）：DSPR 0xD0 别名→0x70；PFlash cached 0x80→物理 0xA0。
// 仅用于数据读写/反汇编；IP 断点地址保持执行视图（PC 为 cached），不归一。
static uint64_t normData(uint64_t a)
{
    if ((a & 0xF0000000u) == 0xD0000000u) return a - 0x60000000u;
    if ((a & 0xF0000000u) == 0x80000000u) return a + 0x20000000u;
    return a;
}
static uint64_t normRam(uint64_t a)
{
    if ((a & 0xF0000000u) == 0xD0000000u) return a - 0x60000000u;
    return a;
}

static void sleepMs(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool DebugSession::ensure(int cpu, std::string& err)
{
    if (open_ && cpu_ == cpu) return true;
    close();
    if (!wiggle_mcd::open(cpu, mcd_, err)) return false;
    open_ = true;
    cpu_ = cpu;
    regmapLoaded_ = false;
    return true;
}

void DebugSession::close()
{
    if (open_) {
        wiggle_mcd::close(mcd_);
        open_ = false;
        trigs_.clear();
        regmapLoaded_ = false;
    }
}

bool DebugSession::readU32(uint64_t addr, uint32_t& val, std::string& err)
{
    uint8_t b[4];
    if (!wiggle_mcd::readMem(mcd_, addr, b, 4, err)) return false;
    val = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
          (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
    return true;
}

const ElfSymbol* DebugSession::symFindCode(uint32_t addr, uint32_t& off)
{
    if (!elf_.isLoaded()) return nullptr;
    const ElfSymbol* s = elf_.findByAddr(addr, off);
    if (s && s->size > 0 && off < s->size) return s;
    // PFlash 物理 0xAx 与 TASKING cached 0x8x 别名
    if (addr >= 0xA0000000u && addr < 0xB0000000u) {
        uint32_t o2 = 0;
        const ElfSymbol* s2 = elf_.findByAddr(addr - 0x20000000u, o2);
        if (s2 && s2->size > 0 && o2 < s2->size) { off = o2; return s2; }
    }
    return nullptr;
}

std::string DebugSession::symAnnotate(uint32_t addr)
{
    uint32_t off = 0;
    const ElfSymbol* s = symFindCode(addr, off);
    if (!s) return "";
    char buf[160];
    snprintf(buf, sizeof(buf), " <%s+0x%X>", s->name.c_str(), off);
    return std::string(buf);
}

bool DebugSession::regLookup(int cpu, const std::string& name, RegInfo& out, std::string& err)
{
    if (!regmapLoaded_) {
        if (!wiggle_mcd::regMap(mcd_, regmap_, err)) return false;
        regmapLoaded_ = true;
    }
    std::string up = name;
    for (auto& ch : up) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    for (const auto& r : regmap_) {
        std::string rn = r.name;
        for (auto& ch : rn) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        if (rn == up) { out = r; return true; }
    }
    // 回退：CSFR 地址表（MCD 寄存器表未列出的核寄存器）
    uint64_t addr = 0;
    if (up == "PC") addr = csfr(cpu, OFF_PC);
    else if (up == "PSW") addr = csfr(cpu, OFF_PSW);
    else if (up == "PCXI") addr = csfr(cpu, OFF_PCXI);
    else if (up.size() >= 2 && (up[0] == 'A' || up[0] == 'D')) {
        for (size_t k = 1; k < up.size(); k++)
            if (!isdigit(static_cast<unsigned char>(up[k]))) return false;
        int idx = atoi(up.c_str() + 1);
        if (idx < 0 || idx > 15) return false;
        addr = csfr(cpu, (up[0] == 'A' ? OFF_A0 : OFF_D0) + static_cast<uint32_t>(idx) * 4);
    } else return false;
    out.name = up;
    out.addr = addr;
    out.space = 0;
    out.bits = 32;
    return true;
}

void DebugSession::cmdState(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint32_t st = 0, tid = 0;
    std::string info;
    wiggle_mcd::stateFull(mcd_, st, tid, info);
    uint32_t pc = 0;
    readU32(csfr(cpu, OFF_PC), pc, err);
    resp["status"] = "ok";
    resp["state"] = st;
    resp["trig_id"] = tid;
    resp["pc"] = pc;
    std::string sym = symAnnotate(pc);
    if (!sym.empty()) resp["pc_symbol"] = sym;
    if (!info.empty()) resp["info"] = info;
}

void DebugSession::cmdHaltGoStep(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    std::string cmd = req.value("cmd", "");
    bool global = req.value("global", false);
    bool ok = true;
    if (cmd == "halt") ok = global ? wiggle_mcd::stopGlobal(mcd_, err) : wiggle_mcd::stop(mcd_, err);
    else if (cmd == "go") ok = global ? wiggle_mcd::runGlobal(mcd_, err) : wiggle_mcd::run(mcd_, err);
    else if (cmd == "step") ok = wiggle_mcd::step(mcd_, req.value("n", 1), err);
    uint32_t st = 0, tid = 0;
    std::string info;
    wiggle_mcd::stateFull(mcd_, st, tid, info);
    uint32_t pc = 0;
    readU32(csfr(cpu, OFF_PC), pc, err);
    resp["status"] = ok ? "ok" : "error";
    if (!ok) resp["message"] = err;
    resp["method"] = "mcd";
    resp["state"] = st;
    resp["trig_id"] = tid;
    resp["pc"] = pc;
    std::string sym = symAnnotate(pc);
    if (!sym.empty()) resp["pc_symbol"] = sym;
}

void DebugSession::cmdTrig(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    std::string cmd = req.value("cmd", "");
    std::string sub = req.value("action", "list");
    uint32_t type = (cmd == "break") ? MCD_TRIG_TYPE_IP : MCD_TRIG_TYPE_WRITE;
    if (cmd == "watch") {
        std::string wt = req.value("type", "write");
        if (wt == "read") type = MCD_TRIG_TYPE_READ;
        else if (wt == "rw") type = MCD_TRIG_TYPE_RW;
    }
    if (sub == "add") {
        uint64_t addr = req.value("addr", 0ULL);
        uint64_t range = req.value("range", 0ULL);
        if (req.contains("sym")) {
            const ElfSymbol* s = elf_.findByName(req.value("sym", ""));
            if (!s) { resp["status"] = "error"; resp["message"] = "symbol not found: " + req.value("sym", ""); return; }
            addr = s->value;
            range = s->size > 0 ? s->size - 1 : 0;
        }
        if (cmd == "watch") addr = normRam(addr);
        uint32_t id = 0;
        if (wiggle_mcd::createTrig(mcd_, type, addr, range, id, err)) {
            trigs_.push_back({id, type, addr, range});
            resp["status"] = "ok";
            resp["id_trig"] = id;
            resp["addr"] = addr;
        } else { resp["status"] = "error"; resp["message"] = err; }
    } else if (sub == "del" || sub == "clear") {
        bool ok = true;
        if (sub == "clear") {
            for (const auto& t : trigs_) ok = ok && wiggle_mcd::removeTrig(mcd_, t.id, err);
            trigs_.clear();
        } else {
            uint32_t id = req.value("id_trig", 0);
            uint64_t addr = req.value("addr", 0ULL);
            auto it = trigs_.end();
            for (auto i = trigs_.begin(); i != trigs_.end(); ++i)
                if ((id && i->id == id) || (!id && i->addr == addr)) { it = i; break; }
            if (it == trigs_.end()) { ok = false; err = "trigger not found"; }
            else { ok = wiggle_mcd::removeTrig(mcd_, it->id, err); trigs_.erase(it); }
        }
        resp["status"] = ok ? "ok" : "error";
        if (!ok) resp["message"] = err;
    } else {
        resp["status"] = "ok";
        resp["trigs"] = nljson::array();
        for (const auto& t : trigs_) {
            nljson e;
            e["id_trig"] = t.id;
            e["kind"] = (t.type == MCD_TRIG_TYPE_IP) ? "break" : "watch";
            e["type"] = t.type;
            e["addr"] = t.addr;
            e["range"] = t.range;
            bool cap = false;
            wiggle_mcd::trigCaptured(mcd_, t.id, cap, err);
            e["captured"] = cap;
            resp["trigs"].push_back(e);
        }
    }
}

void DebugSession::cmdRead(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint64_t addr = normData(req.value("addr", 0ULL));
    uint32_t len = req.value("len", 0);
    if (len == 0 || len > 0x100000) {
        resp["status"] = "error"; resp["message"] = "len must be 1..100000"; return;
    }
    std::vector<uint8_t> buf(len);
    bool ok = true;
    for (uint32_t off = 0; off < len && ok; off += 4096) {
        uint32_t chunk = std::min<uint32_t>(4096, len - off);
        ok = wiggle_mcd::readMem(mcd_, addr + off, buf.data() + off, chunk, err);
    }
    if (!ok) { resp["status"] = "error"; resp["message"] = err; return; }
    std::string hex;
    hex.reserve(len * 2);
    for (auto v : buf) { char h[3]; snprintf(h, 3, "%02X", v); hex += h; }
    resp["status"] = "ok";
    resp["addr"] = addr;
    resp["data"] = hex;
}

void DebugSession::cmdWrite(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint64_t addr = normData(req.value("addr", 0ULL));
    std::string hex = req.value("data", "");
    if (hex.size() % 2 != 0) { resp["status"] = "error"; resp["message"] = "even hex length required"; return; }
    std::vector<uint8_t> buf(hex.size() / 2);
    for (size_t i = 0; i < buf.size(); i++)
        buf[i] = static_cast<uint8_t>(strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16));
    if (wiggle_mcd::writeMem(mcd_, addr, buf.data(), static_cast<uint32_t>(buf.size()), err)) {
        resp["status"] = "ok";
        resp["bytes"] = buf.size();
    } else { resp["status"] = "error"; resp["message"] = err; }
}

void DebugSession::cmdPoke(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint64_t addr = req.value("addr", 0ULL);
    uint32_t val = req.value("value", 0);
    uint8_t b[4] = {static_cast<uint8_t>(val), static_cast<uint8_t>(val >> 8),
                    static_cast<uint8_t>(val >> 16), static_cast<uint8_t>(val >> 24)};
    if (!wiggle_mcd::writeMem(mcd_, addr, b, 4, err)) {
        resp["status"] = "error"; resp["message"] = err; return;
    }
    uint32_t rb = 0;
    readU32(addr, rb, err);
    resp["status"] = "ok";
    resp["addr"] = addr;
    resp["value"] = val;
    resp["readback"] = rb;
    resp["match"] = (rb == val);
}

void DebugSession::cmdReg(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    std::string name = req.value("name", "");
    if (name.empty()) {
        // 全量核寄存器
        resp["status"] = "ok";
        uint32_t pc = 0, psw = 0, pcxi = 0;
        readU32(csfr(cpu, OFF_PC), pc, err);
        readU32(csfr(cpu, OFF_PSW), psw, err);
        readU32(csfr(cpu, OFF_PCXI), pcxi, err);
        resp["pc"] = pc; resp["psw"] = psw; resp["pcxi"] = pcxi;
        resp["a"] = nljson::array(); resp["d"] = nljson::array();
        for (int i = 0; i < 16; i++) {
            uint32_t a = 0, d = 0;
            readU32(csfr(cpu, OFF_A0 + i * 4), a, err);
            readU32(csfr(cpu, OFF_D0 + i * 4), d, err);
            resp["a"].push_back(a); resp["d"].push_back(d);
        }
        std::string sym = symAnnotate(pc);
        if (!sym.empty()) resp["pc_symbol"] = sym;
        return;
    }
    RegInfo r;
    if (!regLookup(cpu, name, r, err)) {
        resp["status"] = "error"; resp["message"] = "invalid register name: " + name; return;
    }
    if (req.contains("value")) {
        uint64_t val = req.value("value", 0ULL);
        if (wiggle_mcd::writeRegVal(mcd_, r, val, err)) {
            resp["status"] = "ok"; resp["register"] = name; resp["value"] = val;
        } else { resp["status"] = "error"; resp["message"] = err; }
    } else {
        uint64_t val = 0;
        if (wiggle_mcd::readRegVal(mcd_, r, val, err)) {
            resp["status"] = "ok"; resp["register"] = name; resp["value"] = val;
        } else { resp["status"] = "error"; resp["message"] = err; }
    }
}

void DebugSession::cmdVar(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    std::string name = req.value("name", "");
    const ElfSymbol* s = elf_.findByName(name);
    if (!s) { resp["status"] = "error"; resp["message"] = "symbol not found: " + name; return; }
    uint64_t addr = normData(s->value);
    uint32_t size = s->size ? s->size : 4;
    resp["name"] = name;
    resp["addr"] = addr;
    resp["size"] = size;
    std::string action = req.value("action", "read");
    if (action == "write") {
        uint64_t val = req.value("value", 0ULL);
        uint32_t n = std::min<uint32_t>(size, 8);
        uint8_t b[8];
        for (uint32_t i = 0; i < n; i++) b[i] = static_cast<uint8_t>(val >> (8 * i));
        if (wiggle_mcd::writeMem(mcd_, addr, b, n, err)) {
            resp["status"] = "ok"; resp["value"] = val;
        } else { resp["status"] = "error"; resp["message"] = err; }
    } else {
        std::vector<uint8_t> buf(size);
        if (!wiggle_mcd::readMem(mcd_, addr, buf.data(), size, err)) {
            resp["status"] = "error"; resp["message"] = err; return;
        }
        resp["status"] = "ok";
        if (size <= 8) {
            uint64_t v = 0;
            for (uint32_t i = 0; i < size; i++) v |= static_cast<uint64_t>(buf[i]) << (8 * i);
            resp["value"] = v;
        }
        std::string hex;
        for (auto v : buf) { char h[3]; snprintf(h, 3, "%02X", v); hex += h; }
        resp["data"] = hex;
    }
}

void DebugSession::cmdBt(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint32_t pc = 0, pcxi = 0, a11 = 0;
    bool ok = readU32(csfr(cpu, OFF_PC), pc, err) &&
              readU32(csfr(cpu, OFF_PCXI), pcxi, err) &&
              readU32(csfr(cpu, OFF_A0 + 11 * 4), a11, err);
    if (!ok) { resp["status"] = "error"; resp["message"] = err; return; }
    resp["status"] = "ok";
    resp["frames"] = nljson::array();
    nljson f0;
    f0["pc"] = pc;
    uint32_t o0 = 0;
    const ElfSymbol* s0 = symFindCode(pc, o0);
    if (s0) { f0["symbol"] = s0->name; f0["offset"] = o0; }
    resp["frames"].push_back(f0);
    uint32_t ra = a11, link = pcxi;
    std::vector<uint32_t> visited;
    while (resp["frames"].size() < BT_MAX_FRAMES) {
        uint32_t ea = csaAddrFromLink(link);
        if (ea == 0) break;
        if (std::find(visited.begin(), visited.end(), ea) != visited.end()) break;
        visited.push_back(ea);
        uint32_t nextLink = 0, nextRa = 0;
        if (!readU32(ea, nextLink, err)) break;
        bool upper = (link & PCXI_UL) != 0;
        if (!readU32(ea + (upper ? CSA_UPPER_RA_OFF : CSA_LOWER_RA_OFF), nextRa, err)) break;
        nljson f;
        f["pc"] = ra;
        f["csa"] = ea;
        uint32_t off = 0;
        const ElfSymbol* s = symFindCode(ra, off);
        if (s) { f["symbol"] = s->name; f["offset"] = off; }
        resp["frames"].push_back(f);
        ra = nextRa;
        link = nextLink;
    }
}

void DebugSession::cmdSym(const nljson& req, nljson& resp)
{
    std::string sub = req.value("action", "");
    std::string err;
    if (sub == "load") {
        if (elf_.load(req.value("file", ""), err)) {
            resp["status"] = "ok"; resp["symbols"] = elf_.count();
        } else { resp["status"] = "error"; resp["message"] = err; }
    } else if (!elf_.isLoaded()) {
        resp["status"] = "error"; resp["message"] = "no ELF loaded";
    } else if (sub == "addr") {
        uint32_t off = 0;
        const ElfSymbol* s = symFindCode(req.value("addr", 0), off);
        if (s) { resp["status"] = "ok"; resp["name"] = s->name; resp["offset"] = off; }
        else { resp["status"] = "error"; resp["message"] = "no symbol"; }
    } else if (sub == "name") {
        const ElfSymbol* s = elf_.findByName(req.value("name", ""));
        if (s) { resp["status"] = "ok"; resp["value"] = s->value; resp["size"] = s->size; }
        else { resp["status"] = "error"; resp["message"] = "symbol not found"; }
    } else if (sub == "list") {
        auto syms = elf_.list(req.value("filter", ""), 100);
        resp["status"] = "ok";
        resp["symbols"] = nljson::array();
        for (const auto* s : syms) {
            nljson e;
            e["name"] = s->name; e["value"] = s->value; e["size"] = s->size;
            resp["symbols"].push_back(e);
        }
    } else { resp["status"] = "error"; resp["message"] = "unknown sym action"; }
}

void DebugSession::cmdReset(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint32_t cls = req.value("class", 2);  // 默认 Application Reset
    bool halt = req.value("halt", false);
    if (!wiggle_mcd::reset(mcd_, cls, halt, err)) {
        resp["status"] = "error"; resp["message"] = err; return;
    }
    trigs_.clear();  // 复位清除硬件触发器
    sleepMs(50);
    uint32_t st = 0, tid = 0;
    std::string info;
    wiggle_mcd::stateFull(mcd_, st, tid, info);
    resp["status"] = "ok";
    resp["method"] = "mcd";
    resp["class"] = cls;
    resp["halt"] = halt;
    resp["state"] = st;
}

void DebugSession::cmdSearch(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint64_t addr = req.value("addr", 0ULL);
    uint32_t len = req.value("len", 0);
    std::string ph = req.value("pattern", "");
    if (ph.size() % 2 != 0 || ph.empty()) { resp["status"] = "error"; resp["message"] = "even hex pattern required"; return; }
    std::vector<uint8_t> pat(ph.size() / 2);
    for (size_t i = 0; i < pat.size(); i++)
        pat[i] = static_cast<uint8_t>(strtoul(ph.substr(i * 2, 2).c_str(), nullptr, 16));
    resp["matches"] = nljson::array();
    uint32_t count = 0;
    bool truncated = false;
    const uint32_t CHUNK = 65536;
    std::vector<uint8_t> buf(CHUNK + pat.size() - 1);
    for (uint32_t off = 0; off < len; off += CHUNK) {
        uint32_t want = std::min<uint32_t>(CHUNK + pat.size() - 1, len - off);
        if (!wiggle_mcd::readMem(mcd_, addr + off, buf.data(), want, err)) {
            resp["status"] = "error"; resp["message"] = err; return;
        }
        for (uint32_t i = 0; i + pat.size() <= want; i++) {
            if (off + i >= len) break;
            if (memcmp(buf.data() + i, pat.data(), pat.size()) == 0) {
                resp["matches"].push_back(addr + off + i);
                if (++count >= 1000) { truncated = true; break; }
            }
        }
        if (truncated) break;
    }
    resp["status"] = "ok";
    resp["match_count"] = count;
    resp["truncated"] = truncated;
}

void DebugSession::cmdCompare(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    std::string file = req.value("file", "");
    uint64_t base = req.value("addr", 0ULL);
    HexParseResult pr;
    if (file.size() >= 4 && file.substr(file.size() - 4) == ".hex")
        pr = parseIntelHex(file);
    else
        pr = loadBinaryFile(file, static_cast<uint32_t>(base));
    if (!pr.success) { resp["status"] = "error"; resp["message"] = pr.errorMsg; return; }
    uint32_t total = 0, mism = 0;
    resp["mismatches"] = nljson::array();
    for (const auto& seg : pr.segments) {
        std::vector<uint8_t> buf(seg.data.size());
        if (!wiggle_mcd::readMem(mcd_, seg.baseAddress, buf.data(),
                                 static_cast<uint32_t>(buf.size()), err)) {
            resp["status"] = "error"; resp["message"] = err; return;
        }
        for (size_t i = 0; i < buf.size(); i++) {
            total++;
            if (buf[i] != seg.data[i]) {
                mism++;
                if (resp["mismatches"].size() < 20) {
                    nljson m;
                    m["addr"] = seg.baseAddress + i;
                    m["file"] = seg.data[i];
                    m["flash"] = buf[i];
                    resp["mismatches"].push_back(m);
                }
            }
        }
    }
    resp["status"] = "ok";
    resp["total_bytes"] = total;
    resp["mismatch_count"] = mism;
    resp["match"] = (mism == 0);
}

void DebugSession::cmdInfo(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    std::string what = req.value("what", "spaces");
    if (what == "spaces") {
        std::vector<MemSpaceInfo> sp;
        if (!wiggle_mcd::memSpaces(mcd_, sp, err)) { resp["status"] = "error"; resp["message"] = err; return; }
        resp["status"] = "ok";
        resp["spaces"] = nljson::array();
        for (const auto& s : sp) {
            nljson e;
            e["id"] = s.id; e["name"] = s.name; e["min"] = s.min; e["max"] = s.max;
            resp["spaces"].push_back(e);
        }
    } else if (what == "regs") {
        if (!regmapLoaded_) {
            if (!wiggle_mcd::regMap(mcd_, regmap_, err)) { resp["status"] = "error"; resp["message"] = err; return; }
            regmapLoaded_ = true;
        }
        resp["status"] = "ok";
        resp["regs"] = nljson::array();
        for (const auto& r : regmap_) {
            nljson e;
            e["name"] = r.name; e["addr"] = r.addr; e["bits"] = r.bits;
            resp["regs"].push_back(e);
        }
    } else if (what == "rst") {
        std::vector<std::pair<uint32_t, std::string>> cls;
        if (!wiggle_mcd::rstClasses(mcd_, cls, err)) { resp["status"] = "error"; resp["message"] = err; return; }
        resp["status"] = "ok";
        resp["rst_classes"] = nljson::array();
        for (const auto& c : cls) {
            nljson e;
            e["class"] = c.first; e["info"] = c.second;
            resp["rst_classes"].push_back(e);
        }
    } else { resp["status"] = "error"; resp["message"] = "unknown what"; }
}

std::string DebugSession::findHldumptc()
{
    if (!hldumptc_.empty()) return hldumptc_;
    auto exists = [](const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (f) { fclose(f); return true; } return false; };
    const char* env = getenv("WIGGLE_HLDUMPTC");
    if (env && exists(env)) return hldumptc_ = env;
    const char* cand[] = {
        "D:/Program Files/TASKING/TriCore v6.3r1/ctc/bin/hldumptc.exe",
        "C:/Program Files/TASKING/TriCore v6.3r1/ctc/bin/hldumptc.exe",
        "C:/Program Files (x86)/TASKING/TriCore v6.3r1/ctc/bin/hldumptc.exe",
    };
    for (auto c : cand) if (exists(c)) return hldumptc_ = c;
    return "";
}

bool DebugSession::buildDisasmCache(std::string& err)
{
    std::string exe = findHldumptc();
    if (exe.empty()) { err = "hldumptc not found (set WIGGLE_HLDUMPTC)"; return false; }
    std::string exeW = exe, elfW = elf_.sourcePath();
    for (auto& c : exeW) if (c == '/') c = '\\';
    for (auto& c : elfW) if (c == '/') c = '\\';
    // 经临时 bat 包装，避免 _popen 嵌套引号被 cmd 剥离
    char batPath[MAX_PATH] = {0};
    GetTempPathA(MAX_PATH, batPath);
    std::string bat = std::string(batPath) + "wiggle_hld.bat";
    { FILE* bf = fopen(bat.c_str(), "w"); if (!bf) { err = "temp bat failed"; return false; }
      fprintf(bf, "@\"%s\" \"%s\"\n", exeW.c_str(), elfW.c_str()); fclose(bf); }
    FILE* p = _popen(bat.c_str(), "r");
    if (!p) { remove(bat.c_str()); err = "hldumptc popen failed"; return false; }
    disasmCache_.clear();
    char line[2048];
    while (fgets(line, sizeof(line), p)) {
        std::istringstream iss(line);
        std::string tok;
        if (!(iss >> tok)) continue;
        if (tok.size() != 8) continue;
        uint32_t a = 0; bool hexok = true;
        for (char ch : tok) { if (!isxdigit((unsigned char)ch)) { hexok = false; break; } }
        if (!hexok) continue;
        a = (uint32_t)strtoul(tok.c_str(), nullptr, 16);
        uint32_t nbytes = 0;
        while (iss >> tok) {
            if (tok.size() == 2 && isxdigit((unsigned char)tok[0]) && isxdigit((unsigned char)tok[1])) nbytes++;
            else break;
        }
        if (nbytes != 2 && nbytes != 4) continue;
        std::string label, text;
        if (!tok.empty() && tok.back() == ':') { label = tok.substr(0, tok.size() - 1); }
        else if (!tok.empty()) { text = tok; }
        std::string rest;
        while (iss >> tok) text += (text.empty() ? "" : " ") + tok;
        if (text.empty()) continue;
        DisasmLine d; d.addr = a; d.len = nbytes; d.text = text; d.label = label;
        disasmCache_[a] = d;
    }
    _pclose(p);
    remove(bat.c_str());
    disasmCacheElf_ = elf_.sourcePath();
    if (disasmCache_.empty()) { err = "hldumptc produced no disassembly"; return false; }
    return true;
}

void DebugSession::cmdDisasm(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint64_t in = req.value("addr", 0ULL);
    uint32_t count = req.value("count", 10);
    if (count == 0 || count > 256) count = 10;

    bool usedHld = false;
    if (elf_.isLoaded()) {
        if (disasmCacheElf_ != elf_.sourcePath() || disasmCache_.empty()) buildDisasmCache(err);
        usedHld = !disasmCache_.empty();
    }

    uint64_t addr = normData(in);
    std::vector<uint8_t> buf(count * 4 + 4);
    bool haveLive = wiggle_mcd::readMem(mcd_, addr, buf.data(), count * 4, err);
    if (!haveLive) { addr = in; haveLive = wiggle_mcd::readMem(mcd_, in, buf.data(), count * 4, err); }

    resp["status"] = "ok";
    resp["backend"] = usedHld ? "hldumptc" : "native";
    resp["insns"] = nljson::array();
    uint32_t cur = (uint32_t)in;
    for (uint32_t i = 0; i < count; i++) {
        nljson e;
        if (usedHld) {
            auto it = disasmCache_.find(cur);
            if (it != disasmCache_.end()) {
                e["addr"] = cur; e["length"] = it->second.len;
                e["mnemonic"] = it->second.text;
                if (!it->second.label.empty()) e["label"] = it->second.label;
                e["source"] = "hldumptc";
                resp["insns"].push_back(e);
                cur += it->second.len;
                continue;
            }
        }
        uint32_t off = cur - (uint32_t)in;
        if (haveLive && off + 2 <= count * 4) {
            DisasmInsn d = tricoreDecode(cur, buf.data() + off, count * 4 - off);
            e["addr"] = cur; e["length"] = d.length; e["mnemonic"] = d.mnemonic;
            e["raw"] = d.raw; e["is32"] = d.is32; e["source"] = "native";
            resp["insns"].push_back(e);
            cur += d.length;
        } else break;
    }
}

// 等待核进入 DEBUG（step-over/out 用），超时返回 false
static bool waitDebug(wiggle_mcd::Session& s, uint32_t timeoutMs)
{
    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        uint32_t st = 0, tid = 0;
        std::string info;
        if (wiggle_mcd::stateFull(s, st, tid, info) && st == MCD_CORE_STATE_DEBUG) return true;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (ms >= timeoutMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void DebugSession::cmdStepOver(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint32_t st = 0, tid = 0;
    std::string info;
    wiggle_mcd::stateFull(mcd_, st, tid, info);
    if (st != MCD_CORE_STATE_DEBUG) { resp["status"] = "error"; resp["message"] = "halt first"; return; }
    uint32_t pc = 0;
    readU32(csfr(cpu, OFF_PC), pc, err);
    uint8_t code[4];
    if (!wiggle_mcd::readMem(mcd_, normData(pc), code, 4, err)) {
        resp["status"] = "error"; resp["message"] = err; return;
    }
    DisasmInsn d = tricoreDecode(pc, code, 4);
    uint32_t target = pc + d.length;  // call 的返回地址=PC+len，故 BP@PC+len 即 step-over
    uint32_t id = 0;
    if (!wiggle_mcd::createTrig(mcd_, MCD_TRIG_TYPE_IP, target, 0, id, err)) {
        resp["status"] = "error"; resp["message"] = err; return;
    }
    wiggle_mcd::run(mcd_, err);
    bool hit = waitDebug(mcd_, 2000);
    wiggle_mcd::removeTrig(mcd_, id, err);
    if (!hit) wiggle_mcd::stop(mcd_, err);  // 分支跳走未命中，收回控制权
    uint32_t npc = 0;
    readU32(csfr(cpu, OFF_PC), npc, err);
    resp["status"] = "ok";
    resp["pc"] = npc;
    resp["target"] = target;
    resp["hit"] = hit;
    std::string sym = symAnnotate(npc);
    if (!sym.empty()) resp["pc_symbol"] = sym;
}

void DebugSession::cmdStepOut(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint32_t st = 0, tid = 0;
    std::string info;
    wiggle_mcd::stateFull(mcd_, st, tid, info);
    if (st != MCD_CORE_STATE_DEBUG) { resp["status"] = "error"; resp["message"] = "halt first"; return; }
    uint32_t ra = 0;
    readU32(csfr(cpu, OFF_A0 + 11 * 4), ra, err);  // A11=返回地址
    uint32_t id = 0;
    if (!wiggle_mcd::createTrig(mcd_, MCD_TRIG_TYPE_IP, ra, 0, id, err)) {
        resp["status"] = "error"; resp["message"] = err; return;
    }
    wiggle_mcd::run(mcd_, err);
    bool hit = waitDebug(mcd_, 2000);
    wiggle_mcd::removeTrig(mcd_, id, err);
    if (!hit) wiggle_mcd::stop(mcd_, err);
    uint32_t npc = 0;
    readU32(csfr(cpu, OFF_PC), npc, err);
    resp["status"] = "ok";
    resp["pc"] = npc;
    resp["return_addr"] = ra;
    resp["hit"] = hit;
    std::string sym = symAnnotate(npc);
    if (!sym.empty()) resp["pc_symbol"] = sym;
}

void DebugSession::cmdSample(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint64_t addr = normData(req.value("addr", 0ULL));
    if (req.contains("sym")) {
        const ElfSymbol* s = elf_.findByName(req.value("sym", ""));
        if (!s) { resp["status"] = "error"; resp["message"] = "symbol not found"; return; }
        addr = normData(s->value);
    }
    uint32_t size = req.value("size", 4);
    if (size == 0 || size > 8) size = 4;
    uint32_t interval = req.value("interval_ms", 100);
    uint8_t b0[8] = {0}, b1[8] = {0};
    if (!wiggle_mcd::readMem(mcd_, addr, b0, size, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    if (!wiggle_mcd::readMem(mcd_, addr, b1, size, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    uint64_t v0 = 0, v1 = 0;
    for (uint32_t i = 0; i < size; i++) { v0 |= static_cast<uint64_t>(b0[i]) << (8 * i); v1 |= static_cast<uint64_t>(b1[i]) << (8 * i); }
    resp["status"] = "ok";
    resp["addr"] = addr;
    resp["value0"] = v0;
    resp["value1"] = v1;
    resp["delta"] = v1 - v0;
    resp["interval_ms"] = interval;
    resp["consistent"] = (v0 == v1);  // 撕裂读防护：双次采样一致才采信
}

// ---------------------------------------------------------------------------
// RTOS 感知。FreeRTOS 用通用 TCB/List 布局（32-bit, configMAX_TASK_NAME_LEN=16）；
// AUTOSAR OS 内核结构项目特定，需符号导出或经 req 配置表参数，缺失时优雅报错。
// TCB: pxTopOfStack+0, xStateListItem+4, uxPriority+44, pxStack+48, pcTaskName+52
// ListItem_t: xItemValue0 pxNext4 pxPrevious8 pvOwner12 pxContainer16 (20B)
// List_t: uxNumberOfItems0 pxIndex4 xListEnd8 (28B); xListEnd.pxNext=+12, 结束标记=+8
// ---------------------------------------------------------------------------
bool DebugSession::freertosTasks(nljson& out, std::string& err)
{
    auto sym = [&](const char* n) { return elf_.findByName(n); };
    const ElfSymbol* cur = sym("pxCurrentTCB");
    const ElfSymbol* ready = sym("pxReadyTasksLists");
    if (!cur || !ready) { err = "FreeRTOS symbols (pxCurrentTCB/pxReadyTasksLists) not found"; return false; }
    uint32_t curTcb = 0; readU32(cur->value, curTcb, err);
    const ElfSymbol* top = sym("uxTopUsedPriority");
    uint32_t topPri = 7; if (top) readU32(top->value, topPri, err);
    const ElfSymbol* nt = sym("uxCurrentNumberOfTasks");
    uint32_t n = 0; if (nt) readU32(nt->value, n, err);
    out["type"] = "freertos"; out["current_tcb"] = curTcb; out["num_tasks"] = n;
    out["tasks"] = nljson::array();
    auto walk = [&](uint32_t list, const char* state) {
        uint32_t item = 0; readU32(list + 12, item, err);
        uint32_t end = list + 8; int guard = 0;
        while (item && item != end && guard++ < 256) {
            uint32_t tcb = item - 4;
            uint8_t nb[16] = {0}; char name[17] = {0};
            if (readMem(tcb + 52, nb, 16, err)) memcpy(name, nb, 16);
            uint32_t pri = 0, tos = 0;
            readU32(tcb + 44, pri, err); readU32(tcb + 0, tos, err);
            nljson t;
            t["name"] = name; t["tcb"] = tcb; t["priority"] = pri;
            t["state"] = (tcb == curTcb) ? "running" : state;
            t["top_of_stack"] = tos;
            out["tasks"].push_back(t);
            uint32_t next = 0; readU32(item + 4, next, err); item = next;
        }
    };
    for (uint32_t p = 0; p <= topPri && p < 16; p++) walk(ready->value + p * 28, "ready");
    if (auto* s = sym("xDelayedTaskList1")) walk(s->value, "delayed");
    if (auto* s = sym("xDelayedTaskList2")) walk(s->value, "delayed");
    if (auto* s = sym("xSuspendedTaskList")) walk(s->value, "suspended");
    if (auto* s = sym("xPendingReadyList")) walk(s->value, "pending");
    return true;
}

bool DebugSession::autosarTasks(nljson& out, std::string& err)
{
    // 配置驱动：req 提供 task_table/stride/name_off/state_off/count 符号或地址；否则探测常见符号
    auto sym = [&](const char* n) { return elf_.findByName(n); };
    const ElfSymbol* tbl = sym("OsTaskTable");
    if (!tbl) tbl = sym("OsTaskInitBlock");
    if (!tbl) { err = "AUTOSAR OS kernel symbols not exported; supply rtos table params"; return false; }
    out["type"] = "autosar"; out["table"] = tbl->name; out["table_addr"] = tbl->value;
    out["tasks"] = nljson::array();
    out["note"] = "AUTOSAR OS layout is project-specific; refine via config";
    return true;
}

bool DebugSession::rtosBacktrace(const nljson& req, nljson& out, std::string& err)
{
    nljson tasks;
    if (!freertosTasks(tasks, err)) return false;
    uint32_t tos = 0;
    std::string want = req.value("task", "");
    bool found = false;
    for (const auto& t : tasks["tasks"]) {
        if (want.empty() ? t.value("current", false) : t.value("name", "") == want) {
            tos = t.value("top_of_stack", 0); found = true; break;
        }
    }
    if (!found) { err = "task not found"; return false; }
    // TriCore FreeRTOS port: pxTopOfStack 指向保存的上下文，word0=PCXI 链接
    uint32_t link = 0, ra = 0;
    if (!readU32(tos, link, err)) return false;
    readU32(tos + CSA_UPPER_RA_OFF, ra, err);
    out["frames"] = nljson::array();
    nljson f0; f0["pc"] = ra;
    uint32_t o0 = 0; auto* s0 = symFindCode(ra, o0);
    if (s0) { f0["symbol"] = s0->name; f0["offset"] = o0; }
    out["frames"].push_back(f0);
    std::vector<uint32_t> visited;
    while (out["frames"].size() < BT_MAX_FRAMES) {
        uint32_t ea = csaAddrFromLink(link);
        if (!ea || std::find(visited.begin(), visited.end(), ea) != visited.end()) break;
        visited.push_back(ea);
        uint32_t nl = 0, nr = 0;
        if (!readU32(ea, nl, err)) break;
        bool up = (link & PCXI_UL) != 0;
        if (!readU32(ea + (up ? CSA_UPPER_RA_OFF : CSA_LOWER_RA_OFF), nr, err)) break;
        nljson f; f["pc"] = nr; f["csa"] = ea;
        uint32_t o = 0; auto* s = symFindCode(nr, o);
        if (s) { f["symbol"] = s->name; f["offset"] = o; }
        out["frames"].push_back(f);
        ra = nr; link = nl;
    }
    return true;
}

void DebugSession::cmdRtos(const nljson& req, nljson& resp)
{
    std::string err;
    int cpu = req.value("cpu", 0);
    if (!ensure(cpu, err)) { resp["status"] = "error"; resp["message"] = err; return; }
    if (!elf_.isLoaded()) { resp["status"] = "error"; resp["message"] = "load ELF first (sym load)"; return; }
    std::string action = req.value("action", "tasks");
    std::string type = req.value("type", "auto");
    if (type == "auto") type = elf_.findByName("pxCurrentTCB") ? "freertos" : "autosar";
    bool ok = false;
    if (action == "tasks") ok = (type == "freertos") ? freertosTasks(resp, err) : autosarTasks(resp, err);
    else if (action == "bt") ok = rtosBacktrace(req, resp, err);
    else { err = "unknown rtos action"; }
    resp["status"] = ok ? "ok" : "error";
    if (!ok) resp["message"] = err;
}

bool DebugSession::dispatch(const nljson& req, nljson& resp)
{
    std::string cmd = req.value("cmd", "");
    if (cmd == "state") cmdState(req, resp);
    else if (cmd == "halt" || cmd == "go" || cmd == "step") cmdHaltGoStep(req, resp);
    else if (cmd == "break" || cmd == "watch") cmdTrig(req, resp);
    else if (cmd == "read" || cmd == "dump") cmdRead(req, resp);
    else if (cmd == "write") cmdWrite(req, resp);
    else if (cmd == "poke") cmdPoke(req, resp);
    else if (cmd == "reg" || cmd == "cpu_reg") cmdReg(req, resp);
    else if (cmd == "var") cmdVar(req, resp);
    else if (cmd == "bt") cmdBt(req, resp);
    else if (cmd == "disasm") cmdDisasm(req, resp);
    else if (cmd == "stepover") cmdStepOver(req, resp);
    else if (cmd == "stepout") cmdStepOut(req, resp);
    else if (cmd == "sample") cmdSample(req, resp);
    else if (cmd == "sym") cmdSym(req, resp);
    else if (cmd == "reset") cmdReset(req, resp);
    else if (cmd == "search") cmdSearch(req, resp);
    else if (cmd == "compare") cmdCompare(req, resp);
    else if (cmd == "rtos") cmdRtos(req, resp);
    else if (cmd == "spaces" || cmd == "regs" || cmd == "rst") {
        nljson r2 = req;
        r2["what"] = cmd;
        cmdInfo(r2, resp);
    }
    else if (cmd == "ping") { resp["status"] = "ok"; resp["mcd"] = open_; }
    else return false;
    return true;
}

}  // namespace wiggle_dbg