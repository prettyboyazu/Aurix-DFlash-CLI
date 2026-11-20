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

---

## 3. CLI 指令（wiggle\wiggle.exe，具体示例）

通用选项：`--json --server <ip> --target <id> --device <name> --config-dir <p> --cpu <n> --elf <f> --hot`

### 3.1 执行控制 / 调试（MCD，非破坏）
```bat
wiggle halt --elf app.elf                 :: 暂停（保留上下文）
wiggle halt --global                      :: 多核同停
wiggle go [--global]                      :: 恢复
wiggle step 5                             :: 单步5条
wiggle stepover / wiggle stepout          :: 跨过 / 跳出
wiggle state --cpu 0                      :: 核状态/停止触发/PC
wiggle cpu_reg                            :: 全核寄存器
wiggle cpu_reg PC                         :: 读单寄存器
wiggle cpu_reg D0 0x12345678              :: 写寄存器
wiggle break add 0x80001000               :: 指令断点
wiggle break list / del 0 / clear
wiggle watch add 0x70008860 --length 4 --type write   :: 数据观察点(写)
wiggle watch add --sym Core0_Task_cnt --elf app.elf   :: 按符号
wiggle var read Core0_Task_cnt --elf app.elf          :: 变量读
wiggle var write Core0_Task_cnt 0x1234 --elf app.elf  :: 变量写
wiggle bt --elf app.elf                   :: 符号化调用栈
wiggle disasm 0x80018250 16 --elf app.elf :: 反汇编(hldumptc优先)
wiggle sample --sym Core0_Task_cnt --interval 100     :: 活体双采样
wiggle rtos tasks --type autosar --elf app.elf        :: AUTOSAR任务清单
wiggle rtos tasks --type freertos --elf app.elf       :: FreeRTOS任务
wiggle rtos bt --task TaskName --elf app.elf          :: 任务回溯
wiggle reset [--halt] [--class 2]         :: 复位(0=POR 1=System 2=App)
```

### 3.2 内存 / Flash / 诊断（TAS）
```bat
wiggle list / info / status
wiggle dump 0x70000000 0x40               :: hex dump
wiggle read -a AF000000 -l 1000 -o dump.bin
wiggle poke 0x70000000 0x1234 --dangerous :: 写内存(需--dangerous)
wiggle reg DMU.HF.STATUS                  :: SVD外设寄存器
wiggle reg 0xF8040010 0x1                 :: 按地址写
wiggle erase --addr AF000000 --sectors 1 --verify
wiggle erase --all --backup dump.bin --verify
wiggle write -f app.hex --verify
wiggle rewrite --addr AF004000 --data 1122334455667788 --verify
wiggle restore --file dump.bin
wiggle ucb read / write --file bmhd.hex --verify / erase --addr AF400000 --sectors 4
wiggle compare -f app.hex -a AF000000
wiggle search 0xAF000000 0x20000 0x11223344
```

### 3.3 服务形态
```bat
wiggle shell                    :: 交互REPL(持久MCD会话)
wiggle serve                    :: JSON-lines持久会话(MCP传输)
wiggle gdbserver --port 3333    :: GDB RSP；GDB: target remote :3333
```

---

## 4. MCP 工具（AI 调用示例）

调试：`wiggle_halt(confirm_risk=True, elf_file=…)`、`wiggle_go(global_run=False)`、
`wiggle_step(n)`、`wiggle_stepover/stepout`、`wiggle_state(cpu)`、`wiggle_cpu_reg(name,value)`、
`wiggle_break(action,address)`、`wiggle_watch(action,sym/address,type,length)`、
`wiggle_var(action,name,value)`、`wiggle_bt`、`wiggle_disasm(address,count)`、
`wiggle_sample(sym,interval_ms)`、`wiggle_rtos(action,type,task)`
Flash/诊断：`wiggle_erase/write/read/rewrite/restore/ucb/dump/poke/reg/status/info/list/compare/search/reset`
PFlash：`aurix_pflash(hex_file, verify=True, erase="all", ucb=False, confirm_risk=…)`
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