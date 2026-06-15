# Wiggle - AURIX MCU Flash & Register Tool

Wiggle 是基于 TAS Client API 的 AURIX 微控制器调试工具，支持 DFlash 读写/擦除、寄存器操作、内存调试等功能。

## 支持的芯片

- **TC2x 系列**: TC21x, TC22x, TC23x, TC26x, TC27x, TC29x
- **TC3x 系列**: TC33x, TC35x, TC36x, TC37x, TC38x, TC39x, TC3Ex

> 不支持 TC4x 系列。

## 环境要求

- **操作系统**: Windows 10/11 (x64)
- **运行时**: Visual C++ Redistributable 2015-2022（Win10/11 通常已内置）
- **TAS Server**: 需要在本地或远程运行 TAS Server（连接调试器硬件）
- **Python 3.x**（仅 MCP Server 集成时需要）

## 目录结构

```
wiggle/
├── wiggle.exe              # 主程序
├── DeviceConfigs/          # 设备配置文件（必须）
│   ├── devices.json        # 设备索引
│   ├── TC33x_A_step.json   # TC33x 配置
│   └── ...                 # 其他芯片配置
├── RegisterDefs/           # 寄存器定义文件（reg 命令需要）
│   ├── TC33x-full.json     # TC33x 全量寄存器
│   └── ...                 # 其他芯片寄存器
└── README.md               # 本文档
```

## 快速开始

```bash
# 查看帮助
wiggle.exe --help

# 列出连接的目标设备
wiggle.exe list

# 查看设备状态
wiggle.exe status --server localhost

# 查看设备信息
wiggle.exe info
```

## 命令一览

### Flash 操作

| 命令 | 说明 |
|------|------|
| `erase` | 擦除 DFlash 扇区 |
| `write` | 从 HEX/BIN 文件写入 DFlash |
| `rewrite` | 任意地址写入 DFlash（Read-Modify-Write） |
| `restore` | 从备份恢复 DFlash（擦除+写入+校验） |
| `read` | 读取 DFlash 内容并保存 |

### 调试命令

| 命令 | 说明 |
|------|------|
| `reg` | 按名字或地址读写寄存器（SVD 驱动） |
| `dump` | 内存 Hex Dump（PFlash/DFlash/SRAM） |
| `poke` | 写入值到内存地址 |
| `status` | Flash 状态寄存器（带字段解码） |
| `info` | 设备信息、内存布局、SVD 摘要 |
| `compare` | 本地文件与 Flash 内容对比 |
| `search` | 搜索内存中的字节模式 |
| `shell` | 交互式 REPL 模式 |

### 其他

| 命令 | 说明 |
|------|------|
| `list` | 列出已连接的 TAS 目标 |
| `reset` | 复位 MCU |
| `ucb` | UCB 读写操作（仅 TC3XX） |

## 使用示例

### 擦除 DFlash

```bash
# 擦除 1 个扇区
wiggle.exe erase --addr AF000000 --sectors 1

# 擦除全部 DFlash 并备份
wiggle.exe erase --all --backup backup.hex --verify

# 查看设备信息（不执行擦除）
wiggle.exe erase --info
```

### 写入 DFlash

```bash
# 从 HEX 文件写入
wiggle.exe write -f data.hex --verify

# 从 BIN 文件写入指定地址
wiggle.exe write -f data.bin --addr 0xAF000000 --verify
```

### 读取 DFlash

```bash
# 读取并保存为 HEX 文件
wiggle.exe read 0xAF000000 0x1000 -o output.hex

# 读取并保存为 BIN 文件
wiggle.exe read 0xAF000000 0x1000 -o output.bin
```

### 寄存器操作

```bash
# 按名字读取寄存器
wiggle.exe reg HF.STATUS

# 按地址读取寄存器
wiggle.exe reg 0xF8050010

# 写入寄存器
wiggle.exe reg HF.CONTROL 0x00000001

# 列出外设的所有寄存器
wiggle.exe reg --list DMU
```

### 内存调试

```bash
# Hex Dump
wiggle.exe dump 0xAF000000 0x40

# 写入内存
wiggle.exe poke 0xAF000000 0xDEADBEEF

# 搜索内存
wiggle.exe search 0xAF000000 0xAF001000 0xDEADBEEF
```

## 全局选项

| 选项 | 说明 |
|------|------|
| `--json` | 输出结构化 JSON（用于 MCP/AI 集成） |
| `--server <ip>` | TAS Server IP 地址（默认: localhost） |
| `--target <id>` | 目标标识符（多目标时选择） |
| `--device <name>` | 手动指定设备类型（跳过自动检测） |
| `--config-dir <path>` | DeviceConfigs 目录路径 |

## JSON 输出模式

添加 `--json` 参数可获得结构化 JSON 输出，便于脚本和 AI 工具解析：

```bash
wiggle.exe status --json
```

输出格式：
```json
{
  "status": "ok",
  "command": "status",
  "register": "HF.STATUS",
  "address": "0xF8040010",
  "value": "0x02080000",
  "fields": { ... }
}
```

## MCP Server 集成

Wiggle 提供 Python MCP Server（`aurix_mcp_server.py`），可集成到 AI 工具中通过自然语言操作 AURIX 芯片。

### 配置

编辑 `mcp_config.json`：
```json
{
  "wiggle_exe": "/path/to/wiggle.exe",
  "aurix_flasher_exe": "/path/to/AURIXFlasher.exe",
  "server": "localhost",
  "target": null,
  "device": null,
  "timeout": 120
}
```

### 启动 MCP Server

```bash
python aurix_mcp_server.py
```

### MCP 工具列表

| MCP Tool | 对应命令 |
|----------|----------|
| `wiggle_list` | `wiggle list --json` |
| `wiggle_status` | `wiggle status --json` |
| `wiggle_info` | `wiggle info --json` |
| `wiggle_reg` | `wiggle reg <name> --json` |
| `wiggle_reg_list` | `wiggle reg --list <peripheral> --json` |
| `wiggle_dump` | `wiggle dump <addr> <len> --json` |
| `wiggle_read` | `wiggle read <addr> <len> --json` |
| `wiggle_search` | `wiggle search ... --json` |
| `wiggle_poke` | `wiggle poke <addr> <val> --json` |
| `wiggle_erase` | `wiggle erase ... --json` |
| `wiggle_reset` | `wiggle reset --json` |
| `wiggle_pflash` | AURIXFlasher PFlash 烧写 |

### wiggle_pflash 参数说明

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `hex_file` | string | (必填) | Intel HEX 或 ELF 文件路径 |
| `verify` | bool | `true` | 烧写后校验 |
| `erase` | string | `""` | 擦除模式：`"all"`=全片擦除, `"used"`=仅擦除已用扇区, 空=AURIXFlasher 默认 |
| `ucb` | bool | `false` | 启用 UCB 编程 |
| `connect` | string | `""` | 连接模式：`"0"`=热连接(不复位), `"1"`=复位并暂停 |
| `start` | string | `""` | 烧写后行为：`"on"`=复位运行, `"off"`=保持暂停 |
| `device_id` | string | `""` | 多目标时的 DAP/设备 ID（如 `"0"`, `"1"`） |

## 设备配置

Wiggle 在运行时自动从 `DeviceConfigs/` 目录加载设备信息。搜索路径：

1. `--config-dir` 参数指定的路径
2. 环境变量 `TAS_DEVICE_CONFIGS`
3. wiggle.exe 同级目录下的 `DeviceConfigs/`

每个设备 JSON 包含：Flash 基地址、扇区大小、页大小、JTAG ID、UCB 配置等信息。

## 寄存器定义

`RegisterDefs/` 目录包含各芯片的完整寄存器定义（从 SVD 转换而来），供 `reg` 命令按名字查找寄存器地址。

- 如果 RegisterDefs 文件不存在或加载失败，`reg` 命令仍可通过地址直接读写
- RegisterDefs 文件较大（单文件 19-65 MB），如不需要 `reg` 命令可不包含

## 许可证

Apache License 2.0
