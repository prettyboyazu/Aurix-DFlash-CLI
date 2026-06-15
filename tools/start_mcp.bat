@echo off
REM ==========================================================================
REM  AURIX MCP Server - Windows 启动脚本
REM ==========================================================================
REM
REM  用法:
REM    start_mcp.bat              # 使用默认配置启动 (stdio 模式)
REM    start_mcp.bat --sse        # 使用 SSE 模式启动 (HTTP 服务)
REM
REM  首次使用请确保已安装 Python 依赖:
REM    pip install -r requirements.txt
REM
REM ==========================================================================

setlocal

set SCRIPT_DIR=%~dp0
set CONFIG_FILE=%SCRIPT_DIR%mcp_config.json
set SERVER_SCRIPT=%SCRIPT_DIR%aurix_mcp_server.py

REM 检测 Python
where python >nul 2>nul
if %errorlevel% equ 0 (
    set PYTHON=python
) else (
    echo [ERROR] Python not found in PATH. Please install Python 3.10+
    pause
    exit /b 1
)

REM 检测依赖
%PYTHON% -c "import mcp" >nul 2>nul
if %errorlevel% neq 0 (
    echo [INFO] Installing Python dependencies...
    %PYTHON% -m pip install -r "%SCRIPT_DIR%requirements.txt"
    if %errorlevel% neq 0 (
        echo [ERROR] Failed to install dependencies
        pause
        exit /b 1
    )
)

echo ==========================================================================
echo  AURIX MCP Server
echo ==========================================================================
echo  Config: %CONFIG_FILE%
echo  Mode:   %1
echo ==========================================================================

if "%1"=="--sse" (
    echo Starting in SSE mode (HTTP server on :8000)...
    %PYTHON% "%SERVER_SCRIPT%" --config "%CONFIG_FILE%" --transport sse
) else (
    echo Starting in stdio mode...
    echo Register this command in your AI client:
    echo   python "%SERVER_SCRIPT%" --config "%CONFIG_FILE%"
    echo.
    %PYTHON% "%SERVER_SCRIPT%" --config "%CONFIG_FILE%"
)

endlocal
