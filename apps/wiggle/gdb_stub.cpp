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
//  GDB RSP stub 实现（winsock TCP）。寄存器编号对齐 GDB tricore-tdep，
//  并通过 qXfer target.xml 声明布局，GDB 据此读 g/p 包。
//********************************************************************************************************************

#include "gdb_stub.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "debug_session.h"
#include "tricore_defs.h"

namespace wiggle_dbg {

namespace {

constexpr int NREG = 41;  // d0-15(0-15) a0-15(16-31) psw32 pc33 pcxi34 fcx35 lcx36 isp37 icr38 biv39 btv40

uint64_t regAddr(int n)
{
    if (n >= 0 && n < 16) return csfr(0, OFF_D0 + n * 4);
    if (n >= 16 && n < 32) return csfr(0, OFF_A0 + (n - 16) * 4);
    switch (n) {
        case 32: return csfr(0, OFF_PSW);
        case 33: return csfr(0, OFF_PC);
        case 34: return csfr(0, OFF_PCXI);
        case 35: return csfr(0, 0x1FE38);  // FCX
        case 36: return csfr(0, 0x1FE3C);  // LCX
        case 37: return csfr(0, 0x1FE28);  // ISP
        case 38: return csfr(0, 0x1FE2C);  // ICR
        case 39: return csfr(0, 0x1FE20);  // BIV
        case 40: return csfr(0, 0x1FE24);  // BTV
        default: return 0;
    }
}

uint64_t normData(uint64_t a)
{
    if ((a & 0xF0000000u) == 0xD0000000u) return a - 0x60000000u;
    if ((a & 0xF0000000u) == 0x80000000u) return a + 0x20000000u;
    return a;
}

std::string hexLE(uint32_t v)
{
    char b[9];
    snprintf(b, sizeof(b), "%02x%02x%02x%02x", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF);
    return b;
}

uint8_t cksum(const std::string& s)
{
    uint32_t sum = 0;
    for (char c : s) sum += static_cast<uint8_t>(c);
    return static_cast<uint8_t>(sum & 0xFF);
}

std::string targetXml()
{
    std::string x = "<?xml version=\"1.0\"?>\n<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
                    "<target version=\"1\">\n<architecture>tricore</architecture>\n"
                    "<feature name=\"org.gnu.gdb.tricore.core\">\n";
    char line[128];
    for (int i = 0; i < 16; i++) { snprintf(line, sizeof(line), "<reg name=\"d%d\" bitsize=\"32\" regnum=\"%d\"/>\n", i, i); x += line; }
    for (int i = 0; i < 16; i++) { snprintf(line, sizeof(line), "<reg name=\"a%d\" bitsize=\"32\" regnum=\"%d\"/>\n", i, 16 + i); x += line; }
    x += "<reg name=\"psw\" bitsize=\"32\" regnum=\"32\"/>\n";
    x += "<reg name=\"pc\" bitsize=\"32\" regnum=\"33\" type=\"code_ptr\"/>\n";
    x += "<reg name=\"pcxi\" bitsize=\"32\" regnum=\"34\"/>\n";
    x += "<reg name=\"fcx\" bitsize=\"32\" regnum=\"35\"/>\n";
    x += "<reg name=\"lcx\" bitsize=\"32\" regnum=\"36\"/>\n";
    x += "<reg name=\"isp\" bitsize=\"32\" regnum=\"37\"/>\n";
    x += "<reg name=\"icr\" bitsize=\"32\" regnum=\"38\"/>\n";
    x += "<reg name=\"biv\" bitsize=\"32\" regnum=\"39\"/>\n";
    x += "<reg name=\"btv\" bitsize=\"32\" regnum=\"40\"/>\n";
    x += "</feature>\n</target>\n";
    return x;
}

struct Sock {
    SOCKET s = INVALID_SOCKET;
    int getc()
    {
        char c;
        int r = recv(s, &c, 1, 0);
        return r == 1 ? static_cast<uint8_t>(c) : -1;
    }
    bool put(const std::string& d)
    {
        return send(s, d.c_str(), static_cast<int>(d.size()), 0) == static_cast<int>(d.size());
    }
    void sendPacket(const std::string& body)
    {
        char ck[3];
        snprintf(ck, sizeof(ck), "%02x", cksum(body));
        put("$" + body + "#" + ck);
        getc();  // ack '+'/'-'
    }
    // 读一个包；返回 body；0x03 中断返回 "\x03"；断开返回 ""
    std::string readPacket()
    {
        int c;
        while ((c = getc()) >= 0) {
            if (c == '$') break;
            if (c == 0x03) return "\x03";
        }
        if (c < 0) return "";
        std::string body;
        while ((c = getc()) >= 0 && c != '#') body += static_cast<char>(c);
        getc(); getc();  // checksum
        put("+");
        return body;
    }
    bool interrupted()
    {
        char c;
        int r = recv(s, &c, 1, MSG_PEEK);
        return r == 1 && static_cast<uint8_t>(c) == 0x03;
    }
    void drainInterrupt()
    {
        char c;
        recv(s, &c, 1, 0);
    }
};

nljson req(const std::string& cmd)
{
    nljson j;
    j["cmd"] = cmd;
    j["cpu"] = 0;
    return j;
}

bool readU32(uint64_t a, uint32_t& v)
{
    std::string e;
    uint8_t b[4];
    if (!g_debug.readMem(a, b, 4, e)) return false;
    v = b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
    return true;
}

}  // namespace

int gdbServe(int port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(ls, 1) != 0) {
        fprintf(stderr, "bind/listen :%d failed\n", port);
        return 1;
    }
    fprintf(stderr, "GDB RSP server listening on 127.0.0.1:%d (target remote :%d)\n", port, port);
    SOCKET cs = accept(ls, nullptr, nullptr);
    if (cs == INVALID_SOCKET) { fprintf(stderr, "accept failed\n"); return 1; }
    Sock sock{cs};

    std::string err;
    if (!g_debug.ensure(0, err)) { fprintf(stderr, "MCD: %s\n", err.c_str()); return 1; }
    { nljson r = req("halt"); nljson resp; g_debug.dispatch(r, resp); }  // GDB 期望初始已停止

    std::string xml = targetXml();
    bool running = true;
    while (running) {
        std::string p = sock.readPacket();
        if (p.empty()) break;              // 断开
        if (p == "\x03") { nljson r = req("halt"); nljson resp; g_debug.dispatch(r, resp); sock.sendPacket("S05"); continue; }
        char cmd = p[0];
        std::string reply;
        switch (cmd) {
            case 'q': {
                if (p.rfind("qSupported", 0) == 0) reply = "PacketSize=1000;qXfer:features:read+;hwbreak+;swbreak+";
                else if (p == "qAttached") reply = "1";
                else if (p == "qfThreadInfo") reply = "m1";
                else if (p == "qsThreadInfo") reply = "l";
                else if (p == "qC") reply = "QC1";
                else if (p == "qTStatus") reply = "";
                else if (p.rfind("qXfer:features:read:target.xml:", 0) == 0) {
                    size_t colon = p.rfind(':');
                    std::string ol = p.substr(colon + 1);
                    size_t comma = ol.find(',');
                    uint32_t off = strtoul(ol.substr(0, comma).c_str(), nullptr, 16);
                    uint32_t len = strtoul(ol.substr(comma + 1).c_str(), nullptr, 16);
                    std::string chunk = xml.substr(off, len);
                    reply = (off + len >= xml.size() ? "l" : "m") + chunk;
                }
                else reply = "";
                break;
            }
            case '?': reply = "S05"; break;
            case 'g': {
                std::string all;
                for (int i = 0; i < NREG; i++) { uint32_t v = 0; readU32(regAddr(i), v); all += hexLE(v); }
                reply = all;
                break;
            }
            case 'p': {
                int n = strtol(p.c_str() + 1, nullptr, 10);  // GDB 寄存器号为十进制
                uint32_t v = 0;
                reply = (n >= 0 && n < NREG && readU32(regAddr(n), v)) ? hexLE(v) : "00000000";
                break;
            }
            case 'P': {
                size_t eq = p.find('=');
                int n = strtol(p.substr(1, eq - 1).c_str(), nullptr, 10);
                uint32_t v = strtoul(p.substr(eq + 1).c_str(), nullptr, 16);
                uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
                std::string e;
                reply = (n >= 0 && n < NREG && g_debug.writeMem(regAddr(n), b, 4, e)) ? "OK" : "E01";
                break;
            }
            case 'm': {
                size_t comma = p.find(',');
                uint64_t a = strtoull(p.substr(1, comma - 1).c_str(), nullptr, 16);
                uint32_t len = strtoul(p.substr(comma + 1).c_str(), nullptr, 16);
                if (len > 4096) len = 4096;
                std::vector<uint8_t> buf(len);
                std::string e;
                if (g_debug.readMem(normData(a), buf.data(), len, e)) {
                    std::string h;
                    for (auto v : buf) { char t[3]; snprintf(t, 3, "%02x", v); h += t; }
                    reply = h;
                } else reply = "E01";
                break;
            }
            case 'M': {
                size_t comma = p.find(',');
                size_t colon = p.find(':');
                uint64_t a = strtoull(p.substr(1, comma - 1).c_str(), nullptr, 16);
                uint32_t len = strtoul(p.substr(comma + 1, colon - comma - 1).c_str(), nullptr, 16);
                std::string hd = p.substr(colon + 1);
                std::vector<uint8_t> buf(len);
                for (uint32_t i = 0; i < len && i * 2 + 1 < hd.size(); i++)
                    buf[i] = static_cast<uint8_t>(strtoul(hd.substr(i * 2, 2).c_str(), nullptr, 16));
                std::string e;
                reply = g_debug.writeMem(normData(a), buf.data(), len, e) ? "OK" : "E01";
                break;
            }
            case 'c': case 's': {
                bool step = (cmd == 's');
                nljson r = req(step ? "step" : "go");
                nljson resp;
                g_debug.dispatch(r, resp);
                if (!step) {
                    // 等待 halt（断点/观察点）或 GDB Ctrl-C
                    while (true) {
                        uint32_t st = 0, tid = 0;
                        std::string info;
                        nljson sr = req("state");
                        nljson sresp;
                        g_debug.dispatch(sr, sresp);
                        st = sresp.value("state", 0);
                        if (st == MCD_CORE_STATE_DEBUG) break;
                        if (sock.interrupted()) { sock.drainInterrupt(); nljson hr = req("halt"); nljson hresp; g_debug.dispatch(hr, hresp); break; }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
                reply = "S05";
                break;
            }
            case 'v': {
                if (p == "vCont?") reply = "vCont;c;s;t";
                else if (p.rfind("vCont;", 0) == 0) {
                    bool step = p.find(";s") != std::string::npos;
                    nljson r = req(step ? "step" : "go");
                    nljson resp;
                    g_debug.dispatch(r, resp);
                    if (!step) {
                        while (true) {
                            nljson sr = req("state");
                            nljson sresp;
                            g_debug.dispatch(sr, sresp);
                            if (sresp.value("state", 0) == MCD_CORE_STATE_DEBUG) break;
                            if (sock.interrupted()) { sock.drainInterrupt(); nljson hr = req("halt"); nljson hresp; g_debug.dispatch(hr, hresp); break; }
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    }
                    reply = "S05";
                }
                else reply = "";
                break;
            }
            case 'Z': case 'z': {
                // Z0/sw Z1/hw Z2/write Z3/read Z4/access
                int type = p[1] - '0';
                size_t c1 = p.find(',');
                size_t c2 = p.find(',', c1 + 1);
                uint64_t a = strtoull(p.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 16);
                nljson r;
                if (type == 0 || type == 1) { r = req("break"); }
                else { r = req("watch"); r["type"] = (type == 2 ? "write" : type == 3 ? "read" : "rw"); }
                r["action"] = (cmd == 'Z') ? "add" : "del";
                r["addr"] = a;
                nljson resp;
                g_debug.dispatch(r, resp);
                reply = (resp.value("status", "") == "ok") ? "OK" : "E01";
                break;
            }
            case 'H': case 'T': reply = "OK"; break;
            case '!': reply = "OK"; break;
            case 'D': case 'k': reply = "OK"; running = false; break;
            case 'R': { nljson r = req("reset"); nljson resp; g_debug.dispatch(r, resp); reply = "OK"; break; }
            default: reply = ""; break;
        }
        sock.sendPacket(reply);
    }
    closesocket(cs);
    closesocket(ls);
    WSACleanup();
    g_debug.close();
    return 0;
}

}  // namespace wiggle_dbg