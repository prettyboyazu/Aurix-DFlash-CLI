# DFlash Tool 使用手册

AURIX MCU DFlash 操作工具 — 支持擦除、读取、设备列表、复位。

---

## 快速开始

```
dflash list                    # 查看连接的设备
dflash erase --info            # 查看 DFlash 信息（不执行擦除）
dflash erase --all --verify    # 擦除全部 DFlash 并验证
dflash reset                   # 复位 MCU
```

---

## 环境要求

| 条件 | 说明 |
|------|------|
| 操作系统 | Windows 10 或更高版本 |
| TAS Server | 必须运行（工具通过 TCP 连接 localhost:24817） |
| 硬件 | miniWiggler 连接 AURIX MCU |
| 文件 | `dflash.exe` + `DeviceConfigs/` 目录放在同一路径下 |

> 无需安装 VC++ Redistributable，exe 为静态链接，零额外依赖。

---

## 命令总览

```
dflash <子命令> [选项]
```

| 子命令 | 功能 |
|--------|------|
| `erase` | 擦除 DFlash 扇区 |
| `read` | 读取 DFlash 内容 |
| `list` | 列出已连接设备 |
| `reset` | 复位 MCU |

---

## erase — 擦除 DFlash

### 语法

```
dflash erase [选项]
```

### 选项

| 选项 | 说明 |
|------|------|
| `--all` | 擦除全部 DFlash |
| `--addr <hex>` | 起始地址（如 `0xAF000000`），自动对齐到扇区边界 |
| `--sectors <n>` | 擦除扇区数（十进制） |
| `--info` | 仅显示设备和 DFlash 信息，不执行擦除 |
| `--verify` | 擦除后回读验证（所有字节应为 0x00） |
| `--backup <file>` | 擦除前备份 DFlash 内容到文件 |
| `--reset` | 擦除完成后复位 MCU（恢复运行） |
| `--no-reset` | 热连接，不复位设备 |
| `--server <ip>` | TAS Server 地址（默认 localhost） |
| `--target <id>` | 指定目标设备 |
| `--config-dir <path>` | 指定 DeviceConfigs 目录路径 |

### 示例

```bash
# 查看设备 DFlash 信息
dflash erase --info

# 擦除第一个扇区并验证
dflash erase --addr 0xAF000000 --sectors 1 --verify

# 擦除全部 DFlash + 备份 + 验证 + 复位
dflash erase --all --backup backup.bin --verify --reset

# 擦除全部（连接远程 TAS Server）
dflash erase --all --server 192.168.1.100
```

---

## read — 读取 DFlash

### 语法

```
dflash read --addr <hex> --length <hex> [选项]
```

### 选项

| 选项 | 说明 |
|------|------|
| `--addr <hex>` / `-a` | 起始地址（必填） |
| `--length <hex>` / `-l` | 读取字节数（必填） |
| `--output <file>` / `-o` | 输出文件（`.bin`=二进制，`.hex`=Intel HEX），不指定则终端输出 hex dump |
| `--server <ip>` | TAS Server 地址 |
| `--target <id>` | 指定目标设备 |

### 示例

```bash
# 终端显示 hex dump（4KB）
dflash read --addr 0xAF000000 --length 0x1000

# 保存为二进制文件
dflash read -a 0xAF000000 -l 0x20000 -o dump.bin

# 保存为 Intel HEX 格式
dflash read -a 0xAF000000 -l 0x20000 -o dump.hex
```

---

## list — 列出设备

### 语法

```
dflash list [--server <ip>]
```

### 示例

```bash
dflash list
```

输出：
```
TAS DFlash Tool - Device List
=============================
Connecting to TAS server at localhost...
  Server: TasServer V2.0 (Aug  7 2025)
  Targets (1):
  [0] TC37x         Application Kit TC367 V2.0
```

---

## reset — 复位 MCU

### 语法

```
dflash reset [--halt] [--server <ip>] [--target <id>]
```

### 选项

| 选项 | 说明 |
|------|------|
| `--halt` | 复位后暂停 CPU（调试模式） |
| `--server <ip>` | TAS Server 地址 |
| `--target <id>` | 指定目标设备 |

### 示例

```bash
# 复位并恢复运行
dflash reset

# 复位并暂停（用于调试或 Flash 操作前准备）
dflash reset --halt
```

---

## 支持的设备

| 设备系列 | 扇区大小 | DFlash 总容量 |
|----------|----------|---------------|
| TC21x, TC22x, TC23x, TC26x | 8 KB | 128 KB |
| TC27x | 8 KB | 384 KB |
| TC29x | 8 KB | 512 KB |
| TC33x, TC35x, TC36x | 4 KB | 128 KB |
| TC37x, TC3Ex | 4 KB | 256 KB |
| TC38x | 4 KB | 512 KB |
| TC39x | 4 KB | 1 MB |

- DFlash 基地址：`0xAF000000`
- 擦除后状态：`0x00`（全零）
- **不支持 TC4x**（地址/命令序列不同）

---

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| 1 | 参数错误 |
| 2 | 服务器连接失败 |
| 3 | 获取目标失败 |
| 4 | 无可用目标 |
| 5 | 会话启动失败 |
| 6 | 设备连接失败 |
| 7 | 不支持的设备 |
| 10 | Flash 操作失败 |
| 17 | 验证失败 |
| 18 | 读取失败 |

---

## 常见问题

**Q: 提示 "Cannot connect to TAS server"**
A: 确保 TAS Server 已启动。检查任务管理器中是否有 `TasServer.exe` 进程。

**Q: 提示 "No targets available"**
A: 检查 miniWiggler USB 连接是否正常，目标板是否上电。

**Q: 提示 "TC4x devices are not supported"**
A: 本工具仅支持 TC2x/TC3x 系列。TC4x 需使用其他工具。

**Q: 擦除后读取报 ECC 错误**
A: 正常现象。擦除后 ECC 校验值失效，但数据读取仍然正确。工具已自动处理此情况。

**Q: 如何添加新设备支持？**
A: 将设备 JSON 文件放入 `DeviceConfigs/` 目录即可，无需重新编译。

---

## 文件结构

```
Erase/
├── dflash.exe           # 主程序（静态链接，无 DLL 依赖）
├── DeviceConfigs/       # 设备配置文件
│   ├── TC23x_A_step.json
│   ├── TC37x_A_step.json
│   └── ...
└── README.md            # 本文档
```
