#!/usr/bin/env python3
"""
AURIX MCP Server - Model Context Protocol Server for Infineon AURIX MCU Tools

Wraps wiggle.exe (DFlash operations + debug) and AURIXFlasher.exe (PFlash programming)
as MCP tools for AI agent integration.

Requirements:
    pip install mcp[cli]

Usage:
    python aurix_mcp_server.py                        # Use default paths
    python aurix_mcp_server.py --config config.json   # Use config file

Configuration (mcp_config.json):
{
    "wiggle_exe": "path/to/wiggle.exe",
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

# Build analysis parsers (imported lazily to avoid hard dependency on pyelftools)
from build_analysis import (
    TaskingMapParser, GccLstParser, MdfParser, ElfParser, BuildRunner,
)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

DEFAULT_CONFIG = {
    "wiggle_exe": None,       # auto-detect
    "aurix_flasher_exe": None, # auto-detect
    "server": "localhost",
    "target": None,
    "device": None,
    "timeout": 120,
    "build": {
        "command": None,
        "work_dir": None,
        "timeout": 300,
    },
    "project": {
        "elf_file": None,
        "map_file": None,
        "lst_file": None,
        "mdf_file": None,
        "hex_file": None,
        "toolchain": None,
    },
}


def find_exe(name: str, env_var: Optional[str] = None, env_dir_var: Optional[str] = None,
            exe_name: Optional[str] = None) -> Optional[str]:
    """Find executable relative to this script, in well-known install locations,
    or on PATH. Returns absolute path or None.

    Lookup order:
      1. env_var         — e.g. "AURIX_FLASHER_EXE" → use directly if set + file exists
      2. env_dir_var     — e.g. "AURIX_FLASHER_DIR"  → join with exe_name (or name)
      3. script_dir / <name>
      4. parent/wiggle/  <name>           (deploy layout for wiggle)
      5. parent/data/    <name>           (repo layout)
      6. parent/AURIXFlasher/ <name>      (deploy layout for the Infineon tool)
      7. Infineon default install locations (Windows glob) — only when name starts with "AURIXFlasher"
      8. PATH via shutil.which (scoop / choco / manual install)
    """
    script_dir = Path(__file__).resolve().parent
    parent = script_dir.parent
    bin_name = exe_name or name

    # 1) Explicit env var: full path
    if env_var:
        v = os.environ.get(env_var)
        if v:
            p = Path(v)
            if p.is_file():
                return str(p)
            if p.is_dir():
                candidate = p / bin_name
                if candidate.is_file():
                    return str(candidate)

    # 2) Explicit env var: directory (join with exe_name)
    if env_dir_var:
        d = os.environ.get(env_dir_var)
        if d:
            candidate = Path(d) / bin_name
            if candidate.is_file():
                return str(candidate)

    candidates = [
        script_dir / name,
        parent / "wiggle" / name,
        parent / "data" / name,
        parent / "AURIXFlasher" / name,
    ]

    # 5) Infineon default install location (Windows) — glob to cover any -<version>
    if sys.platform == "win32" and name.lower().startswith("aurixflasher"):
        import glob
        for pattern in (
            r"C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe",
            r"C:\Program Files\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe",
            r"C:\Program Files (x86)\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe",
        ):
            for hit in glob.glob(pattern):
                if os.path.isfile(hit):
                    candidates.append(Path(hit))

    for c in candidates:
        if c.exists() and c.is_file():
            return str(c)

    # 6) Fallback: PATH lookup (scoop / choco / manual install may put it on PATH)
    from shutil import which
    hit = which(name)
    if hit:
        return hit

    return None


def load_config(config_path: Optional[str] = None) -> dict:
    """Load configuration from file or use defaults."""
    cfg = dict(DEFAULT_CONFIG)
    # Deep copy nested dicts
    cfg["build"] = dict(DEFAULT_CONFIG["build"])
    cfg["project"] = dict(DEFAULT_CONFIG["project"])

    if config_path and os.path.exists(config_path):
        with open(config_path, "r", encoding="utf-8") as f:
            user_cfg = json.load(f)
        for key, val in user_cfg.items():
            if isinstance(val, dict) and key in cfg:
                cfg[key].update(val)
            else:
                cfg[key] = val

    # Auto-detect executables (env var > auto-search)
    if not cfg["wiggle_exe"]:
        exe_name = "wiggle.exe" if sys.platform == "win32" else "wiggle"
        cfg["wiggle_exe"] = find_exe(exe_name,
                                     env_var="WIGGLE_EXE",
                                     env_dir_var="WIGGLE_DIR",
                                     exe_name=exe_name)
    if not cfg["aurix_flasher_exe"]:
        exe_name = "AURIXFlasher.exe"
        cfg["aurix_flasher_exe"] = find_exe(exe_name,
                                            env_var="AURIX_FLASHER_EXE",
                                            env_dir_var="AURIX_FLASHER_DIR",
                                            exe_name=exe_name)

    return cfg


# ---------------------------------------------------------------------------
# Subprocess helpers
# ---------------------------------------------------------------------------

def run_wiggle(args: list[str], cfg: dict, timeout: Optional[int] = None) -> dict:
    """Run wiggle.exe with --json and return parsed JSON result."""
    exe = cfg["wiggle_exe"]
    if not exe:
        return {"status": "error", "code": -1, "message": "wiggle.exe not found"}

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
# Build Analysis - Lazy parser cache
# ---------------------------------------------------------------------------

_map_parser: Optional[TaskingMapParser] = None
_lst_parser: Optional[GccLstParser] = None
_mdf_parser: Optional[MdfParser] = None
_elf_parser: Optional[ElfParser] = None


def _get_project_cfg() -> dict:
    """Return the project section of config, with defaults."""
    return _config.get("project", {})


def _get_build_cfg() -> dict:
    """Return the build section of config, with defaults."""
    return _config.get("build", {})


def _ensure_map_parser():
    """Lazily create and parse the MAP/LST file."""
    global _map_parser, _lst_parser
    proj = _get_project_cfg()
    toolchain = (proj.get("toolchain") or "").lower()

    # Prefer MAP file (TASKING), fall back to LST (GCC)
    map_file = proj.get("map_file")
    lst_file = proj.get("lst_file")

    if map_file and os.path.exists(map_file):
        if _map_parser is None:
            _map_parser = TaskingMapParser()
            _map_parser.parse(map_file)
        return "tasking", _map_parser
    elif lst_file and os.path.exists(lst_file):
        if _lst_parser is None:
            _lst_parser = GccLstParser()
            _lst_parser.parse(lst_file)
        return "gcc", _lst_parser
    else:
        return None, None


def _ensure_elf_parser():
    """Lazily create and parse the ELF file."""
    global _elf_parser
    proj = _get_project_cfg()
    elf_file = proj.get("elf_file")
    if not elf_file or not os.path.exists(elf_file):
        return None
    if _elf_parser is None:
        _elf_parser = ElfParser()
        _elf_parser.parse(elf_file)
    return _elf_parser


def _ensure_mdf_parser():
    """Lazily create and parse the MDF file."""
    global _mdf_parser
    proj = _get_project_cfg()
    mdf_file = proj.get("mdf_file")
    if not mdf_file or not os.path.exists(mdf_file):
        return None
    if _mdf_parser is None:
        _mdf_parser = MdfParser()
        _mdf_parser.parse(mdf_file)
    return _mdf_parser


def _invalidate_parsers():
    """Clear all cached parsers so next query re-parses fresh files."""
    global _map_parser, _lst_parser, _mdf_parser, _elf_parser
    _map_parser = None
    _lst_parser = None
    _mdf_parser = None
    _elf_parser = None
    print("Parser cache invalidated.", file=sys.stderr)


# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------

mcp = FastMCP("aurix-mcp-server")

# Global config - set during startup
_config: dict = {}


@mcp.tool()
def wiggle_status(server: str = "", target: str = "", device: str = "") -> str:
    """Read Flash status register with SVD-driven field decoding.
    Shows busy flags, error status, and register values.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device
    return format_result(run_wiggle(["status"], cfg))


@mcp.tool()
def wiggle_info(server: str = "", target: str = "", device: str = "") -> str:
    """Display device info, memory layout, and SVD register summary.
    Returns device name, JTAG ID, memory regions, DFlash layout, UCB info.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device
    return format_result(run_wiggle(["info"], cfg))


@mcp.tool()
def wiggle_list(server: str = "", target: str = "") -> str:
    """List connected TAS targets.
    Returns available targets with device names and identifiers.
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    return format_result(run_wiggle(["list"], cfg))


@mcp.tool()
def wiggle_erase(
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

    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_write(
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

    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_dump(
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

    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_reg(
    name_or_addr: str,
    value: str = "",
    list_peripheral: str = "",
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Read/write registers by SVD name or address.
    Read: wiggle_reg("DMU.HF.STATUS") or wiggle_reg("0xF8040010")
    Write: wiggle_reg("DMU.HF.STATUS", value="0x1234")
    List: wiggle_reg("", list_peripheral="DMU")
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

    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_poke(
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
    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_compare(
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

    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_search(
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
    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_reset(
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

    return format_result(run_wiggle(args, cfg))


@mcp.tool()
def wiggle_pflash(
    hex_file: str,
    verify: bool = True,
    erase: str = "",
    ucb: bool = False,
    connect: str = "",
    start: str = "",
    device_id: str = "",
) -> str:
    """Program PFlash using AURIXFlasher.exe.
    This is for PFlash programming only. For DFlash, use wiggle_write.

    Args:
        hex_file: Intel HEX or ELF file path (required)
        verify: Verify after programming (default: True)
        erase: Erase mode before programming: 'all' (full chip erase),
               'used' (erase only used sectors), '' (default AURIXFlasher behavior)
        ucb: Enable UCB programming (default: False)
        connect: Connection mode: '0' (hot attach, no reset),
                 '1' (reset and halt), '' (default)
        start: After programming: 'on' (reset and run), 'off' (stay halted), '' (default)
        device_id: Device/DAP ID for multi-target setups (e.g. '0', '1')
    """
    args = ["-hex", hex_file]
    if verify:
        args += ["-v", "on"]
    else:
        args += ["-v", "off"]
    if erase:
        args += ["-e", erase]
    if ucb:
        args += ["-ucb", "on"]
    if connect:
        args += ["-connect", connect]
    if start:
        args += ["-start", start]
    if device_id:
        args += ["-id", device_id]
    return format_result(run_flasher(args, _config, timeout=300))


@mcp.tool()
def wiggle_read(
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

    return format_result(run_wiggle(args, cfg))


# ---------------------------------------------------------------------------
# Build Analysis MCP Tools
# ---------------------------------------------------------------------------

@mcp.tool()
def build_project(clean: bool = False) -> str:
    """Execute the project build command.
    Returns build success/failure, errors, warnings, and detected output files.
    Set clean=True to run clean before build.
    On success, parser caches are automatically invalidated so subsequent
    symbol/address queries reflect the new build.
    The build command and work_dir are configured in mcp_config.json.
    """
    bcfg = _get_build_cfg()
    command = bcfg.get("command")
    if not command:
        return json.dumps({"status": "error",
                           "message": "No build command configured. Set 'build.command' in mcp_config.json."})
    work_dir = bcfg.get("work_dir")
    timeout = bcfg.get("timeout", 300)

    if clean:
        # For GCC/make: append clean target; for batch/ps1: prepend 'clean'
        if "clean" not in command:
            if "make" in command or "cmake" in command or "ninja" in command:
                command = command + " clean"
            else:
                # Windows batch or PowerShell scripts
                clean_cmd = bcfg.get("clean_command", "")
                if clean_cmd:
                    command = clean_cmd + " && " + command

    runner = BuildRunner()
    result = runner.run(command, work_dir, timeout)

    # Invalidate parser cache after successful build
    if result.get("status") == "ok":
        _invalidate_parsers()
        result["parser_cache"] = "invalidated"

    return format_result(result)


@mcp.tool()
def reload_parsers() -> str:
    """Force reload all build analysis parsers (MAP/LST/ELF/MDF).
    Useful after external builds or when files change outside of build_project.
    """
    _invalidate_parsers()
    # Re-parse all configured files
    results = []
    kind, parser = _ensure_map_parser()
    if parser:
        results.append(f"{kind} map/lst: loaded")
    elf = _ensure_elf_parser()
    if elf:
        results.append(f"ELF: loaded ({len(elf.get_all_symbols())} symbols)")
    mdf = _ensure_mdf_parser()
    if mdf:
        results.append("MDF: loaded")
    if not results:
        return json.dumps({"status": "ok", "message": "No build files configured or found."})
    return json.dumps({"status": "ok", "message": "Parsers reloaded.", "loaded": results})


@mcp.tool()
def parse_symbols(summary: bool = True, search: str = "",
                  max_results: int = 100) -> str:
    """Parse MAP/LST file for symbol-address mappings and memory layout.
    Auto-detects toolchain (TASKING .map or GCC .lst).
    summary=True: return memory usage overview (TASKING only).
    search='X': filter symbols containing 'X' (case-insensitive).
    """
    kind, parser = _ensure_map_parser()
    if parser is None:
        return json.dumps({"status": "error",
                           "message": "No MAP or LST file found. Set 'project.map_file' or 'project.lst_file' in config."})

    result = {"toolchain": kind}

    if summary and kind == "tasking":
        result["memory_usage"] = parser.get_memory_summary()

    if search:
        result["symbols"] = parser.get_all_symbols(search=search,
                                                    max_results=max_results)
        result["match_count"] = len(result["symbols"])
    else:
        result["symbols"] = parser.get_all_symbols(max_results=max_results)
        result["total_shown"] = len(result["symbols"])

    return format_result(result)


@mcp.tool()
def parse_elf(functions: bool = False, variables: bool = False,
              sections: bool = True, search: str = "",
              max_results: int = 100) -> str:
    """Parse ELF file for symbols, sections, and debug info.
    Works with both TASKING and GCC ELF files.
    functions=True: show only function symbols.
    variables=True: show only variable symbols.
    search='X': filter symbols by name.
    """
    elf = _ensure_elf_parser()
    if elf is None:
        return json.dumps({"status": "error",
                           "message": "No ELF file found. Set 'project.elf_file' in config."})

    result = {
        "dwarf_available": elf._dwarf_available,
        "sections": elf.get_sections() if sections else [],
    }

    if functions:
        syms = elf.get_functions()
    elif variables:
        syms = elf.get_variables()
    else:
        syms = elf.get_all_symbols(search=search, max_results=max_results)

    if search and not (functions or variables):
        pass  # already filtered above
    elif search:
        syms = [s for s in syms if search.lower() in s["name"].lower()][:max_results]

    result["symbols"] = syms[:max_results]
    result["symbol_count"] = len(result["symbols"])

    return format_result(result)


@mcp.tool()
def lookup_symbol(name: str, source: str = "all") -> str:
    """Look up a symbol by name across MAP/LST/ELF files.
    Returns address, size, section, type, and source location.
    source: 'map', 'elf', or 'all' (default).
    """
    if not name:
        return json.dumps({"status": "error", "message": "Symbol name required."})

    results = []

    if source in ("all", "map"):
        kind, parser = _ensure_map_parser()
        if parser:
            sym = parser.lookup_symbol(name)
            if sym:
                results.append(sym)

    if source in ("all", "elf"):
        elf = _ensure_elf_parser()
        if elf:
            sym = elf.lookup_symbol(name)
            if sym:
                # Strip section_idx (internal detail)
                sym.pop("section_idx", None)
                results.append(sym)

    if not results:
        return format_result({"status": "not_found",
                              "message": f"Symbol '{name}' not found in any source.",
                              "searched": source})
    return format_result({"status": "ok", "results": results})


@mcp.tool()
def lookup_address(address: str, source: str = "all") -> str:
    """Find the nearest symbol for a given address.
    Returns symbol name, offset, section, and source file location.
    Address in hex (e.g. '0x800019de'). Useful for understanding what
    code/data is at a specific memory address.
    """
    if not address:
        return json.dumps({"status": "error", "message": "Address required."})

    try:
        addr = int(address, 16) if address.startswith("0x") else int(address, 16)
    except ValueError:
        return json.dumps({"status": "error",
                           "message": f"Invalid hex address: {address}"})

    results = []

    if source in ("all", "map"):
        kind, parser = _ensure_map_parser()
        if parser:
            sym = parser.lookup_address(addr)
            if sym:
                results.append(sym)

    if source in ("all", "elf"):
        elf = _ensure_elf_parser()
        if elf:
            sym = elf.lookup_address(addr)
            if sym:
                results.append(sym)

    # Also try MDF address translation
    mdf = _ensure_mdf_parser()
    addr_translation = None
    if mdf:
        addr_translation = mdf.translate_address(addr)

    if not results and not addr_translation:
        return format_result({"status": "not_found",
                              "message": f"No symbol found near address {address}",
                              "searched": source})

    resp = {"status": "ok", "address": address, "results": results}
    if addr_translation:
        resp["address_translation"] = addr_translation
    return format_result(resp)


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
    parser.add_argument("--port", type=int, default=8000,
                        help="SSE server port (default: 8000)")
    parser.add_argument("--host", type=str, default="127.0.0.1",
                        help="SSE server host (default: 127.0.0.1)")
    args = parser.parse_args()

    global _config
    _config = load_config(args.config)

    # Validate
    if not _config["wiggle_exe"]:
        print("WARNING: wiggle.exe not found. Set 'wiggle_exe' in config.",
              file=sys.stderr)

    print(f"AURIX MCP Server starting (transport={args.transport})", file=sys.stderr)
    print(f"  wiggle: {_config['wiggle_exe'] or 'NOT FOUND'}", file=sys.stderr)
    print(f"  flasher: {_config['aurix_flasher_exe'] or 'NOT FOUND'}", file=sys.stderr)
    print(f"  server: {_config['server']}", file=sys.stderr)
    proj = _config.get("project", {})
    bld = _config.get("build", {})
    if bld.get("command"):
        print(f"  build: {bld['command']}", file=sys.stderr)
    for key in ("elf_file", "map_file", "lst_file", "mdf_file"):
        if proj.get(key):
            print(f"  {key}: {proj[key]}", file=sys.stderr)

    if args.transport == "sse":
        print(f"  listen: {args.host}:{args.port}", file=sys.stderr)
        mcp.settings.host = args.host
        mcp.settings.port = args.port
        mcp.run(transport="sse")
    else:
        mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
