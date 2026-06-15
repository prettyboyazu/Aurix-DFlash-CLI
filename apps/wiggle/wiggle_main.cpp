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
//  TAS Wiggle Tool for AURIX
//  Subcommands: erase, read, write, rewrite, restore, list, reset, ucb
//  Device configurations are loaded at runtime from JSON files (DeviceConfigs/).
//********************************************************************************************************************

// Prevent Windows min/max macros from interfering with std::min/std::max
#define NOMINMAX

#include "tas_client_rw.h"
#include "tas_utils.h"
#include "tas_device_family.h"
#include "tas_utils_client.h"  // BRKIN/BRKOUT user pins API

#include "device_config_loader.h"
#include "hex_parser.h"
#include "svd_loader.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cctype>

// nlohmann/json for --json output mode
#include "nlohmann/json.hpp"
using nljson = nlohmann::json;

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

//********************************************************************************************************************
//  AURIX Register Addresses and Constants
//********************************************************************************************************************

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

// Flash command register offsets (relative to DFlash base address - same for TC2x/TC3x).
static constexpr uint64_t FLASH_CMD_OFFSET_CLEAR_STATUS  = 0x5554;
static constexpr uint64_t FLASH_CMD_OFFSET_SECTOR_ADDR   = 0xAA50;
static constexpr uint64_t FLASH_CMD_OFFSET_SECTOR_COUNT  = 0xAA58;
static constexpr uint64_t FLASH_CMD_OFFSET_EXECUTE       = 0xAAA8;
static constexpr uint64_t FLASH_CMD_OFFSET_RESET_READ    = 0x5554;

static constexpr uint32_t FLASH_VAL_CLEAR_STATUS  = 0xFA;
static constexpr uint32_t FLASH_VAL_ERASE_CMD1    = 0x80;
static constexpr uint32_t FLASH_VAL_ERASE_CMD2    = 0x50;
static constexpr uint32_t FLASH_VAL_RESET_READ    = 0xF0;

// Flash page programming constants
static constexpr uint64_t FLASH_CMD_OFFSET_LOAD_PAGE_L = 0x55F0;  // Assembly Buffer low word
static constexpr uint64_t FLASH_CMD_OFFSET_LOAD_PAGE_U = 0x55F4;  // Assembly Buffer high word
static constexpr uint32_t FLASH_VAL_PAGE_MODE_DFLASH   = 0x5D;    // Enter DFlash page mode
static constexpr uint32_t FLASH_VAL_WRITE_CMD1         = 0xA0;    // Write trigger
static constexpr uint32_t FLASH_VAL_WRITE_CMD2         = 0xAA;    // Write confirm
static constexpr uint32_t DFLASH_PAGE_SIZE             = 8;        // DFlash page size in bytes

static constexpr uint32_t DFLASH_ERASED_BYTE      = 0x00u;

// Tool version
static const char* WIGGLE_VERSION = "2.2";

//********************************************************************************************************************
//  Exit/error codes (consistent across all subcommands)
//********************************************************************************************************************

static constexpr int EXIT_OK                 = 0;
static constexpr int EXIT_USAGE_ERROR        = 1;   // Bad arguments / usage
static constexpr int EXIT_SERVER_ERROR       = 2;   // Cannot connect to TAS server
static constexpr int EXIT_TARGET_ERROR       = 3;   // Cannot query targets
static constexpr int EXIT_NO_TARGET          = 4;   // No targets found
static constexpr int EXIT_SESSION_ERROR      = 5;   // Session start failed
static constexpr int EXIT_CONNECT_ERROR      = 6;   // Device connect failed
static constexpr int EXIT_DEVICE_ERROR       = 7;   // Device not supported / not found in config
static constexpr int EXIT_BACKUP_ERROR       = 8;   // Backup failed
static constexpr int EXIT_FLASH_STATUS_ERROR = 11;  // Flash status clear failed
static constexpr int EXIT_ERASE_CMD_ERROR    = 12;  // Erase command failed
static constexpr int EXIT_ERASE_TIMEOUT      = 14;  // Erase timeout
static constexpr int EXIT_ERASE_FLAGS        = 15;  // Error flags detected
static constexpr int EXIT_FLASH_RESET_ERROR  = 16;  // Reset to read mode failed
static constexpr int EXIT_VERIFY_ERROR       = 17;  // Verification failed
static constexpr int EXIT_IO_ERROR           = 18;  // File I/O error
static constexpr int EXIT_WRITE_CMD_ERROR    = 19;  // Write page command failed
static constexpr int EXIT_WRITE_VERIFY_ERROR = 20;  // Write verification failed
static constexpr int EXIT_HEX_PARSE_ERROR    = 21;  // HEX file parse error
static constexpr int EXIT_MISMATCH_ERROR     = 22;  // Compare found mismatches
static constexpr int EXIT_NOT_FOUND          = 23;  // Search pattern not found

static constexpr uint32_t WAIT_UNBUSY_TIMEOUT_MS  = 10000;
static constexpr uint32_t POLL_INTERVAL_MS         = 10;

//********************************************************************************************************************
//  Global JSON output mode flag (set from CommonArgs after parsing)
//********************************************************************************************************************

static bool g_json = false;

// JSON helper: print JSON to stdout and return exit code
static int jsonOk(const nljson& data) {
    nljson out = data;
    out["status"] = "ok";
    printf("%s\n", out.dump().c_str());
    return EXIT_OK;
}

static int jsonError(int code, const std::string& msg) {
    nljson out;
    out["status"] = "error";
    out["code"] = code;
    out["message"] = msg;
    printf("%s\n", out.dump().c_str());
    return code;
}

// Conditional printf: only prints when NOT in JSON mode
#define JPRINTF(...) do { if (!g_json) printf(__VA_ARGS__); } while(0)

//********************************************************************************************************************
//  TC4x rejection helper (TC4x has different DFlash base addresses and erase command sequence)
//********************************************************************************************************************

// Check if device is unsupported TC4x, print error and return EXIT_DEVICE_ERROR if so.
// Returns EXIT_OK if device is OK (not TC4x), or EXIT_DEVICE_ERROR if TC4x detected.
static int rejectTc4xDevice(const tas_con_info_st* conInfo)
{
    if (tas_df_check_if_tc4x(conInfo->device_type)) {
        if (g_json) {
            return EXIT_DEVICE_ERROR; // caller will use jsonError
        }
        fprintf(stderr, "ERROR: TC4x devices are not supported by this tool.\n");
        fprintf(stderr, "  Detected: %s (jtag_id=0x%08X)\n",
               tas_get_device_name_str(conInfo->device_type), conInfo->device_type);
        fprintf(stderr, "  TC4x uses different DFlash base addresses (0xAE000000/0xAC000000)\n");
        fprintf(stderr, "  and requires a different erase command sequence.\n");
        return EXIT_DEVICE_ERROR;
    }
    return EXIT_OK;
}

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
//  UCB Locked Regions (chip-level protection - cannot be erased or written)
//********************************************************************************************************************

struct LockedRegion { uint32_t start; uint32_t end; const char* name; };
static const LockedRegion lockedRegions[] = {
    { 0xAF400800, 0xAF400FFF, "UCB security (OTP/DFLASH/DBG/HSM)" },
    { 0xAF401800, 0xAF401FFF, "UCB security (reserved)" },
};

//********************************************************************************************************************
//  DeviceConfigs directory resolution
//********************************************************************************************************************

static std::string findConfigDir(const char* userSpecified)
{
    namespace fs = std::filesystem;

    // 1. User specified via --config-dir
    if (userSpecified && userSpecified[0]) {
        std::error_code ec;
        if (fs::is_directory(userSpecified, ec)) return userSpecified;
        // Even if it doesn't exist, return it so the caller can report a clear error.
        return userSpecified;
    }

#ifdef _WIN32
    // 2. exe-side DeviceConfigs/ (use wide string to support CJK paths)
    wchar_t exePathW[4096] = {0};
    DWORD len = GetModuleFileNameW(nullptr, exePathW, _countof(exePathW));
    if (len > 0 && len < _countof(exePathW)) {
        fs::path exeDir = fs::path(exePathW).parent_path();

        std::error_code ec;
        // 2a. exe/DeviceConfigs/
        fs::path candidate = exeDir / "DeviceConfigs";
        if (fs::is_directory(candidate, ec)) return candidate.string();

        // 2b. exe/../data/DeviceConfigs/  (developer layout)
        candidate = exeDir / ".." / "data" / "DeviceConfigs";
        if (fs::is_directory(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();

        // 2c. exe/../../../data/DeviceConfigs/  (build/<cfg>/<arch>/ layout)
        candidate = exeDir / ".." / ".." / ".." / "data" / "DeviceConfigs";
        if (fs::is_directory(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();

        // 2d. exe/../../../../data/DeviceConfigs/  (extra-deep build layouts)
        candidate = exeDir / ".." / ".." / ".." / ".." / "data" / "DeviceConfigs";
        if (fs::is_directory(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();
    }
#elif defined(__linux__)
    // Linux: resolve exe path via /proc/self/exe
    // Note: macOS would use _NSGetExecutablePath(), FreeBSD uses /proc/curproc/file
    // Currently only Linux is supported for non-Windows platforms.
    std::error_code ec;
    fs::path exeLink = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        fs::path exeDir = exeLink.parent_path();

        // exe/DeviceConfigs/
        fs::path candidate = exeDir / "DeviceConfigs";
        if (fs::is_directory(candidate, ec)) return candidate.string();

        // exe/../data/DeviceConfigs/ (developer layout)
        candidate = exeDir / ".." / "data" / "DeviceConfigs";
        if (fs::is_directory(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();

        // exe/../../../data/DeviceConfigs/ (build layout)
        candidate = exeDir / ".." / ".." / ".." / "data" / "DeviceConfigs";
        if (fs::is_directory(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();
    }
#endif

    // 3. Environment variable TAS_DEVICE_CONFIGS
    const char* envDir = std::getenv("TAS_DEVICE_CONFIGS");
    if (envDir && envDir[0]) {
        std::error_code ec;
        if (std::filesystem::is_directory(envDir, ec)) return envDir;
    }

    // 4. Current working directory fallback
    std::error_code ec;
    if (std::filesystem::is_directory("DeviceConfigs", ec)) return "DeviceConfigs";

    return "";
}

//********************************************************************************************************************
//  Session start with auto-reconnect to existing session
//********************************************************************************************************************

static tas_return_et startSession(CTasClientRw& client, const char* targetId, const char* defaultSessionName)
{
#ifdef _WIN32
    std::string sessionName = std::string(defaultSessionName) + "_" + std::to_string(GetCurrentProcessId());
#else
    std::string sessionName = std::string(defaultSessionName) + "_" + std::to_string(getpid());
#endif

    // TAS protocol limits session_name to TAS_NAME_LEN16 (=16) bytes including NUL terminator.
    // Exceeding this triggers /GS stack-buffer-overrun (0xC0000409) inside the TAS client packet
    // handler in Release builds. Truncate to 15 chars to stay safely within the limit.
    constexpr size_t kMaxSessionNameLen = 16;  // TAS_NAME_LEN16 from src/tas_client/tas_pkt.h
    if (sessionName.size() >= kMaxSessionNameLen) {
        sessionName.resize(kMaxSessionNameLen - 1);
    }

    // Try with our own session name first
    tas_return_et ret = client.session_start(targetId, sessionName.c_str());
    if (ret == TAS_ERR_NONE) return TAS_ERR_NONE;

    // Failed — check if an existing session is running and reconnect with its name
    const char* existingSessionName = nullptr;
    uint64_t sessionStartTime = 0;
    const tas_target_client_info_st* clientInfo = nullptr;
    uint32_t numClients = 0;
    tas_return_et qr = client.get_target_clients(targetId, &existingSessionName, &sessionStartTime, &clientInfo, &numClients);
    if (qr == TAS_ERR_NONE && existingSessionName && existingSessionName[0] != '\0') {
        printf("  Reconnecting to existing session '%s'...\n", existingSessionName);
        printf("  WARNING: Taking over an existing session. Other tools using this session may be affected.\n");
        return client.session_start(targetId, existingSessionName);
    }

    // Last resort: try with empty session name (join any existing session)
    ret = client.session_start(targetId, "");
    if (ret == TAS_ERR_NONE) return TAS_ERR_NONE;

    return ret;
}

//********************************************************************************************************************
//  Flash operations (parametrised on DFlash base address)
//********************************************************************************************************************

static bool clearFlashStatus(CTasClientRw& client, uint64_t flashCmdBase)
{
    return writeReg32(client, flashCmdBase | FLASH_CMD_OFFSET_CLEAR_STATUS,
                      FLASH_VAL_CLEAR_STATUS) == TAS_ERR_NONE;
}

// Atomic erase via execute_trans -- CSI requires continuous 4-write sequence
static bool eraseMultipleSectors(CTasClientRw& client, uint64_t flashCmdBase,
                                 uint32_t sectorAddr, uint32_t numSectors)
{
    uint32_t valAddr  = sectorAddr;
    uint32_t valCount = numSectors;
    uint32_t valCmd1  = FLASH_VAL_ERASE_CMD1;
    uint32_t valCmd2  = FLASH_VAL_ERASE_CMD2;

    const uint64_t addrSectorAddr  = flashCmdBase | FLASH_CMD_OFFSET_SECTOR_ADDR;
    const uint64_t addrSectorCount = flashCmdBase | FLASH_CMD_OFFSET_SECTOR_COUNT;
    const uint64_t addrExecute     = flashCmdBase | FLASH_CMD_OFFSET_EXECUTE;

    tas_rw_trans_st trans[4] = {
        { addrSectorAddr,  4, 0, TAS_AM0, TAS_RW_TT_WR, &valAddr  },
        { addrSectorCount, 4, 0, TAS_AM0, TAS_RW_TT_WR, &valCount },
        { addrExecute,     4, 0, TAS_AM0, TAS_RW_TT_WR, &valCmd1  },
        { addrExecute,     4, 0, TAS_AM0, TAS_RW_TT_WR, &valCmd2  },
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

static bool resetToRead(CTasClientRw& client, uint64_t flashCmdBase)
{
    return writeReg32(client, flashCmdBase | FLASH_CMD_OFFSET_RESET_READ,
                      FLASH_VAL_RESET_READ) == TAS_ERR_NONE;
}

// Enter DFlash page programming mode
static bool enterPageMode(CTasClientRw& client, uint64_t flashCmdBase)
{
    return writeReg32(client, flashCmdBase | FLASH_CMD_OFFSET_CLEAR_STATUS,
                      FLASH_VAL_PAGE_MODE_DFLASH) == TAS_ERR_NONE;
}

// Load 8 bytes into Assembly Buffer (two 32-bit writes)
static bool loadPage(CTasClientRw& client, uint64_t flashCmdBase, uint32_t dataLow, uint32_t dataHigh)
{
    if (writeReg32(client, flashCmdBase | FLASH_CMD_OFFSET_LOAD_PAGE_L, dataLow) != TAS_ERR_NONE)
        return false;
    return writeReg32(client, flashCmdBase | FLASH_CMD_OFFSET_LOAD_PAGE_U, dataHigh) == TAS_ERR_NONE;
}

// Execute page write command (atomic 4-word sequence via execute_trans)
static bool writePageCmd(CTasClientRw& client, uint64_t flashCmdBase, uint32_t pageAddr)
{
    uint32_t valAddr  = pageAddr;
    uint32_t valCount = 0x00;  // Sector count = 0 for page mode
    uint32_t valCmd1  = FLASH_VAL_WRITE_CMD1;
    uint32_t valCmd2  = FLASH_VAL_WRITE_CMD2;

    const uint64_t addrPageAddr    = flashCmdBase | FLASH_CMD_OFFSET_SECTOR_ADDR;
    const uint64_t addrSectorCount = flashCmdBase | FLASH_CMD_OFFSET_SECTOR_COUNT;
    const uint64_t addrExecute     = flashCmdBase | FLASH_CMD_OFFSET_EXECUTE;

    tas_rw_trans_st trans[4] = {
        { addrPageAddr,    4, 0, TAS_AM0, TAS_RW_TT_WR, &valAddr  },
        { addrSectorCount, 4, 0, TAS_AM0, TAS_RW_TT_WR, &valCount },
        { addrExecute,     4, 0, TAS_AM0, TAS_RW_TT_WR, &valCmd1  },
        { addrExecute,     4, 0, TAS_AM0, TAS_RW_TT_WR, &valCmd2  },
    };

    return client.execute_trans(trans, 4) == TAS_ERR_NONE;
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
    // Overflow protection (theoretical: DFlash max 1MB, uint32_t max 4GB - always safe)
    const uint32_t totalBytes = static_cast<uint32_t>(static_cast<uint64_t>(numSectors) * sectorSize);
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
//  Forward declarations
//********************************************************************************************************************
static bool saveToIntelHex(const char* filePath, uint32_t baseAddr, const std::vector<uint8_t>& data);

//********************************************************************************************************************
//  Backup DFlash to file (with ECC tolerance)
//********************************************************************************************************************

static bool backupFlash(CTasClientRw& client, uint32_t startAddr, uint32_t totalBytes, uint32_t sectorSize, const char* filePath)
{
    printf("  Backing up %u bytes from 0x%08X to %s...\n", totalBytes, startAddr, filePath);

    // Determine output format based on file extension
    std::string path(filePath);
    std::string ext;
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos && dotPos + 1 < path.size()) {
        ext = path.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    const uint32_t chunkSize = sectorSize;
    std::vector<uint8_t> buf(chunkSize, 0);
    std::vector<uint8_t> allData;
    allData.reserve(totalBytes);
    uint32_t totalRead = 0;

    for (uint32_t offset = 0; offset < totalBytes; offset += chunkSize) {
        uint32_t bytesRead = 0;
        tas_return_et ret = client.read(startAddr + offset, buf.data(), chunkSize, &bytesRead);
        if (ret != TAS_ERR_NONE && ret != TAS_ERR_RW_READ) {
            printf("  ERROR: Read failed at 0x%08X\n", startAddr + offset);
            return false;
        }
        allData.insert(allData.end(), buf.data(), buf.data() + bytesRead);
        totalRead += bytesRead;
    }

    if (ext == "bin") {
        // Raw binary output
        std::ofstream ofs(filePath, std::ios::binary);
        if (!ofs) {
            printf("  ERROR: Cannot open file '%s' for writing\n", filePath);
            return false;
        }
        ofs.write(reinterpret_cast<const char*>(allData.data()), totalRead);
        if (!ofs) {
            printf("  ERROR: Write to file failed\n");
            return false;
        }
        printf("  Backup saved (binary): %u bytes -> %s\n", totalRead, filePath);
    } else {
        // Default: Intel HEX format
        if (!saveToIntelHex(filePath, startAddr, allData)) {
            printf("  ERROR: Failed to write backup file: %s\n", filePath);
            return false;
        }
        printf("  Backup saved (Intel HEX): %u bytes -> %s\n", totalRead, filePath);
    }

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
    // Open in binary mode to keep LF line endings consistent across platforms
    FILE* fp = fopen(filePath, "wb");
    if (!fp) { fprintf(stderr, "ERROR: Cannot open '%s' for writing\n", filePath); return false; }

    uint16_t currentUpper = 0xFFFF; // force first Type 04 record
    size_t offset = 0;
    while (offset < data.size()) {
        uint64_t addr = static_cast<uint64_t>(baseAddr) + offset;
        uint16_t upperAddr = static_cast<uint16_t>(addr >> 16);
        if (upperAddr != currentUpper) {
            uint8_t ua[2] = { static_cast<uint8_t>(upperAddr >> 8),
                              static_cast<uint8_t>(upperAddr & 0xFF) };
            writeIntelHexRecord(fp, 0x04, 0x0000, ua, 2);
            currentUpper = upperAddr;
        }

        uint16_t loAddr = static_cast<uint16_t>(addr & 0xFFFF);
        uint32_t bytesTo64k = 0x10000u - loAddr;
        uint32_t remaining = static_cast<uint32_t>(data.size() - offset);
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
    // Use strtoull + tail check to reject trailing garbage (e.g. "AF000000xyz")
    if (!s || *s == '\0') return false;
    char* end = nullptr;
    unsigned long long v = strtoull(s, &end, 16);
    if (end == s || *end != '\0') return false;
    if (v > 0xFFFFFFFFULL) {
        fprintf(stderr, "ERROR: Address 0x%llX exceeds 32-bit range\n", v);
        return false;
    }
    val = static_cast<uint32_t>(v);
    return true;
}

//********************************************************************************************************************
//  Common argument parsing
//********************************************************************************************************************

struct CommonArgs {
    const char* serverIp = "localhost";
    const char* targetId = nullptr;
    const char* deviceName = nullptr;
    const char* configDir = nullptr;
    bool jsonOutput = false;
};

// --hot CLI option: override default DCO to HOT_ATTACH (no MCU reset)
static bool g_hotAttach = false;

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
    if (strcmp(argv[idx], "--device") == 0 && idx + 1 < argc) {
        args.deviceName = argv[idx + 1];
        return 2;
    }
    if (strcmp(argv[idx], "--config-dir") == 0 && idx + 1 < argc) {
        args.configDir = argv[idx + 1];
        return 2;
    }
    if (strcmp(argv[idx], "--json") == 0) {
        args.jsonOutput = true;
        return 1;
    }
    if (strcmp(argv[idx], "--hot") == 0) {
        g_hotAttach = true;
        return 1;
    }
    return 0;
}

//********************************************************************************************************************
//  Helper: print supported devices loaded from config
//********************************************************************************************************************

static void printSupportedDevices(const DeviceConfigLoader& loader)
{
    auto names = loader.getSupportedDeviceNames();
    if (names.empty()) {
        fprintf(stderr, "  (no device configurations loaded)\n");
        return;
    }
    fprintf(stderr, "  Supported: ");
    for (size_t i = 0; i < names.size(); i++) {
        fprintf(stderr, "%s", names[i].c_str());
        if (i + 1 < names.size()) fprintf(stderr, ", ");
    }
    fprintf(stderr, "\n");
}

//********************************************************************************************************************
//  Helper: load device configs (with consistent error reporting)
//********************************************************************************************************************

static bool loadDeviceConfigs(const CommonArgs& args, DeviceConfigLoader& loader, std::string& outDir)
{
    outDir = findConfigDir(args.configDir);
    if (outDir.empty()) {
        fprintf(stderr, "ERROR: DeviceConfigs directory not found.\n");
        fprintf(stderr, "  Use --config-dir <path> or set TAS_DEVICE_CONFIGS env var.\n");
        return false;
    }
    if (!loader.loadFromDirectory(outDir)) {
        fprintf(stderr, "ERROR: Failed to load device configs from '%s'\n", outDir.c_str());
        return false;
    }
    printf("  Loaded %zu device configurations from %s\n",
           loader.getDeviceCount(), outDir.c_str());
    return true;
}

//********************************************************************************************************************
//  ToolContext and initTool - common initialization for flash operations
//********************************************************************************************************************

struct ToolContext {
    CTasClientRw client;
    DeviceConfig flashCfg;
    DeviceConfigLoader configLoader;
    SvdLoader svd;
    const tas_con_info_st* conInfo;
    std::string configDirPath;
    bool connected = false;

    ToolContext(const char* clientName) : client(clientName), conInfo(nullptr) {}
};

// Shell shared connection: set by doShell, used by RESOLVE_CTX
static ToolContext* g_shellCtx = nullptr;

// RESOLVE_CTX macro: reuse g_shellCtx if available, otherwise initTool with --hot override
#define RESOLVE_CTX(CTX_NAME, SESSION, DEFAULT_DCO) \
    ToolContext ctxLocal(SESSION); \
    ToolContext* ctxPtr = g_shellCtx; \
    if (!ctxPtr) { \
        tas_clnt_dco_et _dco = g_hotAttach ? TAS_CLNT_DCO_HOT_ATTACH : (DEFAULT_DCO); \
        int _rc = initTool(ctxLocal, args, SESSION, _dco); \
        if (_rc != EXIT_OK) { \
            if (g_json) return jsonError(_rc, "initTool failed"); \
            return _rc; \
        } \
        ctxPtr = &ctxLocal; \
    } \
    ToolContext& CTX_NAME = *ctxPtr

// Common initialization: load configs, connect to server, select target, start session,
// connect to device, reject TC4x, and look up device configuration.
// Returns EXIT_OK on success, or an EXIT_* error code on failure.
static int initTool(ToolContext& ctx, const CommonArgs& args,
                    const char* sessionName, tas_clnt_dco_et dco)
{
    // Load device configs
    if (!loadDeviceConfigs(args, ctx.configLoader, ctx.configDirPath))
        return EXIT_USAGE_ERROR;

    // Connect to TAS server
    JPRINTF("Connecting to TAS server at %s...\n", args.serverIp);
    tas_return_et ret = ctx.client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) {
        fprintf(stderr, "ERROR: %s\n", ctx.client.get_error_info());
        return EXIT_SERVER_ERROR;
    }

    const tas_server_info_st* si = ctx.client.get_server_info();
    JPRINTF("  Server: %s V%d.%d (%s)\n", si->server_name, si->v_major, si->v_minor, si->date);

    // Detect targets
    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = ctx.client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) {
        fprintf(stderr, "ERROR: %s\n", ctx.client.get_error_info());
        return EXIT_TARGET_ERROR;
    }
    if (numTargets == 0) {
        fprintf(stderr, "ERROR: No targets found\n");
        return EXIT_NO_TARGET;
    }

    JPRINTF("  Targets: %u\n", numTargets);

    // Select target
    const char* selectedTargetId = (args.targetId != nullptr) ? args.targetId : targets[0].identifier;
    if (args.targetId == nullptr)
        JPRINTF("  Auto-selected target [0]\n");

    // Start session
    JPRINTF("\nStarting session...\n");
    ret = startSession(ctx.client, selectedTargetId, sessionName);
    if (ret != TAS_ERR_NONE) {
        fprintf(stderr, "ERROR: %s\n", ctx.client.get_error_info());
        return EXIT_SESSION_ERROR;
    }

    // Connect to device
    const char* dcoStr = (dco == TAS_CLNT_DCO_HOT_ATTACH) ? "hot attach" : "reset and halt";
    JPRINTF("Connecting to device (%s)...\n", dcoStr);
    ret = ctx.client.device_connect(dco);
    if (ret != TAS_ERR_NONE) {
        fprintf(stderr, "ERROR: %s\n", ctx.client.get_error_info());
        return EXIT_CONNECT_ERROR;
    }

    // TC4x rejection check
    ctx.conInfo = ctx.client.get_con_info();
    int tc4xCheck = rejectTc4xDevice(ctx.conInfo);
    if (tc4xCheck != EXIT_OK) return tc4xCheck;

    // Find device configuration
    if (args.deviceName != nullptr) {
        if (!ctx.configLoader.findByName(args.deviceName, ctx.flashCfg)) {
            fprintf(stderr, "ERROR: Unknown device '%s'\n", args.deviceName);
            printSupportedDevices(ctx.configLoader);
            return EXIT_DEVICE_ERROR;
        }
    } else {
        if (!ctx.configLoader.findByJtagId(ctx.conInfo->device_type, ctx.flashCfg)) {
            fprintf(stderr, "ERROR: Unsupported device '%s' (jtag_id=0x%08X)\n",
                   tas_get_device_name_str(ctx.conInfo->device_type), ctx.conInfo->device_type);
            printSupportedDevices(ctx.configLoader);
            return EXIT_DEVICE_ERROR;
        }
    }

    ctx.connected = true;
    return EXIT_OK;
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
            printf("Usage: wiggle list [options]\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n"
                   "  --json              Output as JSON\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    JPRINTF("TAS Wiggle Tool - Device List\n");
    JPRINTF("=============================\n");
    JPRINTF("Connecting to TAS server at %s...\n", args.serverIp);

    CTasClientRw client("WiggleList");
    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_SERVER_ERROR, client.get_error_info());
        fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SERVER_ERROR;
    }

    const tas_server_info_st* si = client.get_server_info();
    JPRINTF("  Server: %s V%d.%d (%s)\n", si->server_name, si->v_major, si->v_minor, si->date);

    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_TARGET_ERROR, client.get_error_info());
        fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_TARGET_ERROR;
    }

    JPRINTF("  Targets (%u):\n", numTargets);
    for (uint32_t i = 0; i < numTargets; i++) {
        JPRINTF("  [%u] %-12s  %s\n", i,
               tas_get_device_name_str(targets[i].device_type),
               targets[i].identifier);
    }
    if (numTargets == 0) JPRINTF("  No targets found.\n");

    if (g_json) {
        nljson j;
        j["server"] = std::string(si->server_name) + " V" + std::to_string(si->v_major) + "." + std::to_string(si->v_minor);
        nljson targetsArr = nljson::array();
        for (uint32_t i = 0; i < numTargets; i++) {
            targetsArr.push_back({
                {"index", i},
                {"device", tas_get_device_name_str(targets[i].device_type)},
                {"identifier", targets[i].identifier}
            });
        }
        j["targets"] = targetsArr;
        return jsonOk(j);
    }
    return EXIT_OK;
}

//********************************************************************************************************************
//  doReset - reset the MCU
//********************************************************************************************************************

static int doReset(int argc, char** argv)
{
    CommonArgs args;
    bool halt = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--halt") == 0) { halt = true; i++; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle reset [options]\n\n"
                   "Options:\n"
                   "  --halt              Reset and halt (default: reset and run)\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n"
                   "  --json              Output as JSON\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    JPRINTF("TAS Wiggle Tool - MCU Reset\n");
    JPRINTF("===========================\n");
    JPRINTF("Connecting to TAS server at %s...\n", args.serverIp);

    CTasClientRw client("WiggleReset");
    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_SERVER_ERROR, client.get_error_info());
        fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SERVER_ERROR;
    }

    const tas_server_info_st* si = client.get_server_info();
    JPRINTF("  Server: %s V%d.%d (%s)\n", si->server_name, si->v_major, si->v_minor, si->date);

    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_TARGET_ERROR, client.get_error_info());
        fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_TARGET_ERROR;
    }

    if (numTargets == 0) {
        if (g_json) return jsonError(EXIT_NO_TARGET, "No targets found");
        fprintf(stderr, "ERROR: No targets found\n"); return EXIT_NO_TARGET;
    }

    const char* selectedTargetId = (args.targetId != nullptr) ? args.targetId : targets[0].identifier;
    if (args.targetId == nullptr) {
        JPRINTF("  Target: [0] %s (%s)\n",
               tas_get_device_name_str(targets[0].device_type), targets[0].identifier);
    }

    ret = startSession(client, selectedTargetId, "WiggleReset");
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_SESSION_ERROR, client.get_error_info());
        fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SESSION_ERROR;
    }

    tas_clnt_dco_et dco = halt ? TAS_CLNT_DCO_RESET_AND_HALT : TAS_CLNT_DCO_RESET;
    JPRINTF("Resetting MCU (%s)...\n", halt ? "reset and halt" : "reset and run");
    ret = client.device_connect(dco);
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_CONNECT_ERROR, client.get_error_info());
        fprintf(stderr, "ERROR: Reset failed: %s\n", client.get_error_info());
        return EXIT_CONNECT_ERROR;
    }

    if (g_json) {
        nljson j;
        j["action"] = "reset";
        j["mode"] = halt ? "halt" : "run";
        return jsonOk(j);
    }
    printf("  MCU reset successful.%s\n", halt ? " Device is halted." : " Normal execution resumed.");
    return EXIT_OK;
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
            if (!parseHexAddr(argv[++i], readAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
            hasAddr = true; i++; continue;
        }
        if ((strcmp(argv[i], "--length") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], readLength) || readLength == 0) { fprintf(stderr, "ERROR: Invalid length\n"); return EXIT_USAGE_ERROR; }
            hasLength = true; i++; continue;
        }
        if ((strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            outputFile = argv[++i]; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle read --addr <hex> --length <hex> [options]\n\n"
                   "Options:\n"
                   "  --addr/-a <hex>     Start address (required)\n"
                   "  --length/-l <hex>   Length in bytes (required)\n"
                   "  --output/-o <file>  Output file (.hex default, .bin for raw binary)\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n"
                   "  --device <name>     Override device type (skip auto-detect)\n"
                   "  --config-dir <path> DeviceConfigs JSON directory\n\n"
                   "Examples:\n"
                   "  wiggle read -a AF000000 -l 100\n"
                   "  wiggle read -a AF000000 -l 1000 -o dump.hex\n"
                   "  wiggle read -a AF000000 -l 400 --device TC27x\n\n"
                   "Run 'wiggle erase --info' to see supported devices.\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    if (!hasAddr || !hasLength) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "--addr and --length are required");
        fprintf(stderr, "ERROR: --addr and --length are required\n");
        return EXIT_USAGE_ERROR;
    }

    JPRINTF("TAS Wiggle Tool - Read\n");
    JPRINTF("======================\n");

    RESOLVE_CTX(ctx, "WiggleRead", TAS_CLNT_DCO_HOT_ATTACH);

    DeviceConfig& flashCfg = ctx.flashCfg;

    uint32_t dflashEndAddr = flashCfg.baseAddress + flashCfg.totalSize - 1;
    if (readAddr < flashCfg.baseAddress || readAddr > dflashEndAddr) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "Address out of DFlash range");
        fprintf(stderr, "ERROR: Address 0x%08X out of DFlash range (0x%08X - 0x%08X)\n",
                readAddr, flashCfg.baseAddress, dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }
    if (static_cast<uint64_t>(readAddr) + readLength - 1 > dflashEndAddr) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "Range exceeds DFlash boundary");
        fprintf(stderr, "ERROR: Range exceeds DFlash boundary (max 0x%08X)\n", dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }

    const char* displayName = args.deviceName ? args.deviceName : flashCfg.deviceName.c_str();
    JPRINTF("  Device: %s (%s), DFlash: %u KB\n",
           displayName, flashCfg.family.c_str(), flashCfg.totalSize / 1024);
    JPRINTF("  Read:   0x%08X - 0x%08X (%u bytes)\n\n", readAddr, readAddr + readLength - 1, readLength);

    // Read in sector-sized chunks
    std::vector<uint8_t> allData(readLength, 0);
    uint32_t totalRead = 0;
    const uint32_t chunkSize = flashCfg.sectorSize;

    for (uint32_t offset = 0; offset < readLength; offset += chunkSize) {
        uint32_t remaining = readLength - offset;
        uint32_t thisChunk = std::min(remaining, chunkSize);
        uint32_t bytesRead = 0;
        tas_return_et r = ctx.client.read(readAddr + offset, allData.data() + offset, thisChunk, &bytesRead);
        if (r != TAS_ERR_NONE && r != TAS_ERR_RW_READ) {
            if (g_json) return jsonError(EXIT_IO_ERROR, ctx.client.get_error_info());
            fprintf(stderr, "ERROR: Read failed at 0x%08X: %s\n", readAddr + offset, ctx.client.get_error_info());
            return EXIT_IO_ERROR;
        }
        totalRead += bytesRead;
        if (!g_json) { printf("  Read %u/%u bytes\r", totalRead, readLength); fflush(stdout); }
    }
    JPRINTF("\n  Read complete: %u bytes\n", totalRead);

    // Output
    if (outputFile != nullptr) {
        std::string path(outputFile);
        std::string ext;
        size_t dotPos = path.find_last_of('.');
        if (dotPos != std::string::npos && dotPos + 1 < path.size()) {
            ext = path.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        if (ext == "bin") {
            // Raw binary output
            std::ofstream ofs(outputFile, std::ios::binary);
            if (!ofs) {
                if (g_json) return jsonError(EXIT_IO_ERROR, std::string("Cannot open '") + outputFile + "'");
                fprintf(stderr, "ERROR: Cannot open '%s'\n", outputFile);
                return EXIT_IO_ERROR;
            }
            ofs.write(reinterpret_cast<const char*>(allData.data()), totalRead);
            JPRINTF("  Saved binary: %s (%u bytes)\n", outputFile, totalRead);
        } else {
            // Default: Intel HEX format
            if (!saveToIntelHex(outputFile, readAddr, allData)) {
                if (g_json) return jsonError(EXIT_IO_ERROR, "Failed to save Intel HEX");
                return EXIT_IO_ERROR;
            }
            JPRINTF("  Saved Intel HEX: %s\n", outputFile);
        }
    } else if (!g_json) {
        printf("\n");
        hexDumpXxd(allData.data(), totalRead, readAddr);
    }

    if (g_json) {
        nljson j;
        char addrStr[16];
        snprintf(addrStr, sizeof(addrStr), "0x%08X", readAddr);
        j["address"] = addrStr;
        j["length"] = totalRead;
        std::string hexData;
        hexData.reserve(static_cast<size_t>(totalRead) * 2);
        for (uint32_t i = 0; i < totalRead; ++i) {
            char hb[3];
            snprintf(hb, sizeof(hb), "%02X", allData[i]);
            hexData += hb;
        }
        j["data"] = hexData;
        if (outputFile) j["output_file"] = outputFile;
        return jsonOk(j);
    }

    return EXIT_OK;
}

//********************************************************************************************************************
//  writeSegmentPages - common page-write loop used by doWrite and doRestore
//********************************************************************************************************************

// Returns EXIT_OK on success, or an error exit code on failure.
static int writeSegmentPages(CTasClientRw& client, bool isTc3x, uint64_t flashCmdBase,
                             const std::vector<uint8_t>& segData, uint32_t baseAddress,
                             uint32_t& pagesWritten, uint32_t totalPages,
                             std::chrono::steady_clock::time_point startTime)
{
    // Pad last page if data not page-aligned
    std::vector<uint8_t> paddedData = segData;
    if (paddedData.size() % DFLASH_PAGE_SIZE != 0) {
        size_t padSize = DFLASH_PAGE_SIZE - (paddedData.size() % DFLASH_PAGE_SIZE);
        paddedData.insert(paddedData.end(), padSize, 0x00);
    }
    uint32_t segPages = static_cast<uint32_t>(paddedData.size()) / DFLASH_PAGE_SIZE;

    for (uint32_t p = 0; p < segPages; p++) {
        uint32_t pageAddr = baseAddress + p * DFLASH_PAGE_SIZE;
        const uint8_t* pageData = paddedData.data() + p * DFLASH_PAGE_SIZE;

        // Extract low and high 32-bit words (little-endian)
        uint32_t dataLow  = pageData[0] | (static_cast<uint32_t>(pageData[1]) << 8) |
                            (static_cast<uint32_t>(pageData[2]) << 16) | (static_cast<uint32_t>(pageData[3]) << 24);
        uint32_t dataHigh = pageData[4] | (static_cast<uint32_t>(pageData[5]) << 8) |
                            (static_cast<uint32_t>(pageData[6]) << 16) | (static_cast<uint32_t>(pageData[7]) << 24);

        // Skip all-zero pages - DFlash erased state is 0x00
        if (dataLow == 0 && dataHigh == 0) {
            pagesWritten++;
            continue;
        }

        // 1. Clear status
        if (!clearFlashStatus(client, flashCmdBase)) {
            fprintf(stderr, "\nERROR: Clear status failed at page 0x%08X\n", pageAddr);
            return EXIT_FLASH_STATUS_ERROR;
        }
        // 2. Enter page mode
        if (!enterPageMode(client, flashCmdBase)) {
            fprintf(stderr, "\nERROR: Enter page mode failed at page 0x%08X\n", pageAddr);
            return EXIT_WRITE_CMD_ERROR;
        }
        // 3. Wait busy
        uint32_t elapsedMs = 0;
        if (!waitUnbusyD0(client, isTc3x, elapsedMs)) {
            fprintf(stderr, "\nERROR: Timeout at page 0x%08X\n", pageAddr);
            return EXIT_ERASE_TIMEOUT;
        }
        // 4. Load page data
        if (!loadPage(client, flashCmdBase, dataLow, dataHigh)) {
            fprintf(stderr, "\nERROR: Load page failed at page 0x%08X\n", pageAddr);
            return EXIT_WRITE_CMD_ERROR;
        }
        // 5. Execute write
        if (!writePageCmd(client, flashCmdBase, pageAddr)) {
            fprintf(stderr, "\nERROR: Write command failed at page 0x%08X\n", pageAddr);
            return EXIT_WRITE_CMD_ERROR;
        }
        // 6. Wait for write to complete
        elapsedMs = 0;
        if (!waitUnbusyD0(client, isTc3x, elapsedMs)) {
            fprintf(stderr, "\nERROR: Write timeout at page 0x%08X\n", pageAddr);
            return EXIT_ERASE_TIMEOUT;
        }
        // 7. Check errors
        if (!checkEraseErrors(client, isTc3x)) {
            fprintf(stderr, "\nERROR: Write error flags at page 0x%08X\n", pageAddr);
            return EXIT_WRITE_CMD_ERROR;
        }
        // 8. Reset to read
        if (!resetToRead(client, flashCmdBase)) {
            fprintf(stderr, "\nERROR: Reset to read failed at page 0x%08X\n", pageAddr);
            return EXIT_FLASH_RESET_ERROR;
        }

        pagesWritten++;

        // Progress display (every 128 pages = 1KB)
        if (pagesWritten % 128 == 0 || pagesWritten == totalPages) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            double speed = (elapsed > 0) ? (pagesWritten * DFLASH_PAGE_SIZE * 1000.0 / elapsed) : 0;
            printf("\r  Progress: %u/%u pages (%u%%), %.1f B/s    ",
                   pagesWritten, totalPages, pagesWritten * 100 / totalPages, speed);
            fflush(stdout);
        }
    }
    return EXIT_OK;
}

//********************************************************************************************************************
//  doWrite - write data to DFlash from HEX or BIN file
//********************************************************************************************************************

static int doWrite(int argc, char** argv)
{
    CommonArgs args;
    const char* inputFile = nullptr;
    uint32_t writeAddr = 0;
    bool hasAddr = false;
    bool doVerify = false;

    // Parse arguments
    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) && i + 1 < argc) {
            inputFile = argv[++i]; i++; continue;
        }
        if ((strcmp(argv[i], "--addr") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], writeAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
            hasAddr = true; i++; continue;
        }
        if (strcmp(argv[i], "--verify") == 0 || strcmp(argv[i], "-v") == 0) {
            doVerify = true; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle write --file <path> [--addr 0xAF...] [--verify] [options]\n\n");
            printf("Write data to DFlash from a HEX or BIN file.\n\n");
            printf("Options:\n");
            printf("  --file, -f <path>     Input file (.hex or .bin)\n");
            printf("  --addr, -a <hex>      Start address (required for .bin, optional for .hex)\n");
            printf("  --verify, -v          Read back and verify after writing\n");
            printf("  --device, -d <name>   Device name (e.g. TC23x)\n");
            printf("  --server, -s <ip>     TAS server IP (default: localhost)\n");
            printf("  --target, -t <id>     Target identifier\n");
            printf("  --config-dir <path>   DeviceConfigs directory\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    if (!inputFile) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "--file is required");
        fprintf(stderr, "ERROR: --file is required\n"); return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    // --- Determine file type and parse ---
    std::string filePath(inputFile);
    std::string ext;
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos + 1 < filePath.size()) {
        ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    // ext is empty when no extension found -> treated as binary

    HexParseResult parseResult;
    if (ext == "hex" || ext == "ihex") {
        parseResult = parseIntelHex(filePath);
    } else {
        if (!hasAddr) {
            fprintf(stderr, "ERROR: --addr is required for binary files\n");
            return EXIT_USAGE_ERROR;
        }
        parseResult = loadBinaryFile(filePath, writeAddr);
    }

    if (!parseResult.success) {
        fprintf(stderr, "ERROR: Failed to parse '%s': %s\n", inputFile, parseResult.errorMsg.c_str());
        return EXIT_HEX_PARSE_ERROR;
    }
    if (!parseResult.warningMsg.empty())
        fprintf(stderr, "WARNING: %s\n", parseResult.warningMsg.c_str());

    if (parseResult.segments.empty()) {
        fprintf(stderr, "ERROR: No data found in '%s'\n", inputFile);
        return EXIT_HEX_PARSE_ERROR;
    }

    // For HEX files, use embedded address unless --addr overrides
    if (hasAddr && (ext == "hex" || ext == "ihex")) {
        uint32_t originalBase = parseResult.segments[0].baseAddress;
        int32_t offset = static_cast<int32_t>(writeAddr) - static_cast<int32_t>(originalBase);
        for (auto& seg : parseResult.segments) {
            seg.baseAddress = static_cast<uint32_t>(static_cast<int32_t>(seg.baseAddress) + offset);
        }
    }

    // --- Print header ---
    JPRINTF("TAS Wiggle Tool - Write\n");
    JPRINTF("=======================\n\n");

    // --- Common initialization ---
    RESOLVE_CTX(ctx, "WiggleWrite", TAS_CLNT_DCO_RESET_AND_HALT);

    DeviceConfig& flashCfg = ctx.flashCfg;

    // --- Validate address range ---
    uint32_t dflashEndAddr = flashCfg.baseAddress + flashCfg.totalSize - 1;
    uint32_t totalWriteBytes = 0;
    for (const auto& seg : parseResult.segments) {
        if (seg.data.empty()) continue;
        if (seg.baseAddress < flashCfg.baseAddress || seg.baseAddress > dflashEndAddr) {
            fprintf(stderr, "ERROR: Segment address 0x%08X out of DFlash range (0x%08X - 0x%08X)\n",
                   seg.baseAddress, flashCfg.baseAddress, dflashEndAddr);
            return EXIT_USAGE_ERROR;
        }
        uint32_t segEnd = seg.baseAddress + static_cast<uint32_t>(seg.data.size()) - 1;
        if (segEnd > dflashEndAddr) {
            fprintf(stderr, "ERROR: Segment exceeds DFlash boundary (end=0x%08X, max=0x%08X)\n",
                   segEnd, dflashEndAddr);
            return EXIT_USAGE_ERROR;
        }
        totalWriteBytes += static_cast<uint32_t>(seg.data.size());
    }

    // Check page alignment
    for (const auto& seg : parseResult.segments) {
        if (seg.baseAddress % DFLASH_PAGE_SIZE != 0) {
            fprintf(stderr, "ERROR: Address 0x%08X not aligned to %u-byte page boundary\n",
                   seg.baseAddress, DFLASH_PAGE_SIZE);
            return EXIT_USAGE_ERROR;
        }
    }

    // Check for page-boundary overlap between segments
    // (the previous segment is padded up to a page boundary before being written)
    for (size_t s = 1; s < parseResult.segments.size(); s++) {
        uint32_t prevEnd = parseResult.segments[s-1].baseAddress +
                           static_cast<uint32_t>(parseResult.segments[s-1].data.size());
        // Round up to page boundary
        uint32_t prevEndPage = (prevEnd + DFLASH_PAGE_SIZE - 1) & ~(DFLASH_PAGE_SIZE - 1);
        if (parseResult.segments[s].baseAddress < prevEndPage) {
            fprintf(stderr, "ERROR: Segment %zu at 0x%08X overlaps with previous segment's page boundary (0x%08X)\n",
                   s, parseResult.segments[s].baseAddress, prevEndPage);
            return EXIT_USAGE_ERROR;
        }
    }

    printf("  Device: %s (%s), DFlash: %u KB\n",
           flashCfg.deviceName.c_str(), flashCfg.family.c_str(), flashCfg.totalSize / 1024);
    printf("  Write:  %u bytes (%u pages) in %zu segment(s)\n\n",
           totalWriteBytes, totalWriteBytes / DFLASH_PAGE_SIZE, parseResult.segments.size());

    // --- Write loop ---
    const uint64_t flashCmdBase = static_cast<uint64_t>(flashCfg.baseAddress);
    uint32_t totalPages = 0;
    for (const auto& seg : parseResult.segments) {
        size_t paddedSize = seg.data.size();
        if (paddedSize % DFLASH_PAGE_SIZE != 0)
            paddedSize += DFLASH_PAGE_SIZE - (paddedSize % DFLASH_PAGE_SIZE);
        totalPages += static_cast<uint32_t>(paddedSize) / DFLASH_PAGE_SIZE;
    }
    uint32_t pagesWritten = 0;
    auto startTime = std::chrono::steady_clock::now();

    printf("Writing DFlash:\n");

    for (const auto& seg : parseResult.segments) {
        int writeResult = writeSegmentPages(ctx.client, flashCfg.isTc3x, flashCmdBase,
                                            seg.data, seg.baseAddress,
                                            pagesWritten, totalPages, startTime);
        if (writeResult != EXIT_OK) return writeResult;
    }

    printf("\r  Progress: %u/%u pages (100%%)                    \n", totalPages, totalPages);
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
    printf("  Write completed in %lld ms\n", static_cast<long long>(totalElapsed));

    // --- Verify ---
    if (doVerify) {
        printf("\nVerifying write:\n");
        uint32_t verifyErrors = 0;
        for (const auto& seg : parseResult.segments) {
            std::vector<uint8_t> readBuf(seg.data.size(), 0);
            uint32_t bytesRead = 0;
            tas_return_et vret = ctx.client.read(seg.baseAddress, readBuf.data(),
                                            static_cast<uint32_t>(seg.data.size()), &bytesRead);
            if (vret != TAS_ERR_NONE && vret != TAS_ERR_RW_READ) {
                fprintf(stderr, "  ERROR: Read failed at 0x%08X\n", seg.baseAddress);
                return EXIT_WRITE_VERIFY_ERROR;
            }
            for (size_t i = 0; i < seg.data.size() && i < bytesRead; i++) {
                if (readBuf[i] != seg.data[i]) {
                    if (verifyErrors < 10)
                        printf("  FAIL at 0x%08X: expected 0x%02X, got 0x%02X\n",
                               seg.baseAddress + static_cast<uint32_t>(i), seg.data[i], readBuf[i]);
                    verifyErrors++;
                }
            }
        }
        if (verifyErrors > 0) {
            printf("  FAILED: %u bytes mismatch\n", verifyErrors);
            return EXIT_WRITE_VERIFY_ERROR;
        }
        printf("  PASSED: all %u bytes verified\n", totalWriteBytes);
    }

    JPRINTF("\nDFlash write completed successfully.\n");

    if (g_json) {
        nljson j;
        j["action"] = "write";
        j["file"] = inputFile;
        j["total_bytes"] = totalWriteBytes;
        j["pages"] = totalPages;
        j["segments"] = parseResult.segments.size();
        j["elapsed_ms"] = static_cast<long long>(totalElapsed);
        if (doVerify) j["verified"] = true;
        return jsonOk(j);
    }
    return EXIT_OK;
}

//********************************************************************************************************************
//  doRestore - erase + write + verify (restore DFlash from backup)
//********************************************************************************************************************

static int doRestore(int argc, char** argv)
{
    CommonArgs args;
    const char* inputFile = nullptr;
    bool doVerify = true;  // Verify enabled by default for restore

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) && i + 1 < argc) {
            inputFile = argv[++i]; i++; continue;
        }
        if (strcmp(argv[i], "--no-verify") == 0) {
            doVerify = false; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle restore --file <backup.bin> [--no-verify] [options]\n\n");
            printf("Restore DFlash from a backup file (erase + write + verify).\n\n");
            printf("Options:\n");
            printf("  --file, -f <path>     Backup file to restore (.bin or .hex)\n");
            printf("  --no-verify           Skip verification after restore\n");
            printf("  --device, -d <name>   Device name\n");
            printf("  --server, -s <ip>     TAS server IP (default: localhost)\n");
            printf("  --target, -t <id>     Target identifier\n");
            printf("  --config-dir <path>   DeviceConfigs directory\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    if (!inputFile) { fprintf(stderr, "ERROR: --file is required\n"); return EXIT_USAGE_ERROR; }

    printf("TAS Wiggle Tool - Restore\n");
    printf("=========================\n\n");

    // Load file data
    std::string filePath(inputFile);
    std::string ext;
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos + 1 < filePath.size()) {
        ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    // ext is empty when no extension found -> treated as binary

    // We'll parse the file first, but for .bin files we need the base address from device config.
    // For .hex files, the address is embedded. We'll defer .bin address assignment until after device detection.
    HexParseResult parseResult;
    bool isBinFile = !(ext == "hex" || ext == "ihex");
    if (!isBinFile) {
        parseResult = parseIntelHex(filePath);
        if (!parseResult.success) {
            fprintf(stderr, "ERROR: Failed to parse '%s': %s\n", inputFile, parseResult.errorMsg.c_str());
            return EXIT_HEX_PARSE_ERROR;
        }
        if (!parseResult.warningMsg.empty())
            fprintf(stderr, "WARNING: %s\n", parseResult.warningMsg.c_str());
    } else {
        // Load binary file with placeholder address 0 (will be set after device detection)
        std::ifstream ifs(inputFile, std::ios::binary | std::ios::ate);
        if (!ifs) { fprintf(stderr, "ERROR: Cannot open '%s'\n", inputFile); return EXIT_IO_ERROR; }
        size_t fileSize = static_cast<size_t>(ifs.tellg());
        if (fileSize == 0) {
            fprintf(stderr, "ERROR: File is empty: %s\n", filePath.c_str());
            return EXIT_HEX_PARSE_ERROR;
        }
        ifs.seekg(0);
        std::vector<uint8_t> restoreData(fileSize);
        ifs.read(reinterpret_cast<char*>(restoreData.data()), static_cast<std::streamsize>(fileSize));
        if (!ifs) { fprintf(stderr, "ERROR: Read failed\n"); return EXIT_IO_ERROR; }
        ifs.close();

        HexSegment seg;
        seg.baseAddress = 0;  // Will be set after device detection
        seg.data = std::move(restoreData);
        parseResult.success = true;
        parseResult.entryPoint = 0;
        parseResult.segments.push_back(std::move(seg));
    }

    if (parseResult.segments.empty()) {
        fprintf(stderr, "ERROR: No data found in '%s'\n", inputFile);
        return EXIT_HEX_PARSE_ERROR;
    }

    size_t totalFileBytes = 0;
    for (const auto& seg : parseResult.segments) totalFileBytes += seg.data.size();
    printf("  File: %s (%zu bytes)\n", inputFile, totalFileBytes);

    // --- Common initialization ---
    RESOLVE_CTX(ctx, "WiggleRestore", TAS_CLNT_DCO_RESET_AND_HALT);

    DeviceConfig& flashCfg = ctx.flashCfg;

    // For binary files, set base address to DFlash base
    if (isBinFile) {
        parseResult.segments[0].baseAddress = flashCfg.baseAddress;
    }

    // Validate file size fits in DFlash
    for (const auto& seg : parseResult.segments) {
        if (seg.data.empty()) continue;
        uint32_t segEnd = seg.baseAddress + static_cast<uint32_t>(seg.data.size()) - 1;
        uint32_t dflashEnd = flashCfg.baseAddress + flashCfg.totalSize - 1;
        if (seg.baseAddress < flashCfg.baseAddress || segEnd > dflashEnd) {
            fprintf(stderr, "ERROR: Data range 0x%08X-0x%08X exceeds DFlash (0x%08X-0x%08X)\n",
                   seg.baseAddress, segEnd, flashCfg.baseAddress, dflashEnd);
            return EXIT_USAGE_ERROR;
        }
    }

    // Check for page-boundary overlap between segments (HEX restore may carry multiple)
    for (size_t s = 1; s < parseResult.segments.size(); s++) {
        uint32_t prevEnd = parseResult.segments[s-1].baseAddress +
                           static_cast<uint32_t>(parseResult.segments[s-1].data.size());
        // Round up to page boundary
        uint32_t prevEndPage = (prevEnd + DFLASH_PAGE_SIZE - 1) & ~(DFLASH_PAGE_SIZE - 1);
        if (parseResult.segments[s].baseAddress < prevEndPage) {
            fprintf(stderr, "ERROR: Segment %zu at 0x%08X overlaps with previous segment's page boundary (0x%08X)\n",
                   s, parseResult.segments[s].baseAddress, prevEndPage);
            return EXIT_USAGE_ERROR;
        }
    }

    printf("  Device: %s (%s), DFlash: %u KB\n",
           flashCfg.deviceName.c_str(), flashCfg.family.c_str(), flashCfg.totalSize / 1024);
    printf("  Restore: %zu bytes to 0x%08X\n\n",
           totalFileBytes, parseResult.segments[0].baseAddress);

    const uint64_t flashCmdBase = static_cast<uint64_t>(flashCfg.baseAddress);

    // --- Step 1: Erase required sectors ---
    // Calculate how many sectors to erase (cover all segments)
    uint32_t eraseStart = parseResult.segments[0].baseAddress;
    uint32_t eraseEnd = eraseStart;
    for (const auto& seg : parseResult.segments) {
        if (seg.data.empty()) continue;
        if (seg.baseAddress < eraseStart) eraseStart = seg.baseAddress;
        uint32_t segEnd = seg.baseAddress + static_cast<uint32_t>(seg.data.size());
        if (segEnd > eraseEnd) eraseEnd = segEnd;
    }
    // Align erase start down to sector boundary
    eraseStart = eraseStart & ~(flashCfg.sectorSize - 1);
    uint32_t eraseBytes = eraseEnd - eraseStart;
    uint32_t numSectors = (eraseBytes + flashCfg.sectorSize - 1) / flashCfg.sectorSize;

    printf("Step 1/3: Erasing %u sectors at 0x%08X...\n", numSectors, eraseStart);

    if (!clearFlashStatus(ctx.client, flashCmdBase)) {
        fprintf(stderr, "ERROR: Clear status failed\n"); return EXIT_FLASH_STATUS_ERROR;
    }
    if (!eraseMultipleSectors(ctx.client, flashCmdBase, eraseStart, numSectors)) {
        fprintf(stderr, "ERROR: Erase command failed\n"); return EXIT_ERASE_CMD_ERROR;
    }
    uint32_t elapsedMs = 0;
    if (!waitUnbusyD0(ctx.client, flashCfg.isTc3x, elapsedMs)) {
        fprintf(stderr, "ERROR: Erase timeout\n"); return EXIT_ERASE_TIMEOUT;
    }
    if (!checkEraseErrors(ctx.client, flashCfg.isTc3x)) {
        return EXIT_ERASE_FLAGS;
    }
    if (!resetToRead(ctx.client, flashCmdBase)) {
        fprintf(stderr, "ERROR: Reset to read failed\n"); return EXIT_FLASH_RESET_ERROR;
    }
    printf("  Erase completed in %u ms\n\n", elapsedMs);

    // --- Step 2: Write page by page ---
    printf("Step 2/3: Writing %zu bytes...\n", totalFileBytes);
    uint32_t totalPages = 0;
    for (const auto& seg : parseResult.segments) {
        totalPages += (static_cast<uint32_t>(seg.data.size()) + DFLASH_PAGE_SIZE - 1) / DFLASH_PAGE_SIZE;
    }
    uint32_t pagesWritten = 0;
    auto writeStart = std::chrono::steady_clock::now();

    for (const auto& seg : parseResult.segments) {
        int writeResult = writeSegmentPages(ctx.client, flashCfg.isTc3x, flashCmdBase,
                                            seg.data, seg.baseAddress,
                                            pagesWritten, totalPages, writeStart);
        if (writeResult != EXIT_OK) return writeResult;
    }

    printf("\r  Progress: %u/%u pages (100%%)                    \n", totalPages, totalPages);
    auto writeElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - writeStart).count();
    printf("  Write completed in %lld ms\n", static_cast<long long>(writeElapsed));

    // --- Step 3: Verify ---
    if (doVerify) {
        printf("\nStep 3/3: Verifying restore...\n");
        uint32_t verifyErrors = 0;
        const uint32_t verifyChunkSize = flashCfg.sectorSize;  // Read in sector-sized chunks
        std::vector<uint8_t> readBuf(verifyChunkSize, 0);
        for (const auto& seg : parseResult.segments) {
            uint32_t segSize = static_cast<uint32_t>(seg.data.size());
            for (uint32_t offset = 0; offset < segSize; offset += verifyChunkSize) {
                uint32_t chunkLen = std::min(verifyChunkSize, segSize - offset);
                uint32_t bytesRead = 0;
                tas_return_et vret = ctx.client.read(seg.baseAddress + offset, readBuf.data(),
                                                chunkLen, &bytesRead);
                if (vret != TAS_ERR_NONE && vret != TAS_ERR_RW_READ) {
                    fprintf(stderr, "  ERROR: Read failed at 0x%08X\n", seg.baseAddress + offset);
                    return EXIT_WRITE_VERIFY_ERROR;
                }
                for (uint32_t i = 0; i < bytesRead && i < chunkLen; i++) {
                    if (readBuf[i] != seg.data[offset + i]) {
                        if (verifyErrors < 10)
                            printf("  FAIL at 0x%08X: expected 0x%02X, got 0x%02X\n",
                                   seg.baseAddress + offset + i, seg.data[offset + i], readBuf[i]);
                        verifyErrors++;
                    }
                }
            }
        }
        if (verifyErrors > 0) {
            printf("  FAILED: %u bytes mismatch\n", verifyErrors);
            return EXIT_WRITE_VERIFY_ERROR;
        }
        printf("  PASSED: all %zu bytes verified\n", totalFileBytes);
    } else {
        printf("\nStep 3/3: Verification skipped (use default or remove --no-verify)\n");
    }

    printf("\nDFlash restore completed successfully.\n");
    return EXIT_OK;
}

//********************************************************************************************************************
//  doUcbRead - read UCB (User Configuration Block) content (TC3XX only)
//********************************************************************************************************************

static int doUcbRead(int argc, char** argv)
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
            if (!parseHexAddr(argv[++i], readAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
            hasAddr = true; i++; continue;
        }
        if ((strcmp(argv[i], "--length") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], readLength) || readLength == 0) { fprintf(stderr, "ERROR: Invalid length\n"); return EXIT_USAGE_ERROR; }
            hasLength = true; i++; continue;
        }
        if ((strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            outputFile = argv[++i]; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle ucb read [--addr <hex>] [--length <hex>] [--output <file>] [options]\n\n"
                   "Read UCB (User Configuration Block) content (TC3XX only).\n\n"
                   "Options:\n"
                   "  --addr/-a <hex>     Start address (default: UCB base)\n"
                   "  --length/-l <hex>   Length in bytes (default: entire UCB)\n"
                   "  --output/-o <file>  Output file (.hex default, .bin for raw binary)\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n"
                   "  --device <name>     Override device type (skip auto-detect)\n"
                   "  --config-dir <path> DeviceConfigs JSON directory\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    printf("TAS Wiggle Tool - UCB Read\n");
    printf("==========================\n");

    RESOLVE_CTX(ctx, "WiggleUcbRead", TAS_CLNT_DCO_HOT_ATTACH);

    // Reject TC2x
    if (!ctx.flashCfg.isTc3x) {
        fprintf(stderr, "ERROR: UCB commands only support TC3XX devices.\n");
        return EXIT_DEVICE_ERROR;
    }

    // Check UCB config valid
    if (ctx.flashCfg.ucb.baseAddress == 0) {
        fprintf(stderr, "ERROR: No UCB configuration found for this device.\n");
        return EXIT_DEVICE_ERROR;
    }

    const UCBConfig& ucb = ctx.flashCfg.ucb;

    // Default: read entire UCB
    if (!hasAddr) readAddr = ucb.baseAddress;
    if (!hasLength) readLength = ucb.totalSize;

    // Validate range within UCB
    uint32_t ucbEndAddr = ucb.baseAddress + ucb.totalSize - 1;
    if (readAddr < ucb.baseAddress || readAddr > ucbEndAddr) {
        fprintf(stderr, "ERROR: Address 0x%08X out of UCB range (0x%08X - 0x%08X)\n",
                readAddr, ucb.baseAddress, ucbEndAddr);
        return EXIT_USAGE_ERROR;
    }
    if (static_cast<uint64_t>(readAddr) + readLength - 1 > ucbEndAddr) {
        fprintf(stderr, "ERROR: Range exceeds UCB boundary (max 0x%08X)\n", ucbEndAddr);
        return EXIT_USAGE_ERROR;
    }

    printf("  Device: %s (%s)\n", ctx.flashCfg.deviceName.c_str(), ctx.flashCfg.family.c_str());
    printf("  UCB:    0x%08X - 0x%08X (%u bytes, %u sectors of %u bytes)\n",
           ucb.baseAddress, ucbEndAddr, ucb.totalSize, ucb.numSectors, ucb.sectorSize);
    printf("  Read:   0x%08X - 0x%08X (%u bytes)\n\n", readAddr, readAddr + readLength - 1, readLength);

    // Read in sector-sized chunks
    const uint32_t chunkSize = ucb.sectorSize;
    std::vector<uint8_t> allData(readLength, 0);
    uint32_t totalRead = 0;

    for (uint32_t offset = 0; offset < readLength; offset += chunkSize) {
        uint32_t remaining = readLength - offset;
        uint32_t thisChunk = std::min(remaining, chunkSize);
        uint32_t bytesRead = 0;
        tas_return_et r = ctx.client.read(readAddr + offset, allData.data() + offset, thisChunk, &bytesRead);
        if (r != TAS_ERR_NONE && r != TAS_ERR_RW_READ) {
            fprintf(stderr, "ERROR: Read failed at 0x%08X: %s\n", readAddr + offset, ctx.client.get_error_info());
            return EXIT_IO_ERROR;
        }
        totalRead += bytesRead;
        printf("  Read %u/%u bytes\r", totalRead, readLength);
        fflush(stdout);
    }
    printf("\n  Read complete: %u bytes\n", totalRead);

    // Output
    if (outputFile != nullptr) {
        std::string path(outputFile);
        std::string ext;
        size_t dotPos = path.find_last_of('.');
        if (dotPos != std::string::npos && dotPos + 1 < path.size()) {
            ext = path.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        if (ext == "bin") {
            std::ofstream ofs(outputFile, std::ios::binary);
            if (!ofs) { fprintf(stderr, "ERROR: Cannot open '%s'\n", outputFile); return EXIT_IO_ERROR; }
            ofs.write(reinterpret_cast<const char*>(allData.data()), totalRead);
            printf("  Saved binary: %s (%u bytes)\n", outputFile, totalRead);
        } else {
            if (!saveToIntelHex(outputFile, readAddr, allData)) return EXIT_IO_ERROR;
            printf("  Saved Intel HEX: %s\n", outputFile);
        }
    } else {
        printf("\n");
        hexDumpXxd(allData.data(), totalRead, readAddr);
    }

    return EXIT_OK;
}

//********************************************************************************************************************
//  doUcbWrite - write data to UCB (no erase, TC3XX only)
//********************************************************************************************************************

static int doUcbWrite(int argc, char** argv)
{
    CommonArgs args;
    const char* inputFile = nullptr;
    uint32_t writeAddr = 0;
    bool hasAddr = false;
    bool doVerify = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) && i + 1 < argc) {
            inputFile = argv[++i]; i++; continue;
        }
        if ((strcmp(argv[i], "--addr") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], writeAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
            hasAddr = true; i++; continue;
        }
        if (strcmp(argv[i], "--verify") == 0 || strcmp(argv[i], "-v") == 0) {
            doVerify = true; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle ucb write --file <hex/bin> [--addr <hex>] [--verify] [options]\n\n"
                   "Write data to UCB (no erase, TC3XX only).\n\n"
                   "WARNING: UCB write is a high-risk operation. Incorrect data may lock the device.\n\n"
                   "Options:\n"
                   "  --file, -f <path>     Input file (.hex or .bin)\n"
                   "  --addr, -a <hex>      Start address (required for .bin, optional for .hex)\n"
                   "  --verify, -v          Read back and verify after writing\n"
                   "  --server <ip>         TAS server IP (default: localhost)\n"
                   "  --target <id>         Target identifier\n"
                   "  --device <name>       Override device type (skip auto-detect)\n"
                   "  --config-dir <path>   DeviceConfigs directory\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    if (!inputFile) { fprintf(stderr, "ERROR: --file is required\n"); return EXIT_USAGE_ERROR; }

    printf("TAS Wiggle Tool - UCB Write\n");
    printf("===========================\n");
    printf("WARNING: UCB write is a high-risk operation. Incorrect data may lock the device.\n\n");

    // Parse input file
    std::string filePath(inputFile);
    std::string ext;
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos + 1 < filePath.size()) {
        ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    HexParseResult parseResult;
    if (ext == "hex" || ext == "ihex") {
        parseResult = parseIntelHex(filePath);
    } else {
        if (!hasAddr) {
            fprintf(stderr, "ERROR: --addr is required for binary files\n");
            return EXIT_USAGE_ERROR;
        }
        parseResult = loadBinaryFile(filePath, writeAddr);
    }

    if (!parseResult.success) {
        fprintf(stderr, "ERROR: Failed to parse '%s': %s\n", inputFile, parseResult.errorMsg.c_str());
        return EXIT_HEX_PARSE_ERROR;
    }
    if (!parseResult.warningMsg.empty())
        fprintf(stderr, "WARNING: %s\n", parseResult.warningMsg.c_str());
    if (parseResult.segments.empty()) {
        fprintf(stderr, "ERROR: No data found in '%s'\n", inputFile);
        return EXIT_HEX_PARSE_ERROR;
    }

    // For HEX files, use embedded address unless --addr overrides
    if (hasAddr && (ext == "hex" || ext == "ihex")) {
        uint32_t originalBase = parseResult.segments[0].baseAddress;
        int32_t offset = static_cast<int32_t>(writeAddr) - static_cast<int32_t>(originalBase);
        for (auto& seg : parseResult.segments) {
            seg.baseAddress = static_cast<uint32_t>(static_cast<int32_t>(seg.baseAddress) + offset);
        }
    }

    // --- Common initialization ---
    RESOLVE_CTX(ctx, "WiggleUcbWrite", TAS_CLNT_DCO_RESET_AND_HALT);

    // Reject TC2x
    if (!ctx.flashCfg.isTc3x) {
        fprintf(stderr, "ERROR: UCB commands only support TC3XX devices.\n");
        return EXIT_DEVICE_ERROR;
    }

    // Check UCB config valid
    if (ctx.flashCfg.ucb.baseAddress == 0) {
        fprintf(stderr, "ERROR: No UCB configuration found for this device.\n");
        return EXIT_DEVICE_ERROR;
    }

    const UCBConfig& ucb = ctx.flashCfg.ucb;
    uint32_t ucbEndAddr = ucb.baseAddress + ucb.totalSize - 1;

    // Validate all segments within UCB range
    uint32_t totalWriteBytes = 0;
    for (const auto& seg : parseResult.segments) {
        if (seg.data.empty()) continue;
        if (seg.baseAddress < ucb.baseAddress || seg.baseAddress > ucbEndAddr) {
            fprintf(stderr, "ERROR: Segment address 0x%08X out of UCB range (0x%08X - 0x%08X)\n",
                   seg.baseAddress, ucb.baseAddress, ucbEndAddr);
            return EXIT_USAGE_ERROR;
        }
        uint32_t segEnd = seg.baseAddress + static_cast<uint32_t>(seg.data.size()) - 1;
        if (segEnd > ucbEndAddr) {
            fprintf(stderr, "ERROR: Segment exceeds UCB boundary (end=0x%08X, max=0x%08X)\n",
                   segEnd, ucbEndAddr);
            return EXIT_USAGE_ERROR;
        }
        totalWriteBytes += static_cast<uint32_t>(seg.data.size());
    }

    // Check page alignment
    for (const auto& seg : parseResult.segments) {
        if (seg.baseAddress % DFLASH_PAGE_SIZE != 0) {
            fprintf(stderr, "ERROR: Address 0x%08X not aligned to %u-byte page boundary\n",
                   seg.baseAddress, DFLASH_PAGE_SIZE);
            return EXIT_USAGE_ERROR;
        }
    }

    printf("  Device: %s (%s)\n", ctx.flashCfg.deviceName.c_str(), ctx.flashCfg.family.c_str());
    printf("  UCB:    0x%08X - 0x%08X (%u bytes)\n", ucb.baseAddress, ucbEndAddr, ucb.totalSize);
    printf("  Write:  %u bytes (%u pages) in %zu segment(s)\n\n",
           totalWriteBytes, totalWriteBytes / DFLASH_PAGE_SIZE, parseResult.segments.size());

    // --- High-risk warning + confirmation ---
    printf("\n");
    printf("  *** HIGH RISK OPERATION ***\n");
    printf("  Writing incorrect UCB data may permanently lock the chip!\n");
    printf("  Target: %u bytes in %zu segment(s)\n", totalWriteBytes, parseResult.segments.size());
    printf("\n");
    printf("  Type 'yes' to confirm: ");
    fflush(stdout);

    char confirm[16] = {0};
    if (!fgets(confirm, sizeof(confirm), stdin)) {
        printf("\nAborted (no input).\n");
        return EXIT_USAGE_ERROR;
    }
    // Strip trailing newline/CR
    size_t cl = strlen(confirm);
    while (cl > 0 && (confirm[cl - 1] == '\n' || confirm[cl - 1] == '\r')) confirm[--cl] = '\0';
    if (strcmp(confirm, "yes") != 0) {
        printf("  Aborted.\n");
        return EXIT_USAGE_ERROR;
    }

    // --- Write loop (no erase, direct page programming) ---
    const uint64_t flashCmdBase = static_cast<uint64_t>(ctx.flashCfg.baseAddress);
    uint32_t totalPages = 0;
    for (const auto& seg : parseResult.segments) {
        size_t paddedSize = seg.data.size();
        if (paddedSize % DFLASH_PAGE_SIZE != 0)
            paddedSize += DFLASH_PAGE_SIZE - (paddedSize % DFLASH_PAGE_SIZE);
        totalPages += static_cast<uint32_t>(paddedSize) / DFLASH_PAGE_SIZE;
    }
    uint32_t pagesWritten = 0;
    auto startTime = std::chrono::steady_clock::now();

    printf("Writing UCB:\n");

    for (const auto& seg : parseResult.segments) {
        int writeResult = writeSegmentPages(ctx.client, ctx.flashCfg.isTc3x, flashCmdBase,
                                            seg.data, seg.baseAddress,
                                            pagesWritten, totalPages, startTime);
        if (writeResult != EXIT_OK) return writeResult;
    }

    printf("\r  Progress: %u/%u pages (100%%)                    \n", totalPages, totalPages);
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
    printf("  Write completed in %lld ms\n", static_cast<long long>(totalElapsed));

    // --- Verify ---
    if (doVerify) {
        printf("\nVerifying UCB write:\n");
        uint32_t verifyErrors = 0;
        for (const auto& seg : parseResult.segments) {
            uint32_t segSize = static_cast<uint32_t>(seg.data.size());
            const uint32_t verifyChunkSize = ucb.sectorSize;
            std::vector<uint8_t> readBuf(verifyChunkSize, 0);
            for (uint32_t offset = 0; offset < segSize; offset += verifyChunkSize) {
                uint32_t chunkLen = std::min(verifyChunkSize, segSize - offset);
                uint32_t bytesRead = 0;
                tas_return_et vret = ctx.client.read(seg.baseAddress + offset, readBuf.data(),
                                                chunkLen, &bytesRead);
                if (vret != TAS_ERR_NONE && vret != TAS_ERR_RW_READ) {
                    fprintf(stderr, "  ERROR: Read failed at 0x%08X\n", seg.baseAddress + offset);
                    return EXIT_WRITE_VERIFY_ERROR;
                }
                for (uint32_t i = 0; i < bytesRead && i < chunkLen; i++) {
                    if (readBuf[i] != seg.data[offset + i]) {
                        if (verifyErrors < 10)
                            printf("  FAIL at 0x%08X: expected 0x%02X, got 0x%02X\n",
                                   seg.baseAddress + offset + i, seg.data[offset + i], readBuf[i]);
                        verifyErrors++;
                    }
                }
            }
        }
        if (verifyErrors > 0) {
            printf("  FAILED: %u bytes mismatch\n", verifyErrors);
            return EXIT_WRITE_VERIFY_ERROR;
        }
        printf("  PASSED: all %u bytes verified\n", totalWriteBytes);
    }

    printf("\nUCB write completed successfully.\n");
    return EXIT_OK;
}

//********************************************************************************************************************
//  doUcbErase - erase UCB sectors (TC3XX only, HIGH RISK)
//********************************************************************************************************************

static int doUcbErase(int argc, char** argv)
{
    CommonArgs args;
    uint32_t eraseAddr = 0;
    uint32_t numSectors = 1;  // default: 1 sector
    bool hasAddr = false;
    bool doVerify = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "--addr") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], eraseAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
            hasAddr = true; i++; continue;
        }
        if ((strcmp(argv[i], "--sectors") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
            if (sscanf(argv[++i], "%u", &numSectors) != 1 || numSectors == 0) {
                fprintf(stderr, "ERROR: Invalid sectors\n"); return EXIT_USAGE_ERROR;
            }
            i++; continue;
        }
        if (strcmp(argv[i], "--verify") == 0 || strcmp(argv[i], "-v") == 0) {
            doVerify = true; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle ucb erase [--addr <hex>] [--sectors <n>] [--verify] [options]\n\n"
                   "Erase UCB sectors (TC3XX only, HIGH RISK).\n\n"
                   "WARNING: Erasing security-related UCB sectors may permanently lock the chip.\n\n"
                   "Options:\n"
                   "  --addr, -a <hex>     Start address (default: UCB base, auto-aligned to sector)\n"
                   "  --sectors, -s <n>    Number of sectors to erase (decimal, default: 1)\n"
                   "  --verify, -v         Read back and verify all bytes are 0x00\n"
                   "  --server <ip>        TAS server IP (default: localhost)\n"
                   "  --target <id>        Target identifier\n"
                   "  --device <name>      Override device type (skip auto-detect)\n"
                   "  --config-dir <path>  DeviceConfigs JSON directory\n\n"
                   "Note: Sectors 0xAF400800-0xAF400FFF and 0xAF401800-0xAF401FFF are chip-locked and cannot be erased.\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    printf("TAS Wiggle Tool - UCB Erase\n");
    printf("===========================\n");
    printf("WARNING: UCB erase is a HIGH-RISK operation. May permanently lock the chip.\n\n");

    // --- Common initialization ---
    RESOLVE_CTX(ctx, "WiggleUcbErase", TAS_CLNT_DCO_RESET_AND_HALT);

    // Reject TC2x
    if (!ctx.flashCfg.isTc3x) {
        fprintf(stderr, "ERROR: UCB commands only support TC3XX devices.\n");
        return EXIT_DEVICE_ERROR;
    }

    // Check UCB config valid
    if (ctx.flashCfg.ucb.baseAddress == 0) {
        fprintf(stderr, "ERROR: No UCB configuration found for this device.\n");
        return EXIT_DEVICE_ERROR;
    }

    const UCBConfig& ucb = ctx.flashCfg.ucb;
    uint32_t ucbEndAddr = ucb.baseAddress + ucb.totalSize - 1;

    // Default: UCB base address
    if (!hasAddr) eraseAddr = ucb.baseAddress;

    // Validate address within UCB range
    if (eraseAddr < ucb.baseAddress || eraseAddr > ucbEndAddr) {
        fprintf(stderr, "ERROR: Address 0x%08X out of UCB range (0x%08X - 0x%08X)\n",
                eraseAddr, ucb.baseAddress, ucbEndAddr);
        return EXIT_USAGE_ERROR;
    }

    // Auto-align address to sector boundary (sectorSize must be power of 2)
    uint32_t alignedAddr = eraseAddr & ~(ucb.sectorSize - 1);
    if (alignedAddr != eraseAddr) {
        printf("  Address aligned: 0x%08X -> 0x%08X (%u-byte sector boundary)\n",
               eraseAddr, alignedAddr, ucb.sectorSize);
        eraseAddr = alignedAddr;
    }

    // Range check
    uint64_t eraseBytesU64 = static_cast<uint64_t>(numSectors) * ucb.sectorSize;
    if (eraseBytesU64 > UINT32_MAX) {
        fprintf(stderr, "ERROR: Erase size exceeds 4GB (overflow)\n");
        return EXIT_USAGE_ERROR;
    }
    uint32_t eraseSize = static_cast<uint32_t>(eraseBytesU64);
    if (static_cast<uint64_t>(eraseAddr) + eraseSize - 1 > ucbEndAddr) {
        fprintf(stderr, "ERROR: Range exceeds UCB boundary (end=0x%08llX, max=0x%08X)\n",
                static_cast<unsigned long long>(eraseAddr) + eraseSize - 1, ucbEndAddr);
        return EXIT_USAGE_ERROR;
    }

    printf("  Device: %s (%s)\n", ctx.flashCfg.deviceName.c_str(), ctx.flashCfg.family.c_str());
    printf("  UCB:    0x%08X - 0x%08X (%u bytes, %u sectors of %u bytes)\n",
           ucb.baseAddress, ucbEndAddr, ucb.totalSize, ucb.numSectors, ucb.sectorSize);
    printf("  Erase:  0x%08X, %u sector(s) (%u bytes)\n\n",
           eraseAddr, numSectors, eraseSize);

    // --- Locked UCB regions (cannot be erased - chip-level protection) ---
    // Check if requested erase range overlaps any locked region
    uint32_t eraseEnd = eraseAddr + eraseSize - 1;
    for (const auto& locked : lockedRegions) {
        // Overlap check: !(eraseEnd < locked.start || eraseAddr > locked.end)
        if (!(eraseEnd < locked.start || eraseAddr > locked.end)) {
            fprintf(stderr, "ERROR: Erase range 0x%08X-0x%08X overlaps locked region 0x%08X-0x%08X (%s)\n",
                    eraseAddr, eraseEnd, locked.start, locked.end, locked.name);
            fprintf(stderr, "  These sectors are chip-level protected and cannot be erased.\n");
            return EXIT_USAGE_ERROR;
        }
    }

    // --- High-risk warning + confirmation ---
    printf("WARNING: UCB erase is a HIGH-RISK operation!\n");
    printf("  Erasing security-related UCB sectors may permanently lock the chip.\n");
    printf("  Target: 0x%08X, %u sector(s)\n", eraseAddr, numSectors);
    printf("Type 'yes' to continue: ");
    fflush(stdout);

    char confirm[16] = {0};
    if (!fgets(confirm, sizeof(confirm), stdin)) {
        printf("\nAborted (no input).\n");
        return EXIT_USAGE_ERROR;
    }
    // Strip trailing newline/CR
    size_t cl = strlen(confirm);
    while (cl > 0 && (confirm[cl - 1] == '\n' || confirm[cl - 1] == '\r')) confirm[--cl] = '\0';
    if (strcmp(confirm, "yes") != 0) {
        printf("Aborted by user.\n");
        return EXIT_USAGE_ERROR;
    }

    // --- Erase sequence ---
    const uint64_t flashCmdBase = static_cast<uint64_t>(ctx.flashCfg.baseAddress);

    printf("\nStep 1/5: Clearing flash status...                ");
    if (!clearFlashStatus(ctx.client, flashCmdBase)) {
        printf("FAILED\n"); return EXIT_FLASH_STATUS_ERROR;
    }
    printf("OK\n");

    printf("Step 2/5: Executing erase command...              ");
    if (!eraseMultipleSectors(ctx.client, flashCmdBase, eraseAddr, numSectors)) {
        printf("FAILED\n"); return EXIT_ERASE_CMD_ERROR;
    }
    printf("OK\n");

    printf("Step 3/5: Waiting for erase to complete...        ");
    uint32_t elapsedMs = 0;
    if (!waitUnbusyD0(ctx.client, ctx.flashCfg.isTc3x, elapsedMs)) {
        printf("TIMEOUT\n"); return EXIT_ERASE_TIMEOUT;
    }
    printf("OK (%u ms)\n", elapsedMs);

    printf("Step 4/5: Checking error flags...                 ");
    if (!checkEraseErrors(ctx.client, ctx.flashCfg.isTc3x)) { return EXIT_ERASE_FLAGS; }
    printf("OK\n");

    printf("Step 5/5: Reset to read mode...                   ");
    if (!resetToRead(ctx.client, flashCmdBase)) {
        printf("FAILED\n"); return EXIT_FLASH_RESET_ERROR;
    }
    printf("OK\n");

    printf("\nErase complete: %u sector(s) at 0x%08X (%u ms)\n",
           numSectors, eraseAddr, elapsedMs);

    // --- Verify ---
    if (doVerify) {
        printf("\nVerifying UCB erase:\n");
        if (!verifyErase(ctx.client, eraseAddr, numSectors, ucb.sectorSize)) {
            printf("\nUCB erase completed with verification ERRORS.\n");
            return EXIT_VERIFY_ERROR;
        }
    }

    printf("\nUCB erase completed successfully.\n");
    return EXIT_OK;
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
            if (argc < 2) { printf("ERROR: Missing num_sectors\n"); return EXIT_USAGE_ERROR; }
            if (!parseHexAddr(argv[0], sectorAddr)) { printf("ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
            if (sscanf(argv[1], "%u", &numSectors) != 1 || numSectors == 0) { printf("ERROR: Invalid sectors\n"); return EXIT_USAGE_ERROR; }
            hasAddr = true;
        }
        // Parse options from appropriate offset
        int startIdx = (hasAddr) ? 2 : 1;
        for (int i = startIdx; i < argc; ) {
            int consumed = parseCommonOption(argc, argv, i, args);
            if (consumed > 0) { i += consumed; continue; }
            if (strcmp(argv[i], "--verify") == 0) { doVerify = true; i++; continue; }
            if (strcmp(argv[i], "--backup") == 0 && i + 1 < argc) { backupFile = argv[++i]; i++; continue; }
            if (strcmp(argv[i], "--reset") == 0) { doReset = true; i++; continue; }
            if (strcmp(argv[i], "--all") == 0) { eraseAll = true; i++; continue; }
            if (strcmp(argv[i], "--info") == 0) { infoOnly = true; i++; continue; }
            fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
            return EXIT_USAGE_ERROR;
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
            if (strcmp(argv[i], "--reset") == 0) { doReset = true; i++; continue; }
            if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
                if (!parseHexAddr(argv[++i], sectorAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
                hasAddr = true; i++; continue;
            }
            if (strcmp(argv[i], "--sectors") == 0 && i + 1 < argc) {
                if (sscanf(argv[++i], "%u", &numSectors) != 1 || numSectors == 0) { fprintf(stderr, "ERROR: Invalid sectors\n"); return EXIT_USAGE_ERROR; }
                i++; continue;
            }
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                printf("Usage: wiggle erase [--addr <hex> --sectors <n> | --all] [options]\n\n"
                       "Options:\n"
                       "  --addr <hex>       DFlash start address, auto-aligned to sector\n"
                       "  --sectors <n>      Number of sectors to erase\n"
                       "  --all              Erase entire DFlash\n"
                       "  --info             Show device info only, no erase\n"
                       "  --verify           Verify erase by reading back\n"
                       "  --backup <file>    Backup DFlash before erasing (.hex default, .bin for raw binary)\n"
                       "  --reset            Reset MCU after erase\n"
                       "  --server <ip>      TAS server IP (default: localhost)\n"
                       "  --target <id>      Target identifier\n"
                       "  --device <name>    Override device type (skip auto-detect)\n"
                       "  --config-dir <path> DeviceConfigs JSON directory\n\n"
                       "Examples:\n"
                       "  wiggle erase --addr AF000000 --sectors 1\n"
                       "  wiggle erase --addr AF000000 --sectors 4 --verify\n"
                       "  wiggle erase --all --backup backup.hex\n"
                       "  wiggle erase --info --device TC27x\n\n"
                       "Run with --info to see supported devices.\n");
                return EXIT_OK;
            }
            fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
            return EXIT_USAGE_ERROR;
        }
    }

    if (!infoOnly && !eraseAll && !hasAddr) {
        fprintf(stderr, "ERROR: Specify --addr + --sectors, --all, or --info\n");
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    JPRINTF("TAS Wiggle Tool - Erase\n");
    JPRINTF("=======================\n\n");

    // ---- Common initialization ----
    RESOLVE_CTX(ctx, "WiggleErase", TAS_CLNT_DCO_RESET_AND_HALT);

    DeviceConfig& flashCfg = ctx.flashCfg;

    const char* displayName = args.deviceName ? args.deviceName : flashCfg.deviceName.c_str();
    uint32_t dflashEndAddr = flashCfg.baseAddress + flashCfg.totalSize - 1;
    uint32_t totalSectors = flashCfg.numSectors;

    printf("  Device: %s (%s)\n", displayName, flashCfg.family.c_str());
    printf("  DFlash: %u KB total, %u sectors, sector size %u KB\n",
           flashCfg.totalSize / 1024, totalSectors, flashCfg.sectorSize / 1024);
    printf("  Range:  0x%08X - 0x%08X\n", flashCfg.baseAddress, dflashEndAddr);

    // ---- --info mode: just print info and exit ----
    if (infoOnly) {
        printf("\nDevice info displayed. No erase performed.\n");
        return EXIT_OK;
    }

    // ---- --all mode ----
    if (eraseAll) {
        sectorAddr = flashCfg.baseAddress;
        numSectors = flashCfg.numSectors;
    }

    // ---- Auto-align address ----
    uint32_t alignedAddr = sectorAddr & ~(flashCfg.sectorSize - 1);
    if (alignedAddr != sectorAddr) {
        printf("  Address aligned: 0x%08X -> 0x%08X (%u KB boundary)\n",
               sectorAddr, alignedAddr, flashCfg.sectorSize / 1024);
        sectorAddr = alignedAddr;
    }

    // ---- Validate range ----
    if (sectorAddr < flashCfg.baseAddress || sectorAddr >= flashCfg.baseAddress + flashCfg.totalSize) {
        fprintf(stderr, "ERROR: Address 0x%08X out of range (0x%08X - 0x%08X)\n",
               sectorAddr, flashCfg.baseAddress, dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }
    // Overflow protection (theoretical: DFlash max 1MB, uint32_t max 4GB - always safe)
    uint64_t eraseBytesU64 = static_cast<uint64_t>(numSectors) * flashCfg.sectorSize;
    if (eraseBytesU64 > UINT32_MAX) {
        fprintf(stderr, "ERROR: Erase size exceeds 4GB (overflow)\n");
        return EXIT_USAGE_ERROR;
    }
    uint32_t eraseSize = static_cast<uint32_t>(eraseBytesU64);

    if (static_cast<uint64_t>(sectorAddr) + eraseSize - 1 > dflashEndAddr) {
        fprintf(stderr, "ERROR: Range exceeds DFlash boundary (max 0x%08X)\n", dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }

    // ---- Backup ----
    if (backupFile != nullptr) {
        printf("\nBackup:\n");
        if (!backupFlash(ctx.client, sectorAddr, eraseSize, flashCfg.sectorSize, backupFile)) return EXIT_BACKUP_ERROR;
    }

    // ---- Print erase parameters ----
    printf("\nErasing DFlash:\n");
    printf("  Address:  0x%08X\n", sectorAddr);
    printf("  Sectors:  %u (%u KB)\n\n", numSectors, eraseSize / 1024);

    // ---- Flash command base = DFlash base address (TC2x/TC3x convention) ----
    const uint64_t flashCmdBase = static_cast<uint64_t>(flashCfg.baseAddress);

    // ---- Step 1: Clear status ----
    printf("Step 1/5: Clearing flash status...                ");
    if (!clearFlashStatus(ctx.client, flashCmdBase)) {
        printf("FAILED\n"); return EXIT_FLASH_STATUS_ERROR;
    }
    printf("OK\n");

    // ---- Step 2: Erase (atomic) ----
    printf("Step 2/5: Executing erase command...              ");
    if (!eraseMultipleSectors(ctx.client, flashCmdBase, sectorAddr, numSectors)) {
        printf("FAILED\n"); return EXIT_ERASE_CMD_ERROR;
    }
    printf("OK\n");

    // ---- Step 3: Wait unbusy ----
    printf("Step 3/5: Waiting for erase to complete...        ");
    uint32_t elapsedMs = 0;
    if (!waitUnbusyD0(ctx.client, flashCfg.isTc3x, elapsedMs)) { printf("TIMEOUT\n"); return EXIT_ERASE_TIMEOUT; }
    printf("OK (%u ms)\n", elapsedMs);

    // ---- Step 4: Check error flags ----
    printf("Step 4/5: Checking error flags...                 ");
    if (!checkEraseErrors(ctx.client, flashCfg.isTc3x)) { return EXIT_ERASE_FLAGS; }
    printf("OK\n");

    // ---- Step 5: Reset to read ----
    printf("Step 5/5: Reset to read mode...                   ");
    if (!resetToRead(ctx.client, flashCmdBase)) { printf("FAILED\n"); return EXIT_FLASH_RESET_ERROR; }
    printf("OK\n");

    // ---- Verify ----
    if (doVerify) {
        printf("\nVerifying erase:\n");
        if (!verifyErase(ctx.client, sectorAddr, numSectors, flashCfg.sectorSize)) {
            printf("\nDFlash erase completed with verification ERRORS.\n");
            return EXIT_VERIFY_ERROR;
        }
    }

    // ---- Reset MCU ----
    if (doReset) {
        printf("\nResetting MCU...\n");
        tas_return_et r = ctx.client.device_connect(TAS_CLNT_DCO_RESET);
        if (r != TAS_ERR_NONE) {
            printf("  WARN: Reset failed: %s\n", ctx.client.get_error_info());
        } else {
            printf("  MCU reset. Normal execution resumed.\n");
        }
    }

    JPRINTF("\nDFlash erase completed successfully.\n");

    if (g_json) {
        nljson j;
        j["action"] = "erase";
        j["address"] = sectorAddr;
        j["sectors"] = numSectors;
        j["sector_size"] = flashCfg.sectorSize;
        j["total_bytes"] = eraseSize;
        if (doVerify) j["verified"] = true;
        return jsonOk(j);
    }
    return EXIT_OK;
}

//********************************************************************************************************************
//  parseHexData - parse hex string (e.g. "12345678AABBCCDD") into byte vector
//********************************************************************************************************************

static std::vector<uint8_t> parseHexData(const char* hexStr)
{
    std::vector<uint8_t> result;
    if (!hexStr || *hexStr == '\0') {
        fprintf(stderr, "ERROR: Empty hex data string\n");
        return result;
    }

    size_t len = strlen(hexStr);
    if (len % 2 != 0) {
        fprintf(stderr, "ERROR: Hex data string must have even length (got %zu)\n", len);
        return result;
    }

    result.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        char hi = hexStr[i];
        char lo = hexStr[i + 1];
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hv = hexVal(hi);
        int lv = hexVal(lo);
        if (hv < 0 || lv < 0) {
            fprintf(stderr, "ERROR: Invalid hex character at position %zu: '%c%c'\n", i, hi, lo);
            result.clear();
            return result;
        }
        result.push_back(static_cast<uint8_t>((hv << 4) | lv));
    }
    return result;
}

//********************************************************************************************************************
//  doRewrite - Read-Modify-Write: arbitrary address write with automatic sector alignment
//********************************************************************************************************************

static void printRewriteUsage()
{
    printf("Usage: wiggle rewrite --file <path> [--addr <hex>] [options]\n");
    printf("       wiggle rewrite --addr <hex> --data <hexstring> [options]\n\n");
    printf("Write data to DFlash at arbitrary addresses using Read-Modify-Write.\n");
    printf("Automatically handles sector alignment by reading, merging, erasing,\n");
    printf("and writing back entire sectors.\n\n");
    printf("Data source (mutually exclusive):\n");
    printf("  --file, -f <path>     Input file (.hex or .bin)\n");
    printf("  --data <hexstring>    Hex data string (e.g. 12345678AABBCCDD)\n\n");
    printf("Options:\n");
    printf("  --addr, -a <hex>      Start address (required for .bin and --data)\n");
    printf("  --backup [path]       Backup affected sectors before rewrite\n");
    printf("                        (auto-generates filename if path omitted)\n");
    printf("  --verify, -v          Read back and verify after writing\n");
    printf("  --reset               Reset MCU after rewrite\n");
    printf("  --device <name>       Device name (e.g. TC27x)\n");
    printf("  --server <ip>         TAS server IP (default: localhost)\n");
    printf("  --target <id>         Target identifier\n");
    printf("  --config-dir <path>   DeviceConfigs directory\n\n");
    printf("Examples:\n");
    printf("  wiggle rewrite --file patch.hex --verify\n");
    printf("  wiggle rewrite --file patch.bin --addr AF001000\n");
    printf("  wiggle rewrite --addr AF000010 --data DEADBEEF --backup\n");
    printf("  wiggle rewrite --addr AF000010 --data 0102030405060708 --backup my_backup.hex\n");
}

static int doRewrite(int argc, char** argv)
{
    CommonArgs args;
    const char* inputFile = nullptr;
    const char* dataStr = nullptr;
    uint32_t writeAddr = 0;
    bool hasAddr = false;
    bool doVerify = false;
    bool doResetMcu = false;
    bool doBackup = false;
    const char* backupPath = nullptr;  // nullptr = auto-generate

    // Parse arguments
    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) && i + 1 < argc) {
            inputFile = argv[++i]; i++; continue;
        }
        if ((strcmp(argv[i], "--addr") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
            if (!parseHexAddr(argv[++i], writeAddr)) { fprintf(stderr, "ERROR: Invalid address\n"); return EXIT_USAGE_ERROR; }
            hasAddr = true; i++; continue;
        }
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            dataStr = argv[++i]; i++; continue;
        }
        if (strcmp(argv[i], "--backup") == 0) {
            doBackup = true;
            // Check if next arg is a path (not starting with '--' and not end of args)
            if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                backupPath = argv[++i];
            }
            i++; continue;
        }
        if (strcmp(argv[i], "--verify") == 0 || strcmp(argv[i], "-v") == 0) {
            doVerify = true; i++; continue;
        }
        if (strcmp(argv[i], "--reset") == 0) {
            doResetMcu = true; i++; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printRewriteUsage();
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    // Mutual exclusion check
    if (inputFile && dataStr) {
        fprintf(stderr, "ERROR: --file and --data are mutually exclusive\n");
        return EXIT_USAGE_ERROR;
    }
    if (!inputFile && !dataStr) {
        fprintf(stderr, "ERROR: Either --file or --data is required\n");
        printRewriteUsage();
        return EXIT_USAGE_ERROR;
    }
    if (dataStr && !hasAddr) {
        fprintf(stderr, "ERROR: --addr is required when using --data\n");
        return EXIT_USAGE_ERROR;
    }

    // --- Parse data source ---
    std::vector<uint8_t> newData;

    if (dataStr) {
        newData = parseHexData(dataStr);
        if (newData.empty()) return EXIT_USAGE_ERROR;
    } else {
        // Parse from file
        std::string filePath(inputFile);
        std::string ext;
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos != std::string::npos && dotPos + 1 < filePath.size()) {
            ext = filePath.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        }

        HexParseResult parseResult;
        if (ext == "hex" || ext == "ihex") {
            parseResult = parseIntelHex(filePath);
        } else {
            if (!hasAddr) {
                fprintf(stderr, "ERROR: --addr is required for binary files\n");
                return EXIT_USAGE_ERROR;
            }
            parseResult = loadBinaryFile(filePath, writeAddr);
        }

        if (!parseResult.success) {
            fprintf(stderr, "ERROR: Failed to parse '%s': %s\n", inputFile, parseResult.errorMsg.c_str());
            return EXIT_HEX_PARSE_ERROR;
        }
        if (!parseResult.warningMsg.empty())
            fprintf(stderr, "WARNING: %s\n", parseResult.warningMsg.c_str());
        if (parseResult.segments.empty()) {
            fprintf(stderr, "ERROR: No data found in '%s'\n", inputFile);
            return EXIT_HEX_PARSE_ERROR;
        }

        // For HEX files, use embedded address unless --addr overrides
        if (hasAddr && (ext == "hex" || ext == "ihex")) {
            uint32_t originalBase = parseResult.segments[0].baseAddress;
            int32_t offset = static_cast<int32_t>(writeAddr) - static_cast<int32_t>(originalBase);
            for (auto& seg : parseResult.segments) {
                seg.baseAddress = static_cast<uint32_t>(static_cast<int32_t>(seg.baseAddress) + offset);
            }
        }

        // For rewrite, we flatten all segments into one contiguous block
        // Use the first segment's base address as writeAddr
        if (!hasAddr) {
            writeAddr = parseResult.segments[0].baseAddress;
        }

        // Flatten segments: find min and max address, create contiguous buffer
        uint32_t minAddr = parseResult.segments[0].baseAddress;
        uint32_t maxEnd = minAddr + static_cast<uint32_t>(parseResult.segments[0].data.size());
        for (const auto& seg : parseResult.segments) {
            if (seg.baseAddress < minAddr) minAddr = seg.baseAddress;
            uint32_t segEnd = seg.baseAddress + static_cast<uint32_t>(seg.data.size());
            if (segEnd > maxEnd) maxEnd = segEnd;
        }

        writeAddr = minAddr;
        uint32_t totalLen = maxEnd - minAddr;
        newData.resize(totalLen, DFLASH_ERASED_BYTE);
        for (const auto& seg : parseResult.segments) {
            uint32_t offset = seg.baseAddress - minAddr;
            memcpy(newData.data() + offset, seg.data.data(), seg.data.size());
        }
    }

    if (newData.empty()) {
        fprintf(stderr, "ERROR: No data to write\n");
        return EXIT_USAGE_ERROR;
    }

    uint32_t dataLen = static_cast<uint32_t>(newData.size());

    printf("TAS Wiggle Tool - Rewrite (Read-Modify-Write)\n");
    printf("==============================================\n\n");

    // --- Common initialization ---
    RESOLVE_CTX(ctx, "WiggleRewrite", TAS_CLNT_DCO_RESET_AND_HALT);

    DeviceConfig& flashCfg = ctx.flashCfg;
    const uint64_t flashCmdBase = static_cast<uint64_t>(flashCfg.baseAddress);
    uint32_t dflashEndAddr = flashCfg.baseAddress + flashCfg.totalSize - 1;

    // --- Safety checks ---
    // DFlash address range check
    if (writeAddr < flashCfg.baseAddress || writeAddr > dflashEndAddr) {
        fprintf(stderr, "ERROR: Address 0x%08X out of DFlash range (0x%08X - 0x%08X)\n",
                writeAddr, flashCfg.baseAddress, dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }

    // Overflow protection
    uint64_t writeEndU64 = static_cast<uint64_t>(writeAddr) + dataLen;
    if (writeEndU64 - 1 > dflashEndAddr) {
        fprintf(stderr, "ERROR: Write range exceeds DFlash boundary (end=0x%08llX, max=0x%08X)\n",
                static_cast<unsigned long long>(writeEndU64 - 1), dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }

    // --- Step 1: Determine sector range ---
    uint32_t sectorSize = flashCfg.sectorSize;
    uint32_t sectorStart = writeAddr & ~(sectorSize - 1);
    uint32_t sectorEnd = (writeAddr + dataLen + sectorSize - 1) & ~(sectorSize - 1);
    uint32_t numSectors = (sectorEnd - sectorStart) / sectorSize;
    uint32_t totalSectorBytes = sectorEnd - sectorStart;

    // UCB locked region check
    uint32_t writeEnd = writeAddr + dataLen - 1;
    for (const auto& locked : lockedRegions) {
        if (!(writeEnd < locked.start || writeAddr > locked.end)) {
            fprintf(stderr, "ERROR: Write range 0x%08X-0x%08X overlaps locked region 0x%08X-0x%08X (%s)\n",
                    writeAddr, writeEnd, locked.start, locked.end, locked.name);
            fprintf(stderr, "  These regions are chip-level protected and cannot be modified.\n");
            return EXIT_USAGE_ERROR;
        }
    }

    // Large operation warning
    if (numSectors > 10) {
        printf("  WARNING: Rewrite affects %u sectors (>10). This may take a while.\n", numSectors);
    }

    const char* displayName = args.deviceName ? args.deviceName : flashCfg.deviceName.c_str();
    printf("  Device:  %s (%s), DFlash: %u KB\n",
           displayName, flashCfg.family.c_str(), flashCfg.totalSize / 1024);
    printf("  Write:   0x%08X - 0x%08X (%u bytes)\n", writeAddr, writeEnd, dataLen);
    printf("  Sectors: 0x%08X - 0x%08X (%u sectors, %u bytes)\n\n",
           sectorStart, sectorEnd - 1, numSectors, totalSectorBytes);

    // --- Step 2: Read original sector data ---
    printf("Step 1/5: Reading %u sector(s) from 0x%08X...\n", numSectors, sectorStart);
    std::vector<uint8_t> sectorData(totalSectorBytes, 0);
    uint32_t totalRead = 0;
    const uint32_t chunkSize = sectorSize;

    for (uint32_t offset = 0; offset < totalSectorBytes; offset += chunkSize) {
        uint32_t remaining = totalSectorBytes - offset;
        uint32_t thisChunk = std::min(remaining, chunkSize);
        uint32_t bytesRead = 0;
        tas_return_et r = ctx.client.read(sectorStart + offset, sectorData.data() + offset, thisChunk, &bytesRead);
        if (r != TAS_ERR_NONE && r != TAS_ERR_RW_READ) {
            fprintf(stderr, "ERROR: Read failed at 0x%08X: %s\n", sectorStart + offset, ctx.client.get_error_info());
            return EXIT_IO_ERROR;
        }
        totalRead += bytesRead;
    }
    printf("  Read %u bytes OK\n", totalRead);

    // --- Step 3: Optional backup ---
    if (doBackup) {
        std::string autoBackupName;
        const char* backupFilePath;
        if (backupPath) {
            backupFilePath = backupPath;
        } else {
            char nameBuf[128];
            snprintf(nameBuf, sizeof(nameBuf), "rewrite_backup_%08X.hex", sectorStart);
            autoBackupName = nameBuf;
            backupFilePath = autoBackupName.c_str();
        }
        printf("\nStep 2/5: Backing up to %s...\n", backupFilePath);
        if (!backupFlash(ctx.client, sectorStart, totalSectorBytes, sectorSize, backupFilePath)) {
            fprintf(stderr, "ERROR: Backup failed, aborting rewrite\n");
            return EXIT_BACKUP_ERROR;
        }
    } else {
        printf("Step 2/5: Backup skipped (use --backup to enable)\n");
    }

    // --- Step 4: Merge new data ---
    printf("\nStep 3/5: Merging %u bytes at offset 0x%X...\n", dataLen, writeAddr - sectorStart);
    uint32_t mergeOffset = writeAddr - sectorStart;
    memcpy(sectorData.data() + mergeOffset, newData.data(), dataLen);
    printf("  Merge complete\n");

    // --- Step 5: Erase and write back ---
    printf("\nStep 4/5: Erasing %u sector(s) at 0x%08X...\n", numSectors, sectorStart);
    if (!clearFlashStatus(ctx.client, flashCmdBase)) {
        fprintf(stderr, "ERROR: Clear flash status failed\n");
        return EXIT_FLASH_STATUS_ERROR;
    }
    if (!eraseMultipleSectors(ctx.client, flashCmdBase, sectorStart, numSectors)) {
        fprintf(stderr, "ERROR: Erase command failed\n");
        return EXIT_ERASE_CMD_ERROR;
    }
    uint32_t elapsedMs = 0;
    if (!waitUnbusyD0(ctx.client, flashCfg.isTc3x, elapsedMs)) {
        fprintf(stderr, "ERROR: Erase timeout\n");
        return EXIT_ERASE_TIMEOUT;
    }
    if (!checkEraseErrors(ctx.client, flashCfg.isTc3x)) {
        return EXIT_ERASE_FLAGS;
    }
    if (!resetToRead(ctx.client, flashCmdBase)) {
        fprintf(stderr, "ERROR: Reset to read mode failed\n");
        return EXIT_FLASH_RESET_ERROR;
    }
    printf("  Erase completed in %u ms\n", elapsedMs);

    // Write back merged sector data
    printf("\nStep 5/5: Writing back %u bytes to 0x%08X...\n", totalSectorBytes, sectorStart);
    uint32_t totalPages = (totalSectorBytes + DFLASH_PAGE_SIZE - 1) / DFLASH_PAGE_SIZE;
    uint32_t pagesWritten = 0;
    auto startTime = std::chrono::steady_clock::now();

    int writeResult = writeSegmentPages(ctx.client, flashCfg.isTc3x, flashCmdBase,
                                        sectorData, sectorStart,
                                        pagesWritten, totalPages, startTime);
    if (writeResult != EXIT_OK) return writeResult;

    printf("\r  Progress: %u/%u pages (100%%)                    \n", totalPages, totalPages);
    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
    printf("  Write completed in %lld ms\n", static_cast<long long>(totalElapsed));

    // --- Verify ---
    if (doVerify) {
        printf("\nVerifying rewrite:\n");
        uint32_t verifyErrors = 0;
        std::vector<uint8_t> readBuf(chunkSize, 0);

        for (uint32_t offset = 0; offset < dataLen; offset += chunkSize) {
            uint32_t remaining = dataLen - offset;
            uint32_t thisChunk = std::min(remaining, chunkSize);
            uint32_t bytesRead = 0;
            tas_return_et vret = ctx.client.read(writeAddr + offset, readBuf.data(), thisChunk, &bytesRead);
            if (vret != TAS_ERR_NONE && vret != TAS_ERR_RW_READ) {
                fprintf(stderr, "  ERROR: Verify read failed at 0x%08X\n", writeAddr + offset);
                return EXIT_WRITE_VERIFY_ERROR;
            }
            for (uint32_t i = 0; i < bytesRead && i < thisChunk; i++) {
                if (readBuf[i] != newData[offset + i]) {
                    if (verifyErrors < 10)
                        printf("  FAIL at 0x%08X: expected 0x%02X, got 0x%02X\n",
                               writeAddr + offset + i, newData[offset + i], readBuf[i]);
                    verifyErrors++;
                }
            }
        }
        if (verifyErrors > 0) {
            printf("  FAILED: %u bytes mismatch\n", verifyErrors);
            return EXIT_WRITE_VERIFY_ERROR;
        }
        printf("  PASSED: all %u bytes verified\n", dataLen);
    }

    // --- Reset MCU ---
    if (doResetMcu) {
        printf("\nResetting MCU...\n");
        tas_return_et r = ctx.client.device_connect(TAS_CLNT_DCO_RESET);
        if (r != TAS_ERR_NONE) {
            printf("  WARN: Reset failed: %s\n", ctx.client.get_error_info());
        } else {
            printf("  MCU reset. Normal execution resumed.\n");
        }
    }

    printf("\nDFlash rewrite completed successfully.\n");
    return EXIT_OK;
}

//********************************************************************************************************************
//  UCB Usage
//********************************************************************************************************************

static void printUcbUsage(const char* progName) {
    printf("Usage: %s ucb <subcommand> [options]\n\n", progName);
    printf("Subcommands:\n");
    printf("  read     Read UCB (User Configuration Block) content\n");
    printf("  write    Write data to UCB (no erase, TC3XX only)\n");
    printf("  erase    Erase UCB sectors (TC3XX only, HIGH RISK)\n\n");
    printf("Run '%s ucb <subcommand> --help' for details.\n", progName);
}

//********************************************************************************************************************
//  Helper: parse hex address string (with or without 0x prefix)
//********************************************************************************************************************

static bool parseAddress(const char* str, uint64_t& addr) {
    if (!str || !*str) return false;
    try {
        addr = std::stoull(str, nullptr, 0);
        return true;
    } catch (...) {
        fprintf(stderr, "ERROR: Invalid address '%s'\n", str);
        return false;
    }
}

static bool parseHexBytes(const char* str, std::vector<uint8_t>& bytes) {
    if (!str || !*str) return false;
    std::string s(str);
    // Remove spaces and 0x prefix
    std::string clean;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == ' ') continue;
        clean += s[i];
    }
    // Strip optional 0x/0X prefix
    if (clean.size() >= 2 && (clean.rfind("0x", 0) == 0 || clean.rfind("0X", 0) == 0)) {
        clean = clean.substr(2);
    }
    if (clean.size() % 2 != 0) {
        fprintf(stderr, "ERROR: Hex pattern must have even number of hex digits\n");
        return false;
    }
    bytes.clear();
    for (size_t i = 0; i < clean.size(); i += 2) {
        try {
            bytes.push_back(static_cast<uint8_t>(std::stoul(clean.substr(i, 2), nullptr, 16)));
        } catch (...) {
            fprintf(stderr, "ERROR: Invalid hex pattern '%s'\n", str);
            return false;
        }
    }
    return !bytes.empty();
}

// Cross-platform case-insensitive string comparison helper
static bool iequalsStr(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

//********************************************************************************************************************
//  doReg - read/write registers by name or address (SVD-based)
//********************************************************************************************************************

static int doReg(int argc, char** argv)
{
    CommonArgs args;
    const char* regName = nullptr;
    const char* writeVal = nullptr;
    bool listMode = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--list") == 0) { listMode = true; i++; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle reg <name|addr> [value] [options]\n");
            printf("       wiggle reg --list [peripheral]\n\n");
            printf("  Read/write registers by SVD name or address.\n");
            printf("  Names: DMU.HF.STATUS, HF.STATUS, or hex address\n\n");
            printf("Options:\n");
            printf("  --list [periph]  List registers (optionally filter by peripheral)\n");
            printf("  --json           Output as JSON\n");
            return EXIT_OK;
        }
        if (!regName) { regName = argv[i]; i++; continue; }
        if (!writeVal) { writeVal = argv[i]; i++; continue; }
        fprintf(stderr, "ERROR: Unexpected argument '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    RESOLVE_CTX(ctx, "WiggleReg", TAS_CLNT_DCO_HOT_ATTACH);

    // Load SVD on demand for reg command
    if (!ctx.svd.isLoaded() && !ctx.flashCfg.svdFilePath.empty()) {
        std::string parentDir = std::filesystem::path(ctx.configDirPath).parent_path().string();
        std::string svdPath;
        if (!parentDir.empty()) {
            svdPath = (std::filesystem::path(parentDir) / ctx.flashCfg.svdFilePath).string();
        } else {
            svdPath = ctx.flashCfg.svdFilePath;
        }
        if (!ctx.svd.loadFromFile(svdPath)) {
            fprintf(stderr, "WARNING: Failed to load register definitions from %s\n", svdPath.c_str());
            fprintf(stderr, "Register operations by name may not be available.\n");
        }
    }

    if (listMode) {
        if (!ctx.svd.isLoaded()) {
            if (g_json) return jsonError(EXIT_DEVICE_ERROR, "No SVD register definitions loaded for this device");
            fprintf(stderr, "ERROR: No SVD register definitions loaded for this device\n");
            return EXIT_DEVICE_ERROR;
        }
        auto periphs = ctx.svd.getPeripheralNames();
        if (g_json) {
            // JSON output for --list mode
            nljson regArray = nljson::array();
            std::string matchedPeripheral;
            for (const auto& p : periphs) {
                if (regName && !iequalsStr(p, regName)) continue;
                if (matchedPeripheral.empty()) matchedPeripheral = p;
                auto regs = ctx.svd.getRegisters(p);
                for (const auto* r : regs) {
                    nljson rj;
                    rj["name"] = r->name;
                    rj["fullName"] = r->fullName;
                    char addrBuf[32];
                    snprintf(addrBuf, sizeof(addrBuf), "0x%08llX", (unsigned long long)r->address);
                    rj["address"] = std::string(addrBuf);
                    rj["size"] = r->size;
                    rj["access"] = r->access;
                    regArray.push_back(rj);
                }
            }
            nljson j;
            j["command"] = "reg_list";
            if (regName) j["peripheral"] = matchedPeripheral.empty() ? std::string(regName) : matchedPeripheral;
            j["registerCount"] = regArray.size();
            j["registers"] = regArray;
            return jsonOk(j);
        }
        // Human-readable output
        for (const auto& p : periphs) {
            if (regName && !iequalsStr(p, regName)) continue;
            auto regs = ctx.svd.getRegisters(p);
            printf("%s (%zu registers):\n", p.c_str(), regs.size());
            for (const auto* r : regs) {
                printf("  0x%08llX  %-20s  %s\n",
                       (unsigned long long)r->address, r->fullName.c_str(),
                       r->access.c_str());
            }
        }
        return EXIT_OK;
    }

    if (!regName) {
        fprintf(stderr, "ERROR: Register name or address required\n");
        return EXIT_USAGE_ERROR;
    }

    // Try SVD name lookup first
    const SvdRegister* svdReg = nullptr;
    uint64_t addr = 0;
    if (ctx.svd.isLoaded()) {
        svdReg = ctx.svd.findRegister(regName);
    }
    if (svdReg) {
        addr = svdReg->address;
    } else if (parseAddress(regName, addr)) {
        // Address parsed successfully; if SVD loaded, try to enrich by address
        if (ctx.svd.isLoaded()) {
            svdReg = ctx.svd.findByAddress(addr);
        }
    } else {
        return EXIT_USAGE_ERROR;
    }

    if (writeVal) {
        // Write register
        uint32_t val = 0;
        try { val = static_cast<uint32_t>(std::stoul(writeVal, nullptr, 0)); }
        catch (...) { fprintf(stderr, "ERROR: Invalid value '%s'\n", writeVal); return EXIT_USAGE_ERROR; }

        tas_return_et ret = ctx.client.write32(addr, val);
        if (ret != TAS_ERR_NONE) {
            if (g_json) return jsonError(EXIT_FLASH_STATUS_ERROR, ctx.client.get_error_info());
            fprintf(stderr, "ERROR: Write failed: %s\n", ctx.client.get_error_info());
            return EXIT_FLASH_STATUS_ERROR;
        }
        JPRINTF("Wrote 0x%08X to 0x%08llX", val, (unsigned long long)addr);
        if (svdReg) JPRINTF(" (%s)", svdReg->fullName.c_str());
        JPRINTF("\n");

        // Readback
        uint32_t readback = 0;
        ret = ctx.client.read32(addr, &readback);
        if (ret == TAS_ERR_NONE) {
            JPRINTF("Readback: 0x%08X\n", readback);
        }
        if (g_json) {
            nljson j;
            j["action"] = "write";
            j["address"] = addr;
            j["value"] = val;
            if (svdReg) j["register"] = svdReg->fullName;
            if (ret == TAS_ERR_NONE) j["readback"] = readback;
            return jsonOk(j);
        }
    } else {
        // Read register
        uint32_t val = 0;
        tas_return_et ret = ctx.client.read32(addr, &val);
        if (ret != TAS_ERR_NONE) {
            if (g_json) return jsonError(EXIT_FLASH_STATUS_ERROR, ctx.client.get_error_info());
            fprintf(stderr, "ERROR: Read failed: %s\n", ctx.client.get_error_info());
            return EXIT_FLASH_STATUS_ERROR;
        }

        if (g_json) {
            nljson j;
            j["action"] = "read";
            j["address"] = addr;
            j["value"] = val;
            if (svdReg) {
                j["register"] = svdReg->fullName;
                j["access"] = svdReg->access;
                nljson fields = nljson::object();
                for (const auto& f : svdReg->fields) {
                    uint32_t mask = ((1u << (f.msb - f.lsb + 1)) - 1u) << f.lsb;
                    uint32_t fval = (val & mask) >> f.lsb;
                    nljson fj;
                    fj["value"] = fval;
                    fj["lsb"] = f.lsb;
                    fj["msb"] = f.msb;
                    for (const auto& ev : f.values) {
                        if (ev.value == fval && !ev.desc.empty()) {
                            fj["desc"] = ev.desc;
                            break;
                        }
                    }
                    fields[f.name] = fj;
                }
                j["fields"] = fields;
            }
            return jsonOk(j);
        }

        if (svdReg) {
            printf("%s [0x%08llX] = 0x%08X (%s)\n",
                   svdReg->fullName.c_str(), (unsigned long long)addr, val,
                   svdReg->access.c_str());
            // Parse fields
            for (const auto& f : svdReg->fields) {
                uint32_t mask = ((1u << (f.msb - f.lsb + 1)) - 1u) << f.lsb;
                uint32_t fval = (val & mask) >> f.lsb;
                printf("  %-12s [%2u:%2u] = 0x%X", f.name.c_str(), f.msb, f.lsb, fval);
                // Show enum description if available
                for (const auto& ev : f.values) {
                    if (ev.value == fval && !ev.desc.empty()) {
                        printf("  : %s", ev.desc.c_str());
                        break;
                    }
                }
                if (!f.desc.empty()) {
                    bool hasEnum = false;
                    for (const auto& ev : f.values) { if (ev.value == fval) { hasEnum = true; break; } }
                    if (!hasEnum) printf("  (%s)", f.desc.c_str());
                }
                printf("\n");
            }
        } else {
            printf("[0x%08llX] = 0x%08X\n", (unsigned long long)addr, val);
        }
    }

    return EXIT_OK;
}

//********************************************************************************************************************
//  doDump - hex dump of memory (PFlash/DFlash/SRAM)
//********************************************************************************************************************

static int doDump(int argc, char** argv)
{
    CommonArgs args;
    uint64_t addr = 0;
    uint32_t length = 0;
    const char* outFile = nullptr;
    bool hasAddr = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            outFile = argv[i + 1]; i += 2; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle dump <addr> <length> [-o file] [options]\n\n");
            printf("  Read memory and display hex dump. Supports PFlash, DFlash, SRAM.\n\n");
            printf("Options:\n");
            printf("  -o, --output <file>  Save to file (.bin=raw, .hex=Intel HEX)\n\n");
            printf("Examples:\n");
            printf("  wiggle dump 0xAF000000 0x40\n");
            printf("  wiggle dump 0xA0000000 0x100 -o pflash.hex\n");
            printf("  wiggle dump 0x70000000 0x80 -o sram.bin\n");
            return EXIT_OK;
        }
        if (!hasAddr) { if (!parseAddress(argv[i], addr)) return EXIT_USAGE_ERROR; hasAddr = true; i++; continue; }
        if (length == 0) {
            try { length = static_cast<uint32_t>(std::stoul(argv[i], nullptr, 0)); }
            catch (...) { fprintf(stderr, "ERROR: Invalid length '%s'\n", argv[i]); return EXIT_USAGE_ERROR; }
            i++; continue;
        }
        fprintf(stderr, "ERROR: Unexpected argument '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    if (!hasAddr || length == 0) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "Address and length required");
        fprintf(stderr, "ERROR: Address and length required\n");
        return EXIT_USAGE_ERROR;
    }

    RESOLVE_CTX(ctx, "WiggleDump", TAS_CLNT_DCO_HOT_ATTACH);

    // Allocate buffer
    std::vector<uint8_t> buf(length);
    uint32_t bytesRead = 0;
    JPRINTF("Reading 0x%X bytes from 0x%08llX...\n", length, (unsigned long long)addr);

    tas_return_et ret = ctx.client.read(addr, buf.data(), length, &bytesRead);
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_FLASH_STATUS_ERROR, ctx.client.get_error_info());
        fprintf(stderr, "ERROR: Read failed: %s\n", ctx.client.get_error_info());
        return EXIT_FLASH_STATUS_ERROR;
    }

    // Save to file if requested
    if (outFile) {
        std::string outStr(outFile);
        std::string ext = std::filesystem::path(outStr).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".hex") {
            // Intel HEX format
            std::ofstream ofs(outStr, std::ios::binary);
            if (!ofs.is_open()) {
                fprintf(stderr, "ERROR: Cannot create file '%s'\n", outFile);
                return EXIT_IO_ERROR;
            }
            // Write data records
            uint32_t currentExtAddr = 0;
            for (uint32_t offset = 0; offset < bytesRead; offset += 16) {
                uint64_t absAddr64 = addr + offset;
                uint32_t extAddr = static_cast<uint32_t>((absAddr64 >> 16) & 0xFFFF);
                if (extAddr != currentExtAddr) {
                    // Extended Linear Address record
                    char line[32];
                    snprintf(line, sizeof(line), ":02000004%04X%02X\r\n",
                             extAddr, (uint8_t)(~(0x02 + 0x04 + (extAddr >> 8) + (extAddr & 0xFF)) + 1));
                    ofs << line;
                    currentExtAddr = extAddr;
                }
                uint16_t localAddr = static_cast<uint16_t>(absAddr64 & 0xFFFF);
                uint8_t count = static_cast<uint8_t>(std::min(16u, bytesRead - offset));
                uint8_t cksum = count + (localAddr >> 8) + (localAddr & 0xFF);
                char header[16];
                snprintf(header, sizeof(header), ":%02X%04X00", count, localAddr);
                ofs << header;
                for (uint8_t j = 0; j < count; ++j) {
                    char byte_str[4];
                    snprintf(byte_str, sizeof(byte_str), "%02X", buf[offset + j]);
                    ofs << byte_str;
                    cksum += buf[offset + j];
                }
                char cksum_str[4];
                snprintf(cksum_str, sizeof(cksum_str), "%02X\r\n", (uint8_t)(~cksum + 1));
                ofs << cksum_str;
            }
            // EOF record
            ofs << ":00000001FF\r\n";
            ofs.close();
            printf("Saved %u bytes to %s (Intel HEX)\n", bytesRead, outFile);
        } else {
            // Raw binary
            std::ofstream ofs(outStr, std::ios::binary);
            if (!ofs.is_open()) {
                fprintf(stderr, "ERROR: Cannot create file '%s'\n", outFile);
                return EXIT_IO_ERROR;
            }
            ofs.write(reinterpret_cast<const char*>(buf.data()), bytesRead);
            ofs.close();
            printf("Saved %u bytes to %s\n", bytesRead, outFile);
        }
    }

    // Print hex dump (or JSON)
    if (g_json) {
        nljson j;
        j["address"] = addr;
        j["bytes"] = bytesRead;
        std::string hexData;
        hexData.reserve(bytesRead * 2);
        for (uint32_t i = 0; i < bytesRead; ++i) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", buf[i]);
            hexData += hex;
        }
        j["data"] = hexData;
        if (outFile) j["output_file"] = outFile;
        return jsonOk(j);
    }

    for (uint32_t offset = 0; offset < bytesRead; offset += 16) {
        printf("%08llX  ", (unsigned long long)(addr + offset));
        // Hex
        for (uint32_t j = 0; j < 16; ++j) {
            if (offset + j < bytesRead)
                printf("%02X ", buf[offset + j]);
            else
                printf("   ");
            if (j == 7) printf(" ");
        }
        printf(" |");
        // ASCII
        for (uint32_t j = 0; j < 16 && (offset + j) < bytesRead; ++j) {
            uint8_t c = buf[offset + j];
            printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        printf("|\n");
    }

    return EXIT_OK;
}

//********************************************************************************************************************
//  doPoke - write value to memory address
//********************************************************************************************************************

static int doPoke(int argc, char** argv)
{
    CommonArgs args;
    uint64_t addr = 0;
    uint32_t value = 0;
    int width = 32;
    bool hasAddr = false, hasVal = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = atoi(argv[i + 1]); i += 2; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle poke <addr> <value> [--width 8|16|32|64] [options]\n\n");
            printf("  Write a value to a memory address and verify with readback.\n");
            return EXIT_OK;
        }
        if (!hasAddr) { if (!parseAddress(argv[i], addr)) return EXIT_USAGE_ERROR; hasAddr = true; i++; continue; }
        if (!hasVal) {
            try { value = static_cast<uint32_t>(std::stoul(argv[i], nullptr, 0)); }
            catch (...) { fprintf(stderr, "ERROR: Invalid value '%s'\n", argv[i]); return EXIT_USAGE_ERROR; }
            hasVal = true; i++; continue;
        }
        fprintf(stderr, "ERROR: Unexpected argument '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    if (!hasAddr || !hasVal) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "Address and value required");
        fprintf(stderr, "ERROR: Address and value required\n");
        return EXIT_USAGE_ERROR;
    }

    RESOLVE_CTX(ctx, "WigglePoke", TAS_CLNT_DCO_HOT_ATTACH);

    tas_return_et ret;
    switch (width) {
        case 8:  ret = ctx.client.write8(addr, static_cast<uint8_t>(value)); break;
        case 16: ret = ctx.client.write16(addr, static_cast<uint16_t>(value)); break;
        case 64: ret = ctx.client.write64(addr, static_cast<uint64_t>(value)); break;
        default: ret = ctx.client.write32(addr, value); width = 32; break;
    }
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_FLASH_STATUS_ERROR, ctx.client.get_error_info());
        fprintf(stderr, "ERROR: Write failed: %s\n", ctx.client.get_error_info());
        return EXIT_FLASH_STATUS_ERROR;
    }
    JPRINTF("Wrote 0x%X to 0x%08llX (width=%d)\n", value, (unsigned long long)addr, width);

    // Readback verification
    uint32_t readback = 0;
    ret = ctx.client.read32(addr, &readback);

    // Compute mask based on write width and compare readback against written value
    uint32_t mask;
    switch (width) {
        case 8:  mask = 0x000000FFu; break;
        case 16: mask = 0x0000FFFFu; break;
        case 64: mask = 0xFFFFFFFFu; break; // compare low 32 bits via read32
        default: mask = 0xFFFFFFFFu; break;
    }
    bool match = (ret == TAS_ERR_NONE) && ((readback & mask) == (value & mask));

    if (g_json) {
        nljson j;
        j["action"] = "poke";
        j["address"] = addr;
        j["value"] = value;
        j["width"] = width;
        if (ret == TAS_ERR_NONE) {
            j["readback"] = readback;
            j["match"] = match;
        }
        return jsonOk(j);
    }
    if (ret == TAS_ERR_NONE) {
        printf("Readback: 0x%08X\n", readback);
        if (!match) {
            printf("WARNING: Readback (0x%08X) does not match written value (0x%08X) within width=%d mask\n",
                   readback & mask, value & mask, width);
        }
    }
    return EXIT_OK;
}

//********************************************************************************************************************
//  doStatus - Flash status register (SVD-driven)
//********************************************************************************************************************

static int doStatus(int argc, char** argv)
{
    CommonArgs args;
    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle status [options]\n\n");
            printf("  Display Flash status register with field decoding.\n");
            printf("  Uses SVD definitions (no hardcoded addresses).\n");
            return EXIT_OK;
        }
        i++;
    }
    g_json = args.jsonOutput;

    RESOLVE_CTX(ctx, "WiggleStatus", TAS_CLNT_DCO_HOT_ATTACH);

    // Status register
    std::string statusName;
    if (ctx.svd.isLoaded()) {
        statusName = ctx.svd.getFlashStatusRegName();
    }

    uint64_t statusAddr;
    std::string statusRegName;
    if (!statusName.empty()) {
        const SvdRegister* sr = ctx.svd.findRegister(statusName);
        statusAddr = sr->address;
        statusRegName = sr->fullName;
        JPRINTF("Flash Status: %s [0x%08llX]\n", sr->fullName.c_str(), (unsigned long long)statusAddr);
    } else {
        // Fallback to hardcoded addresses
        statusAddr = ctx.flashCfg.isTc3x ? DMU_HF_STATUS_ADDR : FLASH0_FSR_ADDR;
        JPRINTF("Flash Status: [0x%08llX] (no SVD, using fallback)\n", (unsigned long long)statusAddr);
    }

    uint32_t statusVal = 0;
    tas_return_et ret = ctx.client.read32(statusAddr, &statusVal);
    if (ret != TAS_ERR_NONE) {
        if (g_json) return jsonError(EXIT_FLASH_STATUS_ERROR, ctx.client.get_error_info());
        fprintf(stderr, "ERROR: Read failed: %s\n", ctx.client.get_error_info());
        return EXIT_FLASH_STATUS_ERROR;
    }
    JPRINTF("  Value: 0x%08X\n", statusVal);

    // Parse fields using SVD - collect for JSON
    nljson fieldsJson = nljson::object();
    if (!statusName.empty()) {
        const SvdRegister* sr = ctx.svd.findRegister(statusName);
        for (const auto& f : sr->fields) {
            uint32_t mask = ((1u << (f.msb - f.lsb + 1)) - 1u) << f.lsb;
            uint32_t fval = (statusVal & mask) >> f.lsb;
            JPRINTF("  %-12s [%2u:%2u] = %u", f.name.c_str(), f.msb, f.lsb, fval);
            for (const auto& ev : f.values) {
                if (ev.value == fval && !ev.desc.empty()) {
                    JPRINTF("  (%s)", ev.desc.c_str());
                    break;
                }
            }
            JPRINTF("\n");
            fieldsJson[f.name] = fval;
        }
    } else {
        // Fallback for TC3x
        if (ctx.flashCfg.isTc3x) {
            JPRINTF("  D0BUSY=%u  D1BUSY=%u  P0BUSY=%u\n",
                   !!(statusVal & TC3X_D0BUSY_BIT),
                   !!(statusVal & (1u<<1)),
                   !!(statusVal & (1u<<2)));
            fieldsJson["D0BUSY"] = !!(statusVal & TC3X_D0BUSY_BIT);
            fieldsJson["D1BUSY"] = !!(statusVal & (1u<<1));
            fieldsJson["P0BUSY"] = !!(statusVal & (1u<<2));
        } else {
            JPRINTF("  D0BUSY=%u\n", !!(statusVal & TC2X_D0BUSY_BIT));
            fieldsJson["D0BUSY"] = !!(statusVal & TC2X_D0BUSY_BIT);
        }
    }

    // Error status register
    std::string errName;
    if (ctx.svd.isLoaded()) {
        errName = ctx.svd.getFlashErrorRegName();
    }
    nljson errFieldsJson = nljson::object();
    if (!errName.empty() && errName != statusName) {
        const SvdRegister* er = ctx.svd.findRegister(errName);
        uint32_t errVal = 0;
        ret = ctx.client.read32(er->address, &errVal);
        if (ret == TAS_ERR_NONE) {
            JPRINTF("\nFlash Error Status: %s [0x%08llX]\n", er->fullName.c_str(), (unsigned long long)er->address);
            JPRINTF("  Value: 0x%08X\n", errVal);
            for (const auto& f : er->fields) {
                uint32_t mask = ((1u << (f.msb - f.lsb + 1)) - 1u) << f.lsb;
                uint32_t fval = (errVal & mask) >> f.lsb;
                if (fval != 0) {
                    JPRINTF("  %-12s [%2u:%2u] = %u", f.name.c_str(), f.msb, f.lsb, fval);
                    for (const auto& ev : f.values) {
                        if (ev.value == fval && !ev.desc.empty()) {
                            JPRINTF("  (%s)", ev.desc.c_str());
                            break;
                        }
                    }
                    JPRINTF("\n");
                }
                errFieldsJson[f.name] = fval;
            }
        }
    }

    if (g_json) {
        nljson j;
        j["register"] = statusRegName.empty() ? "unknown" : statusRegName;
        j["address"] = statusAddr;
        j["value"] = statusVal;
        j["fields"] = fieldsJson;
        if (!errFieldsJson.empty()) {
            j["error_register"] = errName;
            j["error_fields"] = errFieldsJson;
        }
        return jsonOk(j);
    }

    return EXIT_OK;
}

//********************************************************************************************************************
//  doInfo - device information
//********************************************************************************************************************

static int doInfo(int argc, char** argv)
{
    CommonArgs args;
    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle info [options]\n\n");
            printf("  Display device info, memory layout, and SVD register summary.\n");
            return EXIT_OK;
        }
        i++;
    }
    g_json = args.jsonOutput;

    RESOLVE_CTX(ctx, "WiggleInfo", TAS_CLNT_DCO_HOT_ATTACH);

    // Device info
    JPRINTF("=== Device Information ===\n");
    JPRINTF("  Device:    %s (%s)\n", ctx.flashCfg.deviceName.c_str(), ctx.flashCfg.family.c_str());
    JPRINTF("  JTAG ID:   0x%08X\n", ctx.conInfo->device_type);
    JPRINTF("  Name:      %s\n", tas_get_device_name_str(ctx.conInfo->device_type));
    JPRINTF("  Identifier: %s\n", ctx.conInfo->identifier);
    JPRINTF("  Phys:       %s\n", ctx.conInfo->dev_con_phys == 0 ? "JTAG/DAP" : "Other");

    // Memory regions
    JPRINTF("\n=== Memory Layout ===\n");
    for (const auto& mr : ctx.flashCfg.memoryRegions) {
        const char* type = mr.isSRAM ? "SRAM" : "Flash";
        JPRINTF("  %-20s  0x%08llX - 0x%08llX  %8u bytes  (%s)\n",
               mr.name.c_str(),
               (unsigned long long)mr.startAddr,
               (unsigned long long)mr.endAddr,
               mr.size, type);
    }

    // DFlash details
    JPRINTF("\n=== DFlash ===\n");
    JPRINTF("  Base:      0x%08X\n", ctx.flashCfg.baseAddress);
    JPRINTF("  Size:      %u bytes (0x%X)\n", ctx.flashCfg.totalSize, ctx.flashCfg.totalSize);
    JPRINTF("  Sectors:   %u x 0x%X\n", ctx.flashCfg.numSectors, ctx.flashCfg.sectorSize);

    // UCB
    if (ctx.flashCfg.ucb.numSectors > 0) {
        JPRINTF("\n=== UCB ===\n");
        JPRINTF("  Base:      0x%08X\n", ctx.flashCfg.ucb.baseAddress);
        JPRINTF("  Sectors:   %u x 0x%X\n", ctx.flashCfg.ucb.numSectors, ctx.flashCfg.ucb.sectorSize);
    }

    // SVD registers
    if (ctx.svd.isLoaded()) {
        JPRINTF("\n=== SVD Registers (%s) ===\n", ctx.svd.getDeviceName().c_str());
        auto periphs = ctx.svd.getPeripheralNames();
        for (const auto& p : periphs) {
            auto regs = ctx.svd.getRegisters(p);
            JPRINTF("  %-8s  %zu registers\n", p.c_str(), regs.size());
        }
    } else {
        JPRINTF("\n  (No SVD register definitions loaded)\n");
    }

    if (g_json) {
        nljson j;
        j["device"] = ctx.flashCfg.deviceName;
        j["family"] = ctx.flashCfg.family;
        j["jtag_id"] = ctx.conInfo->device_type;
        j["device_name"] = tas_get_device_name_str(ctx.conInfo->device_type);
        j["identifier"] = ctx.conInfo->identifier;
        j["phys"] = ctx.conInfo->dev_con_phys == 0 ? "JTAG/DAP" : "Other";

        nljson memArr = nljson::array();
        for (const auto& mr : ctx.flashCfg.memoryRegions) {
            memArr.push_back({
                {"name", mr.name}, {"start", mr.startAddr}, {"end", mr.endAddr},
                {"size", mr.size}, {"type", mr.isSRAM ? "SRAM" : "Flash"}
            });
        }
        j["memory_regions"] = memArr;

        j["dflash"] = {
            {"base", ctx.flashCfg.baseAddress}, {"size", ctx.flashCfg.totalSize},
            {"sectors", ctx.flashCfg.numSectors}, {"sector_size", ctx.flashCfg.sectorSize}
        };

        if (ctx.flashCfg.ucb.numSectors > 0) {
            j["ucb"] = {
                {"base", ctx.flashCfg.ucb.baseAddress},
                {"sectors", ctx.flashCfg.ucb.numSectors},
                {"sector_size", ctx.flashCfg.ucb.sectorSize}
            };
        }

        if (ctx.svd.isLoaded()) {
            nljson svdArr = nljson::array();
            auto periphs = ctx.svd.getPeripheralNames();
            for (const auto& p : periphs) {
                auto regs = ctx.svd.getRegisters(p);
                svdArr.push_back({{"peripheral", p}, {"register_count", regs.size()}});
            }
            j["svd"] = {{"device", ctx.svd.getDeviceName()}, {"peripherals", svdArr}};
        }
        return jsonOk(j);
    }

    return EXIT_OK;
}

//********************************************************************************************************************
//  doCompare - compare local file with Flash content
//********************************************************************************************************************

static int doCompare(int argc, char** argv)
{
    CommonArgs args;
    const char* filePath = nullptr;
    uint64_t addr = 0;
    uint32_t length = 0;
    bool hasAddr = false;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc) {
            filePath = argv[i + 1]; i += 2; continue;
        }
        if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--addr") == 0) && i + 1 < argc) {
            if (!parseAddress(argv[i + 1], addr)) return EXIT_USAGE_ERROR;
            hasAddr = true; i += 2; continue;
        }
        if (strcmp(argv[i], "--length") == 0 && i + 1 < argc) {
            try { length = static_cast<uint32_t>(std::stoul(argv[i + 1], nullptr, 0)); }
            catch (...) { fprintf(stderr, "ERROR: Invalid length\n"); return EXIT_USAGE_ERROR; }
            i += 2; continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle compare -f <file> -a <addr> [--length <len>] [options]\n\n");
            printf("  Compare local file contents with Flash/memory.\n");
            printf("  Supports .hex (Intel HEX) and .bin files.\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unexpected argument '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    if (!filePath || !hasAddr) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "File (-f) and address (-a) required");
        fprintf(stderr, "ERROR: File (-f) and address (-a) required\n");
        return EXIT_USAGE_ERROR;
    }

    // Load file
    std::string fileStr(filePath);
    std::string ext = std::filesystem::path(fileStr).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    HexParseResult fileData;
    if (ext == ".hex") {
        fileData = parseIntelHex(fileStr);
    } else {
        fileData = loadBinaryFile(fileStr, static_cast<uint32_t>(addr));
    }
    if (!fileData.success) {
        if (g_json) return jsonError(EXIT_HEX_PARSE_ERROR, fileData.errorMsg);
        fprintf(stderr, "ERROR: %s\n", fileData.errorMsg.c_str());
        return EXIT_HEX_PARSE_ERROR;
    }
    if (!fileData.warningMsg.empty())
        fprintf(stderr, "WARNING: %s\n", fileData.warningMsg.c_str());

    RESOLVE_CTX(ctx, "WiggleCompare", TAS_CLNT_DCO_HOT_ATTACH);

    uint32_t totalMismatches = 0;
    uint32_t totalBytes = 0;

    for (const auto& seg : fileData.segments) {
        uint64_t segAddr = seg.baseAddress;
        uint32_t segLen = static_cast<uint32_t>(seg.data.size());
        if (length > 0 && segLen > length) segLen = length;

        std::vector<uint8_t> flashData(segLen);
        uint32_t bytesRead = 0;
        tas_return_et ret = ctx.client.read(segAddr, flashData.data(), segLen, &bytesRead);
        if (ret != TAS_ERR_NONE) {
            fprintf(stderr, "ERROR: Read failed at 0x%08llX: %s\n",
                    (unsigned long long)segAddr, ctx.client.get_error_info());
            return EXIT_FLASH_STATUS_ERROR;
        }

        printf("Comparing %s with memory @ 0x%08X (%u bytes)...\n",
               filePath, seg.baseAddress, bytesRead);

        uint32_t mismatches = 0;
        for (uint32_t j = 0; j < bytesRead; ++j) {
            if (seg.data[j] != flashData[j]) {
                if (mismatches < 20) {
                    printf("  [0x%08llX] expected=0x%02X actual=0x%02X\n",
                           (unsigned long long)(segAddr + j), seg.data[j], flashData[j]);
                }
                mismatches++;
            }
        }
        totalMismatches += mismatches;
        totalBytes += bytesRead;
        if (mismatches > 20) printf("  ... (%u more mismatches)\n", mismatches - 20);
    }

    JPRINTF("  Result: %u mismatches in %u bytes\n", totalMismatches, totalBytes);

    if (g_json) {
        nljson j;
        j["file"] = filePath;
        j["total_bytes"] = totalBytes;
        j["mismatches"] = totalMismatches;
        j["match"] = (totalMismatches == 0);
        return jsonOk(j);
    }
    return (totalMismatches > 0) ? EXIT_MISMATCH_ERROR : EXIT_OK;
}

//********************************************************************************************************************
//  doSearch - search memory for byte pattern
//********************************************************************************************************************

static int doSearch(int argc, char** argv)
{
    CommonArgs args;
    uint64_t addr = 0;
    uint32_t length = 0;
    const char* patternStr = nullptr;
    int argIdx = 0;

    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle search <addr> <length> <pattern> [options]\n\n");
            printf("  Search memory for a hex byte pattern.\n\n");
            printf("Examples:\n");
            printf("  wiggle search 0xAF000000 0x20000 DEADBEEF\n");
            printf("  wiggle search 0xA0000000 0x100000 00FF00\n");
            return EXIT_OK;
        }
        if (argIdx == 0) { if (!parseAddress(argv[i], addr)) return EXIT_USAGE_ERROR; argIdx++; i++; continue; }
        if (argIdx == 1) {
            try { length = static_cast<uint32_t>(std::stoul(argv[i], nullptr, 0)); }
            catch (...) { fprintf(stderr, "ERROR: Invalid length\n"); return EXIT_USAGE_ERROR; }
            argIdx++; i++; continue;
        }
        if (argIdx == 2) { patternStr = argv[i]; argIdx++; i++; continue; }
        fprintf(stderr, "ERROR: Unexpected argument '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }
    g_json = args.jsonOutput;

    if (!patternStr) {
        if (g_json) return jsonError(EXIT_USAGE_ERROR, "Address, length, and pattern required");
        fprintf(stderr, "ERROR: Address, length, and pattern required\n");
        return EXIT_USAGE_ERROR;
    }

    std::vector<uint8_t> pattern;
    if (!parseHexBytes(patternStr, pattern)) return EXIT_USAGE_ERROR;

    RESOLVE_CTX(ctx, "WiggleSearch", TAS_CLNT_DCO_HOT_ATTACH);

    // Read memory in chunks for large searches
    const uint32_t CHUNK_SIZE = 0x10000;  // 64KB chunks
    std::vector<uint8_t> buf(CHUNK_SIZE + pattern.size() - 1);
    uint32_t matchCount = 0;
    std::vector<uint64_t> matchAddrs;  // for JSON output

    JPRINTF("Searching 0x%08llX - 0x%08llX for pattern [",
           (unsigned long long)addr, (unsigned long long)(addr + length - 1));
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (i > 0) JPRINTF(" ");
        JPRINTF("%02X", pattern[i]);
    }
    JPRINTF("]...\n");

    for (uint32_t offset = 0; offset < length; ) {
        uint32_t chunkLen = std::min(CHUNK_SIZE, length - offset);
        // Read extra bytes for pattern overlap at chunk boundaries
        uint32_t readLen = chunkLen;
        if (offset + chunkLen < length) {
            readLen = chunkLen + static_cast<uint32_t>(pattern.size()) - 1;
            if (offset + readLen > length) readLen = length - offset;
        }

        uint32_t bytesRead = 0;
        tas_return_et ret = ctx.client.read(addr + offset, buf.data(), readLen, &bytesRead);
        if (ret != TAS_ERR_NONE) {
            if (g_json) return jsonError(EXIT_FLASH_STATUS_ERROR, ctx.client.get_error_info());
            fprintf(stderr, "ERROR: Read failed at offset 0x%X: %s\n", offset, ctx.client.get_error_info());
            return EXIT_FLASH_STATUS_ERROR;
        }

        // Search within chunk
        for (uint32_t j = 0; j + pattern.size() <= bytesRead; ++j) {
            if (memcmp(&buf[j], pattern.data(), pattern.size()) == 0) {
                uint64_t matchAddr = addr + offset + j;
                JPRINTF("  Found at 0x%08llX\n", (unsigned long long)matchAddr);
                matchCount++;
                if (g_json && matchAddrs.size() < 1000) matchAddrs.push_back(matchAddr);
                if (matchCount >= 1000) {
                    JPRINTF("  (stopped after 1000 matches)\n");
                    JPRINTF("%u matches found\n", matchCount);
                    if (g_json) {
                        nljson j;
                        j["address"] = addr;
                        j["length"] = length;
                        std::string patStr;
                        for (auto b : pattern) { char h[3]; snprintf(h,3,"%02X",b); patStr += h; }
                        j["pattern"] = patStr;
                        j["matches"] = matchAddrs;
                        j["match_count"] = matchCount;
                        j["truncated"] = true;
                        return jsonOk(j);
                    }
                    return EXIT_OK;
                }
            }
        }
        offset += chunkLen;
    }

    JPRINTF("%u matches found\n", matchCount);

    if (g_json) {
        nljson j;
        j["address"] = addr;
        j["length"] = length;
        std::string patStr;
        for (auto b : pattern) { char h[3]; snprintf(h,3,"%02X",b); patStr += h; }
        j["pattern"] = patStr;
        j["matches"] = matchAddrs;
        j["match_count"] = matchCount;
        return jsonOk(j);
    }
    return (matchCount > 0) ? EXIT_OK : EXIT_NOT_FOUND;
}

//********************************************************************************************************************
//********************************************************************************************************************
//  doShell - interactive REPL
//********************************************************************************************************************

static int doShell(int argc, char** argv)
{
    CommonArgs args;
    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wiggle shell [options]\n\n");
            printf("  Start interactive REPL mode. Connection is kept alive.\n");
            printf("  Type 'help' for commands, 'quit' to exit.\n");
            return EXIT_OK;
        }
        i++;
    }

    ToolContext ctx("WiggleShell");
    tas_clnt_dco_et shellDco = g_hotAttach ? TAS_CLNT_DCO_HOT_ATTACH : TAS_CLNT_DCO_RESET_AND_HALT;
    int rc = initTool(ctx, args, "WiggleShell", shellDco);
    if (rc != EXIT_OK) return rc;

    // Set shell shared context - subcommands will reuse this connection
    g_shellCtx = &ctx;

    printf("\n=== Wiggle Shell (v%s) ===\n", WIGGLE_VERSION);
    printf("Device: %s (%s)\n", ctx.flashCfg.deviceName.c_str(), ctx.flashCfg.family.c_str());
    if (ctx.svd.isLoaded()) {
        printf("SVD: %s (%zu registers)\n", ctx.svd.getDeviceName().c_str(), ctx.svd.getRegisterCount());
    }
    printf("Type 'help' for commands, 'quit' to exit.\n\n");

    std::string line;
    while (true) {
        printf("wiggle> ");
        fflush(stdout);
        if (!std::getline(std::cin, line)) break;

        // Trim
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);
        if (line.empty()) continue;

        // Parse into argv
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok) tokens.push_back(tok);
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];
        if (cmd == "quit" || cmd == "exit" || cmd == "q") break;
        if (cmd == "help" || cmd == "?") {
            printf("Commands: erase write rewrite restore read dump poke reg status info compare search reset list shell help quit\n");
            continue;
        }

        // Dispatch subcommands - they reuse g_shellCtx via RESOLVE_CTX
        std::vector<char*> subArgv;
        for (size_t i = 1; i < tokens.size(); ++i) {
            subArgv.push_back(const_cast<char*>(tokens[i].c_str()));
        }
        int subArgc = static_cast<int>(subArgv.size());
        char** subArgvPtr = subArgv.empty() ? nullptr : subArgv.data();

        if (cmd == "dump") doDump(subArgc, subArgvPtr);
        else if (cmd == "poke") doPoke(subArgc, subArgvPtr);
        else if (cmd == "reg") doReg(subArgc, subArgvPtr);
        else if (cmd == "status") doStatus(subArgc, subArgvPtr);
        else if (cmd == "info") doInfo(subArgc, subArgvPtr);
        else if (cmd == "compare") doCompare(subArgc, subArgvPtr);
        else if (cmd == "search") doSearch(subArgc, subArgvPtr);
        else if (cmd == "erase") doErase(subArgc, subArgvPtr, false);
        else if (cmd == "write") doWrite(subArgc, subArgvPtr);
        else if (cmd == "rewrite") doRewrite(subArgc, subArgvPtr);
        else if (cmd == "restore") doRestore(subArgc, subArgvPtr);
        else if (cmd == "read") doRead(subArgc, subArgvPtr);
        else if (cmd == "reset") doReset(subArgc, subArgvPtr);
        else if (cmd == "list") doList(subArgc, subArgvPtr);
        else printf("Unknown command '%s'. Type 'help' for list.\n", cmd.c_str());
        fflush(stdout);  // Ensure output is flushed to pipe for MCP integration
    }

    g_shellCtx = nullptr;  // Clear shell shared context
    printf("\nExiting shell.\n");
    return EXIT_OK;
}

//********************************************************************************************************************
//  Usage
//********************************************************************************************************************

static void printUsage(const char* progName, const DeviceConfigLoader* loader = nullptr)
{
    // Show only filename, not full path
    std::string baseName = std::filesystem::path(progName).filename().string();
    const char* base = baseName.c_str();

    printf("TAS Wiggle Tool for AURIX\n\n");
    printf("Usage:\n");
    printf("  %s <subcommand> [options]\n\n", base);
    printf("Subcommands:\n");
    printf("  erase    Erase DFlash sectors\n");
    printf("  write    Write data to DFlash from HEX/BIN file\n");
    printf("  rewrite  Write to DFlash at arbitrary address (Read-Modify-Write)\n");
    printf("  restore  Restore DFlash from backup (erase + write + verify)\n");
    printf("  read     Read DFlash content\n");
    printf("  list     List connected TAS targets\n");
    printf("  reset    Reset the MCU\n");
    printf("  ucb      UCB (User Configuration Block) read/write (TC3XX only)\n\n");
    printf("Debug:\n");
    printf("  reg      Read/write registers by name or address (SVD-based)\n");
    printf("  dump     Hex dump of memory (PFlash/DFlash/SRAM)\n");
    printf("  poke     Write value to memory address\n");
    printf("  status   Flash status register (SVD-driven field decoding)\n");
    printf("  info     Device info, memory layout, SVD summary\n");
    printf("  compare  Compare local file with Flash content\n");
    printf("  search   Search memory for byte pattern\n");
    printf("  shell    Interactive REPL mode\n\n");
    printf("Legacy (backward compatible):\n");
    printf("  %s <addr> <num_sectors> [options]\n", base);
    printf("  %s --all [options]\n", base);
    printf("  %s --info [options]\n\n", base);
    printf("Run '%s <subcommand> --help' for details.\n\n", base);
    printf("Global options:\n");
    printf("  --json             Output structured JSON (for MCP/AI integration)\n");
    printf("  --server <ip>      TAS server IP (default: localhost)\n");
    printf("  --target <id>      Target identifier\n");
    printf("  --device <name>    Override device type\n");
    printf("  --hot              Use HOT_ATTACH (no MCU reset)\n");
    printf("  --config-dir <path> DeviceConfigs directory\n\n");
    printf("Examples:\n");
    printf("  %s list\n", base);
    printf("  %s erase --all --verify\n", base);
    printf("  %s write -f data.hex --verify\n", base);
    printf("  %s dump 0xAF000000 0x40\n", base);
    printf("  %s reg DMU.HF.STATUS\n", base);
    printf("  %s status\n", base);
    printf("  %s info\n", base);
    printf("  %s shell\n\n", base);

    if (loader && loader->getDeviceCount() > 0) {
        printf("Supported devices (from DeviceConfigs):\n  ");
        auto names = loader->getSupportedDeviceNames();
        for (size_t i = 0; i < names.size(); i++) {
            printf("%s", names[i].c_str());
            if (i + 1 < names.size()) printf(", ");
        }
        printf("\n");
    } else {
        printf("Run with '--info' (or 'erase --info') to see supported devices.\n");
        printf("Set DeviceConfigs directory via --config-dir <path> or TAS_DEVICE_CONFIGS env var.\n");
    }
}

//********************************************************************************************************************
//  Main - subcommand dispatch with legacy backward compatibility
//********************************************************************************************************************

int main(int argc, char** argv)
{
    if (argc < 2) { printUsage(argv[0]); return EXIT_USAGE_ERROR; }

    std::string first(argv[1]);

    // Help
    if (first == "--help" || first == "-h") { printUsage(argv[0]); return EXIT_OK; }

    // Version
    if (first == "--version" || first == "-v") { printf("wiggle version %s\n", WIGGLE_VERSION); return EXIT_OK; }

    // Subcommands
    if (first == "erase" || first == "e") return doErase(argc - 2, argv + 2, false);
    if (first == "write" || first == "w") return doWrite(argc - 2, argv + 2);
    if (first == "rewrite" || first == "rw") return doRewrite(argc - 2, argv + 2);
    if (first == "restore") return doRestore(argc - 2, argv + 2);
    if (first == "read"  || first == "r") return doRead(argc - 2, argv + 2);
    if (first == "list"  || first == "l") return doList(argc - 2, argv + 2);
    if (first == "reset") return doReset(argc - 2, argv + 2);

    // Debug subcommands
    if (first == "reg") return doReg(argc - 2, argv + 2);
    if (first == "dump") return doDump(argc - 2, argv + 2);
    if (first == "poke") return doPoke(argc - 2, argv + 2);
    if (first == "status") return doStatus(argc - 2, argv + 2);
    if (first == "info") return doInfo(argc - 2, argv + 2);
    if (first == "compare") return doCompare(argc - 2, argv + 2);
    if (first == "search") return doSearch(argc - 2, argv + 2);
    if (first == "shell") return doShell(argc - 2, argv + 2);

    if (first == "ucb") {
        if (argc < 3) { printUcbUsage(argv[0]); return EXIT_USAGE_ERROR; }
        std::string ucbSub(argv[2]);
        if (ucbSub == "read"  || ucbSub == "r") return doUcbRead(argc - 3, argv + 3);
        if (ucbSub == "write" || ucbSub == "w") return doUcbWrite(argc - 3, argv + 3);
        if (ucbSub == "erase" || ucbSub == "e") return doUcbErase(argc - 3, argv + 3);
        if (ucbSub == "--help" || ucbSub == "-h") { printUcbUsage(argv[0]); return EXIT_OK; }
        fprintf(stderr, "ERROR: Unknown ucb subcommand '%s'\n", argv[2]);
        printUcbUsage(argv[0]);
        return EXIT_USAGE_ERROR;
    }

    // Legacy backward compatibility: --all, --info, or hex address as first arg
    if (first == "--all" || first == "--info" ||
        first.rfind("0x", 0) == 0 || first.rfind("0X", 0) == 0) {
        return doErase(argc - 1, argv + 1, true);
    }

    fprintf(stderr, "ERROR: Unknown subcommand '%s'\n", argv[1]);
    printUsage(argv[0]);
    return EXIT_USAGE_ERROR;
}
