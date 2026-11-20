# wiggle_MCP 独立部署说明

本目录为**自包含** MCP 调试工具部署，不依赖 DAS/TASKING 安装（MCD 客户端库与 TAS server 已随包携带）。

## 目录结构
```
wiggle_MCP/
├─ aurix_mcp_server.py      MCP server（mcp v2 MCPServer，工具层）
├─ build_analysis.py        MAP/LST/MDF/ELF 解析（pyelftools）
├─ mcp_config.json          配置（wiggle_exe 指向 wiggle/wiggle.exe）
├─ requirements.txt         Python 依赖（mcp[cli]、pyelftools）
├─ start_mcp.bat            启动 MCP（stdio）
├─ start_tas_server.bat     启动随包 TAS server（servers/tas_server_console.exe）
├─ wiggle/                  调试可执行 + 数据 + MCD 客户端库
│   ├─ wiggle.exe           CLI / serve / gdbserver（静态链接，无 VC 依赖）
│   ├─ mcdxdas.dll          MCD 客户端库（随包，exe 同目录优先加载）
│   ├─ VCRUNTIME140.dll / VCRUNTIME140_1.dll   mcdxdas.dll 的 VC 运行时
│   ├─ RegisterDefs.zip / DeviceConfigs.zip    数据压缩包（git 只含zip）
│   │     （start_mcp.bat 首次运行自动解压为 RegisterDefs/ DeviceConfigs/）
│   └─ (解压后) RegisterDefs/ DeviceConfigs/
├─ servers/                 随包 TAS/DAS server
│   ├─ tas_server_console.exe / tas_server.exe
│   ├─ FTD2XX.dll           miniWiggler USB 驱动库
│   └─ MSVCP140.dll / VCRUNTIME140*.dll
└─ AURIXFlasher/            PFlash 烧写（**不入 git**，体积大另行分发；aurix_pflash 用）
```

## MCD 是怎么集成的
1. **客户端库**：`wiggle.exe` 运行时**动态加载** `mcdxdas.dll`（先 exe 同目录，再 DAS 安装路径）。
   随包携带 `mcdxdas.dll`+VC runtime 于 `wiggle/`，故无需安装 DAS。
   `mcdxdas.dll` 仅依赖 VC runtime+UCRT，无其它 DAS DLL。
2. **server**：MCD 客户端经 socket(localhost:2000) 连 TAS server。必须先运行
   `start_tas_server.bat`（启动随包 `servers/tas_server_console.exe`，含 FTD2XX）。
3. **后端分工**：调试（halt/go/step/break/watch/regs/mem/bt/reset）走 MCD；
   Flash(erase/write/ucb) 与 list/status/info 走 TAS。MCD 与 TAS 互斥 access HW，
   serve 会话持有 MCD，Flash 命令自动关 MCD 切 TAS、完成后懒重连。
4. **非破坏**：MCD halt/resume/step 不复位；MCD 不可用时调试命令报错（不静默 reset）。

## 运行
```bat
:: 1) 启动 server（占用 miniWiggler，先确保无其它 TAS server 运行）
start_tas_server.bat
:: 2) 启动 MCP（在 AI 客户端注册本目录 aurix_mcp_server.py）
start_mcp.bat
```
Python 需 3.10+（mcp SDK）。`pip install -r requirements.txt`。

## 前端形态
- CLI：`wiggle\wiggle.exe <cmd>`（halt/go/step/break/watch/var/disasm/sample/...）
- 持久会话：`wiggle.exe serve`（JSON-lines，MCP 传输）
- GDB/IDE：`wiggle.exe gdbserver --port 3333` → GDB `target remote :3333`
- MCP：aurix_mcp_server.py（wiggle_* / aurix_pflash / 构建分析 工具）

## 注意
- 同一 miniWiggler 只能被一个 TAS server 占用；切换部署前先停旧 server。
- trace 需带 MCDS 的探针（miniWiggler 不支持）；RTOS 感知为项目特定，未含。
- **反汇编 hldumptc 依赖环境、不随包**（TASKING 许可，避免再分发风险）：
  自动探测 `WIGGLE_HLDUMPTC`/常见 TASKING 路径，或配置 `hldumptc`；
  无 TASKING 时回退原生子集译码（长度正确、助记符子集）。

## Windows 11：wiggle.exe 必须签名（Smart App Control）

`wiggle.exe` 是自编译产物，**没有 Authenticode 签名**；Windows 11 的 Smart App Control
（SAC）会拒绝加载未签名且无云端信誉的文件，现象为：

```
程序"wiggle.exe"无法运行: An Application Control policy has blocked this file
```

CodeIntegrity 日志对应 Id 3033/3077：`did not meet the Enterprise signing level requirements`。
随包的 Infineon 原厂件（`tas_server*.exe` / `mcdxdas.dll`）签名有效，不受影响。

处理方式（管理员 PowerShell，会自动请求提权）：

```bat
powershell -ExecutionPolicy Bypass -File .\sign_wiggle.ps1
```

脚本会生成代码签名证书 → 装入本机受信根 → 签名 `wiggle/wiggle.exe` → 验签并试运行。
回滚信任用 `-Untrust`。若签完仍被拦，再考虑关闭 SAC（不可逆，需重置系统才能再开启）：

```
设置 → 隐私和安全性 → Windows 安全中心 → 应用和浏览器控制 → 智能应用控制 → 关闭 → 重启
```

> Defender 排除项、"解除锁定"、AppLocker 白名单对 SAC **均无效**——SAC 只看签名与信誉。
> 自签根证书 = 在本机新增信任锚，请按贵单位规定确认；如有单位代码签名证书请优先使用。
