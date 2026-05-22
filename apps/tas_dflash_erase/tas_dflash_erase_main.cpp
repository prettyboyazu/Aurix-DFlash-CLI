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
//  TAS DFlash Tool for AURIX TC23x/TC26x/TC27x/TC3x
//  Subcommands: erase, read, list
//********************************************************************************************************************

#include "tas_client_rw.h"
#include "tas_utils.h"
#include "tas_device_family.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>

//********************************************************************************************************************
//  DFlash configuration per device type
//********************************************************************************************************************

struct DFlashConfig {
    uint32_t sectorSize;
    uint32_t totalSize;
    const char* family;
    bool isTc3x;
};

static bool getDFlashConfig(uint32_t deviceType, DFlashConfig& cfg)
{
    uint32_t dt = deviceType & TAS_DT_VERSION_MASK_OUT;

    // TC2x: TC23x, TC26x, TC27x - sector = 8 KB
    cfg.sectorSize = 0x2000u;
    cfg.family     = "TC2x";
    cfg.isTc3x     = false;
    switch (dt) {
    case TAS_DT_TC23X: cfg.totalSize = 0x20000u; return true;
    case TAS_DT_TC26X: cfg.totalSize = 0x20000u; return true;
    case TAS_DT_TC27X: cfg.totalSize = 0x40000u; return true;
    default: break;
    }

    // TC3x - sector = 4 KB
    if (tas_df_check_if_tc3x(deviceType)) {
        cfg.sectorSize = 0x1000u;
        cfg.family     = "TC3x";
        cfg.isTc3x     = true;
        switch (dt) {
        case TAS_DT_TC39X:  cfg.totalSize = 0x100000u; break;
        case TAS_DT_TC38X:  cfg.totalSize = 0x80000u;  break;
        case TAS_DT_TC37X:
        case TAS_DT_TC37XE:  cfg.totalSize = 0x40000u;  break;
        default:             cfg.totalSize = 0x20000u;  break;
        }
        return true;
    }

    return false;
}

//********************************************************************************************************************
//  AURIX Register Addresses and Constants
//********************************************************************************************************************

// SCU_WDTS_CON0: [31:16]=REL, [15:2]=PW, [1]=LCK, [0]=ENDINIT
static constexpr uint64_t SCU_WDTS_CON0_ADDR    = 0xF00362A8ULL;
static constexpr uint32_t CON0_ENDINIT_BIT       = (1u << 0);
static constexpr uint32_t CON0_LCK_BIT           = (1u << 1);
static constexpr uint32_t CON0_PW_SHIFT          = 2;
static constexpr uint32_t CON0_PW_MASK           = 0x3FFFu;
static constexpr uint32_t CON0_REL_SHIFT         = 16;
static constexpr uint32_t CON0_PW_INVERT_MASK    = 0x003Fu;

// TC3x: DMU_HF_STATUS (busy flags) at 0xF8040010
static constexpr uint64_t DMU_HF_STATUS_ADDR     = 0xF8040010ULL;
static constexpr uint32_t TC3X_D0BUSY_BIT        = (1u << 0);

// TC3x: DMU_HF_ERRSR (error flags) at 0xF8040034
static constexpr uint64_t DMU_HF_ERRSR_ADDR      = 0xF8040034ULL;
static constexpr uint32_t TC3X_OPER_BIT           = (1u << 0);
static constexpr uint32_t TC3X_SQER_BIT           = (1u << 1);
static constexpr uint32_t TC3X_PROER_BIT          = (1u << 2);
static constexpr uint32_t TC3X_PVER_BIT           = (1u << 3);
static constexpr uint32_t TC3X_EVER_BIT           = (1u << 4);

// TC2x: FLASH0_FSR (combined status + error) at 0xF8002010
static constexpr uint64_t FLASH0_FSR_ADDR         = 0xF8002010ULL;
static constexpr uint32_t TC2X_D0BUSY_BIT         = (1u << 1);
static constexpr uint32_t TC2X_OPER_BIT           = (1u << 11);
static constexpr uint32_t TC2X_SQER_BIT           = (1u << 12);
static constexpr uint32_t TC2X_PROER_BIT          = (1u << 13);
static constexpr uint32_t TC2X_PVER_BIT           = (1u << 25);
static constexpr uint32_t TC2X_EVER_BIT           = (1u << 26);

// Flash command registers (iLLD offsets)
static constexpr uint64_t FLASH_CMD_BASE          = 0xAF000000ULL;
static constexpr uint64_t FLASH_CMD_CLEAR_STATUS  = FLASH_CMD_BASE | 0x5554;
static constexpr uint64_t FLASH_CMD_SECTOR_ADDR   = FLASH_CMD_BASE | 0xAA50;
static constexpr uint64_t FLASH_CMD_SECTOR_COUNT  = FLASH_CMD_BASE | 0xAA58;
static constexpr uint64_t FLASH_CMD_EXECUTE       = FLASH_CMD_BASE | 0xAAA8;
static constexpr uint64_t FLASH_CMD_RESET_READ    = FLASH_CMD_BASE | 0x5554;

static constexpr uint32_t FLASH_VAL_CLEAR_STATUS  = 0xFA;
static constexpr uint32_t FLASH_VAL_ERASE_CMD1    = 0x80;
static constexpr uint32_t FLASH_VAL_ERASE_CMD2    = 0x50;
static constexpr uint32_t FLASH_VAL_RESET_READ    = 0xF0;

static constexpr uint32_t DFLASH_START_ADDR       = 0xAF000000u;
static constexpr uint32_t DFLASH_ERASED_BYTE      = 0x00u;

static constexpr uint32_t WAIT_UNBUSY_TIMEOUT_MS  = 10000;
static constexpr uint32_t POLL_INTERVAL_MS         = 10;

//********************************************************************************************************************
//  Register read/write wrappers
//********************************************************************************************************************

static tas_return_et readReg32(CTasClientRw& client, uint64_t addr, uint32_t& value)
{
    return client.read32(addr, &value);
}

static tas_return_et writeReg32(CTasClientRw& client, uint64_t addr, uint32_t value)
{
    return client.write32(addr, value);
}

//********************************************************************************************************************
//  Safety Watchdog Password
//********************************************************************************************************************

static bool getSafetyWatchdogPassword(CTasClientRw& client, uint16_t& password, uint16_t& relValue)
{
    uint32_t con0;
    if (readReg32(client, SCU_WDTS_CON0_ADDR, con0) != TAS_ERR_NONE) return false;
    uint16_t pwField = static_cast<uint16_t>((con0 >> CON0_PW_SHIFT) & CON0_PW_MASK);
    password = pwField ^ CON0_PW_INVERT_MASK;
    relValue = static_cast<uint16_t>((con0 >> CON0_REL_SHIFT) & 0xFFFFu);
    return true;
}

//********************************************************************************************************************
//  Clear/Set Safety EndInit
//********************************************************************************************************************

static bool clearSafetyEndInit(CTasClientRw& client, uint16_t password, uint16_t relValue)
{
    uint32_t con0;
    uint32_t unlockVal = CON0_ENDINIT_BIT
                       | (static_cast<uint32_t>(password) << CON0_PW_SHIFT)
                       | (static_cast<uint32_t>(relValue) << CON0_REL_SHIFT);
    if (writeReg32(client, SCU_WDTS_CON0_ADDR, unlockVal) != TAS_ERR_NONE) return false;

    uint32_t clearVal = CON0_LCK_BIT
                      | (static_cast<uint32_t>(password) << CON0_PW_SHIFT)
                      | (static_cast<uint32_t>(relValue) << CON0_REL_SHIFT);
    if (writeReg32(client, SCU_WDTS_CON0_ADDR, clearVal) != TAS_ERR_NONE) return false;

    for (int i = 0; i < 100; i++) {
        if (readReg32(client, SCU_WDTS_CON0_ADDR, con0) != TAS_ERR_NONE) return false;
        if (!(con0 & CON0_ENDINIT_BIT)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

static bool setSafetyEndInit(CTasClientRw& client, uint16_t password, uint16_t relValue)
{
    uint32_t con0;
    uint32_t unlockVal = CON0_ENDINIT_BIT
                       | (static_cast<uint32_t>(password) << CON0_PW_SHIFT)
                       | (static_cast<uint32_t>(relValue) << CON0_REL_SHIFT);
    if (writeReg32(client, SCU_WDTS_CON0_ADDR, unlockVal) != TAS_ERR_NONE) return false;

    uint32_t setVal = CON0_ENDINIT_BIT
                    | CON0_LCK_BIT
                    | (static_cast<uint32_t>(password) << CON0_PW_SHIFT)
                    | (static_cast<uint32_t>(relValue) << CON0_REL_SHIFT);
    if (writeReg32(client, SCU_WDTS_CON0_ADDR, setVal) != TAS_ERR_NONE) return false;

    for (int i = 0; i < 100; i++) {
        if (readReg32(client, SCU_WDTS_CON0_ADDR, con0) != TAS_ERR_NONE) return false;
        if (con0 & CON0_ENDINIT_BIT) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

//********************************************************************************************************************
//  Flash operations
//********************************************************************************************************************

static bool clearFlashStatus(CTasClientRw& client)
{
    return writeReg32(client, FLASH_CMD_CLEAR_STATUS, FLASH_VAL_CLEAR_STATUS) == TAS_ERR_NONE;
}

// Atomic erase via execute_trans -- CSI requires continuous 4-write sequence
static bool eraseMultipleSectors(CTasClientRw& client, uint32_t sectorAddr, uint32_t numSectors)
{
    uint32_t valAddr  = sectorAddr;
    uint32_t valCount = numSectors;
    uint32_t valCmd1  = FLASH_VAL_ERASE_CMD1;
    uint32_t valCmd2  = FLASH_VAL_ERASE_CMD2;

    tas_rw_trans_st trans[4] = {
        { FLASH_CMD_SECTOR_ADDR,  4, 0, TAS_AM0, TAS_RW_TT_WR, .wdata = &valAddr  },
        { FLASH_CMD_SECTOR_COUNT, 4, 0, TAS_AM0, TAS_RW_TT_WR, .wdata = &valCount },
        { FLASH_CMD_EXECUTE,      4, 0, TAS_AM0, TAS_RW_TT_WR, .wdata = &valCmd1  },
        { FLASH_CMD_EXECUTE,      4, 0, TAS_AM0, TAS_RW_TT_WR, .wdata = &valCmd2  },
    };

    return client.execute_trans(trans, 4) == TAS_ERR_NONE;
}

static bool waitUnbusyD0(CTasClientRw& client, bool isTc3x, uint32_t& elapsedMs)
{
    uint64_t statusAddr = isTc3x ? DMU_HF_STATUS_ADDR : FLASH0_FSR_ADDR;
    uint32_t busyBit    = isTc3x ? TC3X_D0BUSY_BIT    : TC2X_D0BUSY_BIT;
    auto startTime = std::chrono::steady_clock::now();
    while (true) {
        uint32_t status;
        if (readReg32(client, statusAddr, status) != TAS_ERR_NONE) return false;
        if (!(status & busyBit)) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            elapsedMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            return true;
        }
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        uint32_t ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        if (ms >= WAIT_UNBUSY_TIMEOUT_MS) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
}

static bool resetToRead(CTasClientRw& client)
{
    return writeReg32(client, FLASH_CMD_RESET_READ, FLASH_VAL_RESET_READ) == TAS_ERR_NONE;
}

//********************************************************************************************************************
//  Check flash error flags after erase
//********************************************************************************************************************

static bool checkEraseErrors(CTasClientRw& client, bool isTc3x)
{
    uint64_t errorAddr = isTc3x ? DMU_HF_ERRSR_ADDR : FLASH0_FSR_ADDR;
    uint32_t pverBit   = isTc3x ? TC3X_PVER_BIT     : TC2X_PVER_BIT;
    uint32_t everBit   = isTc3x ? TC3X_EVER_BIT     : TC2X_EVER_BIT;
    uint32_t proerBit  = isTc3x ? TC3X_PROER_BIT    : TC2X_PROER_BIT;
    uint32_t sqerBit   = isTc3x ? TC3X_SQER_BIT     : TC2X_SQER_BIT;
    uint32_t operBit   = isTc3x ? TC3X_OPER_BIT     : TC2X_OPER_BIT;

    uint32_t status;
    if (readReg32(client, errorAddr, status) != TAS_ERR_NONE) {
        printf("  WARN: Could not read error status register\n");
        return true;
    }

    bool ok = true;
    if (status & pverBit) {  printf("  ERROR: Program Verify Error (PVER)\n"); ok = false; }
    if (status & everBit) {  printf("  ERROR: Erase Verify Error (EVER)\n");    ok = false; }
    if (status & proerBit) { printf("  ERROR: Protection Error (PROER)\n");      ok = false; }
    if (status & sqerBit) {  printf("  ERROR: Sequence Error (SQER)\n");         ok = false; }
    if (status & operBit) {  printf("  ERROR: Operation Error (OPER)\n");        ok = false; }

    if (ok) printf("  No error flags set.\n");
    return ok;
}

//********************************************************************************************************************
//  Verify erased content (with ECC tolerance)
//********************************************************************************************************************

static bool verifyErase(CTasClientRw& client, uint32_t startAddr, uint32_t numSectors, uint32_t sectorSize)
{
    const uint32_t totalBytes = numSectors * sectorSize;
    const uint32_t chunkSize = sectorSize;
    std::vector<uint8_t> buf(chunkSize, 0);
    uint32_t totalErrors = 0;
    uint32_t totalRead = 0;

    for (uint32_t offset = 0; offset < totalBytes; offset += chunkSize) {
        uint32_t bytesRead = 0;
        tas_return_et ret = client.read(startAddr + offset, buf.data(), chunkSize, &bytesRead);
        if (ret != TAS_ERR_NONE && ret != TAS_ERR_RW_READ) {
            printf("  ERROR: Read failed at 0x%08X\n", startAddr + offset);
            return false;
        }
        totalRead += bytesRead;
        for (uint32_t i = 0; i < bytesRead; i++) {
            if (buf[i] != DFLASH_ERASED_BYTE) {
                if (totalErrors < 10) printf("  FAIL at 0x%08X: expected 0x00, got 0x%02X\n", startAddr + offset + i, buf[i]);
                totalErrors++;
            }
        }
    }
    printf("  Read %u bytes from 0x%08X\n", totalRead, startAddr);
    if (totalErrors > 0) { printf("  FAILED: %u bytes not erased\n", totalErrors); return false; }
    printf("  PASSED: all %u bytes are 0x00 (erased)\n", totalRead);
    return true;
}

//********************************************************************************************************************
//  Backup DFlash to file (with ECC tolerance)
//********************************************************************************************************************

static bool backupDFlash(CTasClientRw& client, uint32_t startAddr, uint32_t totalBytes, uint32_t sectorSize, const char* filePath)
{
    printf("  Backing up %u bytes from 0x%08X to %s...\n", totalBytes, startAddr, filePath);

    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs) {
        printf("  ERROR: Cannot open file '%s' for writing\n", filePath);
        return false;
    }

    const uint32_t chunkSize = sectorSize;
    std::vector<uint8_t> buf(chunkSize, 0);
    uint32_t totalWritten = 0;

    for (uint32_t offset = 0; offset < totalBytes; offset += chunkSize) {
        uint32_t bytesRead = 0;
        tas_return_et ret = client.read(startAddr + offset, buf.data(), chunkSize, &bytesRead);
        if (ret != TAS_ERR_NONE && ret != TAS_ERR_RW_READ) {
            printf("  ERROR: Read failed at 0x%08X\n", startAddr + offset);
            return false;
        }
        ofs.write(reinterpret_cast<const char*>(buf.data()), bytesRead);
        totalWritten += bytesRead;
    }

    if (!ofs) {
        printf("  ERROR: Write to file failed\n");
        return false;
    }
    printf("  Backup saved: %u bytes -> %s\n", totalWritten, filePath);
    return true;
}

//********************************************************************************************************************
//  Hex dump (xxd-style)
//********************************************************************************************************************

static void hexDumpXxd(const uint8_t* data, uint32_t length, uint32_t baseAddr)
{
    for (uint32_t row = 0; row < length; row += 16) {
        uint32_t addr = baseAddr + row;
        uint32_t rowLen = std::min(16u, length - row);
        printf("%08X: ", addr);
        for (uint32_t col = 0; col < 16; col++) {
            if (col == 8) printf(" ");
            if (col < rowLen) printf("%02X ", data[row + col]);
            else printf("   ");
        }
        printf(" ");
        for (uint32_t col = 0; col < rowLen; col++) {
            uint8_t c = data[row + col];
            printf("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        printf("\n");
    }
}

//********************************************************************************************************************
//  Intel HEX generation
//********************************************************************************************************************

static uint8_t intelHexChecksum(const uint8_t* data, uint32_t len)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return static_cast<uint8_t>((0x100 - sum) & 0xFF);
}

static void writeIntelHexRecord(FILE* fp, uint8_t recordType,
                                uint16_t addr, const uint8_t* data, uint8_t dataLen)
{
    std::vector<uint8_t> buf;
    buf.push_back(dataLen);
    buf.push_back(static_cast<uint8_t>(addr >> 8));
    buf.push_back(static_cast<uint8_t>(addr & 0xFF));
    buf.push_back(recordType);
    for (uint8_t i = 0; i < dataLen; i++) buf.push_back(data[i]);
    uint8_t ck = intelHexChecksum(buf.data(), static_cast<uint32_t>(buf.size()));
    buf.push_back(ck);

    fprintf(fp, ":");
    for (auto b : buf) fprintf(fp, "%02X", b);
    fprintf(fp, "\n");
}

static bool saveToIntelHex(const char* filePath, uint32_t baseAddr,
                           const std::vector<uint8_t>& data)
{
    FILE* fp = fopen(filePath, "w");
    if (!fp) { fprintf(stderr, "ERROR: Cannot open '%s' for writing\n", filePath); return false; }

    uint16_t currentUpper = 0xFFFF; // force first Type 04 record
    uint32_t offset = 0;
    while (offset < data.size()) {
        uint16_t upperAddr = static_cast<uint16_t>((baseAddr + offset) >> 16);
        if (upperAddr != currentUpper) {
            uint8_t ua[2] = { static_cast<uint8_t>(upperAddr >> 8),
                              static_cast<uint8_t>(upperAddr & 0xFF) };
            writeIntelHexRecord(fp, 0x04, 0x0000, ua, 2);
            currentUpper = upperAddr;
        }

        uint16_t loAddr = static_cast<uint16_t>((baseAddr + offset) & 0xFFFF);
        uint32_t bytesTo64k = 0x10000u - loAddr;
        uint32_t remaining = static_cast<uint32_t>(data.size()) - offset;
        uint8_t chunkLen = static_cast<uint8_t>(std::min({remaining, bytesTo64k, 16u}));
        writeIntelHexRecord(fp, 0x00, loAddr, data.data() + offset, chunkLen);
        offset += chunkLen;
    }

    writeIntelHexRecord(fp, 0x01, 0x0000, nullptr, 0);
    fclose(fp);
    return true;
}

//********************************************************************************************************************
//  Hex address parser
//********************************************************************************************************************

static bool parseHexAddr(const char* s, uint32_t& val)
{
    if (sscanf(s, "%x", &val) == 1) return true;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return sscanf(s + 2, "%x", &val) == 1;
    }
    return false;
}

//********************************************************************************************************************
//  Common argument parsing
//********************************************************************************************************************

struct CommonArgs {
    const char* serverIp = "localhost";
    const char* targetId = nullptr;
};

// Returns: 1 = consumed 1 arg, 2 = consumed 2 args (option + value), 0 = not recognized
static int parseCommonOption(int argc, char** argv, int idx, CommonArgs& args)
{
    if (strcmp(argv[idx], "--server") == 0 && idx + 1 < argc) {
        args.serverIp = argv[idx + 1];
        return 2;
    }
    if (strcmp(argv[idx], "--target") == 0 && idx + 1 < argc) {
        args.targetId = argv[idx + 1];
        return 2;
    }
    return 0;
}

//********************************************************************************************************************
//  doList - list connected targets
//********************************************************************************************************************

static int doList(int argc, char** argv)
{
    CommonArgs args;
    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: tas_dflash_erase list [--server <ip>]\n");
            return 0;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return 1;
    }

    printf("TAS DFlash Tool - Device List\n");
    printf("=============================\n");
    printf("Connecting to TAS server at %s...\n", args.serverIp);

    CTasClientRw client("DFlashList");
    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 2; }

    const tas_server_info_st* si = client.get_server_info();
    printf("  Server: %s V%d.%d (%s)\n", si->server_name, si->v_major, si->v_minor, si->date);

    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 3; }

    printf("  Targets (%u):\n", numTargets);
    for (uint32_t i = 0; i < numTargets; i++) {
        printf("  [%u] %-12s  %s\n", i,
               tas_get_device_name_str(targets[i].device_type),
               targets[i].identifier);
    }
    if (numTargets == 0) printf("  No targets found.\n");
    return 0;
}

//********************************************************************************************************************
//  doRead - read DFlash content
//********************************************************************************************************************

static int doRead(int argc, char** argv)
{
    CommonArgs args;
    uint32_t readAddr = 0;
    uint32_t readLength = 0;
    const char* outputFile = nullptr;
    bool hasAddr = false;
    bool hasLength = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "--addr") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], readAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return 1; }
            hasAddr = true; i++; continue;
        }
        if ((strcmp(argv[i], "--length") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], readLength) || readLength == 0) { fprintf(stderr, "ERROR: Invalid length\n"); return 1; }
            hasLength = true; i++; continue;
        }
        if ((strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            outputFile = argv[++i]; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: tas_dflash_erase read --addr <hex> --length <hex> [options]\n"
                   "  --addr/-a <hex>     Start address (required)\n"
                   "  --length/-l <hex>   Length in bytes (required)\n"
                   "  --output/-o <file>  Output file (.bin or .hex, default: hex dump)\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n");
            return 0;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return 1;
    }

    if (!hasAddr || !hasLength) {
        fprintf(stderr, "ERROR: --addr and --length are required\n");
        return 1;
    }

    printf("TAS DFlash Read Tool\n");
    printf("====================\n");
    printf("Connecting to TAS server at %s...\n", args.serverIp);

    CTasClientRw client("DFlashRead");
    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 2; }

    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 3; }

    const char* selectedTargetId = (args.targetId != nullptr) ? args.targetId
                                   : (numTargets > 0) ? targets[0].identifier : nullptr;
    if (!selectedTargetId) { fprintf(stderr, "ERROR: No targets\n"); return 4; }
    if (args.targetId == nullptr) printf("  Auto-selected target [0]\n");

    ret = client.session_start(selectedTargetId, "DFlashReadSession");
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 5; }

    ret = client.device_connect(TAS_CLNT_DCO_RESET_AND_HALT);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 6; }

    const tas_con_info_st* conInfo = client.get_con_info();
    DFlashConfig flashCfg;
    if (!getDFlashConfig(conInfo->device_type, flashCfg)) {
        fprintf(stderr, "ERROR: Unsupported device '%s'\n", tas_get_device_name_str(conInfo->device_type));
        return 7;
    }

    uint32_t dflashEndAddr = DFLASH_START_ADDR + flashCfg.totalSize - 1;
    if (readAddr < DFLASH_START_ADDR || readAddr > dflashEndAddr) {
        fprintf(stderr, "ERROR: Address 0x%08X out of DFlash range\n", readAddr);
        return 1;
    }
    if (readAddr + readLength - 1 > dflashEndAddr) {
        fprintf(stderr, "ERROR: Range exceeds DFlash boundary\n");
        return 1;
    }

    printf("  Device: %s (%s), DFlash: %u KB\n",
           tas_get_device_name_str(conInfo->device_type), flashCfg.family, flashCfg.totalSize / 1024);
    printf("  Read:   0x%08X - 0x%08X (%u bytes)\n\n", readAddr, readAddr + readLength - 1, readLength);

    // Read in sector-sized chunks
    std::vector<uint8_t> allData(readLength, 0);
    uint32_t totalRead = 0;
    const uint32_t chunkSize = flashCfg.sectorSize;

    for (uint32_t offset = 0; offset < readLength; offset += chunkSize) {
        uint32_t remaining = readLength - offset;
        uint32_t thisChunk = std::min(remaining, chunkSize);
        uint32_t bytesRead = 0;
        tas_return_et r = client.read(readAddr + offset, allData.data() + offset, thisChunk, &bytesRead);
        if (r != TAS_ERR_NONE && r != TAS_ERR_RW_READ) {
            fprintf(stderr, "ERROR: Read failed at 0x%08X: %s\n", readAddr + offset, client.get_error_info());
            return 18;
        }
        totalRead += bytesRead;
        printf("  Read %u/%u bytes\r", totalRead, readLength);
        fflush(stdout);
    }
    printf("\n  Read complete: %u bytes\n", totalRead);

    // Output
    if (outputFile != nullptr) {
        std::string path(outputFile);
        bool isHex = (path.size() >= 4 && path.compare(path.size() - 4, 4, ".hex") == 0);
        if (isHex) {
            if (!saveToIntelHex(outputFile, readAddr, allData)) return 1;
            printf("  Saved Intel HEX: %s\n", outputFile);
        } else {
            std::ofstream ofs(outputFile, std::ios::binary);
            if (!ofs) { fprintf(stderr, "ERROR: Cannot open '%s'\n", outputFile); return 1; }
            ofs.write(reinterpret_cast<const char*>(allData.data()), totalRead);
            printf("  Saved binary: %s (%u bytes)\n", outputFile, totalRead);
        }
    } else {
        printf("\n");
        hexDumpXxd(allData.data(), totalRead, readAddr);
    }

    return 0;
}

//********************************************************************************************************************
//  doErase - erase DFlash sectors
//********************************************************************************************************************

static int doErase(int argc, char** argv, bool legacyMode)
{
    uint32_t sectorAddr = 0;
    uint32_t numSectors = 0;
    CommonArgs args;
    const char* backupFile = nullptr;
    bool doVerify = false;
    bool noReset = false;
    bool doReset = false;
    bool eraseAll = false;
    bool infoOnly = false;
    bool hasAddr = false;

    if (legacyMode) {
        // Legacy: argv[0] may be "0xAF...", "--all", or "--info"
        if (strcmp(argv[0], "--all") == 0) {
            eraseAll = true;
        } else if (strcmp(argv[0], "--info") == 0) {
            infoOnly = true;
        } else {
            if (argc < 2) { printf("ERROR: Missing num_sectors\n"); return 1; }
            if (!parseHexAddr(argv[0], sectorAddr)) { printf("ERROR: Invalid address\n"); return 1; }
            if (sscanf(argv[1], "%u", &numSectors) != 1 || numSectors == 0) { printf("ERROR: Invalid sectors\n"); return 1; }
            hasAddr = true;
        }
        // Parse options from appropriate offset
        int startIdx = (hasAddr) ? 2 : 1;
        for (int i = startIdx; i < argc; ) {
            int consumed = parseCommonOption(argc, argv, i, args);
            if (consumed > 0) { i += consumed; continue; }
            if (strcmp(argv[i], "--verify") == 0) { doVerify = true; i++; continue; }
            if (strcmp(argv[i], "--backup") == 0 && i + 1 < argc) { backupFile = argv[++i]; i++; continue; }
            if (strcmp(argv[i], "--no-reset") == 0) { noReset = true; i++; continue; }
            if (strcmp(argv[i], "--reset") == 0) { doReset = true; i++; continue; }
            if (strcmp(argv[i], "--all") == 0) { eraseAll = true; i++; continue; }
            if (strcmp(argv[i], "--info") == 0) { infoOnly = true; i++; continue; }
            fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
            return 1;
        }
    } else {
        // Subcommand mode
        for (int i = 0; i < argc; ) {
            int consumed = parseCommonOption(argc, argv, i, args);
            if (consumed > 0) { i += consumed; continue; }
            if (strcmp(argv[i], "--all") == 0) { eraseAll = true; i++; continue; }
            if (strcmp(argv[i], "--info") == 0) { infoOnly = true; i++; continue; }
            if (strcmp(argv[i], "--verify") == 0) { doVerify = true; i++; continue; }
            if (strcmp(argv[i], "--backup") == 0 && i + 1 < argc) { backupFile = argv[++i]; i++; continue; }
            if (strcmp(argv[i], "--no-reset") == 0) { noReset = true; i++; continue; }
            if (strcmp(argv[i], "--reset") == 0) { doReset = true; i++; continue; }
            if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
                if (!parseHexAddr(argv[++i], sectorAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return 1; }
                hasAddr = true; i++; continue;
            }
            if (strcmp(argv[i], "--sectors") == 0 && i + 1 < argc) {
                if (sscanf(argv[++i], "%u", &numSectors) != 1 || numSectors == 0) { fprintf(stderr, "ERROR: Invalid sectors\n"); return 1; }
                i++; continue;
            }
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                printf("Usage: tas_dflash_erase erase [--addr <hex> --sectors <n> | --all] [options]\n"
                       "  --addr <hex>       DFlash start address, auto-aligned to sector\n"
                       "  --sectors <n>      Number of sectors to erase\n"
                       "  --all              Erase entire DFlash (requires confirmation)\n"
                       "  --info             Show device info only, no erase\n"
                       "  --verify           Verify erase by reading back\n"
                       "  --backup <file>    Backup DFlash before erasing\n"
                       "  --reset            Reset MCU after erase\n"
                       "  --no-reset         Hot attach (no device reset)\n"
                       "  --server <ip>      TAS server IP (default: localhost)\n"
                       "  --target <id>      Target identifier\n");
                return 0;
            }
            fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!infoOnly && !eraseAll && !hasAddr) {
        fprintf(stderr, "ERROR: Specify --addr + --sectors, --all, or --info\n");
        return 1;
    }
    if (hasAddr && (sectorAddr < DFLASH_START_ADDR || sectorAddr > 0xAF0FFFFFu)) {
        fprintf(stderr, "ERROR: Address 0x%08X out of DFlash range\n", sectorAddr);
        return 1;
    }

    printf("TAS DFlash Erase Tool\n");
    printf("=====================\n\n");

    // ---- Connect to server ----
    printf("Connecting to TAS server at %s...\n", args.serverIp);
    CTasClientRw client("DFlashErase");

    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 2; }

    const tas_server_info_st* serverInfo = client.get_server_info();
    printf("  Server: %s V%d.%d (%s)\n",
           serverInfo->server_name, serverInfo->v_major, serverInfo->v_minor, serverInfo->date);

    // ---- Detect targets ----
    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 3; }

    printf("  Targets: %u\n", numTargets);
    for (uint32_t i = 0; i < numTargets; i++) {
        printf("  [%u] %s (%s)\n", i, tas_get_device_name_str(targets[i].device_type), targets[i].identifier);
    }

    const char* selectedTargetId = (args.targetId != nullptr) ? args.targetId
                                 : (numTargets > 0) ? targets[0].identifier : nullptr;
    if (!selectedTargetId) { fprintf(stderr, "ERROR: No targets\n"); return 4; }
    if (args.targetId == nullptr) printf("  Auto-selected target [0]\n");

    printf("\nStarting session...\n");
    ret = client.session_start(selectedTargetId, "DFlashEraseSession");
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 5; }

    // ---- Connect to device ----
    tas_clnt_dco_et dco = noReset ? TAS_CLNT_DCO_HOT_ATTACH : TAS_CLNT_DCO_RESET_AND_HALT;
    printf("Connecting to device (%s)...\n", noReset ? "hot attach" : "reset and halt");
    ret = client.device_connect(dco);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return 6; }

    // ---- Detect MCU ----
    const tas_con_info_st* conInfo = client.get_con_info();
    DFlashConfig flashCfg;
    if (!getDFlashConfig(conInfo->device_type, flashCfg)) {
        fprintf(stderr, "ERROR: Unsupported device '%s'. Supported: TC23x, TC26x, TC27x, TC3x.\n",
               tas_get_device_name_str(conInfo->device_type));
        return 7;
    }

    const char* deviceName = tas_get_device_name_str(conInfo->device_type);
    uint32_t dflashEndAddr = DFLASH_START_ADDR + flashCfg.totalSize - 1;
    uint32_t totalSectors = flashCfg.totalSize / flashCfg.sectorSize;

    printf("  Device: %s (%s)\n", deviceName, flashCfg.family);
    printf("  DFlash: %u KB total, %u sectors, sector size %u KB\n",
           flashCfg.totalSize / 1024, totalSectors, flashCfg.sectorSize / 1024);
    printf("  Range:  0x%08X - 0x%08X\n", DFLASH_START_ADDR, dflashEndAddr);

    // ---- --info mode: just print info and exit ----
    if (infoOnly) {
        printf("\nDevice info displayed. No erase performed.\n");
        return 0;
    }

    // ---- --all mode ----
    if (eraseAll) {
        sectorAddr = DFLASH_START_ADDR;
        numSectors = totalSectors;

        printf("\n  *** WARNING: Will erase ENTIRE DFlash (%u KB, %u sectors) ***\n",
               flashCfg.totalSize / 1024, numSectors);
        printf("  Type 'yes' to confirm: ");
        fflush(stdout);

        char confirm[16] = {0};
        if (fgets(confirm, sizeof(confirm), stdin) == nullptr || strncmp(confirm, "yes", 3) != 0) {
            printf("  Aborted.\n");
            return 0;
        }
    }

    // ---- Auto-align address ----
    uint32_t alignedAddr = sectorAddr & ~(flashCfg.sectorSize - 1);
    if (alignedAddr != sectorAddr) {
        printf("  Address aligned: 0x%08X -> 0x%08X (%u KB boundary)\n",
               sectorAddr, alignedAddr, flashCfg.sectorSize / 1024);
        sectorAddr = alignedAddr;
    }

    // ---- Validate range ----
    if (sectorAddr < DFLASH_START_ADDR || sectorAddr > dflashEndAddr) {
        fprintf(stderr, "ERROR: Address 0x%08X out of range (0x%08X - 0x%08X)\n",
               sectorAddr, DFLASH_START_ADDR, dflashEndAddr);
        return 1;
    }
    if (sectorAddr + numSectors * flashCfg.sectorSize - 1 > dflashEndAddr) {
        fprintf(stderr, "ERROR: Range exceeds DFlash boundary (max 0x%08X)\n", dflashEndAddr);
        return 1;
    }

    // ---- Backup ----
    if (backupFile != nullptr) {
        printf("\nBackup:\n");
        if (!backupDFlash(client, sectorAddr, numSectors * flashCfg.sectorSize, flashCfg.sectorSize, backupFile)) return 8;
    }

    // ---- Print erase parameters ----
    printf("\nErasing DFlash:\n");
    printf("  Address:  0x%08X\n", sectorAddr);
    printf("  Sectors:  %u (%u KB)\n\n", numSectors, numSectors * flashCfg.sectorSize / 1024);

    // ---- Step 1: Read password ----
    printf("Step 1/8: Reading Safety Watchdog password...     ");
    uint16_t password, relValue;
    if (!getSafetyWatchdogPassword(client, password, relValue)) { printf("FAILED\n"); return 9; }
    printf("OK\n");

    // ---- Step 2: Clear EndInit ----
    printf("Step 2/8: Clearing Safety EndInit...              ");
    if (!clearSafetyEndInit(client, password, relValue)) { printf("FAILED\n"); return 10; }
    printf("OK\n");

    // ---- Step 3: Clear status ----
    printf("Step 3/8: Clearing flash status...                ");
    if (!clearFlashStatus(client)) {
        printf("FAILED\n");
        getSafetyWatchdogPassword(client, password, relValue);
        setSafetyEndInit(client, password, relValue);
        return 11;
    }
    printf("OK\n");

    // ---- Step 4: Erase (atomic) ----
    printf("Step 4/8: Executing erase command...              ");
    if (!eraseMultipleSectors(client, sectorAddr, numSectors)) {
        printf("FAILED\n");
        getSafetyWatchdogPassword(client, password, relValue);
        setSafetyEndInit(client, password, relValue);
        return 12;
    }
    printf("OK\n");

    // ---- Step 5: Restore EndInit ----
    printf("Step 5/8: Restoring Safety EndInit...             ");
    getSafetyWatchdogPassword(client, password, relValue);
    if (!setSafetyEndInit(client, password, relValue)) { printf("FAILED\n"); return 13; }
    printf("OK\n");

    // ---- Step 6: Wait unbusy ----
    printf("Step 6/8: Waiting for erase to complete...        ");
    uint32_t elapsedMs = 0;
    if (!waitUnbusyD0(client, flashCfg.isTc3x, elapsedMs)) { printf("TIMEOUT\n"); return 14; }
    printf("OK (%u ms)\n", elapsedMs);

    // ---- Step 7: Check error flags ----
    printf("Step 7/8: Checking error flags...                 ");
    if (!checkEraseErrors(client, flashCfg.isTc3x)) { return 15; }
    printf("OK\n");

    // ---- Step 8: Reset to read ----
    printf("Step 8/8: Reset to read mode...                   ");
    if (!resetToRead(client)) { printf("FAILED\n"); return 16; }
    printf("OK\n");

    // ---- Verify ----
    if (doVerify) {
        printf("\nVerifying erase:\n");
        if (!verifyErase(client, sectorAddr, numSectors, flashCfg.sectorSize)) {
            printf("\nDFlash erase completed with verification ERRORS.\n");
            return 17;
        }
    }

    // ---- Reset MCU ----
    if (doReset) {
        printf("\nResetting MCU...\n");
        tas_return_et r = client.device_connect(TAS_CLNT_DCO_RESET);
        if (r != TAS_ERR_NONE) {
            printf("  WARN: Reset failed: %s\n", client.get_error_info());
        } else {
            printf("  MCU reset. Normal execution resumed.\n");
        }
    }

    printf("\nDFlash erase completed successfully.\n");
    return 0;
}

//********************************************************************************************************************
//  Usage
//********************************************************************************************************************

static void printUsage(const char* progName)
{
    printf("TAS DFlash Tool for AURIX TC23x/TC26x/TC27x/TC3x\n\n");
    printf("Usage:\n");
    printf("  %s <subcommand> [options]\n\n", progName);
    printf("Subcommands:\n");
    printf("  erase    Erase DFlash sectors\n");
    printf("  read     Read DFlash content\n");
    printf("  list     List connected TAS targets\n\n");
    printf("Legacy (backward compatible):\n");
    printf("  %s <addr> <num_sectors> [options]\n", progName);
    printf("  %s --all [options]\n", progName);
    printf("  %s --info [options]\n\n", progName);
    printf("Run '%s <subcommand> --help' for details.\n\n", progName);
    printf("Supported devices:\n");
    printf("  TC23x/TC26x:  sector=8KB, total=128KB | TC27x: sector=8KB, total=256KB\n");
    printf("  TC33x/TC35x/TC36x: 4KB/128KB | TC37x: 4KB/256KB | TC38x: 4KB/512KB | TC39x: 4KB/1MB\n");
}

//********************************************************************************************************************
//  Main - subcommand dispatch with legacy backward compatibility
//********************************************************************************************************************

int main(int argc, char** argv)
{
    if (argc < 2) { printUsage(argv[0]); return 1; }

    std::string first(argv[1]);

    // Help
    if (first == "--help" || first == "-h") { printUsage(argv[0]); return 0; }

    // Subcommands
    if (first == "erase" || first == "e") return doErase(argc - 2, argv + 2, false);
    if (first == "read"  || first == "r") return doRead(argc - 2, argv + 2);
    if (first == "list"  || first == "l") return doList(argc - 2, argv + 2);

    // Legacy backward compatibility: --all, --info, or hex address as first arg
    if (first == "--all" || first == "--info" ||
        first.rfind("0x", 0) == 0 || first.rfind("0X", 0) == 0) {
        return doErase(argc - 1, argv + 1, true);
    }

    fprintf(stderr, "ERROR: Unknown subcommand '%s'\n", argv[1]);
    printUsage(argv[0]);
    return 1;
}
