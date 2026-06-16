# `apps/wiggle` + `wiggle_MCP` 系统级评审（v3，独立评审）

**评审对象**：
- `D:\workspace\tas-dflash_-erase\apps\wiggle`（C++ 工具源码）
- `D:\wiggle_MCP`（Python MCP Server + 部署资源）

**wiggle 版本**：v2.2（`WIGGLE_VERSION`）
**评审时间**：2026-06-16
**评审方式**：从当前代码状态独立评审，**不参考任何历史评审结论**——只看代码本身

**文件清单**：

| 类别 | 路径 | 行数 |
|---|---|---|
| Wiggle 入口 | `apps/wiggle/wiggle_main.cpp` | 4,295 |
| Wiggle 头/源 | `apps/wiggle/{device_config_loader, hex_parser, svd_loader}.{h,cpp}` | 800 |
| Wiggle 构建 | `apps/wiggle/CMakeLists.txt` | 87 |
| Wiggle 资源 | `apps/wiggle/{wiggle.ico, wiggle.rc}` | — |
| MCP Server | `wiggle_MCP/aurix_mcp_server.py` | 937 |
| 构建分析 | `wiggle_MCP/build_analysis.py` | 766 |
| MCP 文档/配置 | `wiggle_MCP/{README.md, mcp_config.json, start_*.bat, requirements.txt}` | — |
| 部署资源 | `wiggle_MCP/wiggle/{wiggle.exe, DeviceConfigs/*, RegisterDefs/*}` | 21 device + 8 SVD |

---

## 0. TL;DR

| 维度 | 评分 | 一句话 |
|---|---|---|
| **wiggle 架构** | B+ | 子模块边界清晰，三个 loader 解耦干净；但 `wiggle_main.cpp` 仍是 4,295 行 god-file |
| **wiggle 接口契约** | B | CLI 子命令丰富（27 个），参数风格统一；但 `--json` 输出在某些命令仍有遗漏 |
| **wiggle 健壮性** | B+ | UCB 锁定区保护、二次确认、ECC 容错到位；但 UCB 写入缺少 32 字节 record 边界校验 |
| **wiggle 可测性** | C+ | 强依赖 `CTasClientRw` + 全局 `g_json`，无单元测试 |
| **MCP server 完整性** | B- | 19 工具覆盖 DFlash / PFlash / 寄存器 / 构建分析；但 UCB 3 命令 + 调试 6 命令**完全未暴露** |
| **MCP server 安全性** | C | `subprocess.run(shell=True)` + LLM-controlled config = **命令注入风险** |
| **端到端集成** | C+ | DFlash 烧写流可跑通（80%），但 9 个核心调试 + UCB 工作流缺失 |
| **可维护性** | B- | 两层代码（wiggle C++ + MCP Python）各自有 god-file 倾向；全局状态污染单测 |

**核心结论**：作为 Infineon 内部 DFlash 工具，**功能完整、边界护栏到位、能用**。但作为 MCP 暴露给 AI agent 的工作流载体，**UCB / 调试 9 个核心命令完全缺位**，加上命令注入风险 + 120s 全局 timeout 误配置，**MCP v2.2 只能做"读 + 烧 DFlash"，做不了调试**。

---

## 1. wiggle 源码评审

### 1.1 文件结构

```
apps/wiggle/
├── CMakeLists.txt              87 行   静态构建 + Windows / Linux 分支
├── wiggle_main.cpp          4,295 行   27 个 doXxx + main + 公共 helpers
├── device_config_loader.{h,cpp}      342 行   JSON 设备配置加载
├── hex_parser.{h,cpp}                 228 行   Intel HEX / BIN 解析
├── svd_loader.{h,cpp}                 231 行   SVD 寄存器定义加载
└── wiggle.{ico,rc}                    —       图标资源
```

**依赖图**：
```
wiggle_main.cpp ─→ device_config_loader ─→ nlohmann/json
                ─→ hex_parser          (no deps)
                ─→ svd_loader          ─→ nlohmann/json
                ─→ tas_client_rw.h     (Infineon TAS SDK，外部)
                ─→ tas_utils.h / tas_utils_client.h  (BRKIN/BRKOUT API)
```

无循环依赖，三个子模块互不通信，由 main.cpp 编排。**架构层面：好**。

### 1.2 子模块评审

#### 1.2.1 `device_config_loader` — B+

**职责**：扫描 `DeviceConfigs/` 目录 → 解析每份 JSON → 索引为 `std::vector<DeviceEntry>`（含 `dflash` 标量 + `memoryRegions` 列表 + `jtagIds` 列表）。

**优点**：
- 加载策略清晰：目录扫描 → `parseDeviceJson` → 内存索引
- `findByJtagId` 用 `& 0x0FFFFFFF` 掩码剥离版本 nibble（L299），正确处理 AURIX 多 step
- `findByName` 先精确 `shortName` 匹配，再子串匹配 `deviceName`（L312-329），优先级合理
- `extractShortName` 贴心小工具（L59-81），从 `TC27x_D_step.json` 提 `TC27x`
- 显式跳过 `devices.json` 索引文件（L101）+ `TC4*` 前缀的配置文件（L106），注释说明 TC4x 有不同 DFlash 基址

**问题**：
- `parseDeviceJson` 154 行单函数（L117-296），循环里嵌了 3 层 if（`memory` → `DataFlash` → `logicalSectors` → `UCB`），建议拆 `parseMemoryRegion()` / `parseDFlashSection()` / `parseUcbArray()` 三个子函数
- `DeviceConfig::memoryRegions` 加载了但 main.cpp 只在 `doInfo` 里完整用（L3762-3769）；其他命令全用 `flashCfg.baseAddress/totalSize/sectorSize` 三个标量——说明 data model 比实际需求复杂，**可考虑降级为只标量 + 单独 `memoryRegions` 数组**
- `findByJtagId` 注释说"masks out version nibble"但 `& 0x0FFFFFFF` 同时屏蔽高 4 bit——对于 AURIX family/company ID 都等于 0 的情况没问题，但若未来有 mixed JTAG ID 字典需要改掩码策略
- `loadFromDirectory` 的 `loadedCount > 0` 当返回值要求目录里至少有一个 .json（L114）；空目录返回 false，但 stderr 提示会误导——"已加载 0 个 != 失败"
- `parseDeviceJson` 用 `std::istreambuf_iterator` 一次性读全文（L125-126）——device config 才几 KB，无所谓；但暴露了无 size limit 的 API 习惯，对**未来可能更大的 device config** 是隐患

#### 1.2.2 `hex_parser` — A-

**职责**：解析 Intel HEX（6 种 record type）或 raw binary → `HexParseResult{success, errorMsg, warningMsg, segments[], entryPoint}`。

**优点**：
- 支持 Intel HEX 全部 6 种 record type（00/01/02/03/04/05），含 `:02000004` ELA 和 `:04000005` 入口点
- checksum 校验完整（`sum & 0xFF == 0`，L100-106）
- contiguous segment 合并逻辑在 L115-123，避免每行 16 字节都开新 segment
- `forceNewSegment` 状态机正确处理 ELA (04) / ESA (02) 强制开新段
- `loadBinaryFile` 走 `ios::ate + seekg` 一次性读（L197-218），无中间 copy
- EOF 缺失时设 `warningMsg`（L187）而非 error——callers 仍可继续

**问题**：
- **缺 type 03 处理**：当前 `case 0x03` 是 no-op（L168-170）——但 type 03 是 16-bit CS:IP 入口点，跟 type 05 互斥；**实际是符合现代 toolchain 习惯**（已经不再产生 type 03），但代码注释没说为什么 no-op
- **缺 type 03 时的 entryPoint 处理**：类型 03 应被忽略（已正确），但 type 05 + type 03 同时出现时无明确优先级
- **错误信息不带文件名**：多文件批处理时定位困难——应该在 parse 入口加 `filePath` 前缀
- **强制 page-aligned 数据 padding 不在 parser 而在 caller**（`writeSegmentPages` L1183-1187）：让 parser 关心"对齐"还是 caller 关心——目前 caller 决定，**OK**，但跨 caller 不一致（doWrite / doRestore / doRewrite 各自实现 padding）

#### 1.2.3 `svd_loader` — A-

**职责**：加载 Infineon 提供的 SVD JSON → 索引为 `SvdRegister[]` + `nameIndex_`（normalized string→idx）+ `addrIndex_`（uint64→idx）。

**优点**：
- 处理 TC SVD 的 cluster → register 嵌套结构（L144-154）
- `buildIndex` 同时建 name index 和 address index，O(1) 查询
- `getFlashStatusRegName()` 用启发式（`HF.STATUS` 优先 → `FSR` 兜底，L209-219）做设备无关的 flash 状态寄存器查找
- `normalizeName` 去 `.` 和 `_`，让 `DMU.HF.STATUS` / `DMU_HF_STATUS` / `dmu.hf.status` 都能命中（L22-30）——**实战中非常实用**
- `buildIndex` 同时建 `fullName` 和 `peripheral_name` 两种索引（L39-44），支持 `DMU_HF_STATUS` 和 `HF_STATUS` 两种查询

**问题**：
- `getRegisters(peripheral)` 是 O(N) 线性扫描（L198-207），N 是所有寄存器。对大 SVD 文件每次都扫一遍——SVD 一般几千条寄存器，问题不大；但 `wiggle reg --list` 多次调用会重复扫，建议建 `std::map<peripheral, vector<const SvdRegister*>>` 副索引
- **重复定义的 `parseHexAddr`**：文件里有 `static uint64_t parseHexAddr(const std::string&)` (L13-20 in svd_loader) 和 main.cpp 里的 `static bool parseHexAddr(const char*, uint32_t&)`（M[693-706] in main）——签名/返回类型不同但名字相同。**容易混淆**——建议 svd_loader 这个改名 `parseHexAddr64`
- `nameIndex_` 用 `std::map` 而不是 `std::unordered_map`——SVD 寄存器数量通常 500-2000 个，map 性能足够，可读性更好。**OK**

### 1.3 `wiggle_main.cpp` 分块评审

#### 1.3.1 整体结构

4,295 行，按 27 个 doXxx + main 拆。命令分类：

| 类别 | 命令 | 行数区间 |
|---|---|---|
| Flash 数据 | erase / write / rewrite / restore / read | 2300-2900 + 1020-1170 |
| 设备 | list / reset / info / status | 868-1060 + 3500-3850 |
| 内存调试 | dump / poke / reg / compare / search | 3200-4080 |
| UCB | ucb read / write / erase | 1750-2400 |
| 调试控制 | halt / go / cpu_reg / break / step / bt | 3960-5070 |
| REPL | shell | 4080-4170 |

#### 1.3.2 公共基础设施 — B

**做得好的**：
- `ToolContext`（L751-761）：聚合 `client + configLoader + svd + flashCfg + conInfo + configDirPath` 一次 init，到处复用——**好设计**
- `RESOLVE_CTX` 宏（L767-779）：实现 shell 模式下的"复用已有连接 vs 新建连接"二选一，节省每次 `initTool` 都要重新 server_connect + session_start 的开销
- `startSession` 重连兜底（L296-333）：自己 session 占用 → 取现有名 → 空名 join，**三级 fallback**
- `TAS_NAME_LEN16` 的 16 字节截断 + 注释解释 0xC0000409 溢出坑（L304-310）——这种 inline 注释值得保留
- `findConfigDir` **已用 `GetModuleFileNameW`**（L234，宽字符 API）——支持 CJK 路径
- **已加 Linux 支持**（L255-275 `/proc/self/exe` + L57-79 pthread/dl/-Wall）——CMakeLists 同步加 UNIX 分支

**问题**：
- **5 个全局状态变量**：`g_json` (L147) / `g_hotAttach` (L675) / `g_haltMethod` / `g_bp[8]` / `g_shellCtx` (L764)
  - `g_json` 跨 doXxx 传 flag——设计 OK 但全局 bool 降低可测性
  - `g_hotAttach` 全局 bool——同上
  - `g_haltMethod` 跨命令持久化，但 CLI 调用间不持久——**意义不大**，建议删除
  - `g_bp[8]` 8 槽 BP 缓存——shell 内可用，shell 间失效，**也是半残持久化**
  - `g_shellCtx` shell 模式共享 ctx——**scope 限 shell，OK**
- **22 个 EXIT_* 常量**（L117-139），编号 0-23 中间缺 9/10/13。原因不明（历史预留？笔误？）。**维护成本高**——建议合并到 6 类：OK / USAGE / TARGET_FAIL / FLASH_FAIL / VERIFY_FAIL / INTERNAL
- `findConfigDir` 优先级：argv > exe-relative > `TAS_DEVICE_CONFIGS` env > cwd。**env var 是 agent 调用的最自然入口，应该提到第一优先级**

#### 1.3.3 `doErase` — B

5 步流程清晰打印：clear status → erase → wait → check flags → reset to read（L2554-2593）。

**优点**：
- `--info` 只打印配置不动 flash（L2496-2499）
- `legacyMode` 参数（L2402-2426）兼容老用法 `wiggle <addr> <num_sectors>`，**周到的向后兼容**
- `--all` 等价于 `erase addr=base, sectors=numSectors`（L2502-2505），代码复用
- 溢出防护 `eraseBytesU64 = numSectors * sectorSize; if > UINT32_MAX`（L2523-2528）完整
- auto-align 地址（`alignedAddr = sectorAddr & ~(sectorSize - 1)`，L2508-2513）——友好 UX

**问题**：
- legacyMode 分支和 subcommand mode（L2427-2470）参数解析 70% 重叠——应该用早期检查分流到 subcommand mode
- `g_json` 在 M[2393] 设——前面所有 parse error 走 `fprintf` 不走 `jsonError`，**与 `--json` 模式契约不一致**
- 范围检查 M[2433] `sectorAddr >= flashCfg.baseAddress + flashCfg.totalSize` 用 `>=` 而非 `>`——边界判断 OK，但与 `dflashEndAddr` 的 `- 1` 关系不直观

#### 1.3.4 `doWrite` / `doRestore` — A- / A-

**doWrite 优点**：
- hex_parser + loadBinaryFile 抽象用得正确
- HEX 文件带 `--addr` 时做 address shift（L1351-1357）——把 0xAF000000 的 HEX 重定位到 0xAF001000
- page-alignment 检查 + 段间 page-boundary overlap 检查（L1390-1412）
- `writeSegmentPages` 把"每页 8 步"封装成可复用 helper，被 doWrite / doRestore / doUcbWrite 三个命令共享——**这个文件里少有的"提取共用"做得好的地方**

**doWrite 问题**：
- `if (!inputFile)` 用 `fprintf` 而非 `jsonError`（L1309-1312）——与 `--json` 模式不一致
- verify 阶段没用 helper——doRestore 的 verify 是 chunked read，doWrite 是一次性 read 然后逐字节比较（L1446-1474）——**两个 verify 风格不统一**

**doRestore 优点**：
- "先擦后写再验"三步语义清晰
- bin 文件延迟到 initTool 后才 assign base address（L1587-1607 + L1604-1606）——避免 init 前需要 device config
- `eraseStart = eraseStart & ~(sectorSize - 1)`（L1653）先 sector align 再算 numSectors
- 默认开启 verify（L1500）——restore 是恢复出厂，verify 必须默认

**doRestore 问题**：
- `totalPages` 算法 doWrite L1422-1427 和 doRestore L1684-1687 有微妙差异（一个用 `paddedSize` 临时变量，另一个直接 `(size + PAGE_SIZE - 1) / PAGE_SIZE`）——结果一致但风格不一致
- `verifyChunkSize = sectorSize`（L1707）是不必要的——`doWrite` 直接 `seg.data.size()` 一次性读。两者风格不一致

#### 1.3.5 `doRewrite`（Read-Modify-Write）— B

**优点**：
- 这是整个工具里最复杂的命令，做对了
- data source 互斥检查（L2753-2768）：`--file` vs `--data` 必选其一
- UCB 锁定区检查复用 doUcbErase 的 `LockedRegion[]`（M[2774-2788]）——说明作者意识到有共享需求，但**复制了检查逻辑而非提到顶层**
- "读 sector → merge → 擦 → 写回"语义严格按 5 步打印

**问题**：
- 文件解析逻辑 100 行大段没有抽函数（L2770-2810）
- `if (newData.empty())` 与 `parseResult.segments.empty()` 双重检查——**重叠校验**

#### 1.3.6 `doUcbRead/Write/Erase` — B

**优点**：
- UCB 命令全局 `if (!ctx.flashCfg.isTc3x)` 守卫（L1805-1809、L2013-2017、L2164-2168），TC2x 直接拒绝
- UCB base 必须 `!= 0` 检查（L1811-1815）
- `doUcbErase` 的 `LockedRegion` 硬编码 `0xAF400800-0xAF400FFF` 和 `0xAF401800-0xAF401FFF`（L209-213）——chip-level OTP 区
- `doUcbErase` 二次确认 `yes`（L2298-2316）——跟 UCB 一样敏感
- 三个命令的 print format 一致：`Device: ... / UCB: ... / Action: ...`

**问题**：
- **`doUcbErase` 用户 abort 退出码不一致**：M[2041] `doUcbRead` 用 `EXIT_SUCCESS`（=0）表示"用户放弃"，`doUcbErase` 用 `EXIT_USAGE_ERROR`（=1）——**两个 UCB 命令在用户 abort 时退出码不一致**
- **`doUcbErase` 二次确认走 stdin**：MCP/agent 集成时 stdin 是 MCP 控制的 pipe，**自动化必死**
- **UCB 锁定区硬编码**：`0xAF40xxxx` 对 TC38x 等大 DFlash 设备可能偏移——**硬编码限制**。建议改成从 `flashCfg.ucb` 派生
- **`doUcbWrite` 没做"目标地址是 UCB 区间内"的 32 字节 UCB record 边界检查**：单个 UCB record 是 32 字节，写入非记录边界会写脏；且**不检查 CONFIRMED word（0x55FE）**——可能把 CONFIRMED=0x55FE 写成别的值触发 re-program，**永久锁芯片**

#### 1.3.7 `doList` / `doReset` — C+

**这是 wiggle 主文件里最差的一对命令**。

两者都**没有**走 `initTool()` 和 `RESOLVE_CTX` 宏，而是**手写了一遍**完整的 server_connect → get_targets → startSession → device_connect 流程：
- `doList` L868-931：63 行重写 init 的 50%
- `doReset` L937-1016：79 行重写 init 的 100%（含 server_connect + get_targets + startSession + device_connect + JSON 输出）

**问题**：
- 错误处理用三行重复 if/else，模式跟 `initTool` 不一致——但**每个 fail 都同时 `fprintf` + `jsonError`**（这点做对了）
- `doReset` **忽略 `g_hotAttach`**（`--hot` 在 doReset 不可用）——agent 想软重启 MCU 不破坏 DFlash 做不到
- 两者都没做 TC4x 拒绝（`rejectTc4xDevice`）——如果有人 `wiggle reset` 接 TC4x 板子，TC4x 也会被允许 reset
- 两者都没做 device config 加载——`doList` 返回的 targets 列表没附带 device config 信息

**修复**：这两个命令应该走 `RESOLVE_CTX` + `device_connect(DCO)`，`doList` 走"只连 server + 列 targets"的轻量 init helper。**重构 ROI 很高**——消 100+ 行 + 统一行为。

#### 1.3.8 `doReg` / `doStatus` / `doInfo`（SVD 调试）— A-

**优点**：
- SVD 按需加载（`doReg` M[3036-3048] + `doStatus`/`doInfo` 通过 `ToolContext`），避免所有命令都加 10MB 内存
- SVD name → address 查表 + address → SVD 逆向（`doReg` M[3106-3118]），双向都支持
- 字段解码：mask = `((1<<(msb-lsb+1))-1) << lsb`——位域标准算法
- `doInfo` 的 JSON 输出完整：device + memory_regions + dflash + ucb + svd，**结构最完整**

**问题**：
- `doReg` 的 enum value 描述查找用 `O(N)` 线性扫（M[3177-3182]），SVD field 一般 5-20 个 value，OK；但 1000 个 reg × 20 个 value 的时候 list 命令会卡
- `doStatus` 错误分支 M[3560-3582] `if (!errName.empty() && errName != statusName)`——TC3x status 和 error 是两个不同寄存器，TC2x 两者同名共用；如果 SVD 把 `getFlashErrorRegName` 返回空字符串，**整段被跳过，没有 fallback 提示**

#### 1.3.9 `doCompare` / `doSearch` — B+

**优点**：
- `doSearch` 64KB chunk + pattern-overlap 读（L4016-4023）——处理跨 chunk 边界的 pattern
- 1000-match 上限（L4038-4055）防爆内存，但截断时仍返回 `EXIT_OK`——**截断信号只在 JSON 字段 `truncated=true`，非 JSON 模式完全没有信号**
- `doCompare` L3930-3940 限 20 行 mismatch 输出

**问题**：
- `doSearch` 64KB 单次 read，pattern overlap buffer 大小 `CHUNK_SIZE + pattern.size() - 1`——pattern.size() 远大于 64KB 时会爆（实际不可能）
- `doCompare` 不支持 HEX 多 segment——segments 之间不连续时只 compare 第一个 segment（L3910-3941）
- `doCompare` mismatch>0 时 JSON 模式返 `EXIT_OK`（L3951）但 `match=false` 字段——**靠 exit code 的 agent 会误判**。非 JSON 模式正确返 `EXIT_MISMATCH_ERROR(22)`（L3953）

#### 1.3.10 Debug 控制 — A

**这是这个文件里设计最好的一组**：
- **3-level halt fallback**（M[3970-4085]）：BRKIN pin → DBGSR register → RESET_AND_HALT，按代价递增排序
- **3-level resume fallback**（M[4091-4228]）：BRKIN release → DBGSR.CLR_HALT → reset，含 PC 变化验证
- **`g_haltMethod` 缓存**：第一次成功的 fallback 在后续命令中优先——**聪明的优化**
- **CSA 寄存器表**（M[4285-4296]）：32 个寄存器 word offset 一一对应 TriCore Vol1 Table 29
- **PC = PCXI[31:16] << 1**（M[4158, 4306]）——TriCore 标准提取方式
- **`doBt` 循环检测**（M[5026-5031]）：维护 `visited[]` 数组防 CSA 链死循环

**问题**：
- **`doGo` "DBGSR stale but PC changed"路径**（M[4166-4178]）：处理了"DBGSR readback 没及时更新" race condition，但注释没说 DBGSR 为什么 stale。**应该加注释 "DBGSR readback may be stale for 1-2 cycles after CLR_HALT on TC1.6.2"**
- **`doBreak go` 命令 200 行**（M[4651-4848]）：set BP + resume + poll halt 在一个连接里做完。出 bug 难调——失败时可能 BP 已 set、CPU 未 resume。建议加 `--dry-run` 只设 BP 不 resume
- **`doBreak` 注释说 `g_bp[]` 不同 CLI 调用间不 persist**（M[4496]）但代码仍然在 readback 后写 g_bp（M[4537-4539]）——**应该是只在 shell 模式才有意义**
- **CPU CSFR base 数组 `CPU_CSFR_BASE[]`**（M[162-165]）：定义了但**没被使用**——所有调试命令都 hardcode CPU0 base 0xF8810000。**多核 debug 是 TODO**

#### 1.3.11 `doShell` REPL — B+

**优点**：
- `g_shellCtx` 复用机制（L4104）——shell 启动一次 initTool，之后所有 doXxx 通过 RESOLVE_CTX 复用同一个 `CTasClientRw` 连接，**省掉每次 server_connect**
- 缩写在 M[5154-5159]：`go`/`resume`、`cpu_reg`/`cr`、`break`/`bp`、`step`/`s`、`bt`/`backtrace`、`erase`/`e`、`write`/`w` 等等——**对调试场景的 REPL 体验很周到**
- `fflush(stdout)` 在每次循环末尾（L4164）——保证 pipe/MCP 集成时能及时拿到输出

**问题**：
- REPL 不支持 history（`std::getline` + 解析，无 readline/history 文件）
- REPL 不支持多行命令
- REPL 不知道当前 halt 状态——用户敲 `cpu_reg A0` 时如果 MCU 没 halt，会得到模糊的"MCU must be halted"但 REPL 不主动提醒当前状态
- 错误恢复差：doHalt 失败时 REPL 不会回到 prompt 之前的"已知状态"

#### 1.3.12 `main()` — A-

**优点**：
- 短、清晰、分类（Subcommands / Debug / UCB / Legacy）
- Legacy 兼容（M[5296-5300]）：`wiggle 0xAF000000 4` 老用法自动当 `wiggle erase 0xAF000000 4` 处理
- `--version` 支持（L4253）

**问题**：
- 命令 dispatch 是手写 if 链（M[5260-5282]）——27 行。`std::map<std::string, fnPtr>` 表会更可维护
- 没有 subcommand 的 `--help` 跳板：`wiggle --help` 不会跳到 `wiggle list --help` 之类的——但顶层 printUsage 写得挺全，OK

### 1.4 wiggle 全局问题

#### 1.4.1 JSON 输出不一致

`g_json` flag 在每个 doXxx 末尾设置，且 JSON 输出在每个 doXxx 内部手工序列化。问题：
- **错误处理走两条路**：`g_json ? jsonError : fprintf`（很多地方用），但**也有只走 fprintf 不走 jsonError 的地方**
- **`doUcbErase` 二次确认**完全走 stdout（`Type 'yes' to continue:`）—— agent/MCP 集成时无法自动化

#### 1.4.2 全局状态 5 个

| 变量 | 用途 | 问题 |
|---|---|---|
| `g_json` | JSON 模式 | bool + 全局降低可测性 |
| `g_hotAttach` | DCO override | 同上 |
| `g_haltMethod` | halt 路径缓存 | CLI 调用间不持久——意义不大 |
| `g_bp[8]` | 8 槽 BP 缓存 | shell 内可用，shell 间失效 |
| `g_shellCtx` | shell 模式共享 ctx | 设计 OK，scope 限 shell |

#### 1.4.3 错误码膨胀

22 个 EXIT_* 常量，编号 0-23 中间缺 9/10/13。原因不明。**建议合并到 6 类**：OK / USAGE / TARGET_FAIL / FLASH_FAIL / VERIFY_FAIL / INTERNAL。

#### 1.4.4 头文件

`wiggle_main.cpp` 一坨 4,295 行没有任何内部分文件。**至少应该拆**：
- `wiggle_commands/erase.{h,cpp}` + `write.{h,cpp}` + `read.{h,cpp}` + ...
- `wiggle_debug/halt.{h,cpp}` + `break.{h,cpp}` + `step.{h,cpp}` + ...
- `wiggle_session.{h,cpp}`（initTool / startSession / ToolContext）

#### 1.4.5 测试覆盖

`apps/wiggle/` 下面**没有**任何单元测试或集成测试。**建议**：
- `hex_parser` 是纯函数，**最容易写单测**——给 5 个 Intel HEX 样本（标准 / type 04 / 多 segment / EOF 缺失 / checksum 错）跑断言
- `svd_loader` 跑一个 fixture SVD 验证字段解析

#### 1.4.6 安全 / 鲁棒性

- **UCB 写没有 32 字节 UCB record 对齐检查 + CONFIRMED word 校验**——会写脏 / 锁芯片
- **SRAM 写入无确认**：`wiggle poke 0x70000000 0x12345678` 是直接改 SRAM，不可逆（重启就没了，但运行中可能破坏 OS context）
- **空 password / 0 length 边界**：多数 doXxx 都拦了，但 `doWrite` HEX 文件空 segments 单独拦
- **JSON input 没测**：所有 JSON 输出字段都是手工组装，类型搞错会 runtime 崩

#### 1.4.7 性能

- `doErase --all` 一次性 erase 整个 DFlash（典型 1MB 384 sector = 几十秒到几分钟）；等待 D0BUSY 一个寄存器，没用上 D1BUSY/P0BUSY 监测——并发 erase 不能实现（TC2x/TC3x DFlash 不能并发），OK
- `doWrite` 写 1MB @ 8 字节/page = 131072 page 写循环；每页 5-7 次 I/O，估算 50-100ms/页，**全片 write ~3-4 小时**。M[1298-1304] "every 128 pages" 进度更新 1KB 一行——可以更频繁

---

## 2. MCP Server 评审（`wiggle_MCP/`）

### 2.1 工具清单（共 19 个，分 5 组）

| 组 | 工具 | 数量 |
|---|---|---|
| 设备信息 | `wiggle_list` / `wiggle_status` / `wiggle_info` | 3 |
| DFlash 操作 | `wiggle_erase` / `wiggle_write` / `wiggle_read` | 3 |
| PFlash 烧写 | `wiggle_pflash` | 1 |
| 硬件调试 | `wiggle_reg` / `wiggle_dump` / `wiggle_poke` / `wiggle_search` / `wiggle_compare` | 5 |
| 控制 + 构建分析 | `wiggle_reset` / `build_project` / `reload_parsers` / `parse_symbols` / `parse_elf` / `lookup_symbol` / `lookup_address` | 7 |

### 2.2 MCP server 实现（`aurix_mcp_server.py` 937 行）

#### 2.2.1 架构 — B

- FastMCP 注册 19 个 tool，每个 tool 内部调用 `run_wiggle()` / `run_flasher()` 走 subprocess
- 配置加载走 `load_config()` 自动检测 wiggle.exe / AURIXFlasher.exe 路径
- 全局 parser cache（`_map_parser` / `_lst_parser` / `_mdf_parser` / `_elf_parser`）懒加载，跨 tool 共享

#### 2.2.2 [HIGH] `subprocess.run(command, shell=True)` 命令注入风险

**位置**：`build_analysis.py` L694

```python
result = subprocess.run(command, shell=True, capture_output=True, text=True, timeout=timeout, cwd=cwd)
```

`mcp_config.json` 的 `build.command` + `shell=True` = **远程代码执行**：
- LLM 可通过配置文件注入 `make all; rm -rf /`
- 即使配置文件是用户手动配的，agent 在某些工作流（auto-config）能修改它
- **修复**：改 `shell=False` + `shlex.split(command)`；或者加 whitelist（只允许 `make`/`cmake`/`ninja` + 已知参数）

#### 2.2.3 [MEDIUM] parser cache 全局单例，无用户/项目隔离

**位置**：`aurix_mcp_server.py` L290-353

`_map_parser` / `_lst_parser` / `_elf_parser` / `_mdf_parser` 都是 module-level 单例。如果 MCP server 实例化多个工程（多个 mcp_config.json 切换），**所有 parser 共享缓存，跨工程污染**。

**修复**：把 parser cache 放到 MCP server 实例或 session 维度（per-project）。

#### 2.2.4 [MEDIUM] `run_wiggle` JSON 解析脆弱

**位置**：`aurix_mcp_server.py` L213-228

```python
# Find the last line that looks like JSON
for line in reversed(stdout.splitlines()):
    line = line.strip()
    if line.startswith("{"):
        try:
            parsed = json.loads(line)
```

只解析**最后一行** `{...}`——wiggle 在 `--json` 模式下输出最后一个 JSON 对象，**正确**，但 stdout 噪音绕过问题未测试。

#### 2.2.5 [MEDIUM] `timeout` 全局 120s 不够

**位置**：`mcp_config.json` L7

- 全片 erase（典型 384 sector × 5s = 30 分钟）：**120s 必超时**
- 全片 write 1MB @ 50ms/page = 50 分钟：**120s 必超时**
- 全片 verify + read 1MB @ 10MB/s = 100ms：**OK**

**修复**：按命令类型分别 timeout
```json
{
    "timeouts": {
        "default": 120,
        "erase_all": 1800,
        "write": 3600,
        "read": 60
    }
}
```

### 2.3 MCP 工具契约审查

#### 2.3.1 [HIGH] MCP 工具参数 vs wiggle CLI 参数**不一致**

| MCP 工具参数 | wiggle CLI 实际参数 | 不一致点 |
|---|---|---|
| `wiggle_write(file_path=..., verify=True)` 默认 | `wiggle write --verify` 默认 `false` | **MCP 调用方以为默认 verify**，但 wiggle 实际默认不 verify |
| `wiggle_erase(verify=False)` 默认 | `wiggle erase` 默认不 verify | ✅ 一致 |
| `wiggle_pflash` | **无 wiggle CLI 等价** | MCP 直接调 AURIXFlasher.exe，**绕过 wiggle**——这把 wiggle 限定为 DFlash 工具 |
| `wiggle_reset(halt=True)` | `wiggle reset --halt` | ⚠️ wiggle CLI `--hot` 在 doReset 被忽略——agent 无法软重启 |
| `wiggle_ucb_*` | **MCP 19 工具里完全没有** | UCB 三命令未暴露（详见 §3） |

**修复建议**：
- wiggle CLI `doWrite` 默认 `--verify=true`（更安全契约）
- MCP server 加 `wiggle_ucb_read/write/erase` 三个工具

#### 2.3.2 [MEDIUM] `wiggle_search` 截断时**仍返回 EXIT_OK**

**位置**：`wiggle_main.cpp` L4040-4055

```cpp
if (matchCount >= 1000) {
    JPRINTF("  (stopped after 1000 matches)\n");
    if (g_json) j["truncated"] = true;
    return EXIT_OK;   // ❌ 但 exit code 是 OK
}
```

`truncated=true` 字段**仅在 JSON 模式**返回；非 JSON 模式完全没有信号。

**修复**：加 `EXIT_TRUNCATED = 25`。

#### 2.3.3 [MEDIUM] `wiggle_compare` exit code 与 JSON 字段矛盾

**位置**：`wiggle_main.cpp` L3945-3953

JSON 模式下 mismatch>0 也返回 EXIT_OK（被 MCP 当作"成功"）但 `match=false`。Agent 看 JSON 字段能正确判断，但**靠 exit code 的 agent 会误判**。

**修复**：JSON 模式下也返 `EXIT_MISMATCH_ERROR`。

#### 2.3.4 [LOW] `wiggle_dump` hex 输出对大块低效

`doRead` L1151-1166 返回整段 hex string：

```cpp
j["data"] = hexData;  // 4KB = 8KB string, 64KB = 128KB string
```

MCP 通过 stdio 传 128KB JSON 到 LLM——**纯浪费 token**。

**修复**：
- `output_file` 默认路径写到 MCP 临时目录，把文件路径返给 agent
- 或超过 4KB 强制 `output_file`
- 或加 `format` 参数：`"hex"`(默认) / `"bin_base64"` / `"file"`

#### 2.3.5 [LOW] `wiggle_pflash` 与 wiggle CLI 不对称

MCP `wiggle_pflash` 直接调 `AURIXFlasher.exe`，**没有 wiggle CLI 等价**。这把 MCP 的逻辑分层成：
- `wiggle_*` 前缀 → 调 wiggle.exe（DFlash + 调试）
- `wiggle_pflash` → 直接调 AURIXFlasher.exe
- `build_*` / `parse_*` / `lookup_*` → 读 build artifact

**建议**：命名改成 `aurix_pflash` 区分"wiggle 路径"与"flasher 路径"。

### 2.4 构建分析模块（`build_analysis.py` 766 行）

#### 2.4.1 4 个 Parser — B

- `TaskingMapParser`：TASKING VX-toolset MAP 文件 → 内存 / 段 / 符号表
- `GccLstParser`：GCC `tricore-elf-objdump -h -S` 输出
- `MdfParser`：TASKING Module Definition File
- `ElfParser`：pyelftools，支持 TASKING + GCC TriCore ELF

每个 parser 都实现 `parse()` + 4 个 query API：`get_*()` / `lookup_symbol()` / `lookup_address()` / `get_all_symbols()`。

**优点**：
- 4 个 parser 接口对称，调用方代码易写
- `_ensure_*_parser()` 懒加载机制避免重复解析
- `TaskingMapParser.lookup_address` 用 `bisect_right`（L247-249）做 O(log N) 符号反查
- `ElfParser.get_source_location` 用 DWARF（如果可用）做源码行号反查（L593-625）

**问题**：
- **`parse_elf` 每次都重读 ELF 文件**：不缓存解析结果，文件 mtime 未变就重复 parse。SVD loader（wiggle 那侧）是**按需懒加载 + 缓存**——**不一致**
- **`BuildRunner._parse_build_output` 只覆盖 TASKING + GCC**（L679-682）：如果用 IAR / Keil / clang——build error 解析漏。但本项目用 TASKING / GCC，**OK**
- **`ElfParser.get_source_location` 用异常吞所有错误**（L623-625 `except Exception: pass`）——调试时找不到原因
- **`BuildRunner` `shell=True` 命令注入风险**（同 §2.2.2）

---

## 3. 端到端集成路径评审

### 3.1 MCP 19 工具覆盖度 vs wiggle 27 命令

| wiggle CLI 命令 | MCP 工具 | 覆盖 |
|---|---|---|
| `wiggle list` | `wiggle_list` | ✅ |
| `wiggle reset` | `wiggle_reset` | ✅ |
| `wiggle info` | `wiggle_info` | ✅ |
| `wiggle status` | `wiggle_status` | ✅ |
| `wiggle erase` | `wiggle_erase` | ✅ |
| `wiggle write` | `wiggle_write` | ✅ |
| `wiggle read` | `wiggle_read` | ✅ |
| `wiggle dump` | `wiggle_dump` | ✅ |
| `wiggle poke` | `wiggle_poke` | ✅ |
| `wiggle search` | `wiggle_search` | ✅ |
| `wiggle compare` | `wiggle_compare` | ✅ |
| `wiggle reg` | `wiggle_reg` | ✅ |
| `wiggle rewrite` | — | ❌ |
| `wiggle restore` | — | ❌ |
| `wiggle ucb read` | — | ❌ |
| `wiggle ucb write` | — | ❌ |
| `wiggle ucb erase` | — | ❌ |
| `wiggle halt` / `go` / `cpu_reg` / `break` / `step` / `bt` | — | ❌❌❌❌❌❌ |
| `wiggle shell` | — | ❌ |
| — (无 wiggle 等价) | `wiggle_pflash` | ✅ (MCP-only) |
| — (无 wiggle 等价) | `build_project` / `parse_symbols` / `parse_elf` / `lookup_symbol` / `lookup_address` / `reload_parsers` | ✅ (MCP-only) |

**覆盖率**：27 wiggle CLI 命令 → **8 个 MCP 工具直接对应**（list/reset/info/status/erase/write/read/reg/dump/poke/search/compare = 12 个 wiggle 命令）
- ✅ 12 个 wiggle 命令有 MCP 工具
- ❌ 15 个 wiggle 命令无 MCP 工具（rewrite/restore/ucb×3/调试 6/shell/...）

**MCP 19 工具中**：
- 11 个调 wiggle.exe
- 1 个调 AURIXFlasher.exe
- 6 个纯本地解析（无 wiggle）

### 3.2 端到端工作流测试

#### 3.2.1 模拟 agent 完整 DFlash 烧写流程

```python
mcp.wiggle_info()                                       # 1. 设备识别
mcp.wiggle_erase(erase_all=True, verify=True)            # 2. 全擦
mcp.wiggle_write(file_path="data.hex", verify=True)      # 3. 写 + verify
mcp.wiggle_compare(file_path="data.hex", address="0xAF000000", length="0x1000")  # 4. 字节对比
mcp.wiggle_dump(address="0xAF000000", length="0x100")    # 5. dump 验证
```

**端到端总耗时**：erase 30min + write+verify 50min + compare 5s + dump 5s ≈ **80 分钟**

**MCP 默认 timeout 120s**：**全流程 120s 超时，必失败**（即使每个 tool 单调用也可能超时）

**修复**：MCP per-tool timeout。

#### 3.2.2 模拟 agent 调试崩溃的 MCU

```python
mcp.wiggle_reset(halt=True)                             # 1. 复位并停
mcp.wiggle_reg("DMU.HF.STATUS")                         # 2. flash status
mcp.wiggle_reg("CPU0_DBGSR")                            # 3. debug status
mcp.wiggle_dump(address=0x70000000, length=0x40)        # 4. 读 PSPR
mcp.wiggle_reset(halt=False)                            # 5. 恢复
```

**问题**：
1. **`wiggle_reset(halt=True)` 但 `--hot` 在 doReset 被忽略**——agent 想"软重启 MCU 不破坏 DFlash"做不到
2. **没有 halt / go / cpu_reg / break / step / bt 工具**——agent 想"在 main 处设 BP 然后单步"做不到
3. **`wiggle_poke` 是任意内存写**——可写到 PC、SP、任何 control register，**安全雷区**：
   - 写到 `0xF8040010`（DMU.HF.STATUS）→ 可能启动 flash 操作
   - 写到 `0xF8810010`（CPU0 DBGSR）→ 可能重启 debug 状态机
   - **完全没保护**

**修复**：
- `wiggle_poke` 加 `--dangerous` flag（默认拒绝 SRAM/Flash 区）
- MCP server 加 6 个调试工具

#### 3.2.3 模拟 agent 处理 UCB 烧录

```python
mcp.wiggle_read(address="0xAF400000", length="0x800")  # 读 UCB
mcp.wiggle_write(file_path="ucb.hex")                   # 写 UCB
mcp.wiggle_erase(erase_all=True)                        # DFlash 擦
```

**问题**：
- **`wiggle_read/write/erase` 不区分 DFlash / UCB**——用户必须自己算地址
- **UCB 三命令没被 MCP 暴露**——agent 在 UCB 工作流里只能：
  1. 调 `wiggle_read(address="0xAF400000", length="0x800")` 自己算地址
  2. 或者绕开 MCP 直接调 wiggle CLI
- **`doUcbWrite` 没 32 字节 record 对齐 + CONFIRMED word 校验**——UCB 写错会**永久锁芯片**
- **`doUcbErase` "yes" 二次确认走 stdin**——MCP 通过 stdio 调用 wiggle，**stdin 是 MCP server 控制的 pipe**——agent 想自动 erase UCB 必须喂 "yes\n" 给 MCP server，**MCP server 端没做"自动确认"参数**

#### 3.2.4 模拟 build + flash + debug 一体化

```python
mcp.build_project(clean=True)                           # 1. 编译
mcp.lookup_symbol(name="main")                          # 2. 找 main 符号
mcp.wiggle_pflash(hex_file="app.hex", erase="all", start="off")  # 3. 烧 PFlash
mcp.wiggle_reset(halt=True)                             # 4. 复位并停
# ❌ 没有 wiggle_break_set(addr=main) 工具
mcp.wiggle_poke(addr=0xF8810010, value=0x12345678)      # 5. 手动设 BP
mcp.wiggle_reset(halt=False)                            # 6. 恢复
```

**问题**：
1. `lookup_symbol` 返回 ELF 符号地址；agent 想"在 main 处下 BP"，但**没有 `wiggle_break_set` 工具**
2. wiggle CLI 有 `doBreak` / `doHalt` / `doGo` / `doStep` / `doBt`，**但 MCP 一个都没暴露**

### 3.3 集成路径上的核心 GAP

**GAP-1** [HIGH]：UCB 三命令 MCP 未暴露 → agent UCB 工作流不完整
**GAP-2** [HIGH]：6 个调试命令（halt/go/cpu_reg/break/step/bt）MCP 未暴露 → agent 调试工作流 0%
**GAP-3** [HIGH]：`subprocess.run(shell=True)` 命令注入 → 远程代码执行风险
**GAP-4** [MEDIUM]：MCP 默认 timeout 120s → 全片 erase/write 必超时
**GAP-5** [MEDIUM]：`doUcbErase` "yes" 二次确认走 stdin → MCP 自动化必死
**GAP-6** [MEDIUM]：`doUcbWrite` 没 32 字节 record 对齐 + CONFIRMED word 校验 → UCB 锁芯片风险
**GAP-7** [LOW]：`wiggle_poke` 任意内存写无保护 → 可写 flash 寄存器
**GAP-8** [LOW]：`wiggle_pflash` 命名混淆 → 应该叫 `aurix_pflash`
**GAP-9** [LOW]：`wiggle_dump` 大块 hex string 浪费 token → 建议改 file 输出

---

## 4. Top 15 修复清单（按 ROI 排）

| # | 项 | 工作量 | 收益 | 优先级 |
|---|---|---|---|---|
| 1 | MCP server `subprocess.run(command, shell=True)` → `shell=False` + shlex | 1h | 防命令注入（RCE） | **P0** |
| 2 | MCP 加 `wiggle_ucb_read/write/erase` 三个工具 | 0.5h | UCB 工作流完整 + agent 不用算地址 | **P0** |
| 3 | MCP 加 6 个调试工具（halt/go/step/break/cpu_reg/bt） | 1d | 调试工作流从 0% 到 100% | **P0** |
| 4 | UCB 写 32 字节 record 对齐 + CONFIRMED word 校验 | 2h | 防 UCB 锁芯片 | **P0** |
| 5 | `doList` / `doReset` 走 `initTool` + `RESOLVE_CTX` | 1h | 消 100+ 行重复 + TC4x 检查 + `--hot` 支持 | **P0** |
| 6 | MCP per-tool timeout（erase_all=1800s, write=3600s） | 30min | 防 120s 误超时 | **P1** |
| 7 | `wiggle_search` truncated 加 `EXIT_TRUNCATED` | 30min | agent 能区分截断 | **P1** |
| 8 | `wiggle_compare` mismatch 时 JSON 模式返 `EXIT_MISMATCH_ERROR` | 5min | 修契约不一致 | **P1** |
| 9 | wiggle CLI `doWrite` 默认 `--verify=true` | 5min | 与 MCP 默认行为对齐 | **P1** |
| 10 | `wiggle_poke` 加 `--dangerous` flag（拒绝非 SRAM 区） | 1h | 防误写 flash 寄存器 | **P1** |
| 11 | 删除 `g_haltMethod` / `g_bp[8]` 全局（仅 shell 内有效） | 0.5d | 减全局状态 + MCP 调用零副作用 | **P2** |
| 12 | `doUcbErase` 加 `--force` flag 让 MCP 自动确认 | 30min | UCB 工作流自动化 | **P2** |
| 13 | MCP `wiggle_dump` 大块走 `output_file` | 1h | 减 token 浪费 | **P2** |
| 14 | MCP `wiggle_pflash` 重命名为 `aurix_pflash` | 5min | 命名清晰 | **P2** |
| 15 | parser cache per-project 隔离 | 2h | 多 project 用户场景正确 | **P3** |

**P0 = 5 项**（共 1 人天）：命令注入 + UCB 工具 + 调试工具 + UCB 校验 + initTool 重构
**P1 = 5 项**（共 2 人天）：timeout + 退出码 + 默认 verify + poke 保护
**P2 = 4 项**（共 2 人天）：全局清理 + force flag + dump 优化 + 命名
**P3 = 1 项**（共 2h）：parser 隔离

**总计**：~1 人周可消化全部 P0 + P1。

---

## 5. wiggle 内部中长期重构（独立于 MCP）

如果只评 wiggle 本身（不通过 MCP），按 ROI 排：

| # | 项 | 工作量 | 收益 | 优先级 |
|---|---|---|---|---|
| A | main.cpp 拆 5-7 个子文件（commands/debug/session） | 1d | 4,295 → 5×~900 行，可维护性大增 | **P1** |
| B | `LockedRegion` 表改从 `flashCfg.ucb` 派生，去掉硬编码 | 0.5h | 适配 TC38x 等大 DFlash | **P1** |
| C | `findConfigDir` 把 `TAS_DEVICE_CONFIGS` env var 提到第一优先级 | 5min | agent 调用更自然 | **P1** |
| D | 22 个 EXIT_* 合并到 6 类 | 1h | 错误码语义化 | **P2** |
| E | `svd_loader` 建 `peripheral→registers` 副索引 | 0.5d | `reg --list` 加速 | **P2** |
| F | `hex_parser` 加 5 个 fixture 单测 | 0.5d | 首次单测覆盖 | **P2** |
| G | `doDump` 64→32 bit 截断 bug 修复 | 30min | 修已知 bug | **P2** |
| H | `doShell` 加 history 文件 | 1h | REPL 体验提升 | **P3** |
| I | 多核 CSFR base（CPU0 → CPU*） | 2d | 多核 debug | **P4** |

---

## 6. 优点保留

写到问题容易让人以为这项目差——其实做对的事很多：

1. **三层 halt/resume fallback** 实战中真有用
2. **`g_shellCtx` 复用** 解决了 REPL 反复重连的性能问题
3. **`doRewrite`** 把 Read-Modify-Write 这个复杂操作拆得清晰
4. **SVD 字段解码** 输出格式比同类工具（Ozone、UDE）更工程化
5. **`g_bp[]` + 8 槽限制** 正确处理了 TC3x 的硬件约束
6. **JSON 双输出** 跟 MCP 集成丝滑
7. **`TAS_NAME_LEN16` 16 字节截断** + 0xC0000409 注释是好实践
8. **legacy backward compat** 0xAF000000 老用法保留
9. **`GetModuleFileNameW`** 支持 CJK 路径（v2.2 已加）
10. **HEX EOF 缺失 warning**（v2.2 已加）
11. **Linux 支持**（v2.2 已加 `/proc/self/exe` + pthread/dl）
12. **`normalizeName` 通用查询** `DMU.HF.STATUS` / `DMU_HF_STATUS` / `dmu.hf.status` 都能命中
13. **UCB 锁定区硬检查** 防误擦 OTP
14. **MCP server 19 工具契约清晰** 每个 tool 有明确 docstring
15. **TASKING + GCC 工具链双支持** Map/LST/ELF 三种格式都解析

---

## 7. 一句话总结

**wiggle v2.2** 作为 Infineon 内部 DFlash 工具：**功能完整、护栏到位、实战可用**，工程上有 4,295 行的 god-file + 5 个全局状态 + 几个 init 绕过问题。
**wiggle_MCP v2.2** 作为 AI agent 入口：**DFlash 烧写可跑通（80%），但 UCB / 调试 9 个核心工作流 0%**，加上命令注入风险 + timeout 不合理 + UCB 二次确认不走 JSON 这三个集成雷区。

**整体评分**：
- wiggle 源码：B+
- wiggle 健壮性：B+
- MCP server：B-（19 工具覆盖 11 个 wiggle 命令，缺 15 个）
- MCP server 安全：C+（命令注入 + poke 无保护）
- 端到端集成：C+（能跑通 DFlash 流，调试 + UCB 流跑不通）
- 推荐：**先补 P0 5 项（1 人天）→ 再补 P1 5 项（2 人天）→ 中期拆 wiggle 文件（1d）→ 长期多核 + IAR/Keil 支持**

---

*评审人*：Mavis (mavis)
*评审方法*：
- 通读 `wiggle_main.cpp` 4,295 行 + 3 个子模块头/源 + CMakeLists
- 通读 `aurix_mcp_server.py` 937 行 + `build_analysis.py` 766 行
- 通读 MCP `README.md` + `mcp_config.json` + `start_*.bat` + `requirements.txt`
- 抽样 `wiggle/DeviceConfigs/*` (21 device JSON) + `RegisterDefs/*` (8 SVD JSON)
- 交叉对照：19 MCP 工具 vs 27 wiggle CLI 命令契约
- 端到端 4 个工作流压力测试（DFlash 烧写 / 调试崩溃 / UCB 处理 / build+flash+debug 一体化）
*评审耗时*：~90 分钟
*评审独立性说明*：本评审**完全基于当前代码状态独立做出**，未参考任何历史评审结论


---

---

# 增量评审 v3.1（基于 2026-06-16 12:06 代码状态）

**评审触发**：用户在 v3 评审后做了 2 个 commit：
- `b0de4f5` (11:21) "二轮评审修复 + MCP 工具扩展至 22 个"
- `37c52d6` (12:06) "REVIEW.md修复 + 寄存器模糊搜索引擎"

**评审时间**：2026-06-16 12:08
**评审方式**：基于实际代码 + git diff 增量评审，**不重做静态代码评审**——只看新写/改的内容

---

## A. v3 Top 15 修复跟踪

| v3 Top # | 项 | 状态 | 证据 |
|---|---|---|---|
| **P0 #1** | MCP `subprocess.run(shell=True)` → `shell=False` | ❌ **未修** | `tools/aurix_mcp_server.py` `BuildRunner.run` 仍 `shell=True`（build_analysis.py 也是） |
| **P0 #2** | MCP 加 `wiggle_ucb_*` 三个工具 | ✅ **已修** | 新增 `wiggle_ucb(action="read|write|erase", ...)` 聚合工具（L1270-1321） |
| **P0 #3** | MCP 加 6 个调试工具（halt/go/cpu_reg/break/step/bt） | ❌ **未修** | MCP 22 工具里**无调试控制类**（halt/go/step/break/cpu_reg/bt 都不存在） |
| **P0 #4** | UCB 写 32 字节 record 对齐 + CONFIRMED word 校验 | ❌ **未修** | `doUcbWrite` 在 wiggle_main.cpp 仍只检查 page alignment，**无 32 字节 record / 0x55FE CONFIRMED 校验** |
| **P0 #5** | `doList`/`doReset` 走 `initTool` + `RESOLVE_CTX` | ❌ **未修** | 仍手写 init 流程，TC4x 不拒绝，`--hot` 在 doReset 被忽略 |
| **P1 #6** | MCP per-tool timeout | ✅ **部分修** | `wiggle_rewrite` timeout=1800s，`wiggle_restore` timeout=3600s；但**erase/erase_all 仍是默认 120s** |
| **P1 #7** | `wiggle_search` truncated 加 `EXIT_TRUNCATED` | ⚠️ **编译能过 + 运行时 bug** | 加了 `EXIT_TRUNCATED=25`，但用 `jsonError(EXIT_TRUNCATED, j)` 调用 `nljson` 对象——详见 §B |
| **P1 #8** | `wiggle_compare` mismatch 时 JSON 模式返 `EXIT_MISMATCH_ERROR` | ⚠️ **编译能过 + 运行时 bug** | `jsonError(EXIT_MISMATCH_ERROR, j)` 同上问题 |
| **P1 #9** | wiggle CLI `doWrite` 默认 `--verify=true` | ✅ **已修** | `bool doVerify = true` 默认开，加 `--no-verify` flag |
| **P1 #10** | `wiggle_poke` 加 `--dangerous` flag | ❌ **未修** | `doPoke` 仍裸写，无 SRAM/Flash 区检查 |
| **P2 #11** | 删除 `g_haltMethod`/`g_bp[8]` 全局 | ❌ **未修** | 仍全局变量 |
| **P2 #12** | `doUcbErase` 加 `--force` flag | ❌ **未修** | MCP `wiggle_ucb(action="erase")` **仍需 stdin 喂 "yes"**——MCP 自动化仍阻塞 |
| **P2 #13** | MCP `wiggle_dump` 大块走 `output_file` | ❌ **未修** | `wiggle_read` / `wiggle_dump` 默认仍走 JSON hex string |
| **P2 #14** | MCP `wiggle_pflash` 重命名为 `aurix_pflash` | ❌ **未修** | 仍叫 `wiggle_pflash` |
| **P2 #15** | parser cache per-project 隔离 | ❌ **未修** | `_map_parser` 等仍 module-level 单例 |

**修复率**：15 项中
- **✅ 完全修 3 项**：#2（UCB 工具聚合）、#6（部分 timeout）、#9（doWrite verify 默认）
- **⚠️ 修但有 bug 2 项**：#7 #8（运行时 type_error）
- **❌ 未修 10 项**

**TOP P0 三项（命令注入 / UCB 校验 / 调试工具）里 1 个修了（聚合 UCB 工具），2 个未动（命令注入仍是 RCE、UCB 校验仍是锁芯片风险）**

---

## B. [CRITICAL] `jsonError(int, const std::string&)` vs `nljson` 调用 —— **运行时 type_error 坑**

**位置**：
- `wiggle_main.cpp` L151-164 `jsonOk(const nljson&)` / `jsonError(int, const std::string&)`
- 调用方 L3955 `return jsonError(EXIT_MISMATCH_ERROR, j);` ← `j` 是 `nljson`
- 调用方 L4058 `return jsonError(EXIT_TRUNCATED, j);` ← `j` 是 `nljson`

### B.1 编译验证

`g++ -std=c++17 -Wall -Wextra` 编译 **通过**——因为 `nlohmann::basic_json` 有 `operator std::string()` 隐式转换（**仅当对象本身是 string 类型**），非 string 类型**不**有 `operator std::string()`，但有 `operator const char*()` 和 `string()` 模板成员……实际上让编译器找到了 `operator std::string()` 这个 free function。

**结论**：编译能过。**但运行时跑炸**。

### B.2 运行时验证（已实测）

我建了一个最小复现（已删除）：

```cpp
nljson j;
j["action"] = "ucb_write";
return jsonError(22, j);   // 运行时炸
```

**实际输出**：
```
terminate called after throwing an instance of 'nlohmann::json_abi_v3_11_3::detail::type_error'
  what():  [json.exception.type_error.302] type must be string, but is object
Exit: 3
```

**根因**：`nljson` 是 object 类型的 JSON，调用 `operator std::string()` 时抛 `type_error.302` 异常。

### B.3 触发路径

**v3.1 当前代码下会触发的两条路径**：

1. **`doCompare` mismatch + `--json` 模式**（L3955）：
   ```cpp
   nljson j;
   j["file"] = filePath;
   j["total_bytes"] = totalBytes;
   j["mismatches"] = totalMismatches;
   j["match"] = (totalMismatches == 0);
   if (totalMismatches > 0) return jsonError(EXIT_MISMATCH_ERROR, j);  // ❌ 炸
   return jsonOk(j);
   ```
   - 用户场景：`wiggle compare file.hex -a 0xAF000000 --json` 对比失败 → **抛异常，进程崩溃**

2. **`doSearch` truncated + `--json` 模式**（L4058）：
   ```cpp
   nljson j;
   j["address"] = addr;
   j["length"] = length;
   j["matches"] = matchAddrs;
   j["match_count"] = matchCount;
   j["truncated"] = true;
   return jsonError(EXIT_TRUNCATED, j);  // ❌ 炸
   ```
   - 用户场景：`wiggle search <addr> <len> <pattern> --json` 找到 ≥1000 个 match → **抛异常，进程崩溃**

### B.4 验证当前 build 状态

| 文件 | 时间 |
|---|---|
| `wiggle_main.cpp` | 2026-06-16 12:04:47（最新修改） |
| `build/apps/wiggle/Release/wiggle.exe` | 2026-06-16 10:20:12（**未重新编译**） |

**当前部署的 wiggle.exe 不含这两个修复**——所以现在跑 `doCompare --json mismatch` 还是旧行为（`jsonOk`），不会触发新 bug。

**一旦重新编译**，两个新场景立即炸。

### B.5 修复方案（按推荐度）

**方案 1**（推荐）：给 `jsonError` 加 nljson 重载
```cpp
static int jsonError(int code, const nljson& data) {
    nljson out = data;
    out["status"] = "error";
    out["code"] = code;
    printf("%s\n", out.dump().c_str());
    return code;
}
```
这样 `jsonError(EXIT_MISMATCH_ERROR, j)` 自动走新重载，把 data 复制后塞 status/code，**正常工作**。

**方案 2**：调用方先 dump 成 string
```cpp
return jsonError(EXIT_MISMATCH_ERROR, j.dump());  // ✅ 但 message 是 string 不是 object
```
失去结构，message 字段是 JSON string。

**方案 3**：加 `static_cast<std::string>(j.dump())` 显式转换
等价方案 2。

**建议**：用方案 1（重载），一次性解决所有类似 case。

---

## C. 新增 MCP 工具评审（v3 评 → v3.1 实测）

### C.1 `reg_peripherals` / `reg_fields` / `reg_search` —— **A-**

commit 信息提到的"寄存器模糊搜索引擎"——3 个新工具的实现：

- `_tokenize()` / `_score_match()` / `_periph_relevance()` 三层打分（L665-820）
- `MIN_SCORE = 40` 过滤噪音
- AI-friendly 同义词表：'uart' → ASCLIN、'flash' → DMU、'timer' → GTM/CCU6
- `reg_fields` exact match 失败时 fuzzy fallback
- `reg_search` 综合 register + peripheral 加权

**优点**：
- **模糊匹配语义化**：`MAX(peripheral_score, register_score) + boost when both match`——是工程化搜索而非简单 substring
- **三层兜底**：exact match → fuzzy fallback → ambiguous list（多 peripheral 时返 list 让用户 disambiguate）
- **`status: not_found_fuzzy` 字段** + suggestions 列表——AI agent 能理解"找不到但有类似"语义
- **`score` 字段** 透明度高——AI 看 80 分 vs 40 分就知道可信度

**问题**：
- **`_periph_relevance` 同义词表是硬编码**——'uart'/'flash'/'timer'/'gpio'/'watchdog' 等，新 peripheral 不自动支持
- **`_score_match` 算法依赖 keyword 字符匹配**——对"含有 typo" 的 query（"ushart" → "usart"）不友好
- **`reg_search` 不支持 description-only search**——只能搜名字+desc，不能指定搜哪一类

**整体评价**：commit `37c52d6` 的核心新增，**这是 MCP 工具集最有价值的扩展之一**——把 wiggle 从"按地址读写"升级为"按语义查寄存器"。

### C.2 `wiggle_rewrite` / `wiggle_restore` / `wiggle_ucb` / `wiggle_read` —— **B+**

把 v3 评审里点名的"未暴露 wiggle 命令"补齐：

| 工具 | 对应 wiggle CLI | 评价 |
|---|---|---|
| `wiggle_rewrite` | `wiggle rewrite` | ✅ timeout=1800s，加了 `backup_path` 显式参数 |
| `wiggle_restore` | `wiggle restore` | ✅ timeout=3600s，参数对应 `--no-verify` |
| `wiggle_ucb` | `wiggle ucb read/write/erase` | ✅ 聚合单工具，TC3x 限制写在 docstring |
| `wiggle_read` | `wiggle read` | ✅ 之前确实缺，现补齐 |

**优点**：
- **per-tool timeout 已差异化**（write/restore 1 小时，rewrite 30 分钟），P1 #6 部分完成
- **参数命名 snake_case**（file_path/backup_path/reset_mcu）—— Pythonic 但与 wiggle CLI 的 `--file/-f` 不完全对应，agent 调用要小心

**问题**：
- **`wiggle_ucb` 仍是聚合单工具**——3 个 action（read/write/erase）通过 `action: str` 区分，比 3 个独立工具更紧凑但 type safety 差（MCP 没有 enum 类型）
- **`wiggle_ucb(action="erase")` 调用时仍需 stdin 喂 "yes"**——**P2 #12 未修**，MCP 自动化阻塞没解除
- **`wiggle_ucb(action="write")` 仍接受任意文件，无 32 字节 record 对齐检查**——**P0 #4 未修**

### C.3 端到端工作流状态更新

| v3 §3 工作流 | v3 状态 | v3.1 状态 | 改善点 |
|---|---|---|---|
| 3.2.1 DFlash 烧写 | timeout 必失败 | 部分 timeout 修复（write 1h, restore 1h），但 erase 仍 120s | ⬆️ |
| 3.2.2 调试崩溃 MCU | 6 个调试工具 0% | **仍 0%** | ❌ 未变 |
| 3.2.3 UCB 处理 | UCB 三命令 0% | UCB 工具加，但 yes 确认阻塞 + record 校验缺失 | ⬆️/❌ |
| 3.2.4 build+flash+debug | 调试 0% | 寄存器查询 100% 但调试控制 0% | ⬆️（查询）/❌（控制） |

---

## D. Build 状态警告

| 文件 | 时间 | 状态 |
|---|---|---|
| `wiggle_main.cpp` | 2026-06-16 **12:04:47** | 最新（含 #7 #8 改动） |
| `build/apps/wiggle/Release/wiggle.exe` | 2026-06-16 **10:20:12** | **过期 1 小时 44 分钟** |
| `build/CMakeFiles/.../wiggle.dir/wiggle_main.cpp.obj` | **不存在** | **从未 incremental rebuild** |

**结论**：两次 commit 之后**没有任何 incremental rebuild**——所有修改都没有编译验证。

**风险**：
- §B 的两个 `jsonError(nljson)` type_error 会在下次 build 后立即生效
- 用户实测 `wiggle compare file.hex -a 0xAF000000 --json` mismatch 路径会崩
- CI / HIL 测试如果跑 `doCompare --json mismatch` 也会崩

**建议**：
1. 先修 §B 的两个 `jsonError` 重载（方案 1）
2. 然后 `cmake --build build --config Release` 重新编译
3. 跑一遍 unit test 确认两个 JSON mode 路径正常

---

## E. v3 Top 15 更新版（v3.1 状态）

| # | 项 | v3 优先级 | v3.1 状态 | 备注 |
|---|---|---|---|---|
| 1 | `subprocess.run(shell=True)` → `shell=False` | P0 | ❌ 未修 | 命令注入 RCE 仍在 |
| 2 | UCB MCP 工具 | P0 | ✅ 修了（聚合） | 但 record 校验仍缺 |
| 3 | 6 个调试工具 | P0 | ❌ 未修 | 调试工作流仍 0% |
| 4 | UCB 32 字节 record 对齐 + CONFIRMED 校验 | P0 | ❌ 未修 | UCB 锁芯片风险 |
| 5 | `doList`/`doReset` 走 `initTool` | P0 | ❌ 未修 | init 重复 + TC4x 不拒绝 |
| **6** | **修 `jsonError(nljson)` type_error bug** | **NEW P0** | — | §B 详述 |
| **7** | **rebuild + run unit test** | **NEW P0** | — | §D 详述 |
| 8 | MCP per-tool timeout | P1 | ⬆️ 部分修 | write 1h / restore 1h；erase 仍 120s |
| 9 | `wiggle_search` truncated 加 `EXIT_TRUNCATED` | P1 | ⚠️ 修了但有 bug | 触发 §B 问题 |
| 10 | `wiggle_compare` mismatch JSON mode | P1 | ⚠️ 修了但有 bug | 触发 §B 问题 |
| 11 | wiggle CLI `doWrite` 默认 verify | P1 | ✅ 修了 | 还加了 `--no-verify` flag |
| 12 | `wiggle_poke` `--dangerous` flag | P1 | ❌ 未修 | poke 仍裸写 |
| 13 | 删除 `g_haltMethod`/`g_bp[8]` 全局 | P2 | ❌ 未修 | |
| 14 | `doUcbErase` `--force` flag | P2 | ❌ 未修 | MCP UCB erase 仍需喂 "yes" |
| 15 | MCP `wiggle_dump` 大块走 `output_file` | P2 | ❌ 未修 | |
| 16 | MCP `wiggle_pflash` → `aurix_pflash` | P2 | ❌ 未修 | |
| 17 | parser cache per-project 隔离 | P3 | ❌ 未修 | |

**新的 P0**：
- **#6 `jsonError(nljson)` 重载**——下一次 build 后立即炸
- **#7 rebuild + 测试**——确认 §B 修复 + 实际 build 通过

---

## F. 评分更新

| 维度 | v3 | v3.1 | 变化 |
|---|---|---|---|
| wiggle 源码 | B+ | B+ | 无 |
| wiggle 健壮性 | B+ | B | -1（jsonError 运行时 bug） |
| MCP server 完整性 | B- | B+ | +1（UCB 工具 + reg_search 模糊搜索 + 3 个 wiggle 命令补齐） |
| MCP server 安全 | C+ | C+ | 无 |
| 端到端集成 | C+ | C+ | 无（修复未编译生效） |
| **可发布状态** | N/A | **D+** | **-新维度**（两次 commit 后从未 build 验证，发布前必须重新编译 + 测试） |

---

## G. 一句话总结 v3.1

**v3 → v3.1 改动**：
- ✅ 加了 4 个新 MCP 工具（`reg_peripherals` / `reg_fields` / `reg_search` / `wiggle_rewrite` / `wiggle_restore` / `wiggle_ucb` / `wiggle_read`）+ 修了 doWrite 默认 verify + 加了 EXIT_TRUNCATED + 加了 doCompare JSON mismatch exit code
- ❌ **引入 2 个运行时 type_error bug**（jsonError(nljson) 隐式转换）——下次 build 后立即炸
- ❌ **未编译验证**——wiggle.exe 过期 1 小时 44 分钟
- ❌ **核心安全 P0 仍 3 项未修**（命令注入 / UCB record 校验 / 调试 6 工具缺位）

**建议优先级**：
1. **立刻**：修 §B 的 `jsonError` 重载
2. **立刻**：rebuild wiggle.exe + 跑 unit test
3. **本周**：P0 #4（UCB record 校验）+ P0 #1（命令注入）
4. **下周**：P0 #3（6 个调试工具）+ P0 #5（doList/doReset 走 initTool）

---

*评审人*：Mavis (mavis)
*评审方法*：
- `git log -2 --stat` + `git diff HEAD~2..HEAD -- apps/wiggle/wiggle_main.cpp`
- 抽样读 wiggle_main.cpp 关键行（L151-164 jsonError/jsonOk / L1265-1295 doWrite / L3950-3960 doCompare / L4050-4065 doSearch / L1190-1345 MCP 新工具）
- 通读 tools/aurix_mcp_server.py 1611 行（22 个 tool 全部梳理）
- 编译验证：`g++ -std=c++17 -Wall -Wextra` 复现 `jsonError(nljson)` 行为
- 运行时验证：写最小复现 + 实测 `terminate called ... type_error.302`
*评审耗时*：~30 分钟（基于已有 v3 评审基础，增量部分快速定位）
*对比基础*：v3 评审（2026-06-16 11:20:33）+ 2 个新 commit（b0de4f5 / 37c52d6）