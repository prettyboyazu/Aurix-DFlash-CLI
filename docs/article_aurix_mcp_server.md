# 让AI直接操作芯片：AURIX MCP Server 实践

**当AI Agent能读写寄存器、烧写Flash、解析编译符号、分析内存——嵌入式开发的完整闭环，来了。**

---

## 嵌入式调试的痛点

做过AURIX开发的人都熟悉这个循环：

> 代码 → 编译 → 烧写 → 连接调试器 → 读寄存器 → 在MAP文件里找变量地址 → 分析数据 → 发现问题 → 再改代码

每一轮迭代，你都在充当"人肉桥接器"——把IDE里的代码和芯片里的硬件状态手动关联起来。读一个Flash状态寄存器要翻手册找地址；dump一段内存要在调试器里手动操作；查一个变量的地址要在5000行的MAP文件里搜索；比对固件和Flash内容要自己写脚本。

**如果AI能直接操作芯片呢？** 不是帮你写代码那种"辅助"，而是真能连上调试器、读写寄存器、烧写Flash、分析运行状态——像一个不知疲倦的初级工程师，7×24小时帮你跑调试循环。

这就是 AURIX MCP Server 要做的事。

---

## MCP：让AI连接物理世界的标准接口

**MCP（Model Context Protocol）** 是 Anthropic 提出的开放协议，定义了AI Agent与外部工具之间的标准通信方式。可以理解为"AI的USB接口"——只要工具实现了MCP协议，任何AI Agent都能即插即用。

在嵌入式场景下，MCP的意义在于：**把硬件调试能力封装成AI可调用的标准工具**。AI不需要知道底层是JTAG还是DAP、不需要知道iLLD命令序列怎么发——它只需要调用一个结构化的工具接口，就像调用API一样自然。

---

## 架构一览

AURIX MCP Server 是一个基于 FastMCP 的 Python 服务，把底层CLI和编译产物解析封装为 **19个MCP工具**：

```
┌─────────────────────────────────────────────────┐
│          AI Agent (Claude / GPT / 本地模型)        │
├─────────────────────────────────────────────────┤
│              MCP 协议 (stdio / SSE)               │
├─────────────────────────────────────────────────┤
│        AURIX MCP Server (FastMCP · 19工具)        │
├────────────┬──────────────┬─────────────────────┤
│ wiggle.exe │ AURIXFlasher │  编译产物解析引擎     │
│ 调试+DFlash│ PFlash烧写   │  MAP/LST/ELF/MDF     │
├────────────┴──────────────┴─────────────────────┤
│           TAS Server + miniWiggler                │
├─────────────────────────────────────────────────┤
│           AURIX TC2x / TC3x 目标芯片              │
└─────────────────────────────────────────────────┘
```

**职责划分**：

| 模块 | 负责 | MCP工具数 |
|------|------|-----------|
| `wiggle.exe` | DFlash 擦/读/写/调试 + 寄存器 + 内存 | 12 |
| `AURIXFlasher.exe` | PFlash 整片擦 + 编程 + 校验 | 1 (`wiggle_pflash`) |
| 编译产物解析 | 执行编译 + 解析 MAP/LST/ELF + 符号查询 | 6 |

两者互补不重叠 —— DFlash 操作走 wiggle，PFlash 烧写走 AURIXFlasher。

> **关于 AURIXFlasher**：这是 Infineon 的闭源商业工具，**不能随本包分发**。使用者需要自行从 Infineon 官网下载安装（免费），然后在 `mcp_config.json` 中配置路径：
> ```json
> "aurix_flasher_exe": "C:\\Infineon\\AURIXFlasherSoftwareTool-3.0.16\\AURIXFlasher.exe"
> ```
> `wiggle.exe` 是本项目自研的开源工具（Apache 2.0），可以直接包含在包内。MCP Server 启动时会自动在常见目录搜索这两个 exe，找到就用，找不到才需要手动配置路径。

19 个工具的全貌：

**硬件操作（13个）**

| 工具 | 功能 |
|------|------|
| `wiggle_list` / `wiggle_info` / `wiggle_status` | 设备连接、信息、状态 |
| `wiggle_erase` / `wiggle_write` / `wiggle_read` | DFlash 擦/写/读 |
| `wiggle_pflash` | PFlash 烧写 |
| `wiggle_reg` / `wiggle_dump` / `wiggle_poke` / `wiggle_search` / `wiggle_compare` | 调试（寄存器、内存、搜索、比对） |
| `wiggle_reset` | 复位 MCU |

**构建产物分析（6个）**

| 工具 | 功能 |
|------|------|
| `build_project` | 执行编译命令，结构化返回 errors / warnings，成功后自动刷新 parser 缓存 |
| `reload_parsers` | 强制重新加载编译产物解析器（外部编译后使用） |
| `parse_symbols` | 解析 MAP（TASKING）或 LST（GCC）符号表 |
| `parse_elf` | 解析 ELF 符号表、段信息、DWARF 调试信息 |
| `lookup_symbol` | 按名称跨源查找符号（MAP + ELF 联合） |
| `lookup_address` | 按地址反查最近符号 + MDF 地址空间翻译 |

> **19 个工具的完整参数 + 调用示例 + 客户端注册方法 + 退出码 → 见 README.md 和 docs/ 目录**。

---

## AI闭环：从代码到调试，能走多远？

### 场景一：固件烧写 + 自动验证

> "帮我把最新的firmware.hex烧到PFlash，烧完校验一下，如果有问题读一下Flash状态寄存器。"

AI操作链：`wiggle_pflash` → 内置 verify → 失败自动 `wiggle_status` → 解码 PVER/EVER/PROER/SQER/OPER 字段 → 告诉你是校验错还是保护触发。**完全闭环**。

### 场景二：DFlash数据调试

> "我的DFlash配置数据好像不对，帮我看看0xAF004000开始的内容，和我预期的config.bin对比一下。"

AI操作链：`wiggle_dump` 读 → `wiggle_compare` 逐字节比 → 分析差异位置 → 必要时 `wiggle_write` 重写。**完全闭环**。

### 场景三：寄存器级问题排查

> "我的Flash擦除总是超时，帮我看看什么情况。"

AI操作链：`wiggle_status` 看 D0BUSY=1 → `wiggle_reg DMU.HF.ERRSR` 看到 SQER=1（序列错）→ `wiggle_reset` 清状态 → 建议"检查擦除期间是否有其他Flash访问"。**完全闭环**。

### 场景四：编译→符号查询→符号级调试

> "帮我编译项目，然后看看 Os_Counter_10ms 这个变量的值对不对。"

AI操作链：`build_project` 编译 → `lookup_symbol("Os_Counter_10ms")` 从 MAP 文件找到地址 → `wiggle_dump` 读内存 → 解读数值含义。**完全闭环**。

AI 不再只能看到裸地址 `0x70003456`，而是直接理解它是 `Os_Counter_10ms`——一个操作系统计数器变量。这就是编译产物解析带来的质变：从"地址级"升级到"符号级"。

### 场景五：代码→编译→烧写→调试

> "帮我改一下Bootloader的超时参数，编译后烧进去测试一下。"

AI操作链：代码编辑（✓ Agent 天然支持）→ `build_project` 编译（失败则自动分析错误）→ `wiggle_pflash` 烧写 → `wiggle_reset(halt=True)` → `lookup_symbol` 定位入口函数 → `wiggle_reg` 查 PC。**完整闭环**。

---

## 关键设计：为什么用JSON？

MCP 底层依赖 wiggle 的 `--json` 输出。每个 CLI 子命令都返回结构化 JSON：

```json
// 成功
{"status":"ok","register":"HF.STATUS","value":34078720,
 "fields":{"D0BUSY":0,"D1BUSY":0,"P0BUSY":0,...}}

// 失败
{"status":"error","code":14,"message":"Erase timeout"}
```

这个设计至关重要——AI 不需要解析人类可读的终端文本（对 LLM 来说是噩梦），而是直接处理结构化键值对。状态、数值、字段名都是明确的，AI 可以精确理解硬件状态并做出决策。

---

## SVD驱动：不硬编码任何寄存器

MCP 寄存器访问能力基于 **CMSIS-SVD** 标准。SVD 是芯片厂商的官方寄存器描述（每个外设、寄存器、位字段）。

意义：

- **不硬编码** —— 地址、位定义全部从 SVD JSON 加载，换芯片不用改代码
- **字段解码** —— 读一个寄存器，AI 拿到的是解码后的字段（"D0BUSY=0: DF0空闲"），不是裸数值
- **全系列覆盖** —— TC21x 到 TC39x，放对 SVD 文件就自动支持

AI 调试时能直接说"DMU.HF.STATUS 的 D0BUSY 位为1，说明 DataFlash Bank 0 正在忙"，而不是猜测 `0x00000001` 是什么意思。

> 本项目用 `svd-to-full-json` skill 自动从 `data/SVD/*.svd` 生成 8 个芯片的完整 RegisterDefs JSON（~290 MB 总计），覆盖 4251+ 个寄存器、27971+ 个字段。

---

## 编译产物解析：从"地址级"到"符号级"

有了硬件操作工具，AI 能读写任意地址。但裸地址没有语义——`0x80001234` 是 `g_bootConfig.timeout` 还是 `main()` 入口？

编译产物解析解决了这个问题。AI 可以解析编译生成的 MAP / LST / ELF 文件，获得完整的符号→地址映射。

### 双工具链支持

实测了 TASKING 和 GCC 两种编译器的产物格式：

| | TASKING VX-toolset | GCC (tricore-elf) |
|--|---|---|
| 符号表 | `.map`（5000+ 行结构化表格） | `.lst`（objdump 反汇编输出） |
| 地址空间 | `.mdf`（space/chip/map 映射） | 无（GCC 不生成） |
| ELF | 含完整 DWARF 调试信息 | 含 DWARF 调试信息 |
| 自动检测 | 有 `.map` 走 TASKING 模式 | 有 `.lst` 走 GCC 模式 |

### 从地址到符号的质变

```
没有符号解析：读 0x80001234 的值 → 0x000000FF
             （这是什么？谁定义的？完全不知道）

有了符号解析：读 g_bootConfig.timeout 的值 → 255
             （启动超时参数，单位 ms，定义在 bootloader.c:42，
              当前值255意味着等待255ms后跳转应用）
```

完整场景（"Bootloader 跳转后应用没起来"）：

1. `build_project` 编译最新代码
2. `lookup_symbol("JumpToApp")` → 地址 `0x80002400`
3. `wiggle_pflash` 烧入最新固件
4. `wiggle_reset(halt=True)` 让 CPU 停在入口
5. `wiggle_reg` 查 PC 是否在 `JumpToApp` 地址
6. `lookup_symbol("app_start_flag")` → 找到地址，`wiggle_dump` 读值 = 0
7. 结论："JumpToApp 执行了，但 app_start_flag=0，应用初始化失败"

**这才是真正的闭环：从函数名到变量名，从源码到硬件，全程语义级理解。**

---

## 写在最后

回到标题：**AI 能实现从代码到调试的闭环吗？**

答案是：**能。**

当前的 AURIX MCP Server 覆盖了完整链路：编译构建 → 解析 MAP/ELF 获取符号地址 → 烧写 Flash → 读寄存器 → dump 内存 → 符号级调试。AI 可以自主走完"改→编→烧→调→析→再改"的完整循环，不需要人工在 IDE 和调试器之间来回切换。

这不替代嵌入式工程师。它把工程师从重复性的"读寄存器、dump内存、在MAP文件里找地址、比对数据"中解放出来，让时间花在真正需要创造力的地方——**理解问题、设计方案、做出决策**。而 AI 帮你跑那个调试循环。

---

## 参考资源

本项目的 **具体使用说明、命令参数、客户端配置** 不在这篇文章里，请看打包根目录的完整文档：

| 资源 | 路径 | 内容 |
|------|------|------|
| **README** | `README.md` (v2.5, 17 章) | Quick Start / 架构图 / 配置 / 7 个工作流 / UCB 表 / 退出码 / FAQ |
| **MCP 工具手册** | `docs/MCP-Command-Reference.xlsx` | 19 个 MCP 工具的完整签名 + 调用示例 |
| **wiggle CLI 手册** | `wiggle/wiggle-Command-Reference.xlsx` | 17 个 wiggle 子命令 + 8 全局选项 + 24 退出码 |
| **AURIXFlasher CLI 手册** | `AURIXFlasher/AURIXFlasher-Command-Reference.xlsx` | 12 个 flag + 11 典型场景 + 9 个 script 子命令 |

每个 Excel 第 1 页是"符号约定"（解释 `< > [ ] ( ) { } |` 在文档中的含义），第 2 页是带可复制示例的指令表。

---

*AURIX MCP Server 基于 Infineon 开源的 TAS Client API 构建，支持 AURIX TC2x/TC3x 全系列微控制器（TC4x 暂不支持）。19 个 MCP 工具覆盖硬件操作 + 编译产物解析全链路，支持 TASKING 和 GCC 双工具链。*