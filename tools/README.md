# wiggle_MCP — AURIX 通用调试 MCP 工具（详细手册）

基于 TAS Client API + Infineon **MCD** API 的 AURIX TC2x/TC3x 调试/烧录工具，封装为 MCP 供 AI 调用，同时提供 CLI / 持久会话 / GDB server 四种前端。
**调试走 MCD（非破坏）**，**Flash/诊断走 TAS**；单 exe 静态链接，MCD 客户端库与 TAS server 随包。

---

## 1. 运行前提与启动

前提：miniWiggler 连接目标板；**DAS64 版 TAS server**（System32 旧版会使 MCD 失效）；同一探针只能被一个 server 占用。

```bat
:: 方式一（推荐）：一键
start_tas_server.bat      :: 起随包 TAS server（若未运行）
start_mcp.bat             :: 起 MCP（stdio）；--sse 为 HTTP 模式

:: 方式二：手动
servers\tas_server_console.exe
python aurix_mcp_server.py --config mcp_config.json
```

Python 需 ≥3.10（`pip install -r requirements.txt`：mcp[cli]、pyelftools）。
在 AI 客户端注册：`python aurix_mcp_server.py --config mcp_config.json`。

独立部署库：gitee `wiggle_-mcp`。克隆后 `start_mcp.bat` 自动解压数据 zip、找 Python、起 server。
实验性全封包（含便携 Python）：`pack_release.py` → `dist/wiggle_MCP_release.zip`（不入库）。

---

## 2. 配置 mcp_config.json（完整内容）

```json
{
    "wiggle_exe": null,
    "aurix_flasher_exe": null,
    "server": "localhost",
    "target": null,
    "device": null,
    "timeout": 120,
    "das_home": null,
    "hldumptc": null,
    "tasking_home": null,
    "build":   { "command": null, "work_dir": null, "timeout": 300 },
    "project": { "elf_file": null, "map_file": null, "lst_file": null,
                 "mdf_file": null, "hex_file": null, "toolchain": "tasking" }
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| wiggle_exe | 否 | null=随包 `wiggle/wiggle.exe`；可指绝对路径 |
| aurix_flasher_exe | 否 | null=随包/安装版 AURIXFlasher（PFlash 用） |
| server / target / device | 否 | localhost / 首个目标 / 自动识别；远程/多目标时填 |
| timeout | 否 | 单工具超时秒，默认 120 |
| das_home | 否 | DAS 安装路径（MCD/server 回退查找）；null=自动 |
| **hldumptc** | 否 | 完整反汇编后端：**exe 路径或目录**；null=自动探测 |
| **tasking_home** | 否 | TASKING 根目录（自动找 ctc/bin/hldumptc.exe）；null=自动 |
| build.command/work_dir | 仅 build_project | 编译命令/目录 |
| project.elf_file/map_file/… | 仅符号/构建工具 | 不填则这些工具不可用；调试/烧录不受影响；也可调用时传 `elf_file=` |

> 核心调试/烧录**零配置开箱即用**；hldumptc 无 TASKING 时回退原生子集译码。

### 2.1 路径格式与填写示例

路径为 Windows 绝对路径；**JSON 中反斜杠须转义为 `\\`，也可用正斜杠 `/`**。
`hldumptc`/`tasking_home` 填**目录或 exe 文件**均可（目录自动找 `ctc/bin/hldumptc.exe`）。

```json
{
    "wiggle_exe": "D:\\wiggle_MCP\\wiggle\\wiggle.exe",
    "aurix_flasher_exe": "D:\\wiggle_MCP\\AURIXFlasher\\AURIXFlasher.exe",
    "server": "localhost",
    "das_home": "C:\\Program Files\\DAS64",
    "hldumptc": "D:\\Program Files\\TASKING\\TriCore v6.3r1\\ctc\\bin\\hldumptc.exe",
    "tasking_home": "D:\\Program Files\\TASKING\\TriCore v6.3r1",
    "build": { "command": "amk -j8 all", "work_dir": "D:\\workspace\\Tasking\\P_CCU3", "timeout": 300 },
    "project": {
        "elf_file": "D:\\workspace\\Tasking\\P_CCU3\\Debug\\P_CCU3.elf",
        "map_file": "D:\\workspace\\Tasking\\P_CCU3\\Debug\\P_CCU3.map",
        "mdf_file": "D:\\workspace\\Tasking\\P_CCU3\\Debug\\P_CCU3.mdf",
        "hex_file": "D:\\workspace\\Tasking\\P_CCU3\\Debug\\P_CCU3.hex",
        "toolchain": "tasking"
    }
}
```

正斜杠等价写法：`"tasking_home": "D:/Program Files/TASKING/TriCore v6.3r1"`。
相对路径以 **config 文件所在目录**为基准解析（如 `"wiggle_exe": "wiggle/wiggle.exe"`）。

---

## 3. CLI 独立命令行（wiggle\wiggle.exe）

wiggle 可脱离 MCP 单独作命令行工具。把 `wiggle.exe` 与 `DeviceConfigs/`、`RegisterDefs/`
放同一目录即可运行（或 `--config-dir` 指定）。所有子命令支持 `--help`。

### 3.0 约定与通用选项
- **地址/长度默认十六进制**（`0x` 前缀可选）：`AF000000`、`0x1000`；`--sectors`/`step n`/`--cpu` 为十进制。
- `--json` 输出机器可读 JSON（脚本/AI 用）；默认人类可读。
- 通用：`--server <ip>`(默认localhost) `--port <n>`(2000) `--target <id>` `--device <name>`
  `--config-dir <p>` `--json` `--hot`(HOT_ATTACH不复位) `--cpu <n>`(调试核号) `--elf <f>`(符号)。
- 调试类默认 HOT_ATTACH（非破坏）；Flash 类自动 RESET_AND_HALT。

### 3.1 设备 / 连接 / 复位
```bat
wiggle list                          :: 列出TAS目标+设备
wiggle info                          :: 设备/内存布局/SVD摘要
wiggle status                        :: Flash状态寄存器(SVD字段解码)
wiggle reset                         :: 复位并运行
wiggle reset --halt                  :: 复位并暂停
wiggle reset --hot                   :: 热附着不复位
wiggle reset --class 1               :: 复位类 0=POR 1=System 2=Application(默认)
```

### 3.2 Flash（DFlash/UCB，TAS 后端）
```bat
wiggle erase --info                              :: 只看设备/扇区信息不擦
wiggle erase --addr AF000000 --sectors 2 --verify
wiggle erase --all --backup dump.bin --verify    :: 全擦+备份+校验
wiggle read  -a AF000000 -l 20000 -o dump.bin    :: .bin原始 / .hex IntelHEX
wiggle read  -a AF004000 -l 100                  :: 终端hex dump
wiggle write -f app.hex --verify                 :: HEX按文件地址
wiggle write -f data.bin -a AF000000 --verify    :: BIN需-a(8字节对齐)
wiggle rewrite --addr AF004004 --data 1122334455667788 --verify  :: 任意地址R-M-W
wiggle rewrite -f patch.hex --backup --verify
wiggle restore --file dump.bin                   :: 擦+写+校验
wiggle ucb read  -a AF400000 -l 800 -o ucb.hex
wiggle ucb write -f bmhd.hex --verify            :: 需确认,32B对齐+CONFIRMED校验
wiggle ucb erase --addr AF400000 --sectors 4 --verify
```
对齐：read 无；write 8字节页对齐；erase 扇区对齐(自动)；rewrite 无(内部R-M-W)。
UCB 锁定区(安全/OTP)禁止擦写；`--all`/UCB 需交互确认。

### 3.3 内存 / 外设寄存器 / 比对（TAS）
```bat
wiggle dump 0x70000000 0x80                 :: hex dump(PFlash/DFlash/SRAM)
wiggle dump 0xA0000000 0x100 -o pflash.hex
wiggle poke 0x70000000 0x12345678 --dangerous   :: 写内存+读回(需--dangerous)
wiggle poke 0xF0018300 0x1 --width 32 --dangerous
wiggle reg DMU.HF.STATUS                   :: SVD名读+字段解码
wiggle reg 0xF8040010                      :: 按地址读
wiggle reg DMU.HF.CONTROL 0x1              :: 写
wiggle reg --list DMU                      :: 列外设寄存器
wiggle compare -f app.hex -a AF000000      :: 文件vs Flash
wiggle search 0xAF000000 0x20000 0x11223344  :: 模式搜索
```

### 3.4 调试器（MCD，非破坏）
```bat
wiggle halt [--cpu n] [--global] [--elf f]   :: 暂停;--global多核同停
wiggle go   [--global]                       :: 恢复
wiggle step [n]                              :: 单步n条(默认1)
wiggle stepover / stepout                    :: 跨过call / 跳到返回地址
wiggle state [--cpu n]                       :: 状态/停止触发id/PC
wiggle cpu_reg                               :: 全核寄存器(PC/PSW/PCXI/A0-15/D0-15)
wiggle cpu_reg PC                            :: 读单寄存器
wiggle cpu_reg D0 0x12345678                 :: 写寄存器
wiggle break add 0x80001000                  :: 指令断点(执行地址)
wiggle break list / del 0 / del 0x80001000 / clear
wiggle watch add 0x70008860 --length 4 --type write  :: 写观察点
wiggle watch add 0x70008860 --type read|rw   :: 读/读写观察点
wiggle watch add --sym Core0_Task_cnt --elf f:: 按符号(自动长度)
wiggle watch list / del <addr> / clear
wiggle var read  Core0_Task_cnt --elf f      :: 变量读(值+hex)
wiggle var write Core0_Task_cnt 0x1234 --elf f
wiggle bt [--elf f]                          :: 符号化调用栈(CSA链)
wiggle disasm 0x80018250 [count] [--elf f]   :: 反汇编(hldumptc优先,原生回退)
wiggle sample --sym X --interval 100 [--size 4]  :: 活体双采样(防撕裂)
wiggle sample 0x70008860 --interval 200
wiggle rtos tasks [--type auto|freertos|autosar] [--elf f]
wiggle rtos bt --task TaskName [--elf f]
```
注意：halt 后计数器停走，测时序先 `reset`；watch 命中后 `go` 会因变量再写立即再停（正常）。

### 3.5 服务形态
```bat
wiggle shell                  :: 交互REPL,持久MCD会话,help/quit
wiggle serve                  :: JSON-lines持久会话(MCP传输),stdin/stdout
wiggle gdbserver --port 3333  :: GDB RSP server
:: GDB 侧: (gdb) target remote :3333 ; file app.elf ; break main ; continue
```

### 3.6 退出码（脚本判断）
| 码 | 含义 | 码 | 含义 |
|----|------|----|------|
| 0 | 成功 | 14 | 擦除超时 |
| 1 | 参数/地址错 | 15 | 错误标志 |
| 2 | server连接失败 | 17 | 校验失败 |
| 3/4 | 目标查询/无目标 | 18 | IO错误 |
| 5/6 | session/device连接 | 22 | 比对不一致 |
| 7 | 设备不支持 | 23 | 搜索无结果 |
| 8 | 备份失败 | 25 | 结果截断 |
| 11-13 | Flash状态/命令 | 26 | 需先halt |
| | | 27 | 平台不支持resume |

### 3.7 旧版兼容（位置参数）
```bat
wiggle 0xAF000000 4            :: 等价 erase --addr AF000000 --sectors 4
wiggle --all --verify          :: 等价 erase --all --verify
wiggle --info                  :: 等价 erase --info
```

---

## 4. MCP 工具（AI 调用示例）

调试：`wiggle_halt(confirm_risk=True, elf_file=…)`、`wiggle_go(global_run=False)`、
`wiggle_step(n)`、`wiggle_stepover/stepout`、`wiggle_state(cpu)`、`wiggle_cpu_reg(name,value)`、
`wiggle_break(action,address)`、`wiggle_watch(action,sym/address,type,length)`、
`wiggle_var(action,name,value)`、`wiggle_bt`、`wiggle_disasm(address,count)`、
`wiggle_sample(sym,interval_ms)`、`wiggle_rtos(action,type,task)`
Flash/诊断：`wiggle_erase/write/read/rewrite/restore/ucb/dump/poke/reg/status/info/list/compare/search/reset`
PFlash：`aurix_pflash(hex_file, verify=True, erase="all", ucb=False, confirm_risk=…, flax_timeout_seconds=10, log_file="")`

- `erase`：`on`=所用扇区、`all`=全部 PFlash 扇区、`off`=不擦；`connect`：`6`=复位暂停、`0`=热附着（须已暂停）。不使用旧文档中的 `used`/`1`。
- `flax_timeout_seconds` 为厂商内部轮询预算（1–120 秒，默认 10），与外层进程 300 秒超时独立；超时不会自动重试擦写。
- `log_file` 为新的 XML 文件路径；不填则生成唯一临时日志。成功和失败均返回命令、stdout/stderr、日志路径；失败后先检查目标状态。
- `wiggle_read` 已分块读取；跨空白 PFlash 页仍可能报 ECC 错误。失败返回事务地址/长度/bytes_ok、整块前缀和 `output_written=false`，不会把读取失败填成 0x00 或覆盖原输出文件。
寄存器元数据：`reg_peripherals / reg_fields / reg_search`
构建：`build_project / parse_symbols / parse_elf / lookup_symbol / lookup_address / reload_parsers`

示例：
```python
wiggle_halt(confirm_risk=True, elf_file="app.elf")
wiggle_watch(action="add", sym="Core0_Task_cnt", elf_file="app.elf")
wiggle_go()                      # 变量被写时halt
wiggle_bt(elf_file="app.elf")    # 写操作调用栈
wiggle_disasm("0x80018250", 16, elf_file="app.elf")
```

---

## 5. 地址视图 / 安全 / 限制
- DSPR 用 `0x7000_0000`（0xD0 别名自动归一）；PFlash 物理 `0xA000_0000`（0x80 cached 数据读写自动归一，IP 断点用执行视图）；SFR `0xF000_0000+`；DFlash `0xAF00_0000`。
- 符号真址以 MAP/ELF 为准；RAM 变量通过反汇编 lea 偏移复核。
- halt 后计数器停走，测时序前先 `reset`(run)；`sample` 双次采样一致才采信。
- UCB/halt 有 confirm_risk 门槛；poke 需 --dangerous；erase 建议 --backup+--verify。
- 限制：trace 需 MCDS 探针（miniWiggler 不支持）；AUTOSAR OS 任务状态需内核符号（未导出时 best-effort）；hldumptc 依赖环境不随包（许可）。