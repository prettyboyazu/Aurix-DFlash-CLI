# AURIX MCP Server — 打包说明 (v2.2)

> 将 Infineon AURIX TC2x / TC3x 微控制器的 DFlash 调试 + PFlash 烧写能力，封装为
> [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) 工具，让 Claude / GPT / Cursor /
> Qoder / VS Code (Continue/Cline) 等 AI 客户端能通过自然语言操作硬件。

---

## 目录

1. [Quick Start — 30 秒跑起来](#1-quick-start--30-秒跑起来)
2. [包目录结构](#2-包目录结构)
   - 2.5 [工具来源（wiggle vs AURIXFlasher 的本质区别）](#25-工具来源wiggle-vs-aurixflasher-的本质区别)
   - 2.6 [AURIXFlasher 安装与配置](#26-aurixflasher-安装与配置)
3. [三层架构 / 数据流](#3-三层架构--数据流)
4. [环境要求](#4-环境要求)
5. [配置 `mcp_config.json`](#5-配置-mcp_configjson)
6. [启动 MCP Server](#6-启动-mcp-server)
7. [在 AI 客户端中注册](#7-在-ai-客户端中注册)
8. [19 个 MCP 工具速查](#8-19-个-mcp-工具速查)
9. [工具详细参数](#9-工具详细参数)
10. [返回 JSON 格式](#10-返回-json-格式)
11. [典型工作流](#11-典型工作流)
12. [UCB 地址图 (TC3xx)](#12-ucb-地址图-tc3xx)
13. [退出码 / 错误码](#13-退出码--错误码)
14. [测试](#14-测试)
15. [常见问题](#15-常见问题)
16. [配套手册 (Excel)](#16-配套手册-excel)
17. [文档版本](#17-文档版本)

---

## 1. Quick Start — 30 秒跑起来

```bash
# ① 装 Python 依赖（Python 3.10+）
pip install "mcp[cli]"

# ② 启动 Infineon TAS Server（miniWiggler 调试器后端，保持后台运行）

# ③ AURIX 板上电 + miniWiggler USB 插入

# ④ 启动 MCP server（stdio 模式，给 Claude / Cursor / Qoder 用）
python D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json
```

启动成功后，AI 客户端就能调用 **19 个工具**（13 个硬件操作 + 6 个构建产物分析）操作 AURIX。

**5 秒烟雾测试**（不开 AI 客户端，先确认链路通）：

```bash
# wiggle 是否识别到目标？
D:\wiggle_MCP\wiggle\wiggle.exe list

# AURIXFlasher 是否能连上？（如果已安装）
AURIXFlasher.exe -l   # 或完整路径

# 跑全量 MCP 集成测试
cd D:\wiggle_MCP
python test_package.py
# 预期：6 PASS / 0 FAIL
```

---

## 2. 包目录结构

```
D:\wiggle_MCP\                            ← 本包根目录
├── aurix_mcp_server.py                   ← MCP server 入口（Python）
├── build_analysis.py                     ← 构建产物解析库（MAP/ELF/LST/MDF）
├── mcp_config.json                       ← 配置文件
├── requirements.txt                      ← Python 依赖列表
├── start_mcp.bat                         ← Windows 启动脚本
├── start_mcp.sh                          ← Linux/Mac 启动脚本
├── test_package.py                       ← 完整性验证脚本
├── README.md                             ← 本文档
│
├── wiggle\                               ← 自研 DFlash 调试工具
│   ├── wiggle.exe
│   ├── DeviceConfigs\                    ← 21 个芯片的设备配置 JSON
│   │   ├── devices.json
│   │   └── TC{21x..39x,A_step,...}.json
│   ├── RegisterDefs\                     ← 8 个芯片的 SVD 派生完整寄存器定义
│   │   └── TC{23x,27xD,29xB,33x,36x,37x,38x,39xB}-full.json
│   └── wiggle-Command-Reference.xlsx     ← 配套 CLI 手册
│
└── docs\
    ├── MCP-Command-Reference.xlsx        ← 19 个 MCP 工具的完整手册
    ├── AURIXFlasher-Command-Reference.xlsx ← AURIXFlasher CLI 手册
    └── mcp_config_guide.md               ← 配置文件填写指南

> **注意**：`AURIXFlasher.exe` 是 Infineon 闭源工具，**不包含在本包内**，需要自行安装（见 §2.6）。

**总磁盘占用**：约 430 MB（其中 8 个 RegisterDefs JSON 占 ~290 MB）。

---

## 2.5 工具来源（wiggle vs AURIXFlasher 的本质区别）

本包使用两个核心 exe，**来源完全不同**，混淆会导致版权 / 维护 / 升级预期出错：

|  | `wiggle.exe`（本包内含） | `AURIXFlasher.exe`（需自行安装） |
|--|---------------|---------------------|
| **性质** | 本项目**自研** | Infineon **官方商业产品** |
| **作者** | 本项目（基于 Infineon 开源的 TAS Client API） | Infineon Technologies AG |
| **开源** | Apache License 2.0 | **闭源**商业工具 |
| **分发** | **包含在本包内** | **不包含**，需自行从 Infineon 官网下载 |
| **语言/构建** | C++17，CMake + Conan 2，**静态链接**（无 VC++ 运行依赖）| .NET Framework 4.8（看 `AURIXFlasher.exe.config`）|
| **更新方式** | 改源码 → `build.bat` → 重新部署到 `wiggle_MCP\wiggle\` | 下载 Infineon 新版安装包 |
| **能力域** | DFlash 操作 + 调试（12 个 MCP 工具）| PFlash 整片烧写（1 个 MCP 工具：`wiggle_pflash`）|
| **依赖 DLL** | 无（静态链接）| `ConversionLayerdll.dll` / `LATTE.dll` / `Newtonsoft.Json.dll` / `tricore-objcopy.exe` / 多个 `flax_TC*.hex`（Flash Loader）|
| **协议** | TAS Client API（开源 C++ 库，Apache 2.0）| 私有 DAS 协议（Infineon 内部）|
| **对接硬件** | TAS Server (TCP:2000) → miniWiggler → AURIX | DAS Server → miniWiggler → AURIX（也支持 DAP/JTAG/SPD/SWD）|

**一句话总结**：
- **wiggle = 我们写的 DFlash 调试器**（Apache 2.0，本包直接包含）
- **AURIXFlasher = Infineon 的 PFlash 烧写器**（闭源，需自行安装，见 §2.6）

所以 MCP server 里的 19 个工具中：
- **12 个**（`wiggle_list` / `wiggle_info` / `wiggle_status` / `wiggle_read` / `wiggle_erase` / `wiggle_write` / `wiggle_reg` / `wiggle_dump` / `wiggle_poke` / `wiggle_search` / `wiggle_compare` / `wiggle_reset`）→ 调 wiggle.exe
- **1 个**（`wiggle_pflash`）→ 调 AURIXFlasher.exe
- **6 个**（`build_project` / `reload_parsers` / `parse_symbols` / `parse_elf` / `lookup_symbol` / `lookup_address`）→ 解析编译产物（TASKING MAP / GCC LST / ELF / MDF）

要扩展功能（比如支持新芯片型号、加新调试命令）→ 改 wiggle 源码 + 重新 build。
要更新 AURIXFlasher 版本（比如等 TC4x 支持）→ 等 Infineon 发新版。

> `wiggle\RegisterDefs\*.json` 也不属于 Infineon：是用项目自带的 `svd-to-full-json` skill 从 Infineon 公开的 `data/SVD\*.svd` 派生的 JSON（见 [§15 常见问题 / 怎么更新 SVD 寄存器定义](#15-常见问题)）。

---

## 2.6 AURIXFlasher 安装与配置

AURIXFlasher 是 Infineon 提供的**闭源商业工具**，受版权限制**不包含在本包内**。

### 第一步：下载安装

1. 前往 Infineon 官网下载 [AURIX Flasher Software Tool](https://www.infineon.com/cms/en/tools/landing/aurix-tools/aurix-flasher-software-tool/)
2. 安装（免费，无需购买 license）
3. 默认安装路径：`C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\`

### 第二步：MCP Server 自动查找

`aurix_mcp_server.py` 启动时按以下优先级自动搜索 AURIXFlasher.exe：

| 优先级 | 来源 | 说明 |
|--------|------|------|
| 1 | `mcp_config.json` 的 `aurix_flasher_exe` 字段 | 显式配置，最优先 |
| 2 | 环境变量 `AURIX_FLASHER_EXE` | 完整路径 |
| 3 | 环境变量 `AURIX_FLASHER_DIR` | 目录，自动拼 `AURIXFlasher.exe` |
| 4 | 脚本同目录 | `D:\wiggle_MCP\AURIXFlasher.exe` |
| 5 | `D:\wiggle_MCP\AURIXFlasher\` | 同级 AURIXFlasher 子目录 |
| 6 | Infineon 默认安装路径 | `C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>`C:\Program Files\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>`C:\Program Files (x86)\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>（glob 匹配任意版本号）|
| 7 | PATH 环境变量 | scoop / choco / 手动加入 PATH 的能找到 |

**通常情况下**，按默认路径安装后 MCP Server 会自动找到，无需手动配置。

### 第三步：如果自动查找失败

在 `mcp_config.json` 中手动指定：

```json
{
    "aurix_flasher_exe": "C:\\Infineon\\AURIXFlasherSoftwareTool-3.0.16\\AURIXFlasher.exe",
    ...
}
```

或者设置环境变量（优先级高于配置文件）：

```powershell
# 方法 A：完整路径
set AURIX_FLASHER_EXE=C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\AURIXFlasher.exe

# 方法 B：只给目录
set AURIX_FLASHER_DIR=C:\Infineon\AURIXFlasherSoftwareTool-3.0.16
```

### 验证

```bash
# 如果 AURIXFlasher 在 PATH 或已配置好
AURIXFlasher.exe -l

# 或完整路径
C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\AURIXFlasher.exe -l
```

成功应列出已连接的 AURIX 目标。

---

## 3. 三层架构 / 数据流

```
┌────────────────────────────────────────────────────────────────┐
│ L7  AI 客户端 (Claude / Cursor / Qoder / Continue)              │
│      ↓ stdio (JSON-RPC 2.0)  或  SSE (HTTP)                    │
├────────────────────────────────────────────────────────────────┤
│ L6  MCP Server (aurix_mcp_server.py, FastMCP)                   │
│      • 13 个 wiggle_* 工具 → 翻译成 wiggle / AURIXFlasher 调用  │
│      • 5 个 build_* 工具 → 解析 TASKING/GCC 编译产物            │
│      • --json 解析 / 错误码统一                                  │
├────────────────────────────────────────────────────────────────┤
│ L5  CLI 工具 (wiggle.exe / AURIXFlasher.exe)                     │
│      • DFlash: 读 / 擦 / 写 / rewrite / restore / 调试寄存器    │
│      • PFlash: 整片擦 / 编程 / 校验 (AURIXFlasher)              │
├────────────────────────────────────────────────────────────────┤
│ L4  TAS Client API (C++ 静态库, src/tas_client/)                │
├────────────────────────────────────────────────────────────────┤
│ L3  TAS Server (Infineon 官方, 后台进程, TCP:2000)             │
├────────────────────────────────────────────────────────────────┤
│ L2  miniWiggler 调试探针 (USB → DAP/JTAG)                       │
├────────────────────────────────────────────────────────────────┤
│ L1  AURIX TC2x / TC3x 目标板                                    │
│      DFlash 0xAF000000 / PFlash 0xA0000000 / SRAM 0x70000000    │
└────────────────────────────────────────────────────────────────┘
```

**职责划分**：
- **DFlash 操作**（读 / 擦 / 写 / 调试）→ `wiggle.exe`，MCP 工具 12 个
- **PFlash 烧写**（整片擦 + 编程 + 校验）→ `AURIXFlasher.exe`，MCP 工具 1 个：`wiggle_pflash`
- **两者互补不重叠** —— 不要把 wiggle 用到 PFlash，也不要把 AURIXFlasher 用到 DFlash

---

## 4. 环境要求

| 组件 | 最低版本 | 备注 |
|------|----------|------|
| OS | Windows 10/11 x64 或 Linux | 32 位 DLL 路径有差异 |
| **Python** | **3.10+（硬性要求）** | `mcp[cli]` 包不支持 3.9 及以下。`python -V` 查看 |
| `mcp[cli]` | latest | `pip install "mcp[cli]"` |
| TAS Server | 任意 Infineon 受支持版本 | 后台进程，监听 TCP:2000 |
| miniWiggler | 驱动装好 | USB 接 PC，JTAG/DAP 接 AURIX |
| AURIX 目标 | TC2x / TC3x | TC4x **不支持**（base 地址、命令序列不同） |

> **Python 3.8/3.9 用户**：直接 `pip install "mcp[cli]"` 会报 `Could not find a version that satisfies the requirement mcp[cli] (from versions: none)`。**两个解决方式**：
> 1. 装 Python 3.10+（推荐）：[python.org/downloads](https://www.python.org/downloads/)
> 2. 不想换 Python：直接用 wiggle CLI（13 个 MCP 工具本质都是 `wiggle.exe --json` 的封装，跳过 MCP server 也照样用）

---

## 5. 配置 `mcp_config.json`

```json
{
    "wiggle_exe":         "D:\\wiggle_MCP\\wiggle\\wiggle.exe",
    "aurix_flasher_exe":  null,
    "server":             "localhost",
    "target":             null,
    "device":             null,
    "timeout":            120,

    "build": {
        "command":        null,
        "work_dir":       null,
        "timeout":        300
    },
    "project": {
        "elf_file":       null,
        "map_file":       null,
        "lst_file":       null,
        "mdf_file":       null,
        "hex_file":       null,
        "toolchain":      null
    }
}
```

### 5.1 基础配置（硬件操作）

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `wiggle_exe` | string\|null | `null`=自动搜 | wiggle.exe 完整路径 |
| `aurix_flasher_exe` | string\|null | `null`=自动搜 | AURIXFlasher.exe 完整路径 |
| `server` | string | `localhost` | TAS Server IP |
| `target` | string\|null | `null`=首个 | 目标 ID，多目标时指定 |
| `device` | string\|null | `null`=自动 | 设备名覆盖（如 `TC33x_A_step`） |
| `timeout` | int | `120` | 单工具超时秒（`wiggle_pflash` 固定 300 s） |

### 5.2 构建配置（build_project 工具）

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `build.command` | string\|null | `null` | 编译命令（如 `"make all"` 或 `"build.bat"`） |
| `build.work_dir` | string\|null | `null` | 编译工作目录 |
| `build.timeout` | int | `300` | 编译超时秒数 |

### 5.3 项目配置（符号分析工具）

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `project.elf_file` | string\|null | `null` | ELF 文件路径（TASKING/GCC 均可） |
| `project.map_file` | string\|null | `null` | TASKING MAP 文件路径 |
| `project.lst_file` | string\|null | `null` | GCC LST 文件路径（objdump 输出） |
| `project.mdf_file` | string\|null | `null` | TASKING MDF 文件路径（地址空间翻译） |
| `project.hex_file` | string\|null | `null` | HEX 文件路径 |
| `project.toolchain` | string\|null | `null`=自动 | 工具链：`"tasking"` / `"gcc"` / `null` |

**典型配置示例**（TASKING 项目）：
```json
"project": {
    "elf_file": "D:/workspace/MyProject/TriCore Debug (TASKING)/MyApp.elf",
    "map_file": "D:/workspace/MyProject/TriCore Debug (TASKING)/MyApp.map",
    "mdf_file": "D:/workspace/MyProject/TriCore Debug (TASKING)/MyApp.mdf",
    "toolchain": "tasking"
}
```

**典型配置示例**（GCC 项目）：
```json
"project": {
    "elf_file": "D:/workspace/MyProject/TriCore Debug (GCC)/MyApp.elf",
    "lst_file": "D:/workspace/MyProject/TriCore Debug (GCC)/MyApp.lst",
    "toolchain": "gcc"
}
```

### 5.4 自动搜索路径

**优先级**（高 → 低，命中即停）：

| # | 来源 | 示例 / 备注 |
|---|------|------------|
| 0 | **`mcp_config.json` 显式配置** | `wiggle_exe` / `aurix_flasher_exe` 写死绝对路径 |
| 1 | **`AURIX_FLASHER_EXE` 环境变量**（完整路径）| `set AURIX_FLASHER_EXE=C:\Infineon\...\AURIXFlasher.exe` |
| 2 | **`AURIX_FLASHER_DIR` 环境变量**（目录，自动拼 `AURIXFlasher.exe`）| `set AURIX_FLASHER_DIR=C:\Infineon\AURIXFlasherSoftwareTool-3.0.16` |
| 3 | **`WIGGLE_EXE` 环境变量** | `set WIGGLE_EXE=D:\wiggle_MCP\wiggle\wiggle.exe` |
| 4 | **`WIGGLE_DIR` 环境变量** | `set WIGGLE_DIR=D:\wiggle_MCP\wiggle` |
| 5 | 脚本所在目录（`D:\wiggle_MCP\`）| 找 `wiggle.exe` / `AURIXFlasher.exe` |
| 6 | `./wiggle/`（同级 wiggle 子目录）| 找 `wiggle.exe` / `wiggle` |
| 7 | `./data/`（同级 data 子目录 — 兼容开发仓库布局）| |
| 8 | `./AURIXFlasher/`（同级 AURIXFlasher 子目录）| 找 `AURIXFlasher.exe` |
| 9 | **Infineon 默认安装位置**（仅 Windows）：<br>• `C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>• `C:\Program Files\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>• `C:\Program Files (x86)\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe` | glob 匹配任何版本号 |
| 10 | **PATH 兜底**（`shutil.which`）| scoop / choco / 手动 PATH 装的能找到 |
| — | **都找不到** → 启动时报错 `Executable not found: ...` | |

**实测结果**（你机器上的当前配置）：
- `find_exe("AURIXFlasher.exe")` → 命中第 9 条：`C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\AURIXFlasher.exe`
- `find_exe("wiggle.exe")` → 命中第 6 条：`D:\wiggle_MCP\wiggle\wiggle.exe`

> **CI / 容器化部署推荐用 env var**：不用动 `mcp_config.json` 就能指向不同机器的 exe：
> ```bash
> # Linux / WSL 示例
> export AURIX_FLASHER_EXE=/opt/infineon/aurixflasher/AURIXFlasher.exe
> export WIGGLE_EXE=/opt/wiggle/wiggle
> python aurix_mcp_server.py
> ```
>
> **找不到时的处理**：直接启动失败 → 优先用 env var（第 1-4 条）；env var 也不想设就改 `mcp_config.json` 写死路径。**不要把 exe 复制到 `D:\wiggle_MCP\wiggle\` 或 `AURIXFlasher\`**（参见 §2.5：AURIXFlasher 是 Infineon 商业产品，EULA 禁止二次分发）。

---

## 6. 启动 MCP Server

```bash
# 方式 1：用配置文件（推荐）
python D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json

# 方式 2：自动搜索（不传 --config）
python D:\wiggle_MCP\aurix_mcp_server.py

# 方式 3：SSE 远程模式（HTTP，默认端口 8000）
python D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json --transport sse
```

> 启动后进程常驻；客户端断开后进程不会自动退出，需要 Ctrl+C 手动结束。

---

## 7. 在 AI 客户端中注册

### 7.1 Claude Desktop

文件位置（Windows）：`%APPDATA%\Claude\claude_desktop_config.json`

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": [
        "D:/wiggle_MCP/aurix_mcp_server.py",
        "--config",
        "D:/wiggle_MCP/mcp_config.json"
      ]
    }
  }
}
```

### 7.2 Cursor

文件位置：`~/.cursor/mcp.json`

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": [
        "D:/wiggle_MCP/aurix_mcp_server.py",
        "--config",
        "D:/wiggle_MCP/mcp_config.json"
      ]
    }
  }
}
```

### 7.3 VS Code (Continue / Cline / Roo Code)

VS Code 原生不支持 MCP，需先装一个 MCP 扩展，然后在扩展的配置里写：

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": [
        "D:/wiggle_MCP/aurix_mcp_server.py",
        "--config",
        "D:/wiggle_MCP/mcp_config.json"
      ]
    }
  }
}
```

### 7.4 Qoder IDE

Qoder 设置 → MCP → 添加：

- **Name**: `aurix`
- **Command**: `python`
- **Args**: `D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json`

### 7.5 注册后怎么验证

在 AI 客户端里直接问：

```
你有哪些 aurix 相关的工具？
```

应能看到 19 个工具列表（13 个 `wiggle_*` + 6 个 build 分析工具）。然后：

```
帮我查一下当前连接的 AURIX 设备型号和 DFlash 状态
```

AI 应自动调用 `wiggle_list()` + `wiggle_status()` + `wiggle_info()` 给出答案。

---

## 8. 19 个 MCP 工具速查

按功能分组。详细参数见 [§9](#9-工具详细参数)。

### 8.1 设备信息（3 个）

| 工具 | 用途 |
|------|------|
| `wiggle_list` | 列已连 TAS 目标 |
| `wiggle_status` | Flash 状态寄存器（SVD 字段解码） |
| `wiggle_info` | 设备信息 / 内存布局 / SVD 摘要 |

### 8.2 DFlash 操作（3 个）

| 工具 | 用途 |
|------|------|
| `wiggle_erase` | 擦 DFlash 扇区（全部 or 指定地址） |
| `wiggle_write` | 从 HEX/BIN 文件写 DFlash |
| `wiggle_read` | 读 DFlash 内容 |

### 8.3 PFlash 烧写（1 个）

| 工具 | 用途 |
|------|------|
| `wiggle_pflash` | 调 AURIXFlasher 烧 PFlash（仅 hex_file 一个参数） |

### 8.4 调试（5 个）

| 工具 | 用途 |
|------|------|
| `wiggle_reg` | 按 SVD 名字/地址读写寄存器（支持枚举外设寄存器） |
| `wiggle_dump` | 内存 Hex Dump（PFlash/DFlash/SRAM） |
| `wiggle_poke` | 写值到内存（带 readback 校验） |
| `wiggle_search` | 搜索内存字节模式 |
| `wiggle_compare` | 本地文件 vs Flash 比对 |

### 8.5 控制（1 个）

| 工具 | 用途 |
|------|------|
| `wiggle_reset` | 复位 MCU（可选 halt） |

### 8.6 构建产物分析（6 个）

| 工具 | 用途 |
|------|------|
| `build_project` | 执行编译命令，返回 errors/warnings/output_files，成功后自动刷新 parser 缓存 |
| `reload_parsers` | 强制重新加载 MAP/LST/ELF/MDF 解析器（外部编译后使用） |
| `parse_symbols` | 解析 MAP/LST 符号表（自动检测 TASKING/GCC） |
| `parse_elf` | 解析 ELF 文件符号、段、DWARF 调试信息 |
| `lookup_symbol` | 按名称跨源查找符号（MAP + ELF） |
| `lookup_address` | 按地址反查最近符号 + MDF 地址翻译 |

---

## 9. 工具详细参数

参数命名遵循 Python 风格（snake_case），所有十六进制接受 `0x` 前缀或裸 hex，空串 `""` 表示用默认。

### wiggle_list

```
wiggle_list(server='', target='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `server` | string | `""` | TAS server IP（覆盖配置） |
| `target` | string | `""` | 目标 ID |

### wiggle_status

```
wiggle_status(server='', target='', device='') → JSON
```

读 `DMU.HF.STATUS` 等 Flash 状态寄存器，按 SVD 自动解字段（D0BUSY/D1BUSY/P0BUSY/...）。

### wiggle_info

```
wiggle_info(server='', target='', device='') → JSON
```

返回 `{device, family, dflash:{base,sector_size,sectors,size}, memory_regions:[]}`。

### wiggle_read

```
wiggle_read(address, length, output_file='', server='', target='', device='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `address` | string | **必填** | 起始地址（hex） |
| `length` | string | **必填** | 长度（hex） |
| `output_file` | string | `""` | `.bin`=原始二进制 / `.hex`=Intel HEX；空=stdout hex dump |

```
示例：
  wiggle_read('0xAF000000', '0x100')
  wiggle_read('0xAF000000', '0x2000', output_file='dump.bin')
```

### wiggle_erase

```
wiggle_erase(erase_all=True, address='', sectors=0, verify=False,
             server='', target='', device='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `erase_all` | bool | `true` | 整片擦（默认） |
| `address` | string | `""` | 起始地址（`erase_all=false` 时必填，hex） |
| `sectors` | int | `0` | 扇区数（0=1 个） |
| `verify` | bool | `false` | 擦后回读校验（推荐 `true`） |

```
示例：
  wiggle_erase(erase_all=True, verify=True)                                    # 整片擦
  wiggle_erase(erase_all=False, address='0xAF004000', sectors=2, verify=True)  # 局部擦
```

### wiggle_write

```
wiggle_write(file_path, verify=True, server='', target='', device='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `file_path` | string | **必填** | `.hex` 或 `.bin` 路径 |
| `verify` | bool | `true` | 写后回读校验（推荐） |

> 仅支持 DFlash。PFlash 请用 `wiggle_pflash`。

### wiggle_pflash

```
wiggle_pflash(hex_file) → str
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `hex_file` | string | **必填** | Intel HEX 文件路径（暂不支持 ELF） |

**行为**：超时 300 s；走 AURIXFlasher 默认流程（擦已用扇区 + 编程 + 复位启动）。返回 AURIXFlasher 的 stdout + `Pass`/`Fail` 标记。

如需 `erase all` / `verify on` / `-ucb on` / `-connect 0` / `-start off` 等精细控制，**请直接用 AURIXFlasher.exe CLI**，详见 `AURIXFlasher-Command-Reference.xlsx`。

### wiggle_reg

```
wiggle_reg(name_or_addr, value='', list_peripheral='',
           server='', target='', device='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `name_or_addr` | string | 看下 | 寄存器名（如 `DMU.HF.STATUS`）或地址（如 `0xF8040010`）；list 模式可空 |
| `value` | string | `""` | 写入值（空=读） |
| `list_peripheral` | string | `""` | 设置时进入 list 模式，列出外设全部寄存器 |

```
示例：
  wiggle_reg('DMU.HF.STATUS')                    # 读
  wiggle_reg('0xF8040010')                       # 按地址读
  wiggle_reg('DMU.HF.OPERATION', value='0x1')    # 写
  wiggle_reg('', list_peripheral='DMU')          # 列 74 个 DMU 寄存器
```

### wiggle_dump

```
wiggle_dump(address, length, output_file='', server='', target='', device='') → JSON
```

任意内存 hex dump（`0xAF...`=DFlash / `0xA0...`=PFlash / `0x70...`=SRAM）。

### wiggle_poke

```
wiggle_poke(address, value, width=32, server='', target='', device='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `width` | int | `32` | 位宽 8 / 16 / 32 / 64 |

带 readback 校验，返回 `match: true/false`。

### wiggle_search

```
wiggle_search(address, length, pattern, server='', target='', device='') → JSON
```

`pattern` 是 hex 字符串（如 `DEADBEEF`，无 `0x`），返回匹配地址列表。

### wiggle_compare

```
wiggle_compare(file_path, address, length='', server='', target='', device='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `file_path` | string | **必填** | `.hex` 或 `.bin` 路径 |
| `address` | string | **必填** | 起始地址（hex） |
| `length` | string | `""` | 比较长度（空=文件全长） |

### wiggle_reset

```
wiggle_reset(halt=False, server='', target='') → JSON
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `halt` | bool | `false` | `true` = 复位后停 CPU（用于 debug） |

### build_project

```
build_project(clean=False) → JSON
```

执行用户构建命令，返回编译结果。

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `clean` | bool | `false` | 执行 clean 后再 build |

**配置要求**：在 `mcp_config.json` 中设置 `build.command` 和 `build.work_dir`。

### reload_parsers

```
reload_parsers() → JSON
```

强制重新加载所有编译产物解析器（MAP/LST/ELF/MDF）。在以下场景使用：
- 通过 IDE 或其他工具在外部完成了编译
- `build_project` 以外的方式修改了编译产物文件
- 切换了项目配置中的 `elf_file` / `map_file` 路径

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| （无参数） | - | - | 自动重新加载所有已配置的解析器 |

**返回**：`loaded` 数组列出本次加载的解析器和符号数量。

### parse_symbols

```
parse_symbols(summary=True, search='', max_results=100) → JSON
```

解析 MAP/LST 文件的符号表和内存布局。

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `summary` | bool | `true` | 返回内存用量摘要（TASKING MAP 专用） |
| `search` | string | `""` | 按名称过滤符号（不区分大小写） |
| `max_results` | int | `100` | 最多返回符号数 |

**配置要求**：在 `mcp_config.json` 中设置 `project.map_file`（TASKING）或 `project.lst_file`（GCC）。

### parse_elf

```
parse_elf(functions=False, variables=False, sections=True, search='', max_results=100) → JSON
```

解析 ELF 文件的符号、段、DWARF 调试信息。

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `functions` | bool | `false` | 只返回函数符号 |
| `variables` | bool | `false` | 只返回变量符号 |
| `sections` | bool | `true` | 返回段信息 |
| `search` | string | `""` | 按名称过滤符号 |
| `max_results` | int | `100` | 最多返回符号数 |

**配置要求**：在 `mcp_config.json` 中设置 `project.elf_file`。

### lookup_symbol

```
lookup_symbol(name, source='all') → JSON
```

按名称跨源查找符号（MAP + ELF）。

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `name` | string | 必填 | 符号名称 |
| `source` | string | `"all"` | 查找范围：`"map"` / `"elf"` / `"all"` |

### lookup_address

```
lookup_address(address, source='all') → JSON
```

按地址反查最近符号 + MDF 地址翻译。

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `address` | string | 必填 | 十六进制地址（如 `"0x800019de"`） |
| `source` | string | `"all"` | 查找范围：`"map"` / `"elf"` / `"all"` |

**配置要求**（可选）：设置 `project.mdf_file` 可启用 TASKING 地址空间翻译。

---

## 10. 返回 JSON 格式

**所有工具返回字符串形式的 JSON**。Agent 应解析后用 `status` 字段判断成功。

**成功**：
```json
{
  "status": "ok",
  "command": "...",
  "...": "其他字段依工具而定"
}
```

**错误**：
```json
{
  "status": "error",
  "code": -1,
  "message": "错误描述"
}
```

### 常见错误码（MCP 层）

| code | 含义 | 处理建议 |
|------|------|----------|
| `-1` | 可执行文件未找到 | 检查 `mcp_config.json` 的 `wiggle_exe` / `aurix_flasher_exe` 路径 |
| `-2` | 命令超时 | `timeout` 调大，或检查 TAS Server 是否卡住 |
| `-3` | 运行时异常 | 查 stderr 完整堆栈 |
| `>0` | wiggle / AURIXFlasher 进程退出码 | 查 [§13 退出码](#13-退出码--错误码) |

### SVD 字段解码样例

`wiggle_reg("DMU.HF.STATUS")` 返回：

```json
{
  "status": "ok",
  "register": "HF.STATUS",
  "address": 4161011728,
  "value": 34078720,
  "fields": {
    "D0BUSY": {"value": 0, "lsb": 0, "msb": 0, "desc": "DF0 ready, not busy..."},
    "D1BUSY": {"value": 0, "lsb": 1, "msb": 1, "desc": "DF1 ready, not busy..."},
    "P0BUSY": {"value": 0, "lsb": 8, "msb": 8, "desc": "PF0 ready, not busy..."}
  }
}
```

字段定义来自 `wiggle/RegisterDefs/<chip>-full.json`（自动从 `data/SVD/` 派生）。

---

## 11. 典型工作流

### 11.1 上电检查

```
AI → wiggle_list()                       # 确认设备已连
AI → wiggle_info()                       # 读芯片型号、内存布局
AI → wiggle_status()                     # Flash 是否忙 / 错
```

### 11.2 DFlash 备份 → 整片擦 → 校验

```
AI → wiggle_read('0xAF000000', '0x20000', output_file='backup.bin')   # 1. 备份
AI → wiggle_erase(erase_all=True, verify=True)                        # 2. 整片擦 + 校验
AI → wiggle_read('0xAF000000', '0x10')                                # 3. 抽查，确认全 0
```

### 11.3 DFlash 烧数据 + 回读校验

```
AI → wiggle_erase(erase_all=False, address='0xAF004000', sectors=2, verify=True)
AI → wiggle_write(file_path='patch.hex', verify=True)
AI → wiggle_compare(file_path='patch.hex', address='0xAF004000', length='0x1000')
```

### 11.4 PFlash 烧固件（用 AURIXFlasher）

```
AI → wiggle_pflash(hex_file='D:/firmware.hex')
# 预期返回 "AURIXFlasher Exit Status: Pass"
```

如需 `erase all` / `verify on` / UCB 编程 / hot-attach / 烧后不启动等精细控制，**直接用 AURIXFlasher CLI**（详见 `AURIXFlasher-Command-Reference.xlsx`）。

### 11.5 调试：读 / 写 / 枚举寄存器

```
AI → wiggle_reg('', list_peripheral='DMU')          # 1. 列出 DMU 全部 74 个寄存器
AI → wiggle_reg('DMU.HF.STATUS')                     # 2. 读 Flash 状态（含字段解码）
AI → wiggle_reg('0xF8040010')                        # 3. 按绝对地址读
AI → wiggle_reg('DMU.HF.OPERATION', value='0x1')     # 4. 写触发寄存器
AI → wiggle_status()                                 # 5. 再读一次，看变化
```

### 11.6 内存检查 / 模式搜索

```
AI → wiggle_dump('0xAF000000', '0x100')                                    # hex dump
AI → wiggle_search('0xAF000000', '0x10000', 'DEADBEEF')                    # 找 0xDEADBEEF
AI → wiggle_poke('0x70000000', '0xDEADBEEF')                               # 写 SRAM (CPU0 DSPR)
AI → wiggle_read('0x70000000', '0x10', output_file='sram.bin')             # 读回
```

### 11.7 完整烧写 + 复位

```
AI → wiggle_erase(erase_all=True, verify=True)        # 1. 擦 DFlash
AI → wiggle_write(file_path='data.hex', verify=True)  # 2. 写 DFlash
AI → wiggle_compare(file_path='data.hex', address='0xAF000000')  # 3. 对比
AI → wiggle_reset()                                   # 4. 复位跑起来
```

---

## 12. UCB 地址图 (TC3xx)

UCB = User Configuration Block，存 BMHD、安全配置、Flash 保护等。**写错会 brick 芯片** —— 写入操作都要敲 `yes` 确认。

| 区域 | 地址范围 | 大小 | 描述 | 可否擦除 |
|------|----------|------|------|----------|
| BMHD0-3 | `AF400000-AF4007FF` | 2 KB | Boot Mode Headers (0-3) | ✅ |
| **Security** | `AF400800-AF400FFF` | 2 KB | OTP/DFLASH/DBG/HSM | ❌ **LOCKED** |
| BMHD COPY | `AF401000-AF4017FF` | 2 KB | BMHD 备份 | ✅ |
| **Security COPY** | `AF401800-AF401FFF` | 2 KB | Security 备份 | ❌ **LOCKED** |
| PFLASH | `AF402000-AF4027FF` | 2 KB | PFlash 保护 | ✅ |
| SWAP | `AF402800-AF4037FF` | 4 KB | Flash swap | ✅ |
| LBIST | `AF403800-AF4047FF` | 4 KB | Logic BIST | ✅ |
| SSW | `AF404800-AF4057FF` | 4 KB | Startup Software | ✅ |

MCP 暂未封装 UCB 工具，需要时请直接用 wiggle CLI：

```bash
# 读整片 UCB
D:\wiggle_MCP\wiggle\wiggle.exe ucb read

# 读 BMHD 区
D:\wiggle_MCP\wiggle\wiggle.exe ucb read --addr AF400000 --length 800

# 备份到 Intel HEX
D:\wiggle_MCP\wiggle\wiggle.exe ucb read -a AF400000 -l 6000 -o ucb_backup.hex
```

---

## 13. 退出码 / 错误码

### 13.1 wiggle 退出码

| Code | 含义 | Code | 含义 |
|------|------|------|------|
| 0 | Success | 12 | Erase failed |
| 1 | Invalid args / addr OOR | 13 | EndInit restore failed |
| 2 | Server connection failed | 14 | Erase timeout |
| 3 | Get targets failed / none | 15 | Error flags (PVER/EVER/PROER/SQER/OPER) |
| 5 | Session start failed | 16 | Read mode failed |
| 6 | Device connect failed | 17 | Verification failed |
| 7 | Unsupported device (e.g. TC4x) | 18 | Read failed |
| 8 | Backup failed | 19 | Write failed |
| 9 | Safety Watchdog failed | 20 | File I/O failed |
| 10 | EndInit failed | 21 | Restore failed |
| 11 | Flash status failed | 22 | UCB locked |
| | | 23 | Unsupported device for UCB |

### 13.2 AURIXFlasher 输出标识

AURIXFlasher 不返回数字退出码，而是 stdout 末尾一行：

```
AURIXFlasher Exit Status: Pass    ← 成功
AURIXFlasher Exit Status: Fail    ← 失败，看上面 :: 行
```

---

## 14. 测试

```bash
# 全量 MCP 集成测试（12 例，~7 s）
# 需要：TAS Server 运行中 + AURIX 已连
cd D:\wiggle_MCP
python test_mcp_all.py
```

报告自动写到 `D:\wiggle_MCP\wiggle\mcp_test_report.txt`，每行一条：

```
1    PASS       462ms  wiggle_list - List targets
2    PASS       321ms  wiggle_status - Flash status register
...
12   PASS      1859ms  wiggle_pflash - PFlash program + verify
```

**当前基线**（2026-06-12 13:56:31）：**12 PASS / 0 FAIL**。

---

## 15. 常见问题

### Q: AI 客户端连不上 MCP server

A: 三步排查：
1. `python aurix_mcp_server.py --config ...` 在终端能跑起来吗？跑不起来说明路径或 Python 依赖有问题
2. 客户端的 `args` 路径和 `cwd` 是否正确？Windows 路径用 `D:/...` 正斜杠或 `"D:\\\\..."` 双反斜杠
3. 客户端是否需要重启？Claude Desktop / Cursor 改了 MCP 配置必须重启

### Q: `wiggle_exe not found` / `aurix_flasher_exe not found`

A: 两个原因：
- `mcp_config.json` 的路径写错（用 `\\` 转义或 `/` 不用 `\`）
- 用了 `null` 但目录布局不是 `D:\wiggle_MCP\` 默认布局 —— 把路径写死

### Q: 工具调用超时

A: `mcp_config.json` 调大 `timeout`。`wiggle_pflash` 固定 300 s。如果 erase/write 真实需要更久，看是不是设备 busy。

### Q: TC4x 设备报 "unsupported"

A: **本包不支持 TC4x**（AURIX 2G）。DFlash base 地址（`0xC...` 而不是 `0xAF...`）和命令序列都不同。

### Q: 烧完 PFlash 后设备不启动

A: `wiggle_pflash` 走 AURIXFlasher 默认行为（最后复位启动）。如要烧完保持 halt，自己跑：

```bash
D:\wiggle_MCP\AURIXFlasher\AURIXFlasher.exe -hex fw.hex -start off
```

### Q: UCB 写入报错 "UCB locked"

A: Security / Security COPY 区（`AF400800-...` 和 `AF401800-...`）是 OTP，**任何工具都擦不掉**。这是 Infineon 设计的安全机制，不是 bug。

### Q: 怎么更新 SVD 寄存器定义？

A: 8 个 `wiggle/RegisterDefs/<chip>-full.json` 是从 `data/SVD/` 用 `svd-to-full-json` skill 自动生成的。

```bash
# 把新 SVD 放到 D:\workspace\...\data\SVD\<chip>\<ver>\device.svd
# 然后重新跑 skill
python ~/.mavis/agents/mavis/skills/svd-to-full-json/scripts/svd-to-full-json.py \
    D:\workspace\...\data\SVD\<chip>\<ver>\device.svd \
    D:\wiggle_MCP\wiggle\RegisterDefs\<chip>-full.json \
    --device <chip> --svd-name <chip>
```

---

## 16. 配套手册 (Excel)

本 README 是入口，完整指令参考看 3 个 Excel（每个含符号约定 + 完整命令表 + 示例）：

| Excel | 路径 | 内容 |
|-------|------|------|
| **MCP 工具指令** | `D:\wiggle_MCP\docs\MCP-Command-Reference.xlsx` | 19 个 MCP 工具的完整签名 + 调用示例 |
| **AURIXFlasher CLI** | `D:\wiggle_MCP\AURIXFlasher\AURIXFlasher-Command-Reference.xlsx` | 12 个 flag + 11 典型场景 + 9 个 script 子命令 |
| **wiggle CLI** | `D:\wiggle_MCP\wiggle\wiggle-Command-Reference.xlsx` | 17 个子命令 + 8 全局选项 + UCB 表 + 24 退出码 |

每个 Excel 的 **Sheet 1 = 符号约定**（解释 `< > [ ] ( ) { } |` 在文档中的含义），**Sheet 2 = 指令表**（带可复制的命令示例）。

---

## 17. 文档版本

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| **v2.5** | 2026-06-12 | 新增 `reload_parsers` 工具（19 个工具）：`build_project` 成功后自动刷新 parser 缓存；`reload_parsers` 供外部编译后手动刷新；`build_project(clean=True)` 增强：区分 make/cmake/batch/ps1 的 clean 语义 |
| **v2.4** | 2026-06-12 | `find_exe()` 新增 4 个环境变量入口（`AURIX_FLASHER_EXE` / `AURIX_FLASHER_DIR` / `WIGGLE_EXE` / `WIGGLE_DIR`），优先级最高；方便 CI / 容器化部署。删除上一版 find_exe() 残留的死代码。README §5.4 改写成 11 级优先级表 + Linux/WSL env var 示例 |
| **v2.3** | 2026-06-12 | `find_exe()` 增强：自动搜 `C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`（glob 匹配任何版本）和 PATH 兜底（scoop/choco）。实测：当前机器 AURIXFlasher 自动命中 Infineon 安装路径。README §5.4 / §4 / §1 加 Python 3.10+ 硬性要求提示 + 找不到 exe 时的兜底配置指引 |
| **v2.2** | 2026-06-12 | 新增 5 个构建产物分析工具（build_project / parse_symbols / parse_elf / lookup_symbol / lookup_address），支持 TASKING MAP + GCC LST + ELF + MDF 解析；新增 requirements.txt / start_mcp.bat / start_mcp.sh；配置文件新增 build/project 段 |
| **v2.1** | 2026-06-12 | 新增 §2.5 工具来源说明（wiggle = 自研 Apache 2.0；AURIXFlasher = Infineon 闭源商业产品），明确两者的代码位置 / 构建方式 / 更新渠道 / 协议差异 |
| v2.0 | 2026-06-12 | 大幅重写：加 Quick Start / 架构图 / 完整工作流 / UCB 表 / 退出码 / FAQ / 配套 Excel 引用 / 文档版本表；修正自动搜索路径（5→4 条）；修正 `wiggle_pflash` 详细参数（7 个 → 实际 1 个）；修正 Cursor 配置键名 |
| v1.0 | 2026-05 | 初版 |