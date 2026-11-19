# wiggle_MCP 独立部署说明

本目录为**自包含** MCP 调试工具部署，不依赖 DAS/TASKING 安装（MCD 客户端库与 TAS server 已随包携带）。

## 目录结构
```
wiggle_MCP/
├─ aurix_mcp_server.py      MCP server（FastMCP，工具层）
├─ build_analysis.py        MAP/LST/MDF/ELF 解析（pyelftools）
├─ mcp_config.json          配置（wiggle_exe 指向 wiggle/wiggle.exe）
├─ requirements.txt         Python 依赖（mcp[cli]、pyelftools）
├─ start_mcp.bat            启动 MCP（stdio）
├─ start_tas_server.bat     启动随包 TAS server（servers/tas_server_console.exe）
├─ wiggle/                  调试可执行 + 数据 + MCD 客户端库
│   ├─ wiggle.exe           CLI / serve / gdbserver（静态链接，无 VC 依赖）
│   ├─ mcdxdas.dll          MCD 客户端库（随包，exe 同目录优先加载）
│   ├─ VCRUNTIME140.dll / VCRUNTIME140_1.dll   mcdxdas.dll 的 VC 运行时
│   ├─ DeviceConfigs/       设备参数 JSON
│   └─ RegisterDefs/        SVD 寄存器 JSON
├─ servers/                 随包 TAS/DAS server
│   ├─ tas_server_console.exe / tas_server.exe
│   ├─ FTD2XX.dll           miniWiggler USB 驱动库
│   └─ MSVCP140.dll / VCRUNTIME140*.dll
└─ AURIXFlasher/            PFlash 烧写（aurix_pflash 用）
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