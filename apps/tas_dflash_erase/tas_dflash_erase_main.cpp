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
//  TAS DFlash Tool for AURIX
//  Subcommands: erase, read, list, reset
//  Device configurations are loaded at runtime from JSON files (DeviceConfigs/).
//********************************************************************************************************************

// Prevent Windows min/max macros from interfering with std::min/std::max
#define NOMINMAX

#include "tas_client_rw.h"
#include "tas_utils.h"
#include "tas_device_family.h"

#include "device_config_loader.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <filesystem>

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

static constexpr uint32_t DFLASH_ERASED_BYTE      = 0x00u;

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
static constexpr int EXIT_WDTPW_ERROR        = 9;   // Safety Watchdog password read failed
static constexpr int EXIT_ENDINIT_CLR_ERROR  = 10;  // Clear Safety EndInit failed
static constexpr int EXIT_FLASH_STATUS_ERROR = 11;  // Flash status clear failed
static constexpr int EXIT_ERASE_CMD_ERROR    = 12;  // Erase command failed
static constexpr int EXIT_ENDINIT_SET_ERROR  = 13;  // Restore Safety EndInit failed
static constexpr int EXIT_ERASE_TIMEOUT      = 14;  // Erase timeout
static constexpr int EXIT_ERASE_FLAGS        = 15;  // Error flags detected
static constexpr int EXIT_FLASH_RESET_ERROR  = 16;  // Reset to read mode failed
static constexpr int EXIT_VERIFY_ERROR       = 17;  // Verification failed
static constexpr int EXIT_IO_ERROR           = 18;  // File I/O error

static constexpr uint32_t WAIT_UNBUSY_TIMEOUT_MS  = 10000;
static constexpr uint32_t POLL_INTERVAL_MS         = 10;

//********************************************************************************************************************
//  TC4x rejection helper (TC4x has different DFlash base addresses and erase command sequence)
//********************************************************************************************************************

// Check if device is unsupported TC4x, print error and return EXIT_DEVICE_ERROR if so.
// Returns EXIT_OK if device is OK (not TC4x), or EXIT_DEVICE_ERROR if TC4x detected.
static int rejectTc4xDevice(const tas_con_info_st* conInfo)
{
    if (tas_df_check_if_tc4x(conInfo->device_type)) {
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
    // 2. exe-side DeviceConfigs/
    char exePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        fs::path exeDir = fs::path(exePath).parent_path();

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
    // sscanf with %x handles both "AF000000" and "0xAF000000" formats
    return sscanf(s, "%x", &val) == 1;
}

//********************************************************************************************************************
//  Common argument parsing
//********************************************************************************************************************

struct CommonArgs {
    const char* serverIp = "localhost";
    const char* targetId = nullptr;
    const char* deviceName = nullptr;
    const char* configDir = nullptr;
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
    if (strcmp(argv[idx], "--device") == 0 && idx + 1 < argc) {
        args.deviceName = argv[idx + 1];
        return 2;
    }
    if (strcmp(argv[idx], "--config-dir") == 0 && idx + 1 < argc) {
        args.configDir = argv[idx + 1];
        return 2;
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
//  doList - list connected targets
//********************************************************************************************************************

static int doList(int argc, char** argv)
{
    CommonArgs args;
    for (int i = 0; i < argc; ) {
        int consumed = parseCommonOption(argc, argv, i, args);
        if (consumed > 0) { i += consumed; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: dflash list [options]\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    printf("TAS DFlash Tool - Device List\n");
    printf("=============================\n");
    printf("Connecting to TAS server at %s...\n", args.serverIp);

    CTasClientRw client("DFlashList");
    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SERVER_ERROR; }

    const tas_server_info_st* si = client.get_server_info();
    printf("  Server: %s V%d.%d (%s)\n", si->server_name, si->v_major, si->v_minor, si->date);

    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_TARGET_ERROR; }

    printf("  Targets (%u):\n", numTargets);
    for (uint32_t i = 0; i < numTargets; i++) {
        printf("  [%u] %-12s  %s\n", i,
               tas_get_device_name_str(targets[i].device_type),
               targets[i].identifier);
    }
    if (numTargets == 0) printf("  No targets found.\n");
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
            printf("Usage: dflash reset [options]\n\n"
                   "Options:\n"
                   "  --halt              Reset and halt (default: reset and run)\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    printf("TAS DFlash Tool - MCU Reset\n");
    printf("===========================\n");
    printf("Connecting to TAS server at %s...\n", args.serverIp);

    CTasClientRw client("DFlashReset");
    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SERVER_ERROR; }

    const tas_server_info_st* si = client.get_server_info();
    printf("  Server: %s V%d.%d (%s)\n", si->server_name, si->v_major, si->v_minor, si->date);

    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_TARGET_ERROR; }

    if (numTargets == 0) { fprintf(stderr, "ERROR: No targets found\n"); return EXIT_NO_TARGET; }

    const char* selectedTargetId = (args.targetId != nullptr) ? args.targetId : targets[0].identifier;
    if (args.targetId == nullptr) {
        printf("  Target: [0] %s (%s)\n",
               tas_get_device_name_str(targets[0].device_type), targets[0].identifier);
    }

    ret = startSession(client, selectedTargetId, "DFlashReset");
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SESSION_ERROR; }

    tas_clnt_dco_et dco = halt ? TAS_CLNT_DCO_RESET_AND_HALT : TAS_CLNT_DCO_RESET;
    printf("Resetting MCU (%s)...\n", halt ? "reset and halt" : "reset and run");
    ret = client.device_connect(dco);
    if (ret != TAS_ERR_NONE) {
        fprintf(stderr, "ERROR: Reset failed: %s\n", client.get_error_info());
        return EXIT_CONNECT_ERROR;
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
            printf("Usage: dflash read --addr <hex> --length <hex> [options]\n\n"
                   "Options:\n"
                   "  --addr/-a <hex>     Start address (required)\n"
                   "  --length/-l <hex>   Length in bytes (required)\n"
                   "  --output/-o <file>  Output file (.bin or .hex, default: hex dump)\n"
                   "  --server <ip>       TAS server IP (default: localhost)\n"
                   "  --target <id>       Target identifier\n"
                   "  --device <name>     Override device type (skip auto-detect)\n"
                   "  --config-dir <path> DeviceConfigs JSON directory\n\n"
                   "Examples:\n"
                   "  dflash read -a AF000000 -l 100\n"
                   "  dflash read -a AF000000 -l 1000 -o dump.hex\n"
                   "  dflash read -a AF000000 -l 400 --device TC27x\n\n"
                   "Run 'dflash erase --info' to see supported devices.\n");
            return EXIT_OK;
        }
        fprintf(stderr, "ERROR: Unknown option '%s'\n", argv[i]);
        return EXIT_USAGE_ERROR;
    }

    if (!hasAddr || !hasLength) {
        fprintf(stderr, "ERROR: --addr and --length are required\n");
        return EXIT_USAGE_ERROR;
    }

    printf("TAS DFlash Read Tool\n");
    printf("====================\n");

    // ---- Load device configs ----
    DeviceConfigLoader configLoader;
    std::string configDirPath;
    if (!loadDeviceConfigs(args, configLoader, configDirPath)) return EXIT_USAGE_ERROR;

    printf("Connecting to TAS server at %s...\n", args.serverIp);

    CTasClientRw client("DFlashRead");
    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SERVER_ERROR; }

    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_TARGET_ERROR; }

    const char* selectedTargetId = (args.targetId != nullptr) ? args.targetId
                                   : (numTargets > 0) ? targets[0].identifier : nullptr;
    if (!selectedTargetId) { fprintf(stderr, "ERROR: No targets\n"); return EXIT_NO_TARGET; }
    if (args.targetId == nullptr) printf("  Auto-selected target [0]\n");

    ret = startSession(client, selectedTargetId, "DFlashRead");
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SESSION_ERROR; }

    ret = client.device_connect(TAS_CLNT_DCO_RESET_AND_HALT);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_CONNECT_ERROR; }

    const tas_con_info_st* conInfo = client.get_con_info();
    DFlashConfig flashCfg;

    // Check for unsupported TC4x devices
    int tc4xCheck = rejectTc4xDevice(conInfo);
    if (tc4xCheck != EXIT_OK) return tc4xCheck;

    if (args.deviceName != nullptr) {
        if (!configLoader.findByName(args.deviceName, flashCfg)) {
            fprintf(stderr, "ERROR: Unknown device '%s'\n", args.deviceName);
            printSupportedDevices(configLoader);
            return EXIT_DEVICE_ERROR;
        }
        printf("  Device (manual): %s\n", args.deviceName);
    } else {
        if (!configLoader.findByJtagId(conInfo->device_type, flashCfg)) {
            fprintf(stderr, "ERROR: Unsupported device '%s' (jtag_id=0x%08X)\n",
                   tas_get_device_name_str(conInfo->device_type), conInfo->device_type);
            printSupportedDevices(configLoader);
            return EXIT_DEVICE_ERROR;
        }
    }

    uint32_t dflashEndAddr = flashCfg.baseAddress + flashCfg.totalSize - 1;
    if (readAddr < flashCfg.baseAddress || readAddr > dflashEndAddr) {
        fprintf(stderr, "ERROR: Address 0x%08X out of DFlash range (0x%08X - 0x%08X)\n",
                readAddr, flashCfg.baseAddress, dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }
    if (readAddr + readLength - 1 > dflashEndAddr) {
        fprintf(stderr, "ERROR: Range exceeds DFlash boundary (max 0x%08X)\n", dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }

    const char* displayName = args.deviceName ? args.deviceName : flashCfg.deviceName.c_str();
    printf("  Device: %s (%s), DFlash: %u KB\n",
           displayName, flashCfg.family.c_str(), flashCfg.totalSize / 1024);
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
        bool isHex = (path.size() >= 4 && path.compare(path.size() - 4, 4, ".hex") == 0);
        if (isHex) {
            if (!saveToIntelHex(outputFile, readAddr, allData)) return EXIT_IO_ERROR;
            printf("  Saved Intel HEX: %s\n", outputFile);
        } else {
            std::ofstream ofs(outputFile, std::ios::binary);
            if (!ofs) { fprintf(stderr, "ERROR: Cannot open '%s'\n", outputFile); return EXIT_IO_ERROR; }
            ofs.write(reinterpret_cast<const char*>(allData.data()), totalRead);
            printf("  Saved binary: %s (%u bytes)\n", outputFile, totalRead);
        }
    } else {
        printf("\n");
        hexDumpXxd(allData.data(), totalRead, readAddr);
    }

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
            if (strcmp(argv[i], "--no-reset") == 0) { noReset = true; i++; continue; }
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
            if (strcmp(argv[i], "--no-reset") == 0) { noReset = true; i++; continue; }
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
                printf("Usage: dflash erase [--addr <hex> --sectors <n> | --all] [options]\n\n"
                       "Options:\n"
                       "  --addr <hex>       DFlash start address, auto-aligned to sector\n"
                       "  --sectors <n>      Number of sectors to erase\n"
                       "  --all              Erase entire DFlash\n"
                       "  --info             Show device info only, no erase\n"
                       "  --verify           Verify erase by reading back\n"
                       "  --backup <file>    Backup DFlash before erasing\n"
                       "  --reset            Reset MCU after erase\n"
                       "  --no-reset         Hot attach (no device reset)\n"
                       "  --server <ip>      TAS server IP (default: localhost)\n"
                       "  --target <id>      Target identifier\n"
                       "  --device <name>    Override device type (skip auto-detect)\n"
                       "  --config-dir <path> DeviceConfigs JSON directory\n\n"
                       "Examples:\n"
                       "  dflash erase --addr AF000000 --sectors 1\n"
                       "  dflash erase --addr AF000000 --sectors 4 --verify\n"
                       "  dflash erase --all --backup backup.hex\n"
                       "  dflash erase --info --device TC27x\n\n"
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

    printf("TAS DFlash Erase Tool\n");
    printf("=====================\n\n");

    // ---- Load device configs ----
    DeviceConfigLoader configLoader;
    std::string configDirPath;
    if (!loadDeviceConfigs(args, configLoader, configDirPath)) return EXIT_USAGE_ERROR;

    // ---- Connect to server ----
    printf("Connecting to TAS server at %s...\n", args.serverIp);
    CTasClientRw client("DFlashErase");

    tas_return_et ret = client.server_connect(args.serverIp);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SERVER_ERROR; }

    const tas_server_info_st* serverInfo = client.get_server_info();
    printf("  Server: %s V%d.%d (%s)\n",
           serverInfo->server_name, serverInfo->v_major, serverInfo->v_minor, serverInfo->date);

    // ---- Detect targets ----
    const tas_target_info_st* targets;
    uint32_t numTargets;
    ret = client.get_targets(&targets, &numTargets);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_TARGET_ERROR; }

    printf("  Targets: %u\n", numTargets);
    for (uint32_t i = 0; i < numTargets; i++) {
        printf("  [%u] %s (%s)\n", i, tas_get_device_name_str(targets[i].device_type), targets[i].identifier);
    }

    const char* selectedTargetId = (args.targetId != nullptr) ? args.targetId
                                 : (numTargets > 0) ? targets[0].identifier : nullptr;
    if (!selectedTargetId) { fprintf(stderr, "ERROR: No targets\n"); return EXIT_NO_TARGET; }
    if (args.targetId == nullptr) printf("  Auto-selected target [0]\n");

    printf("\nStarting session...\n");
    ret = startSession(client, selectedTargetId, "DFlashErase");
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_SESSION_ERROR; }

    // ---- Connect to device ----
    tas_clnt_dco_et dco = noReset ? TAS_CLNT_DCO_HOT_ATTACH : TAS_CLNT_DCO_RESET_AND_HALT;
    printf("Connecting to device (%s)...\n", noReset ? "hot attach" : "reset and halt");
    ret = client.device_connect(dco);
    if (ret != TAS_ERR_NONE) { fprintf(stderr, "ERROR: %s\n", client.get_error_info()); return EXIT_CONNECT_ERROR; }

    // ---- Detect MCU ----
    const tas_con_info_st* conInfo = client.get_con_info();
    DFlashConfig flashCfg;

    // Check for unsupported TC4x devices
    int tc4xCheck = rejectTc4xDevice(conInfo);
    if (tc4xCheck != EXIT_OK) return tc4xCheck;

    if (args.deviceName != nullptr) {
        if (!configLoader.findByName(args.deviceName, flashCfg)) {
            fprintf(stderr, "ERROR: Unknown device '%s'\n", args.deviceName);
            printSupportedDevices(configLoader);
            return EXIT_DEVICE_ERROR;
        }
        printf("  Device (manual): %s\n", args.deviceName);
    } else {
        if (!configLoader.findByJtagId(conInfo->device_type, flashCfg)) {
            fprintf(stderr, "ERROR: Unsupported device '%s' (jtag_id=0x%08X)\n",
                   tas_get_device_name_str(conInfo->device_type), conInfo->device_type);
            printSupportedDevices(configLoader);
            return EXIT_DEVICE_ERROR;
        }
    }

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

    if (sectorAddr + eraseSize - 1 > dflashEndAddr) {
        fprintf(stderr, "ERROR: Range exceeds DFlash boundary (max 0x%08X)\n", dflashEndAddr);
        return EXIT_USAGE_ERROR;
    }

    // ---- Backup ----
    if (backupFile != nullptr) {
        printf("\nBackup:\n");
        if (!backupDFlash(client, sectorAddr, eraseSize, flashCfg.sectorSize, backupFile)) return EXIT_BACKUP_ERROR;
    }

    // ---- Print erase parameters ----
    printf("\nErasing DFlash:\n");
    printf("  Address:  0x%08X\n", sectorAddr);
    printf("  Sectors:  %u (%u KB)\n\n", numSectors, eraseSize / 1024);

    // ---- Flash command base = DFlash base address (TC2x/TC3x convention) ----
    const uint64_t flashCmdBase = static_cast<uint64_t>(flashCfg.baseAddress);

    // ---- Step 1: Clear status ----
    printf("Step 1/5: Clearing flash status...                ");
    if (!clearFlashStatus(client, flashCmdBase)) {
        printf("FAILED\n"); return EXIT_FLASH_STATUS_ERROR;
    }
    printf("OK\n");

    // ---- Step 2: Erase (atomic) ----
    printf("Step 2/5: Executing erase command...              ");
    if (!eraseMultipleSectors(client, flashCmdBase, sectorAddr, numSectors)) {
        printf("FAILED\n"); return EXIT_ERASE_CMD_ERROR;
    }
    printf("OK\n");

    // ---- Step 3: Wait unbusy ----
    printf("Step 3/5: Waiting for erase to complete...        ");
    uint32_t elapsedMs = 0;
    if (!waitUnbusyD0(client, flashCfg.isTc3x, elapsedMs)) { printf("TIMEOUT\n"); return EXIT_ERASE_TIMEOUT; }
    printf("OK (%u ms)\n", elapsedMs);

    // ---- Step 4: Check error flags ----
    printf("Step 4/5: Checking error flags...                 ");
    if (!checkEraseErrors(client, flashCfg.isTc3x)) { return EXIT_ERASE_FLAGS; }
    printf("OK\n");

    // ---- Step 5: Reset to read ----
    printf("Step 5/5: Reset to read mode...                   ");
    if (!resetToRead(client, flashCmdBase)) { printf("FAILED\n"); return EXIT_FLASH_RESET_ERROR; }
    printf("OK\n");

    // ---- Verify ----
    if (doVerify) {
        printf("\nVerifying erase:\n");
        if (!verifyErase(client, sectorAddr, numSectors, flashCfg.sectorSize)) {
            printf("\nDFlash erase completed with verification ERRORS.\n");
            return EXIT_VERIFY_ERROR;
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
    return EXIT_OK;
}

//********************************************************************************************************************
//  Usage
//********************************************************************************************************************

static void printUsage(const char* progName, const DeviceConfigLoader* loader = nullptr)
{
    // Show only filename, not full path
    const char* base = strrchr(progName, '\\');
    if (!base) base = strrchr(progName, '/');
    if (!base) base = progName; else base++;

    printf("TAS DFlash Tool for AURIX\n\n");
    printf("Usage:\n");
    printf("  %s <subcommand> [options]\n\n", base);
    printf("Subcommands:\n");
    printf("  erase    Erase DFlash sectors\n");
    printf("  read     Read DFlash content\n");
    printf("  list     List connected TAS targets\n");
    printf("  reset    Reset the MCU\n\n");
    printf("Legacy (backward compatible):\n");
    printf("  %s <addr> <num_sectors> [options]\n", base);
    printf("  %s --all [options]\n", base);
    printf("  %s --info [options]\n\n", base);
    printf("Run '%s <subcommand> --help' for details.\n\n", base);
    printf("Examples:\n");
    printf("  %s list\n", base);
    printf("  %s erase --info\n", base);
    printf("  %s erase --addr AF000000 --sectors 1\n", base);
    printf("  %s erase --all --verify\n", base);
    printf("  %s read -a AF000000 -l 1000 -o dump.hex\n\n", base);

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

    // Subcommands
    if (first == "erase" || first == "e") return doErase(argc - 2, argv + 2, false);
    if (first == "read"  || first == "r") return doRead(argc - 2, argv + 2);
    if (first == "list"  || first == "l") return doList(argc - 2, argv + 2);
    if (first == "reset") return doReset(argc - 2, argv + 2);

    // Legacy backward compatibility: --all, --info, or hex address as first arg
    if (first == "--all" || first == "--info" ||
        first.rfind("0x", 0) == 0 || first.rfind("0X", 0) == 0) {
        return doErase(argc - 1, argv + 1, true);
    }

    fprintf(stderr, "ERROR: Unknown subcommand '%s'\n", argv[1]);
    printUsage(argv[0]);
    return EXIT_USAGE_ERROR;
}
