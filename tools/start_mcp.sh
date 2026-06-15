#!/bin/bash
# ==========================================================================
#  AURIX MCP Server - Linux/Mac 启动脚本
# ==========================================================================
#
#  用法:
#    ./start_mcp.sh              # 使用默认配置启动 (stdio 模式)
#    ./start_mcp.sh --sse        # 使用 SSE 模式启动 (HTTP 服务)
#
#  首次使用请确保已安装 Python 依赖:
#    pip install -r requirements.txt
#
# ==========================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/mcp_config.json"
SERVER_SCRIPT="$SCRIPT_DIR/aurix_mcp_server.py"

# 检测 Python
if command -v python3 &> /dev/null; then
    PYTHON=python3
elif command -v python &> /dev/null; then
    PYTHON=python
else
    echo "[ERROR] Python not found. Please install Python 3.10+"
    exit 1
fi

# 检测依赖
if ! $PYTHON -c "import mcp" 2>/dev/null; then
    echo "[INFO] Installing Python dependencies..."
    $PYTHON -m pip install -r "$SCRIPT_DIR/requirements.txt"
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to install dependencies"
        exit 1
    fi
fi

echo "=========================================================================="
echo " AURIX MCP Server"
echo "=========================================================================="
echo " Config: $CONFIG_FILE"
echo " Mode:   $1"
echo "=========================================================================="

if [ "$1" = "--sse" ]; then
    echo "Starting in SSE mode (HTTP server on :8000)..."
    $PYTHON "$SERVER_SCRIPT" --config "$CONFIG_FILE" --transport sse
else
    echo "Starting in stdio mode..."
    echo "Register this command in your AI client:"
    echo "  python \"$SERVER_SCRIPT\" --config \"$CONFIG_FILE\""
    echo
    $PYTHON "$SERVER_SCRIPT" --config "$CONFIG_FILE"
fi
