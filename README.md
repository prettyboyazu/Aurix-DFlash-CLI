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

# dflash — AURIX DFlash Command-Line Tool

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-informational)](#building)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)](#building)
[![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C?logo=cmake)](#building)
[![Maintained](https://img.shields.io/badge/Maintained%3F-yes-green.svg)](https://github.com/Infineon/tas_client_api/commits/master/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen)](https://github.com/Infineon/tas_client_api/pulls)

`dflash` is a standalone command-line tool for reading, erasing, and writing **Data Flash (DFlash)** on Infineon **AURIX TC2x/TC3x** microcontrollers. It communicates with the target MCU through a TAS Server and miniWiggler debug probe — no on-chip software or bootloader required.

Built on top of the **[TAS Client API](#tas-client-api)**, an open-source C++ library for device access on Infineon automotive MCUs.

**[Features](#features)** · **[Quick Start](#quick-start)** · **[Commands](#commands)** · **[UCB Operations](#ucb-user-configuration-block)** · **[Building](#building)**

---

## Features

- **Zero install** — single statically-linked executable, no DLL/runtime dependencies
- **Auto-detect** connected MCU type and load device parameters from JSON configs at runtime
- **Full DFlash lifecycle** — erase, read, write, rewrite (R-M-W), restore from backup
- **UCB access** — read, write, erase User Configuration Blocks on TC3x devices
- **Safety mechanisms** — backup before erase, read-back verify, confirmation prompts for destructive operations
- **Legacy compatible** — original positional-argument syntax still works

## Supported Devices

All TC2x/TC3x AURIX devices whose JSON configuration is present in `DeviceConfigs/`:

| Device Family | DFlash Sector Size | DFlash Total Size |
|---------------|--------------------|-------------------|
| TC21x, TC22x, TC23x, TC26x | 8 KB | 128 KB |
| TC27x | 8 KB | 384 KB |
| TC29x | 8 KB | 512 KB |
| TC33x, TC35x, TC36x | 4 KB | 128 KB |
| TC37x, TC3Ex | 4 KB | 256 KB |
| TC38x | 4 KB | 512 KB |
| TC39x | 4 KB | 1 MB |

DFlash base address: `0xAF000000` (loaded from JSON). Erased state: `0x00` (all bits zero).

> **Note:** TC4x (AURIX 2G+) is **not** supported — different base addresses and command sequences.

## Prerequisites

- Windows 10/11 x64 or Linux
- [TAS Server](https://www.infineon.com/cms/en/product/promopages/aurix-tools/) running and connected via miniWiggler
- Target AURIX MCU connected through the debug probe

## Quick Start

```bash
# List connected devices
dflash list

# Read DFlash (hex dump to terminal)
dflash read --addr AF004000 --length 100

# Save DFlash to a binary file
dflash read --addr AF000000 --length 20000 --output dump.bin

# Erase one sector
dflash erase --addr AF004000 --sectors 1

# Erase entire DFlash with backup and verification
dflash erase --all --backup dump.bin --verify

# Write inline data with verification
dflash rewrite --addr AF004000 --data 1122334455667788 --verify

# Write from a file
dflash write --file firmware.hex --verify

# Restore from backup
dflash restore --file dump.bin

# Reset the MCU
dflash reset
```

---

## Commands

```
dflash <subcommand> [options]
```

### Subcommands

| Command | Shortcut | Description |
|---------|----------|-------------|
| `list` | `l` | List connected TAS targets and device info |
| `read` | `r` | Read DFlash content (hex dump, binary, or Intel HEX) |
| `erase` | `e` | Erase DFlash sectors |
| `write` | `w` | Write data to DFlash from a HEX or binary file |
| `rewrite` | `rw` | Read-Modify-Write to any DFlash address (no alignment needed) |
| `restore` | | Restore DFlash from backup (erase + write + verify) |
| `reset` | | Reset the MCU (with optional halt) |
| `ucb` | `u` | Read/write/erase UCB (TC3x only) |

### Global Options

| Option | Short | Description |
|--------|-------|-------------|
| `--help` | `-h` | Show help |
| `--version` | `-v` | Show version |
| `--server <ip>` | `-s` | TAS Server address (default: `localhost`) |
| `--port <n>` | `-p` | TAS Server port (default: `2000`) |
| `--target <id>` | `-t` | Target identifier (default: first available) |
| `--device <name>` | `-d` | Override auto-detected device name |
| `--config-dir <path>` | | Path to `DeviceConfigs/` directory |

### Alignment Constraints

| Operation | Requirement |
|-----------|-------------|
| read | No alignment |
| write | 8-byte (page) aligned |
| erase | Sector-aligned (4 KB or 8 KB, auto) |
| rewrite | No requirement (handled internally) |

Addresses and lengths are in **hexadecimal** (no `0x` prefix needed).

---

### `erase` — Erase DFlash Sectors

```bash
dflash erase --addr <hex> --sectors <n> [--verify] [--backup <file>]
dflash erase --all [--verify] [--backup <file>]
```

| Option | Description |
|--------|-------------|
| `--addr <hex>` | Start address (auto-aligned to sector boundary) |
| `--sectors <n>` | Number of sectors to erase (decimal) |
| `--all` | Erase entire DFlash (requires confirmation) |
| `--info` | Show device info without erasing |
| `--verify` | Read back and verify all bytes are `0x00` |
| `--backup <file>` | Save DFlash content before erasing |
| `--reset` | Reset MCU after erase (resume execution) |
| `--no-reset` | Hot-attach without reset |

```bash
# Show device info
dflash erase --info

# Erase one sector with verification
dflash erase --addr AF000000 --sectors 1 --verify

# Erase all with backup, verify, and MCU reset
dflash erase --all --backup dump.bin --verify --reset
```

### `read` — Read DFlash Content

```bash
dflash read --addr <hex> --length <hex> [--output <file>]
```

| Option | Short | Description |
|--------|-------|-------------|
| `--addr <hex>` | `-a` | Start address (required) |
| `--length <hex>` | `-l` | Bytes to read (required) |
| `--output <file>` | `-o` | Output file (`.bin` = raw, `.hex` = Intel HEX). Default: hex dump to terminal |

```bash
# Hex dump to terminal
dflash read --addr AF000000 --length 1000

# Save as raw binary
dflash read --addr AF000000 --length 20000 --output dump.bin

# Save as Intel HEX
dflash read --addr AF000000 --length 20000 --output dump.hex
```

### `write` — Write Data to DFlash

```bash
dflash write --file <path> [--addr <hex>] [--verify]
```

| Option | Short | Description |
|--------|-------|-------------|
| `--file <path>` | `-f` | Input file (`.hex` or `.bin`). Required |
| `--addr <hex>` | `-a` | Base address for binary files (default: DFlash start). Ignored for HEX files |
| `--verify` | | Verify by read-back comparison |

```bash
# Write binary to DFlash start
dflash write --file data.bin

# Write HEX file with verification
dflash write --file firmware.hex --verify
```

### `rewrite` — Read-Modify-Write (No Alignment Needed)

```bash
dflash rewrite --addr <hex> --data <hexstring> [--verify] [--backup]
dflash rewrite --file <path> [--addr <hex>] [--verify] [--backup]
```

Rewrites data at any DFlash address without manual sector alignment. Internally performs a 5-step sequence: read affected sectors → optional backup → merge new data → erase → write back.

| Option | Short | Description |
|--------|-------|-------------|
| `--addr <hex>` | `-a` | Target address |
| `--data <hexstring>` | | Hex data string (e.g. `12345678AABBCCDD`) |
| `--file <path>` | `-f` | Input file (`--data` and `--file` are mutually exclusive) |
| `--backup [path]` | | Save original data. Without path: auto-generate filename |
| `--verify` | `-v` | Verify by read-back comparison |

```bash
# Inline write 8 bytes
dflash rewrite --addr AF000004 --data 12345678AABBCCDD --verify

# Write from file with auto backup
dflash rewrite --file patch.hex --backup --verify

# Write from binary to specific address
dflash rewrite --file data.bin --addr AF000100
```

### `restore` — Restore from Backup

```bash
dflash restore --file <path> [--no-verify]
```

Performs erase + write + verify as a single operation.

```bash
dflash restore --file dump.bin
```

### `list` — List Connected Devices

```bash
dflash list
```

### `reset` — Reset MCU

```bash
dflash reset [--halt]
```

| Option | Description |
|--------|-------------|
| `--halt` | Halt CPU after reset (for debugging) |

---

## UCB — User Configuration Block

Access UCB on **TC3x** devices. UCB stores boot configuration, security settings, and flash protection.

> **Warning:** Writing incorrect UCB data can **permanently lock** the chip. All UCB write/erase operations require typing `yes` to confirm.

### UCB Address Map

| Region | Address Range | Size | Description |
|--------|--------------|------|-------------|
| BMHD0-3 | `AF400000`–`AF4007FF` | 2 KB | Boot Mode Headers |
| Security | `AF400800`–`AF400FFF` | 2 KB | OTP/DFLASH/DBG/HSM |
| BMHD COPY | `AF401000`–`AF4017FF` | 2 KB | BMHD backup |
| Security COPY | `AF401800`–`AF401FFF` | 2 KB | Security backup |
| PFLASH | `AF402000`–`AF4027FF` | 2 KB | PFlash protection |
| SWAP | `AF402800`–`AF4037FF` | 4 KB | Flash swap |
| LBIST | `AF403800`–`AF4047FF` | 4 KB | Logic BIST |
| SSW | `AF404800`–`AF4057FF` | 4 KB | Startup Software |

### UCB Commands

```bash
# Read entire UCB (24 KB hex dump)
dflash ucb read

# Read BMHD area
dflash ucb read --addr AF400000 --length 800

# Save UCB to Intel HEX file
dflash ucb read -a AF400000 -l 6000 -o ucb_backup.hex

# Write HEX file to UCB with verification
dflash ucb write --file bmhd_config.hex --verify

# Erase BMHD sectors (0-3)
dflash ucb erase --addr AF400000 --sectors 4 --verify
```

Locked regions (`AF400800`–`AF400FFF`, `AF401800`–`AF401FFF`) cannot be erased — the tool will abort with an error.

---

## TAS Client API

This tool is built on the **TAS (Tool Access Socket) Client API** — an open-source C++ library included in this repository (`src/`) that provides low-level device access for Infineon automotive MCUs.

### What TAS Client API Provides

| Component | Path | Description |
|-----------|------|-------------|
| Read/Write API | `src/tas_client/tas_client_rw.h` | Memory read/write access to target device |
| Channel API | `src/tas_client/tas_client_chl.h` | Channel-based communication |
| Trace API | `src/tas_client/tas_client_trc.h` | Target trace data access |
| Server Connection | `src/tas_client/tas_client_server_con.h` | TAS server connection management |
| Socket Transport | `src/tas_socket/` | TCP socket layer for TAS protocol |
| Packet Handler | `src/tas_client/tas_pkt_handler_*.h` | Protocol packet serialization/deserialization |

### How dflash Uses the TAS API

```
┌──────────────────────────────────────────┐
│              dflash CLI                   │
│  (apps/dflash/dflash_main.cpp)           │
├──────────────────────────────────────────┤
│  Device Config Loader │ HEX Parser       │
│  (JSON → DFlash params)                  │
├──────────────────────────────────────────┤
│          TAS Client API                   │
│  ┌─────────────┬───────────────────┐     │
│  │ Server Con  │  Read/Write API   │     │
│  │ (connect)   │  (mem access)     │     │
│  └─────────────┴───────────────────┘     │
├──────────────────────────────────────────┤
│          TAS Socket Layer                 │
│          (TCP transport)                  │
├──────────────────────────────────────────┤
│          TAS Server + miniWiggler         │
│          (hardware debug probe)           │
├──────────────────────────────────────────┤
│          AURIX TC2x/TC3x Target          │
└──────────────────────────────────────────┘
```

### Using the TAS API in Your Own Projects

The TAS Client API can be used independently of the dflash tool to build custom device access applications:

```cpp
#include "tas_client_rw.h"

// Connect to TAS server
TasClientRw client;
client.server_connect("localhost", 2000);
client.session_start();
client.device_connect(TAS_CLNT_DCO_RESET);

// Read memory
std::vector<uint8_t> buf(256);
client.read_mem(0xAF000000, buf.data(), buf.size());

// Write memory
client.write_mem(0xAF000000, data.data(), data.size());

client.device_disconnect();
client.session_end();
client.server_disconnect();
```

The API is built as a static library (`tas_client`) and linked into the dflash executable. See `CMakeLists.txt` and `conanfile.py` for build configuration.

---

## DeviceConfigs

Device parameters (DFlash base address, sector size, total size) are loaded from JSON files at runtime — **no device-specific values are hard-coded**. JSON files originate from Infineon's AURIXFlasherSoftwareTool.

### Search Path (first match wins)

1. `--config-dir <path>` command-line option
2. `DeviceConfigs/` next to the executable
3. `data/DeviceConfigs/` relative to repository root
4. `TAS_DEVICE_CONFIGS` environment variable

### Adding a New Device

1. Obtain the device's JSON file (e.g. `TC37x_A_step.json`)
2. Drop it into `DeviceConfigs/`
3. Re-run — no rebuild or source change needed

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid arguments / address out of range |
| 2 | Server connection failed |
| 3–4 | Get targets failed / no targets |
| 5–6 | Session / device connect failed |
| 7 | Unsupported device (e.g. TC4x) |
| 8 | Backup failed |
| 9–10 | Safety Watchdog / EndInit failed |
| 11–13 | Flash status / erase / EndInit restore failed |
| 14 | Erase timeout |
| 15 | Error flags (PVER/EVER/PROER/SQER/OPER) |
| 16–17 | Read mode / verification failed |
| 18–20 | Read / write / file failed |
| 21 | Restore failed |
| 22–23 | UCB locked / unsupported device |

---

## Building

### Prerequisites

| Component | Minimum Version |
|-----------|----------------|
| C++ Compiler | C++17 (MSVC 2019+, GCC 9+) |
| CMake | 3.25+ |
| Python | 3.11+ (for Conan 2) |
| Conan | 2.x |

### Quick Build (Windows, MSVC — No Conan Required)

```cmd
build.bat                Build Release
build.bat --deploy       Build + deploy dflash.exe to Erase/
build.bat --all          Clean + Build + deploy
```

Output: `build/apps/dflash/Release/dflash.exe`

### Conan + CMake Build

```bash
# Install dependencies
conan install .

# Windows (MSVC)
cmake --preset conan-default
cmake --build --preset conan-release

# Linux (GCC)
cmake --preset conan-release
cmake --build --preset conan-release
```

### Static Linking

The executable is statically linked — no VC++ Redistributable or other runtime DLLs required. The resulting `dflash.exe` depends only on Windows system DLLs.

### CI Build

```bash
python tools/build.py    # Windows
python3 tools/build.py   # Linux
```

### Python Wrapper

```bash
conan install . -o python=True
# Output: build/python/dist/
```

---

## Full Documentation

- [TAS DFlash Tool Reference](docs/tas_dflash_erase_tool.md) — complete command reference, erase sequence, register architecture, ECC handling
- [AURIX CAN BSL Guide](docs/aurix_can_bsl_guide.md) — bootloader setup reference

## License

Licensed under the [Apache License 2.0](LICENSE).
