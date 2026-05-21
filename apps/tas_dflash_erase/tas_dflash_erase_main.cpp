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
//  TAS DFlash Erase Tool for AURIX TC23x/TC26x/TC27x/TC3x
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

//********************************************************************************************************************
//  DFlash configuration per device type
//********************************************************************************************************************

struct DFlashConfig {
    uint32_t sectorSize;
    uint32_t totalSize;
    const char* family;
};

static bool getDFlashConfig(uint32_t deviceType, DFlashConfig& cfg)
{
    uint32_t dt = deviceType & TAS_DT_VERSION_MASK_OUT;

    // TC2x: TC23x, TC26x, TC27x - sector = 8 KB
    cfg.sectorSize = 0x2000u;
    cfg.family     = "TC2x";
    switch (dt) {
    case TAS_DT_TC23X:
        cfg.totalSize = 0x20000u; return true;
    case TAS_DT_TC26X:
        cfg.totalSize = 0x20000u; return true;
    case TAS_DT_TC27X:
        cfg.totalSize = 0x40000u; return true;
    default:
        break;
    }

    // TC3x - sector = 4 KB
    if (tas_df_check_if_tc3x(deviceType)) {
        cfg.sectorSize = 0x1000u;
        cfg.family     = "TC3x";
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

// DMU_HF_STATUS bits
static constexpr uint64_t DMU_HF_STATUS_ADDR     = 0xF8040010ULL;
static constexpr uint32_t D0BUSY_BIT             = (1u << 0);
static constexpr uint32_t PVER_BIT               = (1u << 6);  // Program Verify Error
static constexpr uint32_t EVER_BIT               = (1u << 7);  // Erase Verify Error
static constexpr uint32_t PROER_BIT              = (1u << 10); // Protection Error
static constexpr uint32_t SEQER_BIT              = (1u << 12); // Sequence Error
static constexpr uint32_t OPERR_MASK             = (PVER_BIT | EVER_BIT | PROER_BIT | SEQER_BIT);

// Flash command registers
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

static bool eraseMultipleSectors(CTasClientRw& client, uint32_t sectorAddr, uint32_t numSectors)
{
    if (writeReg32(client, FLASH_CMD_SECTOR_ADDR, sectorAddr) != TAS_ERR_NONE) return false;
    if (writeReg32(client, FLASH_CMD_SECTOR_COUNT, numSectors) != TAS_ERR_NONE) return false;
    if (writeReg32(client, FLASH_CMD_EXECUTE, FLASH_VAL_ERASE_CMD1) != TAS_ERR_NONE) return false;
    if (writeReg32(client, FLASH_CMD_EXECUTE, FLASH_VAL_ERASE_CMD2) != TAS_ERR_NONE) return false;
    return true;
}

static bool waitUnbusyD0(CTasClientRw& client, uint32_t& elapsedMs)
{
    auto startTime = std::chrono::steady_clock::now();
    while (true) {
        uint32_t status;
        if (readReg32(client, DMU_HF_STATUS_ADDR, status) != TAS_ERR_NONE) return false;
        if (!(status & D0BUSY_BIT)) {
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

static bool checkEraseErrors(CTasClientRw& client)
{
    uint32_t status;
    if (readReg32(client, DMU_HF_STATUS_ADDR, status) != TAS_ERR_NONE) {
        printf("  WARN: Could not read status register\n");
        return true; // don't fail if we can't read
    }

    bool ok = true;
    if (status & PVER_BIT) {  printf("  ERROR: Program Verify Error (PVER)\n"); ok = false; }
    if (status & EVER_BIT) {  printf("  ERROR: Erase Verify Error (EVER)\n");    ok = false; }
    if (status & PROER_BIT) { printf("  ERROR: Protection Error (PROER)\n");      ok = false; }
    if (status & SEQER_BIT) { printf("  ERROR: Sequence Error (SEQER)\n");         ok = false; }

    if (ok) {
        printf("  No error flags set.\n");
    }
    return ok;
}

//********************************************************************************************************************
//  Verify erased content
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
        if (client.read(startAddr + offset, buf.data(), chunkSize, &bytesRead) != TAS_ERR_NONE) {
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
//  Backup DFlash to file
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
        if (client.read(startAddr + offset, buf.data(), chunkSize, &bytesRead) != TAS_ERR_NONE) {
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
//  Usage
//********************************************************************************************************************

static void printUsage(const char* progName)
{
    printf("TAS DFlash Erase Tool for AURIX TC23x/TC26x/TC27x/TC3x\n\n");
    printf("Usage:\n");
    printf("  %s <addr> <num_sectors> [options]\n\n", progName);
    printf("Commands:\n");
    printf("  --info                    Show device info only, no erase\n");
    printf("  --all                     Erase entire DFlash (requires confirmation)\n\n");
    printf("Arguments:\n");
    printf("  addr                      DFlash address in hex, auto-aligned to sector\n");
    printf("  num_sectors               Number of sectors to erase\n\n");
    printf("Options:\n");
    printf("  --server <ip>             TAS server IP (default: localhost)\n");
    printf("  --target <id>             Target identifier (default: first available)\n");
    printf("  --verify                  Verify erase by reading back\n");
    printf("  --backup <file>           Backup DFlash to binary file before erasing\n");
    printf("  --no-reset                Hot attach (no device reset)\n\n");
    printf("Examples:\n");
    printf("  %s --info\n", progName);
    printf("  %s --all --verify\n", progName);
    printf("  %s 0xAF000F10 1\n", progName);
    printf("  %s 0xAF000000 2 --verify --backup dump.bin\n", progName);
    printf("\nSupported devices:\n");
    printf("  TC23x/TC26x:  sector=8KB, total=128KB | TC27x: sector=8KB, total=256KB\n");
    printf("  TC33x/TC35x/TC36x: 4KB/128KB | TC37x: 4KB/256KB | TC38x: 4KB/512KB | TC39x: 4KB/1MB\n");
}

//********************************************************************************************************************
//  Main
//********************************************************************************************************************

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    uint32_t sectorAddr = 0;
    uint32_t numSectors = 0;
    const char* serverIp = "localhost";
    const char* targetId = nullptr;
    const char* backupFile = nullptr;
    bool doVerify = false;
    bool noReset = false;
    bool eraseAll = false;
    bool infoOnly = false;

    // First pass: detect modes
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) eraseAll = true;
        if (strcmp(argv[i], "--info") == 0) infoOnly = true;
    }

    // --info mode
    if (infoOnly) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--info") == 0) { /* handled */ }
            else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) serverIp = argv[++i];
            else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) targetId = argv[++i];
            else if (strcmp(argv[i], "--no-reset") == 0) noReset = true;
            else { printf("ERROR: Unknown option '%s'\n", argv[i]); return 1; }
        }
    }
    // --all mode
    else if (eraseAll) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--all") == 0) { /* handled */ }
            else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) serverIp = argv[++i];
            else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) targetId = argv[++i];
            else if (strcmp(argv[i], "--verify") == 0) doVerify = true;
            else if (strcmp(argv[i], "--backup") == 0 && i + 1 < argc) backupFile = argv[++i];
            else if (strcmp(argv[i], "--no-reset") == 0) noReset = true;
            else { printf("ERROR: Unknown option '%s'\n", argv[i]); return 1; }
        }
    }
    // Normal mode: addr + num_sectors
    else {
        if (argc < 3) { printUsage(argv[0]); return 1; }
        if (sscanf(argv[1], "%x", &sectorAddr) != 1) { printf("ERROR: Invalid address\n"); return 1; }
        if (sscanf(argv[2], "%u", &numSectors) != 1 || numSectors == 0) { printf("ERROR: Invalid sectors\n"); return 1; }

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) serverIp = argv[++i];
            else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) targetId = argv[++i];
            else if (strcmp(argv[i], "--verify") == 0) doVerify = true;
            else if (strcmp(argv[i], "--backup") == 0 && i + 1 < argc) backupFile = argv[++i];
            else if (strcmp(argv[i], "--no-reset") == 0) noReset = true;
            else { printf("ERROR: Unknown option '%s'\n", argv[i]); return 1; }
        }

        if (sectorAddr < DFLASH_START_ADDR || sectorAddr > 0xAF0FFFFFu) {
            printf("ERROR: Address 0x%08X out of DFlash range\n", sectorAddr);
            return 1;
        }
    }

    printf("TAS DFlash Erase Tool\n");
    printf("=====================\n\n");

    // ---- Connect to server ----
    printf("Connecting to TAS server at %s...\n", serverIp);
    CTasClientRw client("DFlashErase");

    tas_return_et ret = client.server_connect(serverIp);
    if (ret != TAS_ERR_NONE) { printf("ERROR: %s\n", client.get_error_info()); return 2; }

    const tas_server_info_st* serverInfo = client.get_server_info();
    printf("  Server: %s V%d.%d (%s)\n",
           serverInfo->server_name, serverInfo->v_major, serverInfo->v_minor, serverInfo->date);

    // ---- Detect targets ----
    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { printf("ERROR: %s\n", client.get_error_info()); return 3; }

    printf("  Targets: %u\n", numTargets);
    for (uint32_t i = 0; i < numTargets; i++) {
        printf("  [%u] %s (%s)\n", i, tas_get_device_name_str(targets[i].device_type), targets[i].identifier);
    }

    const char* selectedTargetId = (targetId != nullptr) ? targetId
                                 : (numTargets > 0) ? targets[0].identifier : nullptr;
    if (!selectedTargetId) { printf("ERROR: No targets\n"); return 4; }
    if (targetId == nullptr) printf("  Auto-selected target [0]\n");

    printf("\nStarting session...\n");
    ret = client.session_start(selectedTargetId, "DFlashEraseSession");
    if (ret != TAS_ERR_NONE) { printf("ERROR: %s\n", client.get_error_info()); return 5; }

    // ---- Connect to device ----
    tas_clnt_dco_et dco = noReset ? TAS_CLNT_DCO_HOT_ATTACH : TAS_CLNT_DCO_RESET_AND_HALT;
    printf("Connecting to device (%s)...\n", noReset ? "hot attach" : "reset and halt");
    ret = client.device_connect(dco);
    if (ret != TAS_ERR_NONE) { printf("ERROR: %s\n", client.get_error_info()); return 6; }

    // ---- Detect MCU ----
    const tas_con_info_st* conInfo = client.get_con_info();
    DFlashConfig flashCfg;
    if (!getDFlashConfig(conInfo->device_type, flashCfg)) {
        printf("ERROR: Unsupported device '%s'. Supported: TC23x, TC26x, TC27x, TC3x.\n",
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

        // Confirmation prompt
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
        printf("ERROR: Address 0x%08X out of range (0x%08X - 0x%08X)\n",
               sectorAddr, DFLASH_START_ADDR, dflashEndAddr);
        return 1;
    }
    if (sectorAddr + numSectors * flashCfg.sectorSize - 1 > dflashEndAddr) {
        printf("ERROR: Range exceeds DFlash boundary (max 0x%08X)\n", dflashEndAddr);
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

    // ---- Step 4: Erase ----
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
    if (!waitUnbusyD0(client, elapsedMs)) { printf("TIMEOUT\n"); return 14; }
    printf("OK (%u ms)\n", elapsedMs);

    // ---- Step 7: Check error flags ----
    printf("Step 7/8: Checking error flags...                 ");
    if (!checkEraseErrors(client)) { return 15; }
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

    printf("\nDFlash erase completed successfully.\n");
    return 0;
}
