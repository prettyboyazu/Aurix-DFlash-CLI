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
    """Find executable in well-known locations. Returns absolute path or None.

    Lookup order:
      1. env_var         — e.g. "AURIX_FLASHER_EXE" → use directly if set + file exists
      2. env_dir_var     — e.g. "AURIX_FLASHER_DIR"  → join with exe_name (or name)
      3. Infineon default install locations (C:\\Infineon\\, Program Files)
      4. script_dir and parent relative paths (MCP deployment layout)
      5. PATH via shutil.which
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

    # 3) Infineon default install location (Windows) — check BEFORE MCP dirs
    if sys.platform == "win32" and name.lower().startswith("aurixflasher"):
        import glob
        for pattern in (
            r"C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe",
            r"C:\Program Files\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe",
            r"C:\Program Files (x86)\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe",
        ):
            for hit in glob.glob(pattern):
                if os.path.isfile(hit):
                    return hit

    # 4) MCP deployment layout: script_dir and parent relative paths
    candidates = [
        script_dir / name,
        script_dir / "wiggle" / name,
        script_dir / "AURIXFlasher" / name,
        parent / "wiggle" / name,
        parent / "data" / name,
        parent / "AURIXFlasher" / name,
    ]

    for c in candidates:
        if c.exists() and c.is_file():
            return str(c)

    # 5) Fallback: PATH lookup (scoop / choco / manual install may put it on PATH)
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

    return format_result(run_wiggle(args, cfg, timeout=1800))


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

    return format_result(run_wiggle(args, cfg, timeout=3600))


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


# ---------------------------------------------------------------------------
# Register Metadata Tools (Python-side, no wiggle.exe needed)
# ---------------------------------------------------------------------------

_reg_cache: dict = {}  # device -> parsed JSON


def _find_register_defs_dir(cfg: dict) -> Optional[str]:
    """Locate the RegisterDefs directory relative to wiggle.exe or script."""
    candidates = []
    wiggle = cfg.get("wiggle_exe")
    if wiggle:
        candidates.append(Path(wiggle).parent / "RegisterDefs")
    script_dir = Path(__file__).resolve().parent
    parent = script_dir.parent
    candidates.extend([
        script_dir / "wiggle" / "RegisterDefs",
        parent / "wiggle" / "RegisterDefs",
        parent / "data" / "RegisterDefs",
    ])
    for c in candidates:
        if c.is_dir():
            return str(c)
    return None


def _detect_device(cfg: dict) -> Optional[str]:
    """Detect device by calling wiggle info, or from config."""
    dev = cfg.get("device")
    if dev:
        return dev
    # Try auto-detect via wiggle info
    try:
        result = run_wiggle(["info"], cfg, timeout=15)
        if result.get("status") == "ok":
            out = result.get("stdout", "")
            # Parse "Device: TC33x" or similar from output
            for line in out.splitlines():
                if "device" in line.lower() and ":" in line:
                    val = line.split(":", 1)[1].strip()
                    if val.startswith("TC"):
                        # Extract family: "TC33x_A_step" -> "TC33x"
                        parts = val.split("_")
                        return parts[0] if parts else val
    except Exception:
        pass
    return None


def _load_register_defs(device: str, cfg: dict) -> Optional[dict]:
    """Load and cache RegisterDefs JSON for a device."""
    if device in _reg_cache:
        return _reg_cache[device]

    defs_dir = _find_register_defs_dir(cfg)
    if not defs_dir:
        return None

    # Try exact match first, then case-insensitive
    target_file = Path(defs_dir) / f"{device}-full.json"
    if not target_file.is_file():
        # Case-insensitive search
        for f in Path(defs_dir).glob("*-full.json"):
            if f.stem.lower().startswith(device.lower()):
                target_file = f
                break
        else:
            return None

    with open(target_file, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    _reg_cache[device] = data
    return data


# ── Fuzzy search: synonym table & scoring engine ─────────────────────

# AI-friendly term → hardware peripheral/register abbreviations
# Used for bidirectional matching: AI says "uart" ↔ finds ASCLIN
_REG_SYNONYMS: dict[str, list[str]] = {
    # Communication
    "uart":     ["asclin", "serial"],
    "serial":   ["asclin", "uart"],
    "spi":      ["qspi", "asclin", "spi"],
    "i2c":      ["i2c"],
    "can":      ["can", "mcan"],
    "lin":      ["asclin"],
    # Timers / PWM
    "timer":    ["gtm", "ccu6", "gpt120", "stm", "tom"],
    "pwm":      ["gtm", "ccu6", "tom", "atom"],
    "capture":  ["gtm", "ccu6"],
    "counter":  ["ccu6", "gpt120", "stm"],
    # Analog
    "adc":      ["evadc", "adc"],
    "dac":      ["dac"],
    "analog":   ["evadc"],
    # Memory / Flash
    "flash":    ["dmu", "pfi", "pmu", "flash"],
    "program":  ["dmu", "pfi"],
    "eeprom":   ["dmu"],
    "memory":   ["dmu", "pmu", "sbcu"],
    # GPIO / Pins
    "gpio":     ["p00","p02","p10","p11","p13","p14","p15",
                 "p20","p21","p22","p23"],
    "port":     ["p00","p02","p10","p11"],
    # Clock / Reset
    "clock":    ["scu", "ccu", "pll", "osc"],
    "reset":    ["scu", "reset"],
    "pll":      ["scu", "pll"],
    # Interrupts
    "interrupt": ["int", "src", "irq", "nvic"],
    "irq":      ["int", "src", "irq"],
    "vector":   ["int", "src"],
    # DMA
    "dma":      ["dma"],
    "transfer": ["dma"],
    # Watchdog
    "watchdog": ["wdt", "smu"],
    "wdt":      ["wdt", "smu"],
    # Safety / Protection
    "safety":   ["smu", "pms", "sbcu"],
    "protect":  ["smu", "pms"],
    # Debug
    "debug":    ["cbs", "cpu", "mcu"],
    "trace":    ["cbs", "cpu"],
    "breakpoint": ["cbs", "cpu"],
    # Ethernet
    "ethernet": ["eth", "gmac"],
    "eth":      ["eth", "gmac"],
    # USB
    "usb":      ["usb"],
    # SENT protocol
    "sent":     ["sent"],
    # HSM (Hardware Security Module)
    "security": ["hsm", "smu"],
    "crypto":   ["hsm"],
    "hsm":      ["hsm"],
    # Core / CPU
    "core":     ["cpu0", "cpu"],
    "cpu":      ["cpu0", "cpu"],
}


def _tokenize(name: str) -> list[str]:
    """Split register/peripheral name into searchable tokens.
    'HF_STATUS' -> ['HF', 'STATUS']
    'GTM_TOM0_CTRL' -> ['GTM', 'TOM', '0', 'CTRL']
    """
    import re as _re
    # Split on _ first, then camelCase boundaries
    parts = name.replace("_", " ").split()
    tokens = []
    for p in parts:
        # Split camelCase: "hfStatus" -> ["hf", "Status"]
        tokens.extend(_re.findall(r'[A-Z]+(?=[A-Z][a-z])|[A-Za-z]+|\d+', p))
    return [t for t in tokens if t]


def _score_match(keyword: str, name: str, desc: str) -> int:
    """Score how well keyword matches a register name + description.
    Returns 0 (no match) to 100 (perfect match).
    Scoring requires the REGISTER ITSELF to be relevant;
    peripheral-level matches only provide a small bonus.
    """
    kw = keyword.lower().strip()
    name_lower = name.lower()
    desc_lower = desc.lower()
    tokens = _tokenize(name)
    tokens_lower = [t.lower() for t in tokens]
    score = 0

    # 1. Exact full-name match
    if name_lower == kw:
        return 100

    # 2. Full name starts with keyword
    if name_lower.startswith(kw):
        score = max(score, 90)

    # 3. Keyword is substring of full name
    if kw in name_lower:
        score = max(score, 70)

    # 4. Keyword matches a token exactly
    if kw in tokens_lower:
        score = max(score, 85)

    # 5. Token starts with keyword (e.g. "stat" matches "STATUS")
    for t in tokens_lower:
        if t.startswith(kw) and len(kw) >= 3:
            score = max(score, 65)
            break

    # 6. Keyword starts with a token (e.g. "status_reg" matches "STATUS")
    for t in tokens_lower:
        if len(t) >= 3 and kw.startswith(t):
            score = max(score, 55)
            break

    # 7. Description contains keyword
    if kw in desc_lower:
        score = max(score, 60)

    # 8. Each word in desc matches keyword
    desc_words = desc_lower.split()
    if kw in desc_words:
        score = max(score, 65)

    # 9. Synonym matching — token-level only, no substring
    kw_base = kw.split()[0] if " " in kw else kw
    synonyms = _REG_SYNONYMS.get(kw_base, [])
    if synonyms:
        for syn in synonyms:
            syn_l = syn.lower()
            # Synonym must match a register name TOKEN exactly
            for t in tokens_lower:
                if t == syn_l:
                    score = max(score, 75)
                    break

    # 10. Reverse synonym: keyword is a hardware abbreviation,
    #     check if the AI-friendly term matches a register token
    for syn_key, syn_vals in _REG_SYNONYMS.items():
        if kw_base == syn_key:
            continue
        if kw_base in [v.lower() for v in syn_vals]:
            # Only match if syn_key appears as a token in the name
            if syn_key.lower() in tokens_lower:
                score = max(score, 70)
            elif syn_key.lower() in desc_lower:
                score = max(score, 55)

    # 11. Fuzzy subsequence — only for keywords >= 4 chars, low score
    if score < 30 and len(kw) >= 4:
        ki = 0
        for ch in name_lower:
            if ki < len(kw) and ch == kw[ki]:
                ki += 1
        if ki == len(kw):
            score = max(score, 20)

    return score


def _periph_relevance(keyword: str, periph_name: str, periph_desc: str) -> int:
    """Score how relevant a peripheral is to the search keyword.
    Checks name tokens AND synonym table for AI→hardware mapping.
    Returns 0-100.
    """
    kw = keyword.lower().strip()
    pname = periph_name.lower()
    ptokens = [t.lower() for t in _tokenize(periph_name)]
    score = 0

    # Direct match
    if pname == kw:
        return 100
    if pname.startswith(kw):
        score = max(score, 85)
    if kw in pname:
        score = max(score, 70)
    for t in ptokens:
        if t == kw:
            score = max(score, 85)
        elif t.startswith(kw) and len(kw) >= 2:
            score = max(score, 65)

    # Description match
    if kw in periph_desc.lower():
        score = max(score, 50)

    # Synonym match: keyword → hardware peripheral names
    kw_base = kw.split()[0] if " " in kw else kw
    for syn in _REG_SYNONYMS.get(kw_base, []):
        syn_l = syn.lower()
        if syn_l in ptokens:
            score = max(score, 80)
        elif syn_l == pname:
            # Exact peripheral name match (e.g. "p00" == "p00")
            score = max(score, 80)
        elif pname.startswith(syn_l) or syn_l.startswith(pname):
            # Prefix match (e.g. "p00" starts with "p0", or "asclin0" starts with "asclin")
            if len(syn_l) >= 3:
                score = max(score, 65)

    # Reverse: keyword is hardware name, peripheral matches AI term
    for syn_key, syn_vals in _REG_SYNONYMS.items():
        if kw_base == syn_key:
            continue
        if kw_base in [v.lower() for v in syn_vals]:
            if syn_key.lower() in ptokens:
                score = max(score, 75)
            elif syn_key.lower() in pname:
                score = max(score, 60)

    return score


@mcp.tool()
def reg_peripherals(device: str = "") -> str:
    """List all peripherals of the target MCU with register counts.
    Reads from SVD-derived RegisterDefs (no hardware connection needed).
    If device is empty, auto-detects from config or hardware.
    """
    cfg = dict(_config)
    if device:
        cfg["device"] = device

    dev = _detect_device(cfg)
    if not dev:
        return json.dumps({"status": "error",
                           "message": "Device not specified and auto-detect failed. Set 'device' in config or pass device parameter."})

    data = _load_register_defs(dev, cfg)
    if not data:
        return json.dumps({"status": "error",
                           "message": f"No RegisterDefs found for {dev}. Check wiggle/RegisterDefs/ directory."})

    summary = data.get("summary", {})
    peripherals = []
    for p in data.get("peripherals", []):
        peripherals.append({
            "name": p["name"],
            "baseAddress": p.get("baseAddress", ""),
            "registerCount": p.get("registerCount", len(p.get("registers", []))),
            "desc": p.get("desc", ""),
        })

    return format_result({
        "status": "ok",
        "device": dev,
        "svdSource": data.get("svdSource", ""),
        "total_peripherals": summary.get("peripherals", len(peripherals)),
        "total_registers": summary.get("registers", 0),
        "total_fields": summary.get("fields", 0),
        "peripherals": peripherals,
    })


@mcp.tool()
def reg_fields(name: str, device: str = "") -> str:
    """Show complete bit-field definition of a register.
    Returns every field with its bit range, access type, description,
    and enumerated values (if any).
    name: Register name in 'PERIPHERAL.REGISTER' format (e.g. 'DMU.HF.STATUS')
          or just register name if unique (e.g. 'CLC').
    """
    if not name:
        return json.dumps({"status": "error", "message": "Register name required."})

    cfg = dict(_config)
    if device:
        cfg["device"] = device

    dev = _detect_device(cfg)
    if not dev:
        return json.dumps({"status": "error",
                           "message": "Device not specified and auto-detect failed."})

    data = _load_register_defs(dev, cfg)
    if not data:
        return json.dumps({"status": "error",
                           "message": f"No RegisterDefs found for {dev}."})

    # Parse name: "DMU.HF.STATUS" or "CLC"
    parts = name.split(".")
    if len(parts) >= 2:
        target_periph = parts[0].upper()
        target_reg = parts[-1].upper()
    else:
        target_periph = None
        target_reg = parts[0].upper()

    # Search for matching register
    matches = []
    for p in data.get("peripherals", []):
        if target_periph and p["name"].upper() != target_periph:
            continue
        for r in p.get("registers", []):
            if r["name"].upper() == target_reg:
                matches.append(r)

    if not matches:
        # Fuzzy fallback: try scoring search when exact match fails
        fuzzy_hits = []
        for p in data.get("peripherals", []):
            if target_periph and p["name"].upper() != target_periph:
                continue
            for r in p.get("registers", []):
                full = f"{p['name']}.{r['name']}"
                s = _score_match(target_reg, r["name"], r.get("desc", ""))
                s2 = _score_match(target_reg, full, r.get("desc", ""))
                best = max(s, s2)
                if best >= 40:
                    fuzzy_hits.append((best, p, r))
        if fuzzy_hits:
            fuzzy_hits.sort(key=lambda x: -x[0])
            suggestions = []
            for sc, p, r in fuzzy_hits[:10]:
                suggestions.append({
                    "name": f"{p['name']}.{r['name']}",
                    "desc": r.get("desc", ""),
                    "score": sc,
                })
            return format_result({
                "status": "not_found_fuzzy",
                "message": f"Exact register '{name}' not found, but similar registers found:",
                "device": dev,
                "suggestions": suggestions,
                "hint": "Use the 'name' field from suggestions for exact lookup.",
            })
        return format_result({"status": "not_found",
                              "message": f"Register '{name}' not found in {dev} RegisterDefs.",
                              "hint": "Use reg_search to find register names."})

    if len(matches) > 1 and not target_periph:
        # Ambiguous — return list of matches
        options = [{"peripheral": r["peripheral"], "name": r["name"],
                     "address": r.get("address", "")} for r in matches]
        return format_result({"status": "ambiguous",
                              "message": f"Register '{name}' found in {len(matches)} peripherals. Specify as PERIPHERAL.{name}.",
                              "matches": options[:20]})

    reg = matches[0]
    fields_out = []
    for f in reg.get("fields", []):
        entry = {
            "name": f["name"],
            "bits": f"[{f['msb']}:{f['lsb']}]" if f['msb'] != f['lsb'] else f"[{f['lsb']}]",
            "access": f.get("access", ""),
            "desc": f.get("desc", ""),
        }
        if f.get("values"):
            entry["values"] = [{"value": v["value"], "desc": v.get("desc", "")}
                               for v in f["values"]]
        fields_out.append(entry)

    return format_result({
        "status": "ok",
        "device": dev,
        "register": reg.get("fullName", reg["name"]),
        "peripheral": reg.get("peripheral", ""),
        "address": reg.get("address", ""),
        "offset": reg.get("offset", ""),
        "size": reg.get("size", 32),
        "access": reg.get("access", ""),
        "resetValue": reg.get("resetValue", ""),
        "desc": reg.get("desc", ""),
        "fieldCount": len(fields_out),
        "fields": fields_out,
    })


@mcp.tool()
def reg_search(keyword: str, device: str = "", max_results: int = 50) -> str:
    """Fuzzy search registers by keyword with relevance scoring.
    Supports AI-friendly terms: 'uart' finds ASCLIN, 'gpio' finds P00/P10,
    'flash' finds DMU, 'timer' finds GTM/CCU6, etc.
    Searches register names, peripheral names, and descriptions.
    Results sorted by relevance score (0-100).
    keyword: Search term — can be hardware name ('WDT'), function ('watchdog'),
             or partial name ('STAT', 'CLK', 'TIMER').
    max_results: Maximum number of results (default 50).
    """
    if not keyword:
        return json.dumps({"status": "error", "message": "Search keyword required."})

    cfg = dict(_config)
    if device:
        cfg["device"] = device

    dev = _detect_device(cfg)
    if not dev:
        return json.dumps({"status": "error",
                           "message": "Device not specified and auto-detect failed."})

    data = _load_register_defs(dev, cfg)
    if not data:
        return json.dumps({"status": "error",
                           "message": f"No RegisterDefs found for {dev}."})

    # Score all registers against keyword
    # Strategy: register-level score is primary;
    # peripheral-level match adds a bonus (AI searching "uart" → ASCLIN peripheral).
    MIN_SCORE = 40  # Filter noise: only show meaningful matches
    scored: list[tuple[int, dict]] = []
    for p in data.get("peripherals", []):
        periph_name = p["name"]
        periph_desc = p.get("desc", "")
        # Peripheral-level relevance (uses synonym-aware scoring)
        periph_s = _periph_relevance(keyword, periph_name, periph_desc)
        for r in p.get("registers", []):
            reg_name = r["name"]
            full_name = f"{periph_name}.{reg_name}"
            desc = r.get("desc", "")

            # Register-level score (name + desc, no synonym peripheral leak)
            reg_s = _score_match(keyword, full_name, desc)

            # Combined score
            if reg_s > 0 and periph_s > 0:
                # Both match: boost (cap at 100)
                s = min(reg_s + periph_s // 3, 100)
            elif reg_s > 0:
                # Register matches but peripheral doesn't
                s = reg_s
            elif periph_s >= 65:
                # Peripheral strongly matches (synonym/name)
                # All registers in this peripheral are potentially relevant
                s = periph_s // 2
            else:
                s = 0

            if s >= MIN_SCORE:
                scored.append((s, {
                    "name": full_name,
                    "address": r.get("address", ""),
                    "size": r.get("size", 32),
                    "access": r.get("access", ""),
                    "desc": desc,
                    "fieldCount": len(r.get("fields", [])),
                    "score": s,
                }))

    # Sort by score descending, then by name for stable ordering
    scored.sort(key=lambda x: (-x[0], x[1]["name"]))
    results = [item for _, item in scored[:max_results]]

    return format_result({
        "status": "ok",
        "device": dev,
        "keyword": keyword,
        "totalMatches": len(scored),
        "matchCount": len(results),
        "truncated": len(scored) > max_results,
        "results": results,
    })


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

    args = ["poke", address, value, "--width", str(width), "--dangerous"]
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
def aurix_pflash(
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
def wiggle_rewrite(
    file_path: str = "",
    address: str = "",
    data: str = "",
    backup: bool = False,
    backup_path: str = "",
    verify: bool = True,
    reset_mcu: bool = False,
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Write to DFlash at arbitrary address using Read-Modify-Write.
    Automatically handles sector alignment by reading, merging, erasing,
    and writing back entire sectors.

    Data source (mutually exclusive):
      file_path: Input file (.hex or .bin)
      data: Hex data string (e.g. 'DEADBEEF', '0102030405060708')

    Args:
        file_path: Input file path (.hex or .bin)
        address: Start address in hex (required for .bin and data)
        data: Hex data string (alternative to file_path)
        backup: Backup affected sectors before rewrite
        backup_path: Backup file path (auto-generated if empty and backup=True)
        verify: Read back and verify after writing (default: True)
        reset_mcu: Reset MCU after rewrite (default: False)
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["rewrite"]
    if file_path:
        args += ["-f", file_path]
    if address:
        args += ["-a", address]
    if data:
        args += ["--data", data]
    if backup:
        args.append("--backup")
        if backup_path:
            args.append(backup_path)
    if verify:
        args.append("--verify")
    if reset_mcu:
        args.append("--reset")

    return format_result(run_wiggle(args, cfg, timeout=1800))


@mcp.tool()
def wiggle_restore(
    file_path: str,
    no_verify: bool = False,
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """Restore DFlash from a backup file (erase + write + verify).
    This is a destructive operation that erases the target area first.

    Args:
        file_path: Backup file to restore (.bin or .hex)
        no_verify: Skip verification after restore (default: False)
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    args = ["restore", "-f", file_path]
    if no_verify:
        args.append("--no-verify")

    return format_result(run_wiggle(args, cfg, timeout=3600))


@mcp.tool()
def wiggle_ucb(
    action: str,
    address: str = "",
    length: str = "",
    sectors: int = 1,
    file_path: str = "",
    verify: bool = False,
    output_file: str = "",
    server: str = "",
    target: str = "",
    device: str = "",
) -> str:
    """UCB (User Configuration Block) operations. TC3XX only.
    WARNING: UCB write/erase are high-risk operations that may lock the device.

    Args:
        action: 'read', 'write', or 'erase'
        address: Start address in hex (optional, defaults to UCB base)
        length: Length in hex (for read, defaults to entire UCB)
        sectors: Number of sectors to erase (for erase, default: 1)
        file_path: Input file for write (.hex or .bin)
        verify: Verify after operation (default: False)
        output_file: Output file for read (.hex or .bin)
    """
    cfg = dict(_config)
    if server: cfg["server"] = server
    if target: cfg["target"] = target
    if device: cfg["device"] = device

    if action not in ("read", "write", "erase"):
        return json.dumps({"status": "error", "message": "action must be 'read', 'write', or 'erase'"})

    args = ["ucb", action]
    if address:
        args += ["-a", address]
    if action == "read":
        if length:
            args += ["-l", length]
        if output_file:
            args += ["-o", output_file]
    elif action == "write":
        if not file_path:
            return json.dumps({"status": "error", "message": "file_path required for UCB write"})
        args += ["-f", file_path]
    elif action == "erase":
        if sectors:
            args += ["-s", str(sectors)]
    if verify:
        args.append("--verify")

    return format_result(run_wiggle(args, cfg))


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
                    command = clean_cmd + " && " + command if sys.platform != "win32" else clean_cmd + " & " + command

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
        addr = int(address, 16)
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
