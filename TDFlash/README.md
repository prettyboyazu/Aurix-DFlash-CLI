# DFlash Tool v2.0 用户手册

## 简介
DFlash 是基于 TAS Client API 的 AURIX MCU Data Flash (DFlash) 操作命令行工具，支持 TC2xx/TC3xx 系列芯片。

## 运行环境要求
- Windows 10/11 x64
- TAS Server 已启动并连接 miniWiggler 调试器
- 目标芯片通过 miniWiggler 连接
- DeviceConfigs/ 目录与 dflash.exe 同级放置

## 文件结构
```
TDFlash/
├── dflash.exe          # 主程序（静态链接，无 DLL 依赖）
├── DeviceConfigs/      # 设备配置 JSON 文件
├── README.md           # 本文档
└── test_report_v2.0.txt # 测试报告
```

## 子命令一览

| 子命令 | 功能 | 示例 |
|--------|------|------|
| list | 列出连接的设备 | `dflash list` |
| read | 读取 DFlash 数据 | `dflash read --addr AF004000 --length 100` |
| erase | 擦除 DFlash sector | `dflash erase --addr AF004000 --sectors 1` |
| write | 从文件写入 DFlash | `dflash write --file data.hex` |
| rewrite | 任意地址写入（R-M-W） | `dflash rewrite --addr AF004010 --data DEADBEEF --verify` |
| restore | 从备份文件还原 | `dflash restore backup.hex` |
| reset | 复位 MCU | `dflash reset` |
| ucb read | 读取 UCB 区域 | `dflash ucb read --addr AF400000 --length 200` |
| ucb write | 写入 UCB（高风险） | `dflash ucb write --file ucb_data.hex` |
| ucb erase | 擦除 UCB（高风险） | `dflash ucb erase --addr AF400000` |

## 全局参数

| 参数 | 简写 | 说明 |
|------|------|------|
| --help | -h | 显示帮助信息 |
| --version | -v | 显示版本号 |
| --server | -s | TAS Server 地址（默认 localhost） |
| --port | -p | TAS Server 端口（默认 2000） |
| --device | -d | 设备标识符 |
| --config-dir | | DeviceConfigs 目录路径 |

## 对齐约束

| 操作 | 对齐要求 |
|------|----------|
| read | 无对齐要求 |
| write | 8 字节（page）对齐 |
| erase | 0x2000（sector）自动对齐 |
| rewrite | 无要求（内部自动处理） |

## 重要注意事项

1. **AURIX DFlash 擦除态为 0x00**（非传统 0xFF）
2. **UCB 操作高风险**：写错误数据可能永久锁死芯片，工具会要求输入 "yes" 确认
3. **地址和长度参数均为十六进制**，无需 0x 前缀
4. **rewrite 使用 Read-Modify-Write 模式**：自动读取整个 sector → 合并新数据 → 擦除 → 写回，确保相邻数据不被破坏
5. **--backup** 参数可选路径：`--backup` 自动生成文件名，`--backup path.hex` 指定路径
6. **--verify** 参数：写入后自动读回比对，确保数据正确

## 快速开始

```bash
# 1. 启动 TAS Server（确保 miniWiggler 已连接）
start_tas_server.bat

# 2. 列出设备
dflash list

# 3. 读取 DFlash 数据
dflash read --addr AF004000 --length 100

# 4. 擦除一个 sector
dflash erase --addr AF004000 --sectors 1

# 5. 写入数据（带验证）
dflash rewrite --addr AF004000 --data 1122334455667788 --verify

# 6. 带备份的写入
dflash rewrite --addr AF004000 --data AABBCCDD --backup --verify

# 7. 复位芯片
dflash reset
```

## 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 2.0 | 2026-05 | 新增 rewrite/ucb 子命令、--version、安全确认机制、C++17 |
| 1.0 | 2026-04 | 初始版本，支持 erase/read/write/restore/list/reset |
