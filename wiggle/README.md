# wiggle — AURIX 通用调试 CLI（MCD 后端）

基于 TAS Client API + Infineon MCD API 的 AURIX TC2x/TC3x 调试工具。
调试走 **MCD（非破坏）**，Flash/诊断走 **TAS**；单 exe 静态链接，MCD 客户端库随包。

## 运行前提
- TAS server 运行（`start_tas_server.bat` 或 DAS 安装），miniWiggler 连接目标。
- MCD 需 DAS64 版 server；`mcdxdas.dll` 随包（exe 同目录优先加载）。

## 命令总览
### 执行控制 / 调试（MCD，非破坏）
| 命令 | 说明 |
|------|------|
| `halt [--global] [--cpu n]` | 暂停 CPU（多核全局可选） |
| `go` / `resume` | 恢复运行 |
| `step [n]` / `s` | 硬件单步 n 条 |
| `stepover` / `so` | 跨过（到 PC+指令长/call 返回） |
| `stepout` / `su` | 跳出到返回地址(A11) |
| `state` | 核状态/停止触发/PC |
| `cpu_reg [name] [val]` / `cr` | 核寄存器读写（PC/PSW/A0-15/D0-15/CSFR） |
| `break add/del/list/clear` / `bp` | 指令断点（MCD trigger） |
| `watch add/del/list/clear [--sym n]` | 数据观察点 read/write/rw |
| `var read/write <sym> [val]` | 变量符号闭环读写 |
| `bt` | 符号化调用栈（CSA 链） |
| `disasm <addr> [n]` | TriCore 反汇编（原生长度校验） |
| `sample <addr|--sym> [--interval ms]` | 活体双采样（防撕裂/比值） |
| `sym load/list/addr/name` | ELF 符号 |
| `reset [--halt] [--class n]` | 复位（MCD，默认 Application） |

### 内存 / Flash（TAS）
| 命令 | 说明 |
|------|------|
| `read -a <hex> -l <hex> [-o file]` | 读内存/DFlash |
| `dump <addr> <len> [-o file]` | _hex dump_ |
| `poke <addr> <val> --dangerous` | 写内存 |
| `write -f <hex/bin> [--verify]` | 写 DFlash |
| `erase --addr/--all [--backup] [--verify]` | 擦 DFlash |
| `rewrite` / `restore` | R-M-W / 备份恢复 |
| `ucb read/write/erase` | UCB（TC3x，确认门槛） |
| `reg <name/addr> [val]` | 外设寄存器（SVD） |
| `status` / `info` / `list` | Flash 状态/设备信息/目标列表 |
| `compare` / `search` | 文件比对/模式搜索 |

### 服务形态
| 命令 | 说明 |
|------|------|
| `serve` | 持久会话 JSON-lines（MCP 传输，状态跨命令持久） |
| `gdbserver [--port N]` | GDB RSP server，`target remote :N` |
| `shell` | 交互 REPL |

## 通用选项
`--json` `--server <ip>` `--target <id>` `--device <name>` `--config-dir <path>` `--cpu <n>` `--elf <file>` `--hot`

## 地址视图（TC2x/TC3x）
- DSPR 用 `0x7000_0000`（0xD0 为别名，自动归一）
- PFlash 物理 `0xA000_0000`（0x80 cached，数据读写自动归一；IP 断点用执行视图）
- SFR `0xF000_0000+`；DFlash `0xAF00_0000`
- 符号真址以 MAP/ELF 为准；RAM 变量可用反汇编 lea 偏移复核

## 示例
```bash
wiggle halt --elf app.elf
wiggle var read Core0_Task_cnt --elf app.elf
wiggle watch add --sym Core0_Task_cnt --elf app.elf
wiggle go                      # 变量被写时 halt
wiggle bt --elf app.elf        # 写操作调用栈
wiggle disasm 0x80018250 8
wiggle gdbserver --port 3333   # GDB: target remote :3333
```

## 注意
- halt 后计数器停走，测时序前先 `reset`（run）。
- 同一 miniWiggler 只能被一个 TAS server 占用。
- trace 需 MCDS 探针；RTOS 感知项目特定，未含。