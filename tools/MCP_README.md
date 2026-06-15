# AURIX MCP Server 配置与使用说明

## 概述

AURIX MCP Server 是一个基于 [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) 的服务器，将 `wiggle.exe`（DFlash 操作 + 调试）和 `AURIXFlasher.exe`（PFlash 烧写）封装为 AI 工具，使 LLM（如 Claude、GPT）能通过自然语言操作 Infineon AURIX 微控制器。

## 环境要求

- **Python**: 3.10+
- **依赖包**: `mcp[cli]`
- **wiggle.exe**: AURIX DFlash/调试工具
- **AURIXFlasher.exe**: Infineon PFlash 烧写工具（可选，仅 PFlash 烧写时需要）
- **TAS Server**: 必须运行中，提供调试器硬件连接

## 安装

```bash
pip install "mcp[cli]"
```

## 配置

### 配置文件 (mcp_config.json)

```json
{
    "wiggle_exe": "D:\\path\\to\\wiggle\\wiggle.exe",
    "aurix_flasher_exe": "C:\\Infineon\\AURIXFlasherSoftwareTool-3.0.16\\AURIXFlasher.exe",
    "server": "localhost",
    "target": null,
    "device": null,
    "timeout": 120
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `wiggle_exe` | string | wiggle.exe 完整路径（不设则自动搜索） |
| `aurix_flasher_exe` | string | AURIXFlasher.exe 完整路径（不设则自动搜索） |
| `server` | string | TAS Server IP 地址，默认 `localhost` |
| `target` | string\|null | 目标标识符，多目标时指定；null=自动选择 |
| `device` | string\|null | 设备类型覆盖（如 `TC33x`）；null=自动检测 |
| `timeout` | int | 命令超时秒数，默认 120 |

### 自动搜索路径

未配置 exe 路径时，MCP Server 按以下顺序搜索：

1. `tools/` 目录（脚本所在目录）
2. `../wiggle/` 目录（部署结构）
3. `../data/` 目录
4. `../AURIXFlasher/` 目录

## 启动

```bash
# 默认 stdio 传输 + 自动搜索路径
python aurix_mcp_server.py

# 指定配置文件
python aurix_mcp_server.py --config mcp_config.json

# 使用 SSE 传输（HTTP 模式）
python aurix_mcp_server.py --config mcp_config.json --transport sse
```

### 在 AI 客户端中配置

#### Claude Desktop (claude_desktop_config.json)

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": ["D:\\path\\to\\tools\\aurix_mcp_server.py", "--config", "D:\\path\\to\\tools\\mcp_config.json"]
    }
  }
}
```

#### Cursor / VS Code MCP 配置

```json
{
  "mcp": {
    "servers": {
      "aurix": {
        "command": "python",
        "args": ["D:\\path\\to\\tools\\aurix_mcp_server.py", "--config", "D:\\path\\to\\tools\\mcp_config.json"]
      }
    }
  }
}
```

## MCP 工具列表

### 设备信息

| 工具 | 说明 |
|------|------|
| `wiggle_list` | 列出已连接的 TAS 目标设备 |
| `wiggle_status` | Flash 状态寄存器（SVD 字段解码） |
| `wiggle_info` | 设备信息、内存布局、SVD 摘要 |

### DFlash 操作

| 工具 | 说明 |
|------|------|
| `wiggle_erase` | 擦除 DFlash 扇区（全部或指定地址） |
| `wiggle_write` | 从 HEX/BIN 文件写入 DFlash |
| `wiggle_read` | 读取 DFlash 内容 |

### PFlash 烧写

| 工具 | 说明 |
|------|------|
| `wiggle_pflash` | 调用 AURIXFlasher 烧写 PFlash（支持 HEX/ELF） |

### 调试

| 工具 | 说明 |
|------|------|
| `wiggle_reg` | 按名字或地址读写寄存器 |
| `wiggle_dump` | 内存 Hex Dump（PFlash/DFlash/SRAM） |
| `wiggle_poke` | 写入值到内存地址 |
| `wiggle_search` | 搜索内存中的字节模式 |
| `wiggle_compare` | 本地文件与 Flash 内容对比 |

### 控制

| 工具 | 说明 |
|------|------|
| `wiggle_reset` | 复位 MCU |

---

## 工具详细参数

### wiggle_list

列出已连接的目标设备。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `server` | string | `""` | TAS Server IP（覆盖配置） |
| `target` | string | `""` | 目标标识符 |

---

### wiggle_status

读取 Flash 状态寄存器，带 SVD 字段解码。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

---

### wiggle_info

获取设备信息、内存布局、SVD 寄存器摘要。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

---

### wiggle_erase

擦除 DFlash 扇区。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `erase_all` | bool | `true` | 擦除整个 DFlash |
| `address` | string | `""` | DFlash 起始地址（十六进制，erase_all=false 时必填） |
| `sectors` | int | `0` | 擦除扇区数（0=1个扇区） |
| `verify` | bool | `false` | 擦除后回读校验 |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

**示例**：
```
擦除全部 DFlash：wiggle_erase(erase_all=True, verify=True)
擦除指定扇区：wiggle_erase(erase_all=False, address="0xAF000000", sectors=2)
```

---

### wiggle_write

从 HEX/BIN 文件写入 DFlash。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `file_path` | string | (必填) | HEX 或 BIN 文件路径 |
| `verify` | bool | `true` | 写入后回读校验 |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

> 注意：wiggle_write 仅支持 DFlash，PFlash 请使用 wiggle_pflash。

---

### wiggle_read

读取 DFlash 内容。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `address` | string | (必填) | 起始地址（十六进制） |
| `length` | string | (必填) | 读取长度（十六进制） |
| `output_file` | string | `""` | 保存路径（.hex 或 .bin），空=返回数据 |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

---

### wiggle_pflash

调用 AURIXFlasher.exe 烧写 PFlash。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `hex_file` | string | (必填) | Intel HEX 或 ELF 文件路径 |
| `verify` | bool | `true` | 烧写后校验 |
| `erase` | string | `""` | 擦除模式：`"all"`=全片擦除, `"used"`=仅已用扇区, 空=默认 |
| `ucb` | bool | `false` | 启用 UCB 编程 |
| `connect` | string | `""` | 连接模式：`"0"`=热连接(不复位), `"1"`=复位并暂停 |
| `start` | string | `""` | 烧写后行为：`"on"`=复位运行, `"off"`=保持暂停, 空=默认(复位启动) |
| `device_id` | string | `""` | 多目标时的 DAP/设备 ID（如 `"0"`, `"1"`） |

**示例**：
```
基本烧写：wiggle_pflash(hex_file="firmware.hex")
烧写后不启动：wiggle_pflash(hex_file="firmware.hex", start="off")
全擦除+烧写+热连接：wiggle_pflash(hex_file="app.elf", erase="all", connect="0")
```

---

### wiggle_reg

按 SVD 名字或地址读写寄存器。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `name_or_addr` | string | (必填*) | 寄存器名(如 `DMU.HF.STATUS`) 或地址(如 `0xF8040010`) |
| `value` | string | `""` | 写入值（空=读取） |
| `list_peripheral` | string | `""` | 列出外设寄存器（设置时忽略 name_or_addr） |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

**示例**：
```
读取寄存器：wiggle_reg("DMU.HF.STATUS")
写入寄存器：wiggle_reg("DMU.HF.STATUS", value="0x00000001")
按地址读取：wiggle_reg("0xF8040010")
列出DMU外设：wiggle_reg("", list_peripheral="DMU")
```

---

### wiggle_dump

读取内存并返回 Hex Dump（支持 PFlash/DFlash/SRAM）。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `address` | string | (必填) | 起始地址（十六进制） |
| `length` | string | (必填) | 读取长度（十六进制） |
| `output_file` | string | `""` | 保存路径（.bin 或 .hex） |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

---

### wiggle_poke

写入值到内存地址并回读验证。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `address` | string | (必填) | 目标地址（十六进制） |
| `value` | string | (必填) | 写入值（十六进制） |
| `width` | int | `32` | 位宽：8, 16, 32, 64 |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

---

### wiggle_search

搜索内存中的字节模式。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `address` | string | (必填) | 搜索起始地址 |
| `length` | string | (必填) | 搜索范围长度 |
| `pattern` | string | (必填) | 十六进制字节模式（如 `DEADBEEF`） |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

---

### wiggle_compare

比较本地文件与 Flash/内存内容。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `file_path` | string | (必填) | 本地文件路径（.hex 或 .bin） |
| `address` | string | (必填) | 比较的起始地址 |
| `length` | string | `""` | 比较长度（空=文件全长） |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |
| `device` | string | `""` | 设备类型覆盖 |

---

### wiggle_reset

复位 MCU。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `halt` | bool | `false` | 复位后暂停（true=暂停, false=运行） |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 目标标识符 |

---

## 返回格式

所有工具返回 JSON 字符串，统一格式：

**成功**：
```json
{
  "status": "ok",
  "command": "...",
  ...
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

常见错误码：
| code | 含义 |
|------|------|
| -1 | 可执行文件未找到 |
| -2 | 命令超时 |
| -3 | 运行时异常 |
| >0 | 程序退出码（非0） |

## 测试

```bash
# 运行全量 MCP 测试（需要 TAS Server + 设备连接）
python test_mcp_all.py

# 输出测试报告到文件
# 报告自动保存到 ../wiggle/mcp_test_report.txt
```

## 典型工作流

### 1. 查看设备状态
```
AI → wiggle_list() → 获取连接的目标
AI → wiggle_info() → 获取设备型号、内存布局
AI → wiggle_status() → 检查 Flash 状态
```

### 2. DFlash 擦除 + 写入
```
AI → wiggle_erase(erase_all=True, verify=True)
AI → wiggle_write(file_path="data.hex", verify=True)
AI → wiggle_compare(file_path="data.hex", address="0xAF000000")
```

### 3. PFlash 烧写固件
```
AI → wiggle_pflash(hex_file="firmware.hex", verify=True, start="off")
AI → wiggle_reset(halt=False)  # 手动启动
```

### 4. 调试寄存器
```
AI → wiggle_reg("", list_peripheral="DMU")  # 列出DMU寄存器
AI → wiggle_reg("DMU.HF.STATUS")            # 读取状态
AI → wiggle_reg("DMU.HF.CONTROL", value="0x1")  # 写入控制寄存器
```

### 5. 内存检查
```
AI → wiggle_dump(address="0xAF000000", length="0x100")
AI → wiggle_search(address="0xAF000000", length="0x10000", pattern="DEADBEEF")
```
