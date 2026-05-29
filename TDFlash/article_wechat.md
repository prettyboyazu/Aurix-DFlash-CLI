# 一条命令搞定 AURIX Data Flash 读写——DFlash Tool 介绍

做 AURIX MCU 开发的同学大概都有过这样的经历：

想擦一块 DFlash，得打开 TAS Client，连上 miniWiggler，点好几层菜单。只是想改几个字节，却要在 IDE 里折腾半天。读个 DFlash 数据做分析，还得先写个小脚本。

**DFlash Tool** 就是来解决这些问题的。

---

## 为什么要开发这个工具

在做 AURIX MCU 的开发和调试时，经常需要操作 Data Flash —— 擦除旧数据、写入新数据、读取内容做分析。

但你会发现，**目前市面上没有一个好用的 DFlash 命令行工具。**

Infineon 提供的 Memtool 是图形界面的，功能强大但操作流程重。想擦一个扇区，要点好几层菜单。在产线上做批量操作或者集成到自动化脚本里，基本不可能。

TAS Client API 本身提供了底层读写接口，但它只是一个 C++ 库。要操作 DFlash，你得自己写程序处理 flash 命令序列、忙等待、错误标志检查、扇区对齐这些底层细节。门槛不低，而且容易出错。

其他的第三方工具？要么不支持 DFlash，要么只覆盖个别芯片型号，要么干脆找不到。

**所以我们开发了这个工具。**

目标是把 DFlash 操作变成一条命令的事情。不需要写代码，不需要打开 IDE，不需要研究 flash 寄存器。终端里敲一行命令，擦除、写入、读取、校验，全部搞定。

---

## 它是什么

DFlash Tool 是一个命令行工具，通过 miniWiggler 调试器连接 AURIX 芯片，直接对 Data Flash 进行读写和擦除操作。

基于 Infineon 官方的 TAS Client API 开发，支持 TC2xx 和 TC3xx 全系列芯片。包括 TC21x、TC22x、TC23x、TC26x、TC27x、TC29x、TC33x、TC35x、TC36x、TC37x、TC38x、TC39x、TC3Ex，覆盖了当前主流的 AURIX 型号。

不依赖任何 IDE 或大型工具链。一个 exe 文件，一个配置目录，命令行里直接用。

---

## 它能做什么

8 个子命令，覆盖 DFlash 操作的常见场景：

**查看设备**

```
dflash list
```

列出当前连接的所有调试目标，显示芯片型号和板卡信息。

**读取数据**

```
dflash read --addr AF004000 --length 100
```

直接在终端显示 hex dump。加 `-o dump.hex` 可以导出为文件，支持 Intel HEX 和二进制格式。

**擦除扇区**

```
dflash erase --addr AF004000 --sectors 1
```

擦除指定地址开始的若干个扇区。地址会自动对齐到扇区边界，不用手动计算。

加 `--backup backup.hex` 可以在擦除前自动备份。

**写入数据**

有两种方式。从文件写入：

```
dflash write --file data.hex --verify
```

直接写 hex 字符串，不用准备文件：

```
dflash rewrite --addr AF004000 --data 1122334455667788 --verify
```

`rewrite` 是 Read-Modify-Write 模式。不需要关心扇区对齐，工具会自动读取整个扇区、合并新数据、擦除、再写回去。旁边的数据不会被破坏。

**从备份还原**

```
dflash restore --file backup.bin
```

一条命令完成 擦除 → 写入 → 校验 的完整流程。

**UCB 操作（TC3xx 专属）**

```
dflash ucb read --addr AF400000 --length 200
dflash ucb write --file ucb_data.hex
dflash ucb erase --addr AF400000
```

读写 User Configuration Block。UCB 是芯片的配置保护区，写错了可能锁死芯片，所以 erase 操作需要输入 `yes` 确认才会执行。

**复位芯片**

```
dflash reset
```

---

## 怎么用

准备工作很简单。

**你需要：**

- 一台 Windows 10/11 电脑
- 一个 miniWiggler 调试器
- 一块 AURIX 目标板
- 已启动的 TAS Server

**操作步骤：**

1. 把 `dflash.exe` 和 `DeviceConfigs/` 文件夹放在同一个目录下
2. 确保 miniWiggler 已连接目标板，TAS Server 已启动
3. 打开终端，进入该目录，直接运行命令

不需要安装。`dflash.exe` 是静态链接的，不依赖任何运行时 DLL。

---

## 几个注意事项

- DFlash 擦除后的状态是 **0x00**，不是 0xFF。这是 AURIX 的特性。
- 地址参数用十六进制，但**不需要加 `0x` 前缀**。写 `AF004000` 就行。
- `--verify` 参数建议每次写入时都带上，工具会自动读回比对，确保数据正确。
- UCB 操作有风险，务必确认数据无误后再执行。
- 目前不支持 TC4x 系列，TC4x 的 DFlash 架构和操作序列与 TC2x/TC3x 不同。

---

## 典型使用场景

**产线调试** —— 快速擦写 DFlash，验证标定数据是否正确写入。

**故障分析** —— 读取现场设备的 DFlash 内容，导出文件做离线分析。

**UCB 配置** —— 在开发阶段调整安全配置，无需刷写整个固件。

**批量操作** —— 命令行工具天然适合脚本化，可以集成到自动化测试流程中。

---

## 下载

Gitee：https://gitee.com/prettyboyazu/tas-dflash_-erase/releases/tag/V2.0

下载解压后即可使用，无需安装。

---

## 写在最后

DFlash Tool 的设计思路很简单：把 DFlash 操作从图形界面变成一条命令。

能命令行搞定的事情，就不必点鼠标了。

如果你在做 AURIX 相关的开发或测试工作，可以试试看。
