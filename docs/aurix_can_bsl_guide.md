# AURIX TC3xx CAN Bootstrap Loader (BSL) 开发指南

> 目标器件：Infineon AURIX TC334 (TC33x 系列)  
> 开发板：KIT-A2G-TC334-LITE（板载 miniWiggler）  
> 上位机：Python + python-can + PCAN-USB  
> 文档版本：1.0

---

## 目录

1. [BSL 概述](#1-bsl-概述)
2. [TC334 Lite Kit 硬件基础](#2-tc334-lite-kit-硬件基础)
3. [启用 BSL 的方法](#3-启用-bsl-的方法)
4. [HWCFG 与 BMHD 的关系](#4-hwcfg-与-bmhd-的关系)
5. [CAN BSL vs UART BSL](#5-can-bsl-vs-uart-bsl)
6. [CAN BSL 上位机开发计划](#6-can-bsl-上位机开发计划)
7. [bsl_protocol.py 使用说明](#7-bsl_protocolpy-使用说明)
8. [参考资料](#8-参考资料)

---

## 1. BSL 概述

**BSL（Bootstrap Loader）** 是 AURIX TC3xx 内置在 Boot ROM 中的固化程序，无需外部调试器（DAP/JTAG）即可对 Flash 进行编程操作。

### 主要用途

| 场景 | 说明 |
|------|------|
| 量产烧录 | 不依赖昂贵的调试探针，适合生产线 |
| 器件救砖 | DAP 接口被锁死或 Flash 损坏时的恢复手段 |
| 无调试器环境 | 仅有 CAN/UART 接口时的烧录方案 |
| 自动化测试 | 脚本化控制烧录/擦除流程 |

### BSL 支持的通信接口

TC3xx 的 **Generic Bootstrap Loader** 支持双接口自动竞争检测：

```
上电进入 BSL 后，SSW 同时监听：
  ├── ASCLIN0（UART/ASC）── 先收到合法帧 → 走 ASC BSL
  └── MultiCAN（CAN）─── 先收到合法帧 → 走 CAN BSL
```

哪个接口先收到握手帧，就使用哪个接口——**无需额外配置**。

---

## 2. TC334 Lite Kit 硬件基础

### 板载调试接口

TC334 Lite Kit（KIT-A2G-TC334-LITE）通过**一根 Micro-AB USB 线**同时提供两路通信：

```
[PC USB 口]
     │  Micro-AB USB
[板载 miniWiggler JDS 芯片]
     ├── 通道 1: DAP 调试（TAS Server → ADS / 工具）
     └── 通道 2: Virtual COM Port（UART，115200 bps 常用）
```

### CAN 接口

| 资源 | 状态 |
|------|------|
| 板载 CAN 收发器（TLE9251V 或同类） | ✅ 已集成 |
| CAN 连接器（DB9 / 螺丝端子） | ✅ 已引出 |
| MCU MultiCAN 模块 | ✅ TC334 原生支持 |

> **注意**：USB 接口**不承载 CAN 流量**，CAN BSL 需要额外的 USB-to-CAN 适配器。

### 推荐 USB-to-CAN 设备

| 设备 | 备注 |
|------|------|
| **PEAK PCAN-USB** | 最常见，python-can 原生支持，MemTool 官方支持 |
| Vector VN1600 / CANcase | 专业级 |
| Kvaser Leaf Light | python-can 支持 |

---

## 3. 启用 BSL 的方法

TC3xx 提供三种进入 BSL 的方式：

### 方法 A：修改 HWCFG 引脚（硬件方式）

通过 HWCFG 引脚的电平组合，在上电时直接选择 BSL 启动模式：

| HWCFG[5] | HWCFG[4] | HWCFG[3] | 启动模式 |
|----------|----------|----------|---------|
| 1 | x | x | 禁用引脚模式选择（使用 BMHD） |
| 0 | 1 | 1 | Internal Start from Flash（正常启动） |
| 0 | 1 | 0 | Alternate Boot Mode（ABM） |
| **0** | **0** | **1** | **Generic BSL（ASC / CAN）** |
| **0** | **0** | **0** | **Generic BSL（ASC / CAN）** |

**操作步骤：**

```
1. 断电
2. 将 HWCFG[5] 拉低至 GND（使能引脚模式选择）
3. 将 HWCFG[4]、HWCFG[3] 拉低至 GND（选择 Generic BSL）
4. 上电 → MCU 进入 BSL 模式，等待 CAN 或 UART 握手帧
```

> **Lite Kit 注意**：HWCFG 引脚无拨码开关，需飞线到 GND 或使用测试点。

### 方法 B：修改 BMHD（纯软件方式，零硬件改动）

通过 DAP（ADS / MemTool）修改 UCB 中的 BMHD，使 MCU 在下次启动时直接进入 BSL：

```
1. 通过 DAP 连接开发板（正常 USB → miniWiggler → TAS Server）
2. 用 MemTool / ADS 修改 UCB_BMHD：
     HWCFG_MSEL = 1（忽略引脚）
     BOOTMODE   = Generic BSL
3. 更新 CRCBMHD 和 CRCBMHD_N（否则 BMHD 失效！）
4. 重启 MCU → 直接进入 BSL
```

> ⚠️ **风险提示**：修改 BMHD 时必须同步更新 CRC，且建议先备份原始 UCB 内容。

### 方法 C：利用 BMHD 失效自动回退（紧急恢复）

当 TC3xx 的所有 BMHD 副本均无效（CRC 错误或已擦除）时，SSW 自动进入 Generic BSL：

```
触发条件：BMHD0_ORIG、BMHD0_COPY、BMHD1_ORIG … BMHD3_COPY 均无效
结果：    上电直接进入 Generic BSL，无需任何引脚配置
风险：    如果 UCB 操作中途断电，可能导致器件无法正常启动
```

---

## 4. HWCFG 与 BMHD 的关系

### SSW 启动决策流程

```
上电 → SSW 读取 BMHD
         │
         ▼
  BMHD 是否有效且 CONFIRMED？
  （CONFIRMATION = 0x43211234）
         │
    YES  │           NO（所有 BMHD 均无效）
         │                    │
         ▼                    ▼
  读取 BMHD.BMI           自动进入 Generic BSL ✓
  中的 HWCFG_MSEL 位
         │
  HWCFG_MSEL = 0？        HWCFG_MSEL = 1？
    （允许引脚覆盖）          （忽略引脚）
         │                    │
         ▼                    ▼
  由 HWCFG[5:3]          BMHD 内指定的
  引脚决定启动模式         启动模式生效
```

### 默认 BMHD 分析（以 TC364_UCB_only.hex 为例）

| 字段 | 值 | 含义 |
|------|-----|------|
| BMHDID | `0xB359003F` | 有效标识，内含 BMI |
| BMHDID[6] (HWCFG_MSEL) | **0** | HWCFG 引脚**已启用** |
| STAD | `0xA0000000` | 用户程序入口（PFlash0 起始） |
| CRCBMHD | `0xE50C941B` | CRC 校验 |
| CRCBMHD_N | `0x1AF36BE4` | CRC 反码（= ~CRCBMHD ✓） |
| CONFIRMATION | `0x43211234` | UCB 已确认 |

**结论**：使用默认 BMHD 时，**无需修改 BMHD**，只需改 HWCFG 引脚即可进入 BSL。

### BMHD UCB 地址（TC3xx）

| BMHD | 地址 |
|------|------|
| BMHD0_ORIG | 0xAF400000 |
| BMHD0_COPY | 0xAF400200 |
| BMHD1_ORIG | 0xAF400400 |
| BMHD1_COPY | 0xAF400600 |
| BMHD2_ORIG | 0xAF401000 |
| BMHD2_COPY | 0xAF401200 |
| BMHD3_ORIG | 0xAF401400 |
| BMHD3_COPY | 0xAF401600 |

---

## 5. CAN BSL vs UART BSL

| 对比项 | UART BSL（ASC） | CAN BSL |
|--------|----------------|---------|
| PC 端硬件 | 无需额外（板载 USB 虚拟串口） | 需要 USB-to-CAN 适配器（如 PCAN-USB） |
| 最高速率 | ~115.2 kbps | **最高 1 Mbps**（快约 8 倍） |
| 大文件烧录速度 | 慢 | **快** |
| 抗干扰性 | 一般（单端信号） | **强**（CAN 差分信号） |
| 典型场景 | 开发调试、小文件 | **量产烧录、ECU 生产线** |
| python-can 支持 | 通过 serial | **原生 PCAN 支持** |
| MemTool 支持 | ✅ | ✅ |

### 硬件连线

```
[PC]
  ├── Micro-AB USB ──→ 板载 miniWiggler（DAP 调试 / UART BSL）
  └── PCAN-USB
         ├── CAN_H ──→ TC334 Lite Kit CAN 连接器 CAN_H
         ├── CAN_L ──→ TC334 Lite Kit CAN 连接器 CAN_L
         └── GND   ──→ GND（必须共地！）
```

> **终端电阻**：CAN 总线两端各需一个 120Ω 电阻。Lite Kit 板上通常已有，检查跳线是否使能。

---

## 6. CAN BSL 上位机开发计划

### 项目目标

使用 Python + python-can + PCAN-USB 实现 TC334 CAN BSL 主机端工具，支持：
- 自动波特率同步
- Flash 擦除、烧录、校验
- Intel HEX 文件解析
- 命令行接口

### 五阶段计划

#### 阶段 1：环境搭建与 CAN 链路验证（1 天）

**安装依赖：**

```bash
pip install python-can intelhex
```

**验证 PCAN 连通性：**

```python
import can

bus = can.Bus(interface='pcan',
              channel='PCAN_USBBUS1',
              bitrate=500_000)
print("PCAN 连接成功，等待接收...")
for msg in bus:
    print(f"ID: 0x{msg.arbitration_id:03X}  Data: {msg.data.hex()}")
    break
bus.shutdown()
```

#### 阶段 2：进入 BSL 模式（0.5 天）

**推荐步骤（硬件引脚方式）：**

```
1. 断电
2. 飞线：HWCFG[5] → GND，HWCFG[4] → GND，HWCFG[3] → GND
3. 连接 PCAN-USB（CAN_H / CAN_L / GND）
4. 上电 → MCU 进入 Generic BSL，等待 CAN 握手
```

#### 阶段 3：CAN BSL 协议层实现（3 天）

核心协议参考 **TC3xx UM Part1 Section 3.1.3 "Bootstrap Loaders - CAN BSL"**。

**已知关键协议要点（来自 UM 片段）：**

| 阶段 | CAN ID | DLC | 数据格式 |
|------|--------|-----|---------|
| 波特率初始化帧 | 0x555 | 8 | `[0x55, 0x55, ACKID, 0x55×5]` |
| MCU ACK 帧 | ACKID | 0 | 空帧（DLC=0）|
| 命令帧 | 0x555 | 8 | `[CMD, ADDR_H, ADDR_M, ADDR_L, LEN_H, LEN_L, ...]` |
| 数据帧 | 0x555 | 8 | 连续 8 字节数据 |
| MCU 响应帧 | ACKID | 1-8 | `[STATUS, ...]` |

> ⚠️ 上述表格需对照 TC3xx UM §3.1.3 逐字段核对后再使用。

#### 阶段 4：CLI 工具实现（1.5 天）

**命令行接口设计：**

```bash
# 烧录完整 HEX（擦除 + 写入 + 校验）
python tools/can_bsl/bsl_protocol.py --hex app.hex --bitrate 500000

# 仅擦除 DFlash 全部扇区
python tools/can_bsl/bsl_protocol.py --erase --addr 0xAF000000 --sectors 32

# 查看设备信息（仅握手，不操作 Flash）
python tools/can_bsl/bsl_protocol.py --channel PCAN_USBBUS1

# 烧录后复位执行
python tools/can_bsl/bsl_protocol.py --hex app.hex --execute
```

#### 阶段 5：测试验证（1 天）

| 测试项 | 预期结果 |
|--------|---------|
| BSL 握手连接 | 返回 TC334 JTAG ID（0x1020B083 masked） |
| 擦除 DFlash 全部 | 32 扇区 × 4 KB = 128 KB，擦除为 0x00 |
| 写入 PFlash | 从 0xA0000000 起，按 32 字节页写入 |
| CRC-32 校验 | MCU 计算值与 HEX 文件值一致 |
| 执行跳转 | MCU 复位并运行用户程序 |

### 文件结构

```
tools/can_bsl/
  ├── bsl_protocol.py      # 核心协议实现（已创建）
  ├── README.md            # 使用说明
  └── test_loopback.py     # CAN 链路自测脚本（待创建）
```

---

## 7. bsl_protocol.py 使用说明

文件位置：`tools/can_bsl/bsl_protocol.py`

### 类结构

| 类/函数 | 职责 |
|---------|------|
| `_CanTransport` | python-can 封装层（send / recv / flush） |
| `TC3xxCanBsl` | BSL 协议主类，所有操作的入口 |
| `connect()` | Phase 1：发送 0x555 初始化帧，等待波特率 ACK |
| `erase(addr, num_sectors)` | 擦除 Flash 扇区 |
| `write(addr, data)` | 分块写入 Flash |
| `verify(addr, length, crc)` | CRC-32 校验 |
| `execute(addr)` | 跳转执行用户程序 |
| `flash_hex(path)` | 一键：解析 HEX → 擦除 → 写入 → 校验 |

### 基本用法

```python
from bsl_protocol import TC3xxCanBsl

with TC3xxCanBsl(
    interface='pcan',
    channel='PCAN_USBBUS1',
    bitrate=500_000,
) as bsl:
    # 连接 + 波特率同步
    info = bsl.connect()
    print(f"Connected: {info}")  # JTAG-ID=0x1020B083  Flash=2048 KB

    # 擦除 DFlash
    bsl.erase(addr=0xAF000000, num_sectors=32)

    # 烧录 HEX 文件
    bsl.flash_hex("firmware.hex", do_erase=True, do_verify=True)

    # 跳转执行
    bsl.execute(addr=0xA0000000)
```

### 需要验证的协议字段

在代码中搜索 `# UM §3.1.3`，共有以下几处需对照 UM 核实：

1. **初始化帧布局**（`connect()` 方法）
   - DB2 ACKID 编码方式（1 字节低位 vs 2 字节）
   - DB3-DB7 填充内容
   - MCU ACK 帧格式（DLC=0 空帧 vs 有数据）

2. **命令 Opcode**（`Cmd` 枚举）
   - GET_DEVICE_ID、ERASE_SECTOR、WRITE_DATA、VERIFY_DATA、EXECUTE 的实际值

3. **命令帧字节布局**（`_send_command()` 方法）
   - 地址/长度字段大小端
   - 各字段偏移位置

4. **响应帧布局**
   - 状态字节位置（DB0 or DB1）
   - ACK 值和 NACK 值

---

## 8. 参考资料

| 文档 | 内容 |
|------|------|
| **TC3xx UM Part1 §3.1.3** | CAN BSL 协议完整规范（命令表、帧格式）|
| **TC3xx UM Part1 §3.1.2** | ASC BSL 协议（结构类似，可参考对比）|
| **TC33x/TC32x DataSheet** | TC334 Flash 大小、PSPR、地址范围 |
| **KIT-A2G-TC334-LITE UM** | Lite Kit 硬件引脚图、HWCFG 测试点位置 |
| python-can 文档 | PCAN 接口配置参数 |
| IntelHex 库文档 | HEX 文件解析 API |

### 关键地址速查

| 资源 | 地址 | 大小 |
|------|------|------|
| PFlash0 | 0xA0000000 | 2 MB（TC334，单 Bank） |
| DFlash0 | 0xAF000000 | 128 KB（4 KB/扇区 × 32） |
| UCB_BMHD 区 | 0xAF400000 | 各 512 bytes |
| CPU0 PSPR | 0xC0000000 | 8 KB |

### Flash 操作粒度

| Flash 类型 | 最小编程单位（Page） | 最小擦除单位（Logical Sector） |
|-----------|-------------------|---------------------------|
| PFlash | 32 bytes | 16 KB |
| DFlash | 8 bytes | 4 KB |

> **重要**：AURIX Flash 擦除态为 `0x00`（非 0xFF），读取已擦除扇区可能产生 ECC 错误，属正常现象。
