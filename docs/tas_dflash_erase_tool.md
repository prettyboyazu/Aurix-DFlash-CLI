<!--
 Copyright (c) 2026 Infineon Technologies AG.

 This file is part of TAS Client, an API for device access for Infineon's
 automotive MCUs.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 ****************************************************************************************************************-->
# TAS DFlash Tool {#tas_dflash_tool}

## Overview

`dflash` is a command-line tool for accessing DFlash on Infineon AURIX microcontrollers via the TAS Client API. It supports the following subcommands:

- **erase** - Erase DFlash sectors with AURIX-compliant sequence
- **read** - Read DFlash content and output as hex dump, binary, or Intel HEX
- **write** - Write data to DFlash from a HEX or binary file
- **restore** - Restore DFlash from a backup file (erase + write + verify)
- **list** - List connected TAS targets and device info
- **reset** - Reset the MCU (with optional halt)
- **ucb** - Read, write, and erase UCB (User Configuration Block) on TC3x devices

The tool connects to a TAS server, auto-detects the connected MCU, and performs device-specific register access (TC2x uses PMU `FLASH0_FSR`, TC3x uses DMU `DMU_HF_STATUS`/`DMU_HF_ERRSR`).

## Supported Devices

The tool supports **all TC2x/TC3x AURIX devices whose configuration files are present in the `DeviceConfigs/` directory**. Device parameters (DFlash base address, total size, sector size) are loaded from the JSON configuration files at runtime — no device-specific values are hard-coded.

Currently shipped device configurations:

`TC21x`, `TC22x`, `TC23x`, `TC26x`, `TC27x`, `TC29x`, `TC33x`, `TC35x`, `TC36x`, `TC37x`, `TC38x`, `TC39x`, `TC3Ex`

Typical DFlash parameters (loaded from JSON, sample values):

| Device | DFlash Sector Size | DFlash Total Size |
|--------|--------------------|-------------------|
| TC21x, TC22x, TC23x, TC26x | 8 KB | 128 KB |
| TC27x | 8 KB | 384 KB |
| TC29x | 8 KB | 512 KB |
| TC33x, TC35x, TC36x | 4 KB | 128 KB |
| TC37x, TC3Ex | 4 KB | 256 KB |
| TC38x | 4 KB | 512 KB |
| TC39x | 4 KB | 1 MB |

DFlash base address for TC2x/TC3x is `0xAF000000` (also loaded from the JSON files). AURIX DFlash erased state is `0x00` (all bits zero).

> **Note:** TC4x (AURIX 2G+) devices are **not supported**. TC4x uses different DFlash base addresses (`0xAE000000`/`0xAC000000`) and a different erase command sequence. If a TC4x device is detected, the tool will display an error message and exit.

To add support for an additional device, drop the corresponding `<Device>_<step>.json` file into the `DeviceConfigs/` directory — no source-code change or rebuild is required.

## Prerequisites

- A running TAS Server with an AURIX target device connected
- Network connectivity between the host PC and the TAS Server
- Static-linked executable (no additional DLL dependencies)

## Usage

```
dflash <subcommand> [options]
```

### Subcommands

| Subcommand | Shortcut | Description |
|------------|----------|-------------|
| `erase` | `e` | Erase DFlash sectors |
| `read` | `r` | Read DFlash content |
| `write` | `w` | Write data to DFlash |
| `restore` | | Restore DFlash from backup |
| `list` | `l` | List connected TAS targets |
| `reset` | | Reset the MCU |
| `ucb` | `u` | Read/write/erase UCB (TC3x only) |

### Legacy Mode (backward compatible)

The tool supports the original positional-argument syntax, automatically routed to `erase`:

```
dflash <addr> <num_sectors> [options]
dflash --all [options]
dflash --info [options]
```

---

## `erase` Subcommand

```
dflash erase [--addr <hex> --sectors <n> | --all] [options]
```

### Options

| Option | Description |
|--------|-------------|
| `--addr <hex>` | DFlash start address (e.g. `0xAF000000`). Auto-aligned to sector boundary. |
| `--sectors <n>` | Number of sectors to erase (decimal). |
| `--all` | Erase entire DFlash. Requires typing `yes` to confirm. |
| `--info` | Show device info and DFlash parameters without erasing. |
| `--verify` | Read back all erased bytes and verify they are `0x00`. |
| `--backup <file>` | Save DFlash content to a binary file before erasing. |
| `--reset` | Reset MCU after successful erase (device resumes normal execution). |
| `--no-reset` | Hot-attach to device without reset (default: reset and halt). |
| `--server <ip>` | TAS server IP address. Default: `localhost` |
| `--target <id>` | Target identifier string. Default: first available target |
| `--config-dir <path>` | Path to the `DeviceConfigs/` directory containing device JSON files. Overrides the default search paths (see [DeviceConfigs Configuration](#deviceconfigs-configuration)). |

### Examples

Show device info:
```
dflash erase --info
```

Erase a single sector with verification:
```
dflash erase --addr 0xAF000000 --sectors 1 --verify
```

Erase entire DFlash with backup, verify, and MCU reset:
```
dflash erase --all --backup dump.bin --verify --reset
```

Legacy syntax (still supported):
```
dflash 0xAF000000 1 --verify
dflash --all --verify
```

---

## `read` Subcommand

```
dflash read --addr <hex> --length <hex> [--output <file>] [options]
```

### Options

| Option | Shortcut | Description |
|--------|----------|-------------|
| `--addr <hex>` | `-a` | DFlash start address (required). |
| `--length <hex>` | `-l` | Number of bytes to read (required). |
| `--output <file>` | `-o` | Output file. Extension determines format: `.bin` = raw binary, `.hex` = Intel HEX. Default: xxd-style hex dump to terminal. |
| `--server <ip>` | | TAS server IP address. Default: `localhost` |
| `--target <id>` | | Target identifier string. Default: first available target |
| `--config-dir <path>` | | Path to the `DeviceConfigs/` directory. See [DeviceConfigs Configuration](#deviceconfigs-configuration). |

### Examples

Hex dump to terminal:
```
dflash read --addr 0xAF000000 --length 0x1000
```

Save as raw binary:
```
dflash read --addr 0xAF000000 --length 0x20000 --output dump.bin
```

Save as Intel HEX:
```
dflash read --addr 0xAF000000 --length 0x20000 --output dump.hex
```

---

## `write` Subcommand

```
dflash write --file <path> [--addr <address>] [--verify] [options]
```

Write data to DFlash from a HEX or binary file.

### Options

| Option | Shortcut | Description |
|--------|----------|-------------|
| `--file <path>` | `-f` | Input file (`.hex` or `.bin`). Required. |
| `--addr <address>` | `-a` | Base address for binary files (default: DFlash start address). Ignored for HEX files. |
| `--verify` | | Verify written data by read-back comparison. |
| `--server <ip>` | `-s` | TAS server IP address. Default: `localhost` |
| `--target <id>` | `-t` | Target identifier string. Default: first available target |
| `--device <name>` | `-d` | Device name (overrides auto-detection). |
| `--config-dir <path>` | | Path to the `DeviceConfigs/` directory. See [DeviceConfigs Configuration](#deviceconfigs-configuration). |

### Examples

Write a binary file to DFlash start address:
```
dflash write --file data.bin
```

Write a binary file to a specific address with verification:
```
dflash write --file data.bin --addr 0xAF001000 --verify
```

Write an Intel HEX file (addresses embedded in file):
```
dflash write --file firmware.hex --verify
```

---

## `restore` Subcommand

```
dflash restore --file <path> [--no-verify] [options]
```

Restore DFlash from a backup file. This command performs erase + write + verify as a single operation, providing a convenient way to restore a previously saved DFlash image.

### Options

| Option | Shortcut | Description |
|--------|----------|-------------|
| `--file <path>` | `-f` | Backup file to restore (`.bin` or `.hex`). Required. |
| `--no-verify` | | Skip verification after restore. |
| `--server <ip>` | `-s` | TAS server IP address. Default: `localhost` |
| `--target <id>` | `-t` | Target identifier string. Default: first available target |
| `--device <name>` | `-d` | Device name (overrides auto-detection). |
| `--config-dir <path>` | | Path to the `DeviceConfigs/` directory. See [DeviceConfigs Configuration](#deviceconfigs-configuration). |

### Examples

Restore DFlash from a binary backup:
```
dflash restore --file dump.bin
```

Restore from a HEX file without verification:
```
dflash restore --file backup.hex --no-verify
```

Restore to a remote target:
```
dflash restore --file dump.bin --server 192.168.1.100
```

---

## `list` Subcommand

```
dflash list [--server <ip>]
```

Lists all targets connected to the TAS server, showing device type and identifier string. Does not require session start or device connect.

### Example

```
dflash list
```

Output:
```
TAS DFlash Tool - Device List
=============================
Connecting to TAS server at localhost...
  Server: TasServer V2.0 (Aug  7 2025)
  Targets (1):
  [0] TC36x          Application Kit TC367 V2.0
```

---

## `reset` Subcommand

```
dflash reset [--halt] [--server <ip>] [--target <id>]
```

Resets the MCU. By default, the MCU resumes normal execution after reset. Use `--halt` to halt the CPU after reset (useful for debugging or before performing flash operations).

### Options

| Option | Description |
|--------|-------------|
| `--halt` | Halt the CPU after reset (reset and halt mode). |
| `--server <ip>` | TAS server IP address. Default: `localhost` |
| `--target <id>` | Target identifier string. Default: first available target |

### Examples

Reset MCU and resume execution:
```
dflash reset
```

Reset and halt MCU:
```
dflash reset --halt
```

---

## `ucb` Subcommand

```
dflash ucb <operation> [options]
```

Access the User Configuration Block (UCB) on TC3x AURIX devices. UCB stores critical boot configuration, security settings, and flash protection parameters.

> **Note:** UCB operations are only supported on **TC3x** devices. TC2x devices do not have UCB support in this tool.

### UCB Address Space

| Region | Address Range | Size | Description |
|--------|--------------|------|-------------|
| Full UCB | `0xAF400000` - `0xAF405FFF` | 24 KB | 48 sectors × 512 bytes |
| BMHD0-3 | `0xAF400000` - `0xAF4007FF` | 2 KB | Boot Mode Headers |
| Security | `0xAF400800` - `0xAF400FFF` | 2 KB | OTP/DFLASH/DBG/HSM |
| BMHD COPY | `0xAF401000` - `0xAF4017FF` | 2 KB | BMHD backup |
| Security COPY | `0xAF401800` - `0xAF401FFF` | 2 KB | Security backup |
| PFLASH | `0xAF402000` - `0xAF4027FF` | 2 KB | PFlash protection + backup |
| SWAP | `0xAF402800` - `0xAF4037FF` | 4 KB | Flash swap + backup |
| LBIST | `0xAF403800` - `0xAF4047FF` | 4 KB | Logic BIST + backup |
| SSW | `0xAF404800` - `0xAF4057FF` | 4 KB | Startup SW + backup |
| Reserved | `0xAF405800` - `0xAF405FFF` | 2 KB | Reserved |

### Operations

| Operation | Shortcut | Description |
|-----------|----------|-------------|
| `read` | `r` | Read UCB content |
| `write` | `w` | Write data to UCB |
| `erase` | `e` | Erase UCB sectors |

### `ucb read`

```
dflash ucb read [--addr <hex> --length <hex>] [--output <file>] [options]
```

Read UCB content. Without `--addr`/`--length`, reads the entire 24 KB UCB area.

| Option | Shortcut | Description |
|--------|----------|-------------|
| `--addr <hex>` | `-a` | Start address within UCB range. Default: `0xAF400000`. |
| `--length <hex>` | `-l` | Number of bytes to read. Default: entire UCB (0x6000). |
| `--output <file>` | `-o` | Output file (`.hex` = Intel HEX, `.bin` = binary). Default: hex dump to terminal. |

#### Examples

Read entire UCB (24 KB hex dump):
```
dflash ucb read
```

Read BMHD area (first 4 sectors):
```
dflash ucb read --addr AF400000 --length 800
```

Save UCB to Intel HEX file:
```
dflash ucb read -a AF400000 -l 6000 -o ucb_backup.hex
```

### `ucb write`

```
dflash ucb write --file <path> [--addr <hex>] [--verify] [options]
```

Write data to UCB from a HEX or binary file.

| Option | Shortcut | Description |
|--------|----------|-------------|
| `--file <path>` | `-f` | Input file (`.hex` or `.bin`). Required. |
| `--addr <hex>` | `-a` | Base address for binary files. Default: `0xAF400000`. |
| `--verify` | `-v` | Verify written data by read-back comparison. |

#### Examples

Write Intel HEX file to UCB:
```
dflash ucb write --file bmhd_config.hex --verify
```

Write binary file to specific UCB address:
```
dflash ucb write --file data.bin --addr AF400000 --verify
```

### `ucb erase`

```
dflash ucb erase --addr <hex> --sectors <n> [--verify] [options]
```

Erase UCB sectors. **High-risk operation** — requires user confirmation.

| Option | Shortcut | Description |
|--------|----------|-------------|
| `--addr <hex>` | `-a` | Start address (must be within UCB range). Required. |
| `--sectors <n>` | `-s` | Number of sectors to erase (decimal). Required. |
| `--verify` | `-v` | Verify erased sectors are all `0x00`. |

#### Locked Regions (Protected)

The following regions cannot be erased (CONFIRMED state):
- `0xAF400800` - `0xAF400FFF` (sectors 4-7: OTP/DFLASH/DBG/HSM)
- `0xAF401800` - `0xAF401FFF` (sectors 12-15: Security COPY)

If the specified range overlaps a locked region, the tool will report an error and abort.

#### Examples

Erase BMHD sectors (0-3):
```
dflash ucb erase --addr AF400000 --sectors 4 --verify
```

Erase BMHD COPY sectors (8-11):
```
dflash ucb erase --addr AF401000 --sectors 4 --verify
```

### Common Options (all UCB operations)

| Option | Description |
|--------|-------------|
| `--server <ip>` | TAS server IP address. Default: `localhost` |
| `--target <id>` | Target identifier string. Default: first available target |
| `--config-dir <path>` | Path to `DeviceConfigs/` directory. |

### Parameter Format

- `--addr` and `--length` values are **hexadecimal** (0x prefix optional)
- `--sectors` is **decimal**
- All addresses must fall within the UCB range (`0xAF400000` - `0xAF405FFF`)

---

## DeviceConfigs Configuration

The tool no longer hard-codes any device-specific memory parameters. On startup it automatically loads the device descriptions (DFlash base address, total size, sector size, etc.) from a directory of JSON files called `DeviceConfigs/`.

### Search-path priority

The `DeviceConfigs/` directory is resolved at runtime by trying the following locations **in order**, and the first hit wins:

1. The path provided via the `--config-dir <path>` command-line option (highest priority).
2. A `DeviceConfigs/` directory located **next to the executable** (typical install/deploy layout).
3. `data/DeviceConfigs/` relative to the repository root (typical development layout).
4. The path specified by the environment variable `TAS_DEVICE_CONFIGS` (lowest priority).

If none of these locations contain a usable `DeviceConfigs/` directory, the tool reports a configuration error and exits.

### JSON file source

The shipped `*.json` files originate from Infineon's **AURIXFlasherSoftwareTool — DeviceConfigs**. They describe each MCU's memory map (DFlash, PFlash, SRAM …) as well as DFlash sector size and total size. The tool reads only the fields it needs (`memoryStartAddress`, `memorySize`, `sectorSize` of the DFlash region) and ignores everything else, so the upstream files can be used **as-is** without any modification.

### Adding a new device

To add support for a new TC2x/TC3x device:

1. Obtain the device's JSON file from AURIXFlasherSoftwareTool's `DeviceConfigs/` directory (e.g. `TC37x_A_step.json`).
2. Drop the file into the `DeviceConfigs/` directory the tool is using (see search-path priority above).
3. Re-run the tool — no rebuild and no source change is needed.

The auto-detected device name (from the connected target) is matched against the JSON file name to pick the correct configuration.

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid arguments or address out of range |
| 2 | Server connection failed |
| 3 | Get targets failed |
| 4 | No targets available |
| 5 | Session start failed |
| 6 | Device connect failed |
| 7 | Unsupported device type |
| 8 | Backup failed |
| 9 | Safety Watchdog password read failed |
| 10 | Clear Safety EndInit failed |
| 11 | Flash status clear failed |
| 12 | Erase command failed |
| 13 | Restore Safety EndInit failed |
| 14 | Erase timeout (device still busy) |
| 15 | Error flags detected (PVER/EVER/PROER/SQER/OPER) |
| 16 | Reset to read mode failed |
| 17 | Verification failed (bytes not erased) |
| 18 | Read failed (read subcommand) |
| 19 | Write failed |
| 20 | File open/parse failed |
| 21 | Restore failed |
| 22 | UCB erase failed (locked region overlap) |
| 23 | UCB operation on unsupported device (not TC3x) |

## Erase Sequence

The tool performs the following 8 steps for each erase operation:

1. **Read Safety Watchdog password** - Reads `SCU_WDTS_CON0` (`0xF00362A8`) and extracts the 14-bit password
2. **Clear Safety EndInit** - Unlocks and clears EndInit protection to allow flash modifications
3. **Clear flash status** - Writes `0xFA` to flash status register (`0xAF005554`)
4. **Execute erase command** - Sends 4-word erase sequence **atomically** via `execute_trans()`:
   - Write sector address to `0xAF00AA50`
   - Write sector count to `0xAF00AA58`
   - Write `0x80` to `0xAF00AAA8` (erase trigger)
   - Write `0x50` to `0xAF00AAA8` (confirm)
   - All 4 writes are sent as a single atomic transaction to prevent CSI sequence interruption
5. **Restore Safety EndInit** - Re-enables EndInit protection (always executed even if previous steps fail)
6. **Wait for completion** - Polls busy bit until clear (timeout: 10 s):
   - TC3x: `DMU_HF_STATUS` (`0xF8040010`) bit D0 (D0BUSY)
   - TC2x: `FLASH0_FSR` (`0xF8002010`) bit D1 (D0BUSY)
7. **Check error flags** - Reads device-specific error register:
   - TC3x: `DMU_HF_ERRSR` (`0xF8040034`) — OPER(D0), SQER(D1), PROER(D2), PVER(D3), EVER(D4)
   - TC2x: `FLASH0_FSR` (`0xF8002010`) — OPER(D11), SQER(D12), PROER(D13), PVER(D25), EVER(D26)
8. **Reset to read mode** - Writes `0xF0` to `0xAF005554`

## Register Architecture

### TC3x (DMU Module)

TC3x devices use separate DMU registers for status and error:

| Register | Address | Purpose |
|----------|---------|---------|
| `DMU_HF_STATUS` | `0xF8040010` | Busy flags: D0BUSY(D0), D1BUSY(D1) |
| `DMU_HF_ERRSR` | `0xF8040034` | Error flags: OPER(D0), SQER(D1), PROER(D2), PVER(D3), EVER(D4) |
| `DMU_HF_CLRE` | `0xF8040038` | Clear error bits by writing 1 |

### TC2x (PMU Module)

TC2x devices use a single `FLASH0_FSR` register for both status and errors:

| Register | Address | Purpose |
|----------|---------|---------|
| `FLASH0_FSR` | `0xF8002010` | Combined status + error: D0BUSY(D1), OPER(D11), SQER(D12), PROER(D13), PVER(D25), EVER(D26) |

Error bits are `rwh` type (read/write, cleared by hardware on write of 1).

## ECC Error Tolerance

After erasing DFlash, the ECC (Error Correction Code) state becomes invalid because erased data no longer matches stored ECC checksums. Reading such sectors may return `TAS_ERR_RW_READ` (0x0600) from the TAS Client, but the read data is still valid.

The tool treats `TAS_ERR_RW_READ` as a successful read in the following operations:
- **Verify erase** (`--verify`) - ECC errors are expected; erased data is still checked as `0x00`
- **Backup** (`--backup <file>`) - ECC errors are tolerated; data is saved to file
- **Read subcommand** - ECC errors are tolerated for post-erase DFlash reads
- **Write verify** (`write --verify`) - ECC errors are tolerated during read-back verification
- **Restore verify** - ECC errors are tolerated during post-restore verification

## Build Environment Setup

### System Requirements

| Component | Minimum Version | Notes |
|-----------|----------------|-------|
| C++ Compiler | C++17 support | MSVC 2019+, GCC 9+, MinGW GCC 9+ |
| CMake | 3.23+ | 3.25+ recommended |
| Python | 3.11+ | For Conan 2 package manager |
| Conan | 2.x | C/C++ dependency manager |

### Windows (MSVC / Visual Studio)

1. Install [Visual Studio 2019 or later](https://visualstudio.microsoft.com/) with the "Desktop development with C++" workload
2. Install [Python 3.11+](https://www.python.org/downloads/)
3. Install [CMake 3.25+](https://cmake.org/download/) (or use the one bundled with Visual Studio)

```powershell
# Install Conan
python -m pip install conan

# Generate default Conan profile (auto-detects MSVC)
conan profile detect
```

Edit the Conan profile at `%USERPROFILE%\.conan2\profiles\default` and ensure C++17 is set:
```ini
compiler.cppstd=17
```

### Windows (MinGW GCC)

1. Install [MSYS2](https://www.msys2.org/) to `C:\msys64`
2. Install the MinGW64 toolchain from an MSYS2 terminal:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make
```

3. Install Python and Conan from a regular Windows terminal:

```powershell
python -m pip install conan
```

4. Generate and edit the Conan profile:

```powershell
conan profile detect
```

Edit `%USERPROFILE%\.conan2\profiles\default` to match MinGW:
```ini
[settings]
arch=x86_64
build_type=Release
compiler=gcc
compiler.version=15
compiler.libcxx=libstdc++11
compiler.cppstd=17
os=Windows
[conf]
tools.cmake.cmaketoolchain:generator=MinGW Makefiles
```

> Set `compiler.version` to match your GCC version (`gcc --version`).

### Linux (GCC)

Install build tools:
```bash
# Debian/Ubuntu
sudo apt install build-essential cmake python3 python3-pip

# Fedora
sudo dnf install gcc gcc-c++ cmake python3 python3-pip
```

Install Conan and generate profile:
```bash
python3 -m pip install conan
conan profile detect
```

Edit `~/.conan2/profiles/default`:
```ini
compiler.cppstd=17
```

## Building

All commands must be run from the **root of the repository**.

### Method 1: Manual Step-by-Step

#### Step 1: Install Dependencies (Conan)

```bash
# Release build
conan install .

# Debug build (optional)
conan install . -s build_type=Debug --build=missing

# With optional components
conan install . -o "&:python=True" -o "&:docs=True" -o "&:tests=True"
```

Conan downloads all build dependencies and generates CMake toolchain files under `build/`.

#### Step 2: Configure CMake

**Windows (MSVC)**
```powershell
cmake --preset conan-default
```

**Windows (MinGW)**
```powershell
set PATH=C:\msys64\mingw64\bin;%PATH%
cmake --preset conan-release
```

**Linux**
```bash
cmake --preset conan-release
# or for debug:
cmake --preset conan-debug
```

#### Step 3: Build

**Windows (MSVC)**
```powershell
cmake --build --preset conan-release
```

**Windows (MinGW)**
```powershell
set PATH=C:\msys64\mingw64\bin;%PATH%
cmake --build --preset conan-release
```

**Linux**
```bash
cmake --build --preset conan-release
```

### Method 2: Build Script (Windows)

For quick standalone builds without Conan:

```cmd
build.bat                    Build Release
build.bat --deploy           Build Release + deploy to Erase/
build.bat --all              Clean + Build Release + deploy
build.bat --debug --deploy   Build Debug + deploy
build.bat --clean            Clean and rebuild
```

The build script uses CMake directly with MSVC, automatically sets C++17 and static CRT linking. Conan is not required for this method.

### Build Output

The compiled executable is located at:

```
build/apps/dflash/Release/dflash.exe   (Windows, build.bat)
build/Release/apps/dflash/dflash        (Linux, Conan)
```

Deployed (with `build.bat --deploy`):
```
Erase/dflash.exe                                  # Standalone executable
Erase/DeviceConfigs/*.json                        # Device configuration files
```

### Static Linking

The tool uses static linking to eliminate all runtime DLL dependencies, producing a standalone `.exe` that runs on any Windows 10+ machine without additional installations.

**MSVC** — Static CRT (`/MT`), configured globally in the root `CMakeLists.txt`:
```cmake
# Static CRT linking (MSVC): produce standalone exe without VCRUNTIME DLL dependency
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
```

**MinGW** — Static libgcc/libstdc++, configured in `apps/dflash/CMakeLists.txt`:
```cmake
if (MINGW)
    target_link_options(${EXE_NAME} PRIVATE -static -static-libgcc -static-libstdc++)
endif()
```

The resulting `dflash.exe` depends only on Windows system DLLs (`KERNEL32.dll`, `WS2_32.dll`, `SHELL32.dll`, `ADVAPI32.dll`) — no VC++ Redistributable or MinGW DLLs required.

### Troubleshooting

| Problem | Solution |
|---------|----------|
| `conan: command not found` | Run `python -m pip install conan` and restart terminal |
| `CMake Error: No CMAKE_CXX_COMPILER` | Ensure compiler is installed and in PATH. For MinGW: add `C:\msys64\mingw64\bin` to PATH |
| `mingw32-make: not found` | Install `mingw-w64-x86_64-make` via `pacman -S mingw-w64-x86_64-make` |
| `libwinpthread-1.dll not found` at runtime | Build with MinGW (static linking) or copy DLLs to exe folder |
| `compiler.version` Conan error | Edit `~/.conan2/profiles/default` and add `compiler.version=<your_gcc_version>` |
| `compiler.libcxx` Conan error | Add `compiler.libcxx=libstdc++11` to Conan profile |
| `pacman db.lck` error in MSYS2 | Delete `/var/lib/pacman/db.lck` and retry |
| Conan preset not found | Run `conan install .` first to generate presets |

## Notes

- The tool auto-detects the connected MCU type and configures sector size, DFlash range, and register addresses accordingly
- TC2x and TC3x have different Flash controller architectures: TC2x uses PMU with `FLASH0_FSR`, TC3x uses DMU with separate `DMU_HF_STATUS` and `DMU_HF_ERRSR` registers
- Address alignment is performed automatically (4 KB for TC3x, 8 KB for TC2x)
- The erase command is sent as an atomic 4-write transaction via `execute_trans()` to ensure CSI sequence integrity
- Safety EndInit is always restored (Step 5) even if the erase operation fails
- The Safety Watchdog password is re-read before each EndInit operation (hardware may auto-increment)
- `--reset` option calls `device_connect(TAS_CLNT_DCO_RESET)` to resume normal MCU execution after erase
- `--no-reset` option uses `TAS_CLNT_DCO_HOT_ATTACH` to connect without resetting the MCU
- ECC errors (`TAS_ERR_RW_READ = 0x0600`) after erase are expected and tolerated during verification and read operations
