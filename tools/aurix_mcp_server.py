#!/usr/bin/env python3
"""
AURIX MCP Server - Model Context Protocol Server for Infineon AURIX MCU Tools

Wraps dflash.exe (DFlash operations + debug) and AURIXFlasher.exe (PFlash programming)
as MCP tools for AI agent integration.

Requirements:
    pip install mcp[cli]

Usage:
    python aurix_mcp_server.py                        # Use default paths
    python aurix_mcp_server.py --config config.json   # Use config file

Configuration (mcp_config.json):
{
    "dflash_exe": "path/to/dflash.exe",
    "aurix_flasher_exe": "path/to/AURIXFlasher.exe",
    "server": "localhost",
    "target": null,
    "device": null,
    "timeout": 120
}
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Optional

from mcp.server.fastmcp import FastMCP

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

DEFAULT_CONFIG = {
    "dflash_exe": None,       # auto-detect
    "aurix_flasher_exe": None, # auto-detect
    "server": "localhost",
    "target": None,
    "device": None,
    "timeout": 120,
}


def find_exe(name: str) -> Optional[str]:
    """Find executable relative to this script or in common locations."""
    script_dir = Path(__file__).resolve().parent

    # Check script_dir (tools/)
    candidate = script_dir / name
    if candidate.exists():
        return str(candidate)

    # Check parent/Erase/ (deploy layout)
    candidate = script_dir.parent / "Erase" / name
    if candidate.exists():
        return str(candidate)

    # Check parent/data/ (data layout)
    candidate = script_dir.parent / "data" / name
    if candidate.exists():
        return str(candidate)

    # Check parent/AURIXFlasher/
    candidate = script_dir.parent / "AURIXFlasher" / name
    if candidate.exists():
        return str(candidate)

    return None


def load_config(config_path: Optional[str] = None) -> dict:
    """Load configuration from file or use defaults."""
    cfg = dict(DEFAULT_CONFIG)

    if config_path and os.path.exists(config_path):
        with open(config_path, "r", encoding="utf-8") as f:
            user_cfg = json.load(f)
        cfg.update(user_cfg)

    # Auto-detect executables
    if not cfg["dflash_exe"]:
        exe_name = "dflash.exe" if sys.platform == "win32" else "dflash"
        cfg["dflash_exe"] = find_exe(exe_name)
    if not cfg["aurix_flasher_exe"]:
        exe_name = "AURIXFlasher.exe"
        cfg["aurix_flasher_exe"] = find_exe(exe_name)

    return cfg


# ---------------------------------------------------------------------------
# Subprocess helpers
# ---------------------------------------------------------------------------

def run_dflash(args: list[str], cfg: dict, timeout: Optional[int] = None) -> dict:
    """Run dflash.exe with --json and return parsed JSON result."""
    exe = cfg["dflash_exe"]
    if not exe:
        return {"status": "error", "code": -1, "message": "dflash.exe not found"}

    cmd = [exe] + args + ["--json"]

    # Add common args from config
    if cfg.get("server"):
        cmd += ["--server", cfg["server"]]
    if cfg.get("target"):
        cmd += ["--target", cfg["target"]]
    if cfg.get("device"):
        cmd += ["--device", cfg["device"]]

    tout = timeout or cfg.get("timeout", 120)

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=tout,
            cwd=os.path.dirname(exe) if os.path.dirname(exe) else None,
        )
    except subprocess.TimeoutExpired:
        return {"status": "error", "code": -2, "message": f"Timeout after {tout}s"}
    except FileNotFoundError:
        return {"status": "error", "code": -1, "message": f"Executable not found: {exe}"}
    except Exception as e:
        return {"status": "error", "code": -3, "message": str(e)}

    # Try to parse JSON from stdout (last non-empty line)
    stdout = result.stdout.strip()
    stderr = result.stderr.strip()

    if stdout:
        # Find the last line that looks like JSON
        for line in reversed(stdout.splitlines()):
            line = line.strip()
            if line.startswith("{"):
                try:
                    parsed = json.loads(line)
                    # Add stderr as diagnostic if there was an error
                    if parsed.get("status") == "error" and stderr:
                        parsed["stderr"] = stderr
                    return parsed
                except json.JSONDecodeError:
                    continue

    # If no JSON found, construct error from stderr
    error_msg = stderr or stdout or "Unknown error"
    return {
        "status": "error",
        "code": result.returncode,
        "message": error_msg,
    }


def run_flasher(args: list[str], cfg: dict, timeout: Optional[int] = None) -> dict:
    """Run AURIXFlasher.exe and return result."""
    exe = cfg["aurix_flasher_exe"]
    if not exe:
        return {"status": "error", "code": -1, "message": "AURIXFlasher.exe not found"}

    cmd = [exe] + args
    tout = timeout or cfg.get("timeout", 300)

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=tout,
            cwd=os.path.dirname(exe) if os.path.dirname(exe) else None,
        )
    except subprocess.TimeoutExpired:
        return {"status": "error", "code": -2, "message": f"Timeout after {tout}s"}
    except FileNotFoundError:
        return {"status": "error", "code": -1, "message": f"Executable not found: {exe}"}
    except Exception as e:
        return {"status": "error", "code": -3, "message": str(e)}

    # AURIXFlasher doesn't support --json, so we construct result
    stdout = result.stdout.strip()
    stderr = result.stderr.strip()

    if result.returncode == 0:
        return {
            "status": "ok",
            "returncode": 0,
            "stdout": stdout,
        }
    else:
        return {
            "status": "error",
            "code": result.returncode,
            "message": stderr or stdout or "AURIXFlasher failed",
        }


def format_result(result: dict) -> str:
    """Format result dict as readable string for MCP response."""
    return json.dumps(result, indent=2, ensure_ascii=False)


# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------

mcp = FastMCP("aurix-mcp-server")

# Global config - set during startup
_config: dict = {}


@mcp.tool()
def dflash_status(server: str = "", target: str = "", device: str = "") -> str:
    """Read Flash status register with SVD-driven field decoding.
    Shows busy flags, error status, and register values.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device
    return format_result(run_dflash(["status"], cfg))


@mcp.tool()
def dflash_info(server: str = "", target: str = "", device: str = "") -> str:
    """Display device info, memory layout, and SVD register summary.
    Returns device name, JTAG ID, memory regions, DFlash layout, UCB info.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device
    return format_result(run_dflash(["info"], cfg))


@mcp.tool()
def dflash_list(server: str = "", target: str = "") -> str:
    """List connected TAS targets.
    Returns available targets with device names and identifiers.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    return format_result(run_dflash(["list"], cfg))


@mcp.tool()
def dflash_erase(
    erase_all: bool = True,
    address: str = "",
    sectors: int = 0,
    verify: bool = False,
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Erase DFlash sectors. Use erase_all=True to erase entire DFlash,
    or specify address + sectors for partial erase.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["erase"]
    if erase_all:
        args.append("--all")
    else:
        if not address:
            return json.dumps({"status": "error", "message": "address required when erase_all=False"})
        args += ["--addr", address, "--sectors", str(sectors or 1)]
    if verify:
        args.append("--verify")

    return format_result(run_dflash(args, cfg))


@mcp.tool()
def dflash_write(
    file_path: str,
    verify: bool = True,
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Write data to DFlash from a HEX or BIN file.
    Supports Intel HEX (.hex) and raw binary (.bin) files.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["write", "-f", file_path]
    if verify:
        args.append("--verify")

    return format_result(run_dflash(args, cfg))


@mcp.tool()
def dflash_dump(
    address: str,
    length: str,
    output_file: str = "",
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Read memory and return hex dump. Supports PFlash, DFlash, SRAM.
    Address and length in hex (e.g. '0xAF000000', '0x100').
    Optionally save to file (-o for .bin or .hex).
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["dump", address, length]
    if output_file:
        args += ["-o", output_file]

    return format_result(run_dflash(args, cfg))


@mcp.tool()
def dflash_reg(
    name_or_addr: str,
    value: str = "",
    list_peripheral: str = "",
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Read/write registers by SVD name or address.
    Read: dflash_reg("DMU.HF.STATUS") or dflash_reg("0xF8040010")
    Write: dflash_reg("DMU.HF.STATUS", value="0x1234")
    List: dflash_reg("", list_peripheral="DMU")
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["reg"]
    if list_peripheral:
        args += ["--list", list_peripheral]
    else:
        if not name_or_addr:
            return json.dumps({"status": "error", "message": "name_or_addr required"})
        args.append(name_or_addr)
        if value:
            args.append(value)

    return format_result(run_dflash(args, cfg))


@mcp.tool()
def dflash_poke(
    address: str,
    value: str,
    width: int = 32,
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Write a value to a memory address and verify with readback.
    Width: 8, 16, 32 (default), or 64 bits.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["poke", address, value, "--width", str(width)]
    return format_result(run_dflash(args, cfg))


@mcp.tool()
def dflash_compare(
    file_path: str,
    address: str,
    length: str = "",
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Compare local file contents with Flash/memory content.
    Supports .hex (Intel HEX) and .bin files.
    Returns mismatch count and match status.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["compare", "-f", file_path, "-a", address]
    if length:
        args += ["--length", length]

    return format_result(run_dflash(args, cfg))


@mcp.tool()
def dflash_search(
    address: str,
    length: str,
    pattern: str,
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Search memory for a hex byte pattern.
    Pattern is hex string (e.g. 'DEADBEEF', '00FF00').
    Returns list of match addresses.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["search", address, length, pattern]
    return format_result(run_dflash(args, cfg))


@mcp.tool()
def dflash_reset(
    halt: bool = False,
    server: str = "",
    target: str = "",
) -> str:
    """Reset the MCU. Set halt=True to reset and halt (default: reset and run)."""
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target

    args = ["reset"]
    if halt:
        args.append("--halt")

    return format_result(run_dflash(args, cfg))


@mcp.tool()
def flash_pflash(hex_file: str) -> str:
    """Program PFlash using AURIXFlasher.exe with an Intel HEX file.
    This is for PFlash programming only. For DFlash, use dflash_write.
    """
    args = ["-hex", hex_file]
    return format_result(run_flasher(args, _config, timeout=300))


@mcp.tool()
def dflash_read(
    address: str,
    length: str,
    output_file: str = "",
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Read DFlash content. Address and length in hex.
    Optionally save to output file (.hex or .bin).
    Without output file, returns hex dump data.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["read", "-a", address, "-l", length]
    if output_file:
        args += ["-o", output_file]

    return format_result(run_dflash(args, cfg))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="AURIX MCP Server")
    parser.add_argument("--config", type=str, default=None,
                        help="Path to config JSON file")
    parser.add_argument("--transport", type=str, default="stdio",
                        choices=["stdio", "sse"],
                        help="MCP transport (default: stdio)")
    args = parser.parse_args()

    global _config
    _config = load_config(args.config)

    # Validate
    if not _config["dflash_exe"]:
        print("WARNING: dflash.exe not found. Set 'dflash_exe' in config.",
              file=sys.stderr)

    print(f"AURIX MCP Server starting (transport={args.transport})", file=sys.stderr)
    print(f"  dflash: {_config['dflash_exe'] or 'NOT FOUND'}", file=sys.stderr)
    print(f"  flasher: {_config['aurix_flasher_exe'] or 'NOT FOUND'}", file=sys.stderr)
    print(f"  server: {_config['server']}", file=sys.stderr)

    if args.transport == "sse":
        mcp.run(transport="sse")
    else:
        mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
