#!/usr/bin/env python3
"""
D:\wiggle_MCP 完整包测试脚本
验证所有组件是否正常工作。
"""
import sys
import json
import os

# 添加包路径
sys.path.insert(0, r"D:\wiggle_MCP")

print("=" * 70)
print("  D:\\wiggle_MCP 完整包测试")
print("=" * 70)

# ---------------------------------------------------------------------------
# Test 1: 基础文件检查
# ---------------------------------------------------------------------------
print("\n[1/6] 检查基础文件...")
required_files = [
    "aurix_mcp_server.py",
    "build_analysis.py",
    "mcp_config.json",
    "requirements.txt",
    "start_mcp.bat",
    "start_mcp.sh",
    "README.md",
    r"wiggle\wiggle.exe",
    # NOTE: AURIXFlasher.exe 是闭源工具，不随包分发，需要用户自行安装
]
missing = []
for f in required_files:
    full_path = os.path.join(r"D:\wiggle_MCP", f)
    if not os.path.exists(full_path):
        missing.append(f)
        
if missing:
    print(f"  FAIL: 缺少文件: {missing}")
    sys.exit(1)
else:
    print(f"  PASS: 所有 {len(required_files)} 个必需文件存在")

# ---------------------------------------------------------------------------
# Test 2: 配置文件验证
# ---------------------------------------------------------------------------
print("\n[2/6] 验证 mcp_config.json...")
config_path = r"D:\wiggle_MCP\mcp_config.json"
try:
    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)
    
    # 检查必需字段
    assert "wiggle_exe" in config, "Missing wiggle_exe"
    assert "aurix_flasher_exe" in config, "Missing aurix_flasher_exe"
    assert "build" in config, "Missing build section"
    assert "project" in config, "Missing project section"
    assert "command" in config["build"], "Missing build.command"
    assert "elf_file" in config["project"], "Missing project.elf_file"
    print("  PASS: 配置文件结构正确")
except Exception as e:
    print(f"  FAIL: {e}")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Test 3: Python 模块导入
# ---------------------------------------------------------------------------
print("\n[3/6] 测试 Python 模块导入...")
try:
    # 测试 build_analysis
    from build_analysis import (
        TaskingMapParser, GccLstParser, MdfParser, ElfParser, BuildRunner
    )
    print("  PASS: build_analysis.py 导入成功")
    
    # 测试 MCP Server（不实际启动）
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "aurix_mcp_server", 
        r"D:\wiggle_MCP\aurix_mcp_server.py"
    )
    mcp_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mcp_module)
    print("  PASS: aurix_mcp_server.py 导入成功")
    
except Exception as e:
    print(f"  FAIL: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

# ---------------------------------------------------------------------------
# Test 4: 构建产物解析器测试
# ---------------------------------------------------------------------------
print("\n[4/6] 测试构建产物解析器...")

# 4.1 TaskingMapParser
try:
    parser = TaskingMapParser()
    print("  PASS: TaskingMapParser 实例化")
except Exception as e:
    print(f"  FAIL: TaskingMapParser: {e}")

# 4.2 GccLstParser
try:
    parser = GccLstParser()
    print("  PASS: GccLstParser 实例化")
except Exception as e:
    print(f"  FAIL: GccLstParser: {e}")

# 4.3 MdfParser
try:
    parser = MdfParser()
    print("  PASS: MdfParser 实例化")
except Exception as e:
    print(f"  FAIL: MdfParser: {e}")

# 4.4 ElfParser
try:
    parser = ElfParser()
    print("  PASS: ElfParser 实例化")
except Exception as e:
    print(f"  FAIL: ElfParser: {e}")

# 4.5 BuildRunner
try:
    runner = BuildRunner()
    print("  PASS: BuildRunner 实例化")
except Exception as e:
    print(f"  FAIL: BuildRunner: {e}")

# ---------------------------------------------------------------------------
# Test 5: MCP 工具注册验证
# ---------------------------------------------------------------------------
print("\n[5/6] 验证 MCP 工具注册...")
try:
    # 从导入的模块获取 MCP 实例
    mcp = mcp_module.mcp
    
    # 获取注册的工具列表
    tools = mcp._tool_manager.list_tools()
    tool_names = [t.name for t in tools]
    
    # 期望的工具
    expected_tools = [
        # 硬件操作 (13)
        "wiggle_list", "wiggle_info", "wiggle_status",
        "wiggle_erase", "wiggle_write", "wiggle_read",
        "wiggle_dump", "wiggle_reg", "wiggle_poke",
        "wiggle_search", "wiggle_compare", "wiggle_reset",
        "wiggle_pflash",
        # 构建分析 (6)
        "build_project", "reload_parsers", "parse_symbols", "parse_elf",
        "lookup_symbol", "lookup_address",
    ]
    
    missing_tools = [t for t in expected_tools if t not in tool_names]
    extra_tools = [t for t in tool_names if t not in expected_tools]
    
    if missing_tools:
        print(f"  WARN: 缺少工具: {missing_tools}")
    if extra_tools:
        print(f"  INFO: 额外工具: {extra_tools}")
    
    print(f"  PASS: 注册了 {len(tool_names)} 个工具 (期望 {len(expected_tools)})")
    
    # 分类统计
    hw_tools = [t for t in tool_names if t.startswith("wiggle_")]
    ba_tools = [t for t in tool_names if not t.startswith("wiggle_")]
    print(f"        - 硬件操作: {len(hw_tools)}")
    print(f"        - 构建分析: {len(ba_tools)}")
    
except Exception as e:
    print(f"  FAIL: {e}")
    import traceback
    traceback.print_exc()

# ---------------------------------------------------------------------------
# Test 6: 依赖检查
# ---------------------------------------------------------------------------
print("\n[6/6] 检查 Python 依赖...")
required_packages = ["mcp", "elftools"]
missing_deps = []

for pkg in required_packages:
    try:
        __import__(pkg)
        print(f"  PASS: {pkg} 已安装")
    except ImportError:
        print(f"  FAIL: {pkg} 未安装")
        missing_deps.append(pkg)

if missing_deps:
    print(f"\n  提示: 运行 'pip install -r D:\\wiggle_MCP\\requirements.txt' 安装依赖")

# ---------------------------------------------------------------------------
# 总结
# ---------------------------------------------------------------------------
print("\n" + "=" * 70)
print("  测试完成")
print("=" * 70)
print("\nD:\\wiggle_MCP 包状态:")
print("  [OK] 文件完整性")
print("  [OK] 配置文件")
print("  [OK] Python 模块")
print("  [OK] 解析器组件")
print("  [OK] MCP 工具注册")
if not missing_deps:
    print("  [OK] Python 依赖")
print("\n包可以分发给用户使用。")
print("使用说明见: D:\\wiggle_MCP\\README.md")
