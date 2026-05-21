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
# TAS DFlash Erase Tool {#tas_dflash_erase_tool}

## Overview

`tas_dflash_erase` is a command-line tool that erases DFlash sectors on Infineon AURIX microcontrollers via the TAS Client API. It connects to a TAS server, detects the connected MCU, and performs the AURIX-compliant DFlash erase sequence including Safety EndInit control, flash command dispatch, busy polling, and error flag verification.

## Supported Devices

| Device | DFlash Sector Size | DFlash Total Size |
|--------|--------------------|-------------------|
| TC23x, TC26x | 8 KB | 128 KB |
| TC27x | 8 KB | 256 KB |
| TC33x, TC33xE, TC35x, TC36x | 4 KB | 128 KB |
| TC37x, TC37xE | 4 KB | 256 KB |
| TC38x | 4 KB | 512 KB |
| TC39x | 4 KB | 1 MB |

DFlash base address for all supported devices: `0xAF000000`. AURIX DFlash erased state is `0x00` (all bits zero).

## Prerequisites

- A running TAS Server with an AURIX target device connected
- Network connectivity between the host PC and the TAS Server
- Static-linked executable (no additional DLL dependencies)

## Usage

```
tas_dflash_erase <addr> <num_sectors> [options]
tas_dflash_erase --all [options]
tas_dflash_erase --info [options]
```

### Arguments

| Argument | Description |
|----------|-------------|
| `addr` | DFlash start address in hex (e.g. `0xAF000000`). Auto-aligned to sector boundary. |
| `num_sectors` | Number of sectors to erase (decimal). |

### Commands

| Command | Description |
|---------|-------------|
| `--info` | Show connected device info and DFlash parameters without performing erase. |
| `--all` | Erase entire DFlash. Requires typing `yes` to confirm. |

### Options

| Option | Description |
|--------|-------------|
| `--server <ip>` | TAS server IP address. Default: `localhost` |
| `--target <id>` | Target identifier string. Default: first available target |
| `--verify` | Verify erase by reading back all erased bytes and checking they are `0x00` |
| `--backup <file>` | Save DFlash content to a binary file before erasing |
| `--no-reset` | Hot-attach to device without reset (default: reset and halt) |

### Exit Codes

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
| 15 | Error flags detected (PVER/EVER/PROER/SEQER) |
| 16 | Reset to read mode failed |
| 17 | Verification failed (bytes not erased) |

## Examples

### Show device info
```
tas_dflash_erase --info
```
Output example:
```
TAS DFlash Erase Tool
=====================

Connecting to TAS server at localhost...
  Server: TasServer V2.0 (Aug  7 2025)
  Targets: 1
  [0] TC36x (Application Kit TC367 V2.0)
  Auto-selected target [0]

Starting session...
Connecting to device (reset and halt)...
  Device: TC36x (TC3x)
  DFlash: 128 KB total, 32 sectors, sector size 4 KB
  Range:  0xAF000000 - 0xAF01FFFF

Device info displayed. No erase performed.
```

### Erase a single sector with verification
```
tas_dflash_erase 0xAF000000 1 --verify
```
Erases one sector (4 KB on TC3x, 8 KB on TC2x) starting at `0xAF000000`, then reads back to confirm all bytes are `0x00`.

### Erase with non-aligned address
```
tas_dflash_erase 0xAF000F10 1
```
The address `0xAF000F10` is automatically aligned down to `0xAF000000` (4 KB boundary on TC3x).

### Erase multiple sectors with backup and verify
```
tas_dflash_erase 0xAF000000 4 --backup dump.bin --verify
```
Backs up 16 KB of DFlash data to `dump.bin`, then erases 4 sectors and verifies.

### Erase entire DFlash
```
tas_dflash_erase --all --verify
```
Prompts for confirmation:
```
  *** WARNING: Will erase ENTIRE DFlash (128 KB, 32 sectors) ***
  Type 'yes' to confirm: yes
```
Erases all DFlash sectors and verifies. Use `--backup` to save contents first.

### Connect to remote TAS server
```
tas_dflash_erase 0xAF000000 1 --server 192.168.1.100
```

## Erase Sequence

The tool performs the following 8 steps for each erase operation:

1. **Read Safety Watchdog password** - Reads `SCU_WDTS_CON0` (`0xF00362A8`) and extracts the 14-bit password
2. **Clear Safety EndInit** - Unlocks and clears EndInit protection to allow flash modifications
3. **Clear flash status** - Writes `0xFA` to flash status register (`0xAF005554`)
4. **Execute erase command** - Sends 4-word erase sequence to flash command registers:
   - Write sector address to `0xAF00AA50`
   - Write sector count to `0xAF00AA58`
   - Write `0x80` to `0xAF00AAA8` (erase trigger)
   - Write `0x50` to `0xAF00AAA8` (confirm)
5. **Restore Safety EndInit** - Re-enables EndInit protection (always executed even if previous steps fail)
6. **Wait for completion** - Polls `DMU_HF_STATUS` (`0xF8040010`) D0BUSY bit until clear (timeout: 10 s)
7. **Check error flags** - Reads `DMU_HF_STATUS` for PVER (bit 6), EVER (bit 7), PROER (bit 10), SEQER (bit 12)
8. **Reset to read mode** - Writes `0xF0` to `0xAF005554`

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

### Method 2: Build Script

```powershell
# Windows
python tools\build.py

# Linux
python3 tools/build.py
```

Options:
```
-c release|debug|all    Build configuration (default: release)
-p, --python            Build Python wrapper
-d, --docs              Build API documentation
-t, --tests             Build test executables
```

### Build Output

The compiled executable is located at:

```
build/Release/apps/tas_dflash_erase/tas_dflash_erase.exe   (Windows)
build/Release/apps/tas_dflash_erase/tas_dflash_erase       (Linux)
```

Other build artifacts:
```
build/Release/apps/tas_rw_api_demo/tas_rw_api_demo.exe     # RW API demo
build/Release/apps/tas_chl_api_demo/tas_chl_api_demo.exe   # Channel API demo
build/Release/src/tas_client/libtas_client.a                # Static library
build/Release/src/tas_socket/libtas_socket.a                # Socket library
```

### Static Linking (Windows MinGW)

The tool uses static linking to eliminate DLL dependencies, so the `.exe` can run standalone on any Windows machine without requiring MinGW runtime DLLs (`libwinpthread-1.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`).

This is configured in `apps/tas_dflash_erase/CMakeLists.txt`:
```cmake
if (MINGW)
    target_link_options(${EXE_NAME} PRIVATE -static -static-libgcc -static-libstdc++)
endif()
```

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

- The tool auto-detects the connected MCU type and configures sector size and DFlash range accordingly
- Address alignment is performed automatically (4 KB for TC3x, 8 KB for TC2x)
- Safety EndInit is always restored (Step 5) even if the erase operation fails, using a goto-cleanup pattern
- The Safety Watchdog password is re-read before each EndInit operation (hardware may auto-increment)
- Verification reads DFlash in sector-sized chunks to handle large DFlash sizes
