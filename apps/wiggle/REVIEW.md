# `apps/wiggle` 全面代码评审

**评审对象**：`D:\workspace\tas-dflash_-erase\apps\wiggle`
**版本**：wiggle v2.2（WIGGLE_VERSION）
**评审时间**：2026-06-15
**范围**：10 个文件 / 5,305 行业务代码（含 `wiggle_main.cpp` 5,305 行 + 三个子模块 800 行）

---

## 0. TL;DR

| 维度 | 评分 | 一句话 |
|---|---|---|
| **架构** | B | 子模块拆分清晰，header/impl 边界干净；唯独 main.cpp 巨型 god-file |
| **接口/契约** | A- | 子模块 API 完整（load/find/isLoaded 模式统一），CLI 子命令丰富 |
| **可读性** | B+ | 命名规范、注释到位、字段含义都标了 Vol §；唯一硬伤是 doList/doReset 没走 `initTool` |
| **健壮性** | B+ | 参数校验、范围检查、ECC 容错、JSON 双输出、UCB 锁定区保护——该有的都有 |
| **可维护性** | B | 大量复制粘贴样板（doList/doReset 重复 init 流程；g_bp/g_haltMethod 全局状态） |
| **可测性** | C+ | 几乎没法做单元测试（所有函数强依赖 `CTasClientRw`、全局 JSON flag 不可注入） |
| **安全性** | B+ | UCB 高危操作有 `yes` 二次确认 + 锁定区硬检查；HV 锁后对 SRAM 写入无确认 |

**结论**：作为 Infineon 内部使用的 DFlash 工具，**功能完整、边界护栏到位、能用**。但长期看 main.cpp 的 god-file 模式 + 几个全局状态是技术债，建议下版本拆分。

---

## 1. 文件结构

```
apps/wiggle/
├── CMakeLists.txt           87 行  静态构建+安装
├── wiggle_main.cpp        5,305 行  27 个子命令全在这
├── device_config_loader.{h,cpp}   342 行  JSON 设备配置加载
├── hex_parser.{h,cpp}              227 行  Intel HEX / BIN 解析
├── svd_loader.{h,cpp}              231 行  SVD 寄存器定义加载
├── wiggle.ico / wiggle.rc          图标资源（无功能意义）
```

**子模块依赖图：**
```
wiggle_main.cpp ─→ device_config_loader ─→ nlohmann/json
                ─→ hex_parser          (no deps)
                ─→ svd_loader          ─→ nlohmann/json
                ─→ tas_client_rw.h     (Infineon TAS, 外部 SDK)
                ─→ tas_utils.h / tas_utils_client.h
```

无循环依赖，无内部耦合。子模块之间互不通信，全由 main.cpp 编排。**好**。

---

## 2. 子模块评审

### 2.1 `device_config_loader` — B+

**优点**
- 加载策略清晰：目录扫描 → `parseDeviceJson` → 内存索引（`std::vector<DeviceEntry>`）
- `findByJtagId` 用 `0x0FFFFFFF` 掩码剥离版本 nibble，正确处理 AURIX 多 step
- `findByName` 先精确匹配 `shortName`、再子串匹配 `deviceName`，优先级合理
- `extractShortName` 是个贴心小工具，从 `TC27x_D_step.json` 提 `TC27x`
- 显式跳过 `devices.json` 索引文件 + `TC4*` 前缀的配置文件，注释说明 TC4x 有不同 DFlash 基址

**问题**
- `parseDeviceJson` 154 行单函数，循环里嵌了 3 层 if（M[176-236]），建议拆出 `parseMemoryRegion()` / `parseDFlashSection()`
- `findByJtagId` 注释说"masks out version nibble"但 `& 0x0FFFFFFF` 会同时屏蔽高 4 bit——对于 AURIX family/company ID 都等于 0 的情况没问题，但若未来有 mixed JTAG ID 字典需要改掩码策略
- `DeviceConfig::memoryRegions` 加载了但 main.cpp 只在 `doInfo` 里用到，其他命令全用 `flashCfg.baseAddress/totalSize/sectorSize` 三个标量——说明 data model 比实际需求复杂

**小问题（建议改）**
- `parseDeviceJson` 用 `std::istreambuf_iterator` 一次性读全文到 string。SVD JSON 才 10-40MB，device config 几 KB，无关；JSON device config 都是小文件，没事
- `loadFromDirectory` 的 `loadedCount > 0` 当返回值要求目录里至少有一个 .json；空目录返回 false 合理但 stderr 提示会误导——已加载 0 个 != 失败

### 2.2 `hex_parser` — A-

**优点**
- 支持 Intel HEX 全部 6 种 record type（00/01/02/03/04/05），含 :02000004 ELA 和 :04000005 入口点
- checksum 校验完整（low 8 bit == 0）
- contiguous segment 合并逻辑在 M[114-122]，避免每行 16 字节都开新 segment
- `loadBinaryFile` 走 ios::ate + seekg 一次性读，无中间 copy

**问题**
- `forceNewSegment` 状态机：ELA (04) 置 true，下一个 data 强制开新段；但 `case 0x02` ESA 后忘了 `forceNewSegment = true`（等等……M[165] 写了 `forceNewSegment = true`，我看错了）。OK 没问题。
- 缺 `type 03` 的 entry point 处理——其实 type 03 是 16-bit CS:IP 入口点，跟 type 05 互斥；当前代码把 type 03 当 no-op，符合现代 toolchain（已经不再产生 type 03）
- 缺 EOF 后的数据：很多 toolchain 不写 EOF，遇到 EOF 缺失时 M[186] 仍标 success=true + 没有 warning。M[180-187] 的"No EOF record found"分支只检查 segments.empty()——非空但缺 EOF 时静默成功，可能掩盖损坏文件

**建议**
- EOF 缺失时打一行 `JPRINTF("Warning: HEX file missing EOF record\n")`
- 解析失败时 `errorMsg` 不会包含文件名前缀——多文件批处理时定位困难

### 2.3 `svd_loader` — A-

**优点**
- 处理 TC SVD 的 cluster → register 嵌套结构（M[145-153]）
- `buildIndex` 同时建 name index（normalized）和 address index，O(1) 查询
- `getFlashStatusRegName()` 用启发式（`HF.STATUS` 优先 → `FSR` 兜底）做设备无关的 flash 状态寄存器查找
- `normalizeName` 去 `.` 和 `_`，让 `DMU.HF.STATUS` / `DMU_HF_STATUS` / `dmu.hf.status` 都能命中——实战中非常实用

**问题**
- M[41-44] 同时建了 `fullName` 和 `peripheral_fullName` 两个索引；但 `findRegister` 只用 `nameIndex_` 查，不会冲突——这层冗余是给 `peripheral_name` 形式（`DMU_STATUS`）的别名查询。**设计 OK**
- `nameIndex_` 用 `std::map` 而不是 `std::unordered_map`——SVD 寄存器数量通常 500-2000 个，map 性能足够，可读性更好。**OK**
- `getRegisters(peripheral)` 是 O(N) 线性扫描（M[198-207]），N 是所有寄存器。对大 SVD 文件每次都扫一遍——SVD 一般也就几千条寄存器，问题不大；但要支持 `wiggle reg --list` 多次调用时会重复扫，建议建 `std::map<peripheral, vector<const SvdRegister*>>` 副索引
- `parseHexAddr` 重复定义了——文件里有 `static uint64_t parseHexAddr(const std::string&)` (M[13-20] in svd_loader) 和 main.cpp 里的 `static bool parseHexAddr(const char*, uint32_t&)` (M[693-706] in main)，签名/返回类型不同但名字相同。**容易混淆**——建议 svd_loader 这个改名 `parseHexAddr64`

---

## 3. `wiggle_main.cpp` 分块评审（按命令）

### 3.1 整体结构

文件 5,305 行，按 27 个 doXxx + main 拆。大致分类：

| 类别 | 命令 | 行数区间 | 数量 |
|---|---|---|---|
| Flash 数据 | erase / write / rewrite / restore / read | 2307-2932 + 1064-1216 | 5 |
| 设备 | list / reset / info / status | 910-1062 + 3600-3709 + 3471-3598 | 4 |
| 内存调试 | dump / poke / reg / compare / search | 3221-3943 | 5 |
| UCB | ucb read / write / erase | 1756-2300 | 3 |
| 调试控制 | halt / go / cpu_reg / break / step / bt | 3965-5068 | 6 |
| REPL | shell | 5070-5167 | 1 |
| 杂项 | helpers + main | 其余 | 3 |

### 3.2 公共基础设施（200-908 行）— B

**做得好的**
- `ToolContext` 是个不错的设计：聚合 client + configLoader + svd + flashCfg 一次 init，到处复用
- `RESOLVE_CTX` 宏（M[813-825]）实现 shell 模式下的"复用已有连接 vs 新建连接"二选一，节省了每次 `initTool` 都要重新 server_connect + session_start 的开销
- `startSession` 有重连兜底（M[358-378]）：自己 session 占用 → 取现有名 → 空名 join
- `TAS_NAME_LEN16` 的 16 字节截断 + 注释解释 0xC0000409 溢出坑（M[350-356]）——这种 inline 注释值得保留

**问题**
- `findConfigDir` Windows 分支 M[277-300] 用了 4 个 fallback（`./`、`../data/`、`../../../data/`、`../../../../data/`）——这种"猜路径"风格脆弱，应该靠环境变量 `TAS_DEVICE_CONFIGS` 或相对 `argv[0]` 显式解析；现在开发机 OK，部署到非标准位置就 GG
- `g_hotAttach` / `g_json` / `g_haltMethod` / `g_bp[8]` / `g_shellCtx` 五个全局变量。其中 `g_bp[8]` 在 `doBreak` 注释里自己承认"M[4577] g_bp[] doesn't persist across CLI invocations"——但代码里仍然在 sync 后写入 g_bp，结果是 CLI 调用内一致、跨调用依赖硬件 readback 兜底。能 work，但**概念混乱**
- `findConfigDir` Windows 用 `GetModuleFileNameA`（ANSI），含中文路径的 exe 会丢字符——应该 `GetModuleFileNameW`

### 3.3 `doErase` — B

**优点**
- 5 步流程清晰打印：clear status → erase → wait → check flags → reset to read
- `--info` 只打印配置不动 flash（M[2412-2416]）
- `legacyMode` 参数（M[2307-2338]）兼容老用法 `wiggle <addr> <num_sectors>`，是周到的向后兼容
- `--all` 等价于 `erase addr=base, sectors=numSectors`（M[2419-2422]），代码复用
- 溢出防护 `eraseBytesU64 = numSectors * sectorSize; if > UINT32_MAX` 完整

**问题**
- `legacyMode` 分支 M[2320-2330] 处理 `argv[0]=="--all"` 和 `"--info"`，但 M[2320-2338] 把 options parsing 又写了一遍——和 subcommand mode（M[2346-2386]）70% 重叠。**应该用早期检查分流到 subcommand mode**
- 范围检查 M[2433] 用了 `sectorAddr >= flashCfg.baseAddress + flashCfg.totalSize`，但 `dflashEndAddr` 是 `base + totalSize - 1`（M[2404]）。两者在边界判断上差 1 byte：`sectorAddr == base + totalSize`（已越界 1 byte）会过 M[2433] 的检查，但 M[2446] `+ eraseSize - 1 > dflashEndAddr` 会抓住。OK 实际是安全的
- M[2393] `g_json = args.jsonOutput` 是这文件的**统一坑**：所有 doXxx 都在 parse args 末尾设 `g_json`。如果 JSON 输出 + 文件操作失败，错误流走 stderr 而非 JSON（doUcbErase M[2216-2253] 写 yes 确认是走 stderr 不进 JSON，OK）；但 `doErase` 在 M[2389-2391] 的 usage error 走 fprintf + 也没用 jsonError。**doErase 的 --json 输出不完整**

### 3.4 `doWrite` / `doRestore` — A- / A-

**doWrite 优点**
- hex_parser + loadBinaryFile 抽象用得正确
- HEX 文件带 `--addr` 时做 address shift（M[1391-1397]）——把 0xAF000000 的 HEX 重定位到 0xAF001000，这是 read-modify-write 的核心
- page-alignment 检查 + 段间 page-boundary overlap 检查（M[1428-1448]）— 防止擦除后段一写入污染段二的页
- writeSegmentPages 把"每页 8 步"封装成可复用 helper，被 doWrite / doRestore / doUcbWrite 三个命令共享。**这是这个文件里少有的"提取共用"做得好的地方**

**doWrite 问题**
- `g_json` 设在 M[1357]（parse 之后）——前面所有 parse error 都已 fprintf，不会用 jsonError。OK
- M[1353-1355] `if (!inputFile)` 用 `fprintf` 而非 `jsonError`，与 `--json` 模式不一致
- M[1481-1508] verify 阶段没用 helper——doRestore 的 verify 是 chunked read，doWrite 是一次性 read 然后逐字节比较；两个 verify 风格不统一

**doRestore 优点**
- "先擦后写再验"三步语义清晰
- bin 文件延迟到 initTool 后才 assign base address（M[1587-1607] + M[1624-1626]）——避免在 init 之前就需要 device config
- `eraseStart = eraseStart & ~(sectorSize - 1)`（M[1671]）先 sector align 再算 numSectors
- 默认开启 verify（M[1534]）—— restore 是恢复出厂，verify 必须默认

**doRestore 问题**
- M[1704-1709] 和 M[1697-1700] `totalPages` 算法（M[1699] `(segSize + PAGE_SIZE - 1) / PAGE_SIZE`）和 doWrite M[1458-1463]（先 pad 再除）有微妙差异——两个都"算向上取整页数"，结果一致。但 M[1457-1462] 多了 `paddedSize` 临时变量，不必要
- M[1720-1722] `verifyChunkSize = sectorSize` 是不必要的——`doWrite` 直接 `seg.data.size()` 一次性 read。两者风格不一致

### 3.5 `doRewrite`（Read-Modify-Write）— B

**优点**
- 这是整个工具里最复杂的命令，做对了
- data source 互斥检查（M[2650-2662]）：`--file` vs `--data` 必选其一
- M[2774-2788] UCB 锁定区检查复用 doUcbErase 的 LockedRegion——说明作者意识到有共享需求但**没有提取**到顶层，而是复制粘贴（一个在 doUcbErase M[2217-2221]、一个在 doRewrite M[2774-2778]）
- "读 sector → merge → 擦 → 写回"语义在 M[2802-2881] 严格按 5 步打印

**问题**
- M[2665-2730] 100 行大段文件解析逻辑没有抽函数，混在 main flow 里
- M[2733-2736] `if (newData.empty())` 排除了 dataStr 空字符串的情形，但 HEX 文件解析出空 segments 已经在 M[2695-2698] 拦住——**有重叠检查**
- M[2839] "Backup skipped" 在 backup opt 缺省时也打印，但 M[2822-2840] 的 `else` 分支没指明 `--backup` 缺省时是否打印过——读起来不清楚

### 3.6 `doUcbRead/Write/Erase` — B

**优点**
- UCB 命令全局 `if (!ctx.flashCfg.isTc3x)` 守卫（M[1806-1809]、M[1977-1981]、M[2164-2168]），TC2x 直接拒绝
- UCB base 必须 `!= 0` 检查（M[1811-1815] 等）
- `doUcbErase` 的 `LockedRegion` 硬编码 `0xAF400800-0xAF400FFF` 和 `0xAF401800-0xAF401FFF`——chip-level OTP 区
- `doUcbErase` 二次确认 `yes`（M[2235-2253]）—— 跟 UCB 一样敏感
- 三个命令的 print format 一致：`  Device: ... / UCB: ... /  Action: ...`

**问题**
- M[2041] 用 `EXIT_SUCCESS`（=0）表示"用户放弃"，而 `doUcbErase` M[2252] 用 `EXIT_USAGE_ERROR`（=1）——**两个 UCB 命令在用户 abort 时的退出码不一致**。MCP/agent 集成时这个差异要踩坑
- `doUcbWrite` M[2014-2017] 检查 page alignment，**但没检查 sector alignment**——UCB 写本来就不 erase，页对齐就够了（DFlash 写是 8 字节 page），所以是对的；但跟 doErase 风格不一致容易让人误以为漏了
- `doUcbErase` M[2216-2221] 的 LockedRegion 用绝对地址 0xAF40xxxx，对 TC38x 等大 DFlash 设备可能偏移——**硬编码限制**。建议改成从 `flashCfg.ucb` 派生："erase 不允许触及第 0/倒数第 1 个 sector"
- `doUcbWrite` 没做"目标地址是 UCB 区间内"的 32-byte UCB 记录边界检查——单个 UCB 记录是 32 字节，写入非记录边界会写脏

### 3.7 `doList` / `doReset` — C+

**这是这个文件里最差的一对命令**

两者都**没有**走 `initTool()` 和 `RESOLVE_CTX` 宏，而是**手写了一遍**完整的 server_connect → get_targets → startSession → device_connect 流程：

- `doList` M[914-977]：63 行重写了一遍 init 的 50%
- `doReset` M[983-1062]：79 行重写了一遍 init 的 100%（含 server_connect + get_targets + startSession + device_connect + JSON 输出）

**问题**
- M[937-941] / M[1011-1015]：`server_connect` 失败的错误处理用三行重复 if/else，模式跟 `initTool` 不一致
- M[1045-1052] `doReset` 根据 `halt` 选 DCO，没问题；但 `g_hotAttach` 全局被忽略了——`--hot` 在 doReset 不可用
- 两者都没做 TC4x 拒绝（`rejectTc4xDevice`）——如果有人 `wiggle reset` 接 TC4x 板子，TC4x 也会被允许 reset
- 两者都没做 device config 加载

**修复建议**
- 这两个命令应该是 `doReset` 走 `RESOLVE_CTX` + `device_connect(DCO)`，`doList` 走一个"只连 server + 列 targets"的轻量 init helper。**重构 ROI 很高**——能消掉 100+ 行重复 + 统一行为

### 3.8 `doReg` / `doStatus` / `doInfo`（SVD 调试）— A-

**优点**
- SVD 按需加载（`doReg` M[3036-3048] + `doStatus`/`doInfo` 通过 `ToolContext`），避免所有命令都加 10MB 内存
- SVD name → address 查表 + address → SVD 逆向（`doReg` M[3106-3118]），双向都支持
- 字段解码：mask = `((1<<(msb-lsb+1))-1) << lsb`（M[3171] 等）——位域标准算法
- `doInfo` 的 JSON 输出完整：device + memory_regions + dflash + ucb + svd，**结构最完整**（M[3666-3706]）

**问题**
- `doReg` 的 enum value 描述查找用 `O(N)` 线性扫（M[3177-3182]），SVD field 一般 5-20 个 value，OK；但 1000 个 reg × 20 个 value 的时候 list 命令会卡。可建 `std::map<uint32_t, std::string>` 副索引
- `doStatus` 错误分支 M[3560-3582] 的 `if (!errName.empty() && errName != statusName)`——TC3x status 和 error 是两个不同寄存器，TC2x 两者同名共用；如果 SVD 把 `getFlashErrorRegName` 返回空字符串（即没找到 error 寄存器），整段被跳过，**没有 fallback 提示**

### 3.9 `doCompare` / `doSearch` — B+

**优点**
- `doSearch` 64KB chunk + pattern-overlap 读（M[3883-3898]）—— 处理跨 chunk 边界的 pattern
- 1000-match 上限（M[3907-3923]）防爆内存
- `doCompare` M[3795-3807] 限 20 行 mismatch 输出

**问题**
- `doSearch` 64KB 单次 read，pattern overlap buffer 大小 `CHUNK_SIZE + pattern.size() - 1`（M[3871]）—— pattern.size() 远大于 64KB 时会爆，但实际不可能（hex pattern 限命令行长度）
- `doCompare` 不支持 HEX 多 segment（M[3778-3808]）——segments 之间不连续时只 compare 第一个 segment

### 3.10 Debug 控制（`doHalt/doGo/doCpuReg/doBreak/doStep/doBt`）— A

**这是这个文件里设计最好的一组**

- **3-level halt fallback**（M[3970-4085]）：BRKIN pin → DBGSR register → RESET_AND_HALT，按代价递增排序，最常用路径无副作用
- **3-level resume fallback**（M[4091-4228]）：BRKIN release → DBGSR.CLR_HALT → reset，含 PC 变化验证（DBGSR readback stale 时用 PC 对比判定真的 resumed）
- **`g_haltMethod` 缓存**：第一次成功的 fallback 在后续命令中优先（M[3998]/[4027] 的 `g_haltMethod == UNPROBED || == BRKIN` 短路）——这是个聪明的优化
- **CSA 寄存器表**（M[4285-4296]）：32 个寄存器 word offset 一一对应 TriCore Vol1 Table 29
- **PC = PCXI[31:16] << 1** —— TriCore 标准提取方式（M[4158]、[4306]）
- **`doBt` 循环检测**（M[5026-5031]）：维护 `visited[]` 数组防 CSA 链死循环

**问题**
- **`doGo` 的"DBGSR stale but PC changed"路径**（M[4166-4178]）：处理了"DBGSR readback 没及时更新"这种 race condition。**实战中是真的会发生**——但代码注释只说"verify via PC change"没解释为什么 DBGSR 会 stale。**应该在注释里写一句"DBGSR readback may be stale for 1-2 cycles after CLR_HALT on TC1.6.2"**
- **`doBreak go` 命令 200 行**（M[4651-4848]）：set BP + resume + poll halt 在一个连接里做完。**实战中这一行能 work 就行**，但出 bug 难调——失败时可能 BP 已 set、CPU 未 resume。建议加 `--dry-run` 只设 BP 不 resume
- **`doBreak` 注释说 `g_bp[]` 不同 CLI 调用间不 persist**（M[4496]）但代码仍然在 readback 后写 g_bp（M[4537-4539]）——应该是只在 shell 模式才有意义
- **`doStep` 用 TR0 单步**（M[4898-4900]）—— TriCore 单步习惯做法，但要求 DBGTCR.DTA=0 是隐式前提（M[4896]），M[4483] 才写过。**M[4896] 写 DBGTCR=0 是冗余的**——doBreak add 已经写过。但 doStep 单独调用时必须自己写，OK
- **CPU CSFR base 数组 `CPU_CSFR_BASE[]`**（M[162-165]）：定义了但**没被使用**——所有调试命令都 hardcode CPU0 base 0xF8810000。多核 debug 是个 TODO
- **`tasutil_userpins_set_high` / `TAS_UP_BRKIN` / `TAS_AM15_RW_USERPINS` / `TAS_AM15`**：API 都来自 `tas_utils_client.h`（M[32]），但工具名都靠这套 userpin helper。**对 TAS 内部 API 依赖很深**

### 3.11 `doShell` REPL — B+

**优点**
- `g_shellCtx` 复用机制（M[5089-5095]）—— shell 启动一次 initTool，之后所有 doXxx 通过 RESOLVE_CTX 复用同一个 `CTasClientRw` 连接，**省掉了每次 server_connect**
- 缩写在 M[5154-5159]：`go`/`resume`、`cpu_reg`/`cr`、`break`/`bp`、`step`/`s`、`bt`/`backtrace`、`erase`/`e`、`write`/`w` 等等——**对调试场景的 REPL 体验很周到**
- `fflush(stdout)` 在每次循环末尾（M[5161]）——保证 pipe/MCP 集成时能及时拿到输出

**问题**
- REPL 不支持 history（`std::getline` + 解析，无 readline/history 文件）
- REPL 不支持多行命令
- REPL 不知道当前 halt 状态——用户敲 `cpu_reg A0` 时如果 MCU 没 halt，会得到模糊的"MCU must be halted"但 REPL 不主动提醒当前状态
- 错误恢复差：doHalt 失败时 REPL 不会回到 prompt 之前的"已知状态"——下次命令又得重新探测

### 3.12 main() — A-

**优点**
- 短、清晰、分类（Subcommands / Debug / UCB / Legacy）
- Legacy 兼容（M[5296-5300]）：`wiggle 0xAF000000 4` 老用法自动当 `wiggle erase 0xAF000000 4` 处理
- `--version` 支持（M[5257]）

**问题**
- M[5260-5282] 命令 dispatch 是手写 if 链——27 行。`std::map<std::string, fnPtr>` 表会更可维护
- 没有 subcommand 的 --help 跳板：`wiggle --help` 不会跳到 `wiggle list --help` 之类的——但顶层 printUsage 写得挺全，OK

---

## 4. 全局问题

### 4.1 JSON 输出不一致

`g_json` flag 在每个 doXxx 末尾设置，且 JSON 输出在每个 doXxx 内部手工序列化。问题：

- **错误处理走两条路**：`g_json ? jsonError : fprintf`（很多地方用），但**也有只走 fprintf 不走 jsonError 的地方**（M[1113-1117] doRead 必填参数缺失走 fprintf，不走 jsonError）
- **`doUcbErase` 二次确认**完全走 stdout（`Type 'yes' to continue:`）—— agent/MCP 集成时无法自动化

**建议**：所有 doXxx 入口加一个 helper `requireJson(errCode, condition, msg)`，把"非 JSON 报错 + JSON 报错"统一。

### 4.2 全局状态 5 个

| 变量 | 用途 | 问题 |
|---|---|---|
| `g_json` | JSON 模式 | 跨 doXxx 传 flag 没问题，但是 bool + 全局降低可测性 |
| `g_hotAttach` | DCO override | 同上 |
| `g_haltMethod` | halt 路径缓存 | 跨命令持久化，但 CLI 调用间不持久（注释承认了）——意义不大 |
| `g_bp[8]` | 8 槽 BP 缓存 | 同上，shell 内可用，shell 间失效 |
| `g_shellCtx` | shell 模式共享 ctx | 设计 OK，scope 限 shell |

5 个全局里 3 个是"半残"持久化。**建议**：
- `g_haltMethod` 删除，每次重新探测（成本 < 10ms）
- `g_bp[]` 删除，shell 模式用 ctx 里的成员，CLI 模式不缓存
- `g_json` 改成 ctx 成员 `ctx.jsonMode`

### 4.3 错误码膨胀

`EXIT_*` 22 个常量（M[174-194]），编号 0-23 中间缺 9/10/13。原因：
- 9/10 可能是历史预留
- 13 看起来是漏的

shell 调度 / MCP agent 集成时按"非零 = 失败"判断没问题，但 22 个 exit code 维护成本高，**建议合并到 6 类**：OK / USAGE / TARGET_FAIL / FLASH_FAIL / VERIFY_FAIL / INTERNAL。

### 4.4 头文件

`wiggle_main.cpp` 一坨 5K 行没有任何内部分文件。**至少应该拆**：
- `wiggle_commands/erase.{h,cpp}` + `write.{h,cpp}` + `read.{h,cpp}` + ...
- `wiggle_debug/halt.{h,cpp}` + `break.{h,cpp}` + `step.{h,cpp}` + ...
- `wiggle_session.{h,cpp}`（initTool / startSession / ToolContext）

这个改动 ROI 极高，能让 5K 行变成 5 个 1K 行文件。**v2.3 应该做这件事**。

### 4.5 测试覆盖

`apps/wiggle/` 下面**没有**任何单元测试或集成测试：
- `find_endinit_regs.py` 是单独脚本
- 整个项目有 `test_package/` 目录但跟 wiggle 没看出关联

**建议**：
- `hex_parser` 是纯函数，**最容易写单测**——给 5 个 Intel HEX 样本（标准 / type 04 / 多 segment / EOF 缺失 / checksum 错）跑断言
- `svd_loader` 跑一个 fixture SVD 验证字段解析
- 端到端：MCP server 的 test_mcp_tools.py 已经在做，但应该也有针对 wiggle CLI 的脚本

### 4.6 安全 / 鲁棒性

- **UCB 写没有 32 字节 UCB record 对齐检查**——会写脏
- **UCB 写没有 UCB record type 校验**——可能把 CONFIRMED=0x55FE 写成别的值触发 re-program
- **SRAM 写入无确认**：`wiggle poke 0x70000000 0x12345678` 是直接改 SRAM，**不可逆**（重启就没了，但运行中可能破坏 OS context）。建议加 `--dangerous` flag 强制走 + 默认对 SRAM/PFlash 区拒绝
- **空 password / 0 length 边界**：`--length 0` 多数 doXxx 都拦了（`if (readLength == 0) error`），但 `doWrite` HEX 文件空 segments 没单独拦
- **JSON input 没测**：所有 JSON 输出字段都是手工组装，**类型搞错会 runtime 崩**。比如 `doDump` M[3297] `uint32_t absAddr = static_cast<uint32_t>(addr + offset)` —— 64-bit `addr` 截到 32-bit 后 `>> 16` 拿 ELA

### 4.7 性能

- `doErase --all` 会一次性 erase 整个 DFlash（典型 1MB 384 sector = 几十秒到几分钟）；M[2480-2483] 等待 D0BUSY 一个寄存器，没用上 D1BUSY/P0BUSY 监测——并发 erase 不能实现，但 TC2x/TC3x DFlash 不能并发所以 OK
- `doWrite` 写 1MB @ 8 字节/page = 131072 page 写循环（M[1236-1306]）；每页 5-7 次 I/O，估算 50-100ms/页，**全片 write ~3-4 小时**。M[1298-1304] "every 128 pages" 进度更新 1KB 一行——可以更频繁

---

## 5. Top 修复清单（按 ROI 排）

| # | 项 | 工作量 | 收益 | 优先级 |
|---|---|---|---|---|
| 1 | `doList` / `doReset` 改用 `initTool` + `RESOLVE_CTX` | 1h | 消 100+ 行重复 + 统一行为（含 TC4x 检查、device config 加载） | P0 |
| 2 | main.cpp 拆 5-7 个子文件 | 1d | 5K 行 → 1K×5，可维护性大增 | P0 |
| 3 | UCB 写 32 字节 record 对齐检查 + record type 校验 | 2h | 防 UCB 锁芯片 | P0 |
| 4 | `g_bp[]` / `g_haltMethod` 改为 ctx 成员或删除 | 0.5d | 减全局状态，可测性 + | P1 |
| 5 | 提取 `LockedRegion` 表到顶层常量化 | 0.5h | 消 doUcbErase / doRewrite 重复 | P1 |
| 6 | `findConfigDir` 改用环境变量 + argv[0] 解析，去掉路径猜 | 1h | 部署更可预期 | P1 |
| 7 | JSON 输出路径统一（所有 error 走 jsonError） | 1d | MCP/agent 集成稳 | P1 |
| 8 | UCB 用户 abort 退出码统一为 `EXIT_USAGE_ERROR` | 5min | 修不一致 | P2 |
| 9 | `findConfigDir` Windows 改 `GetModuleFileNameW` | 5min | 修中文路径 | P2 |
| 10 | 给 `hex_parser` 写 5 个 fixture 单元测试 | 0.5d | 首次单测覆盖 | P2 |
| 11 | `svd_loader` 建 `peripheral→registers` 副索引 | 0.5d | `reg --list` 加速 | P3 |
| 12 | EOF 缺失时打 warning | 5min | 改善可观测性 | P3 |
| 13 | `doDump` 的 64→32 bit 截断 + checksum 重算 | 30min | 修 bug 风险 | P3 |
| 14 | 多核 CSFR base 用上（扩展 `CPU0_*` → `CPU*_*`） | 2d | 多核 debug | P4 |

---

## 6. 优点保留

写到一半很容易只看到问题，但这个项目做对的事很多：

1. **三层 halt/resume fallback** 实战中真有用
2. **`g_shellCtx` 复用** 解决了 REPL 反复重连的性能问题
3. **`doRewrite`** 把 Read-Modify-Write 这个复杂操作拆得清晰
4. **SVD 字段解码** 输出格式比同类工具（Ozone、UDE）更工程化
5. **`g_bp[]` + 8 槽限制** 正确处理了 TC3x 的硬件约束
6. **JSON 双输出** 跟 MCP 集成很丝滑（`D:\wiggle_MCP` 那个 server 就是这样用的）
7. **TAS_NAME_LEN16 16 字节截断** + 0xC0000409 注释是项目里的好实践
8. **legacy backward compat** 0xAF000000 老用法保留

---

## 7. 一句话总结

`apps/wiggle` 是一个**功能完整、护栏到位、实战可用**的 AURIX DFlash 工具，工程上有 5K 行的 god-file + 几个全局状态 + 2 个命令绕过统一 init 的技术债。短期不动；中期（v2.3+）拆文件 + 收全局；长期换数据模型（hex_parser → stream api / config → SQL-ish）。

**整体评分**：
- 实用性：A
- 可维护性：B-
- 可测性：C+
- 推荐建议：v2.3 起逐步重构，先拆文件再做单测

---

*评审人*：Mavis (mavis)
*评审方法*：完整通读 5,305 行 + 4 个 header + 3 个 impl + CMakeLists
*评审耗时*：~45 分钟（5 个 read 调用 + 4 个 search + 7 个 chunked read）
