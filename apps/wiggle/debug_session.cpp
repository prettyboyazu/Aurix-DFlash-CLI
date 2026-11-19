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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "tricore_defs.h"
#include "hex_parser.h"

namespace wiggle_dbg {

DebugSession g_debug;

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
    bool ok = true;
    if (cmd == "halt") ok = wiggle_mcd::stop(mcd_, err);
    else if (cmd == "go") ok = wiggle_mcd::run(mcd_, err);
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
    uint64_t addr = req.value("addr", 0ULL);
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
    uint64_t addr = req.value("addr", 0ULL);
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
    else if (cmd == "bt") cmdBt(req, resp);
    else if (cmd == "sym") cmdSym(req, resp);
    else if (cmd == "reset") cmdReset(req, resp);
    else if (cmd == "search") cmdSearch(req, resp);
    else if (cmd == "compare") cmdCompare(req, resp);
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