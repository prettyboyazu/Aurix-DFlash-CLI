#!/usr/bin/env python3
"""
MCP Server Full Integration Test
Tests all MCP tools by simulating the same subprocess calls the server makes.
Generates a structured test report.
"""
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

# Configuration (same as mcp_config.json)
WIGGLE_EXE = r"D:\workspace\tas-dflash_-erase\wiggle\wiggle.exe"
FLASHER_EXE = r"C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\AURIXFlasher.exe"
HEX_FILE = r"D:\workspace\tas-dflash_-erase\TC334_Lite_Kit_FreeRTOS_Basic.hex"
SERVER = "localhost"
TIMEOUT = 30

results = []


def run_wiggle(args, timeout=TIMEOUT):
    """Run wiggle.exe with --json, same as MCP server's run_wiggle()."""
    cmd = [WIGGLE_EXE] + args + ["--json", "--server", SERVER]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           cwd=os.path.dirname(WIGGLE_EXE))
        stdout = r.stdout.strip()
        stderr = r.stderr.strip()
        # Parse JSON from stdout
        if stdout:
            for line in reversed(stdout.splitlines()):
                line = line.strip()
                if line.startswith("{"):
                    try:
                        return r.returncode, json.loads(line), stdout, stderr
                    except json.JSONDecodeError:
                        continue
        return r.returncode, None, stdout, stderr
    except subprocess.TimeoutExpired:
        return -2, None, "", "Timeout"
    except Exception as e:
        return -3, None, "", str(e)


def run_flasher(args, timeout=300):
    """Run AURIXFlasher.exe, same as MCP server's run_flasher()."""
    cmd = [FLASHER_EXE] + args
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           cwd=os.path.dirname(FLASHER_EXE))
        return r.returncode, r.stdout.strip(), r.stderr.strip()
    except subprocess.TimeoutExpired:
        return -2, "", "Timeout"
    except Exception as e:
        return -3, "", str(e)


def test(name, func):
    """Run a test and record result."""
    print(f"  [{len(results)+1:2d}] {name}...", end=" ", flush=True)
    t0 = time.time()
    try:
        status, detail = func()
    except Exception as e:
        status, detail = "ERROR", str(e)
    elapsed = time.time() - t0
    results.append({
        "id": len(results) + 1,
        "name": name,
        "status": status,
        "time_ms": int(elapsed * 1000),
        "detail": detail,
    })
    print(f"{status} ({int(elapsed*1000)}ms)")


# ============ Test Functions ============

def test_list():
    rc, data, stdout, stderr = run_wiggle(["list"])
    if rc == 0 and data and data.get("status") == "ok":
        targets = data.get("targets", [])
        return "PASS", f"Found {len(targets)} target(s): {[t.get('device','?') for t in targets]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_status():
    rc, data, stdout, stderr = run_wiggle(["status"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_info():
    rc, data, stdout, stderr = run_wiggle(["info"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_reg_read():
    rc, data, stdout, stderr = run_wiggle(["reg", "DMU_HF_STATUS"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_reg_list():
    rc, data, stdout, stderr = run_wiggle(["reg", "--list", "DMU"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_dump():
    # Read 16 bytes from DFlash base
    rc, data, stdout, stderr = run_wiggle(["dump", "0xAF000000", "0x10"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_read():
    # Read DFlash
    rc, data, stdout, stderr = run_wiggle(["read", "-a", "AF000000", "-l", "10"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_search():
    # Search for 0x00 pattern in DFlash (should find something)
    rc, data, stdout, stderr = run_wiggle(["search", "0xAF000000", "0x100", "00000000"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_poke():
    # Write to SRAM (safe area) - 0x70000000 is DSPR on TC33x
    rc, data, stdout, stderr = run_wiggle(["poke", "0x70000000", "0xDEADBEEF", "--width", "32"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_erase():
    # Erase 1 DFlash sector at base address
    rc, data, stdout, stderr = run_wiggle(["erase", "--addr", "AF000000", "--sectors", "1"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_reset():
    rc, data, stdout, stderr = run_wiggle(["reset"])
    if rc == 0 and data and data.get("status") == "ok":
        return "PASS", json.dumps(data, ensure_ascii=False)[:300]
    if data:
        return "FAIL", f"rc={rc}, response={json.dumps(data)[:200]}"
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


def test_wiggle_pflash():
    """Test PFlash programming via AURIXFlasher."""
    if not os.path.exists(HEX_FILE):
        return "SKIP", f"Hex file not found: {HEX_FILE}"
    if not os.path.exists(FLASHER_EXE):
        return "SKIP", f"AURIXFlasher not found: {FLASHER_EXE}"
    rc, stdout, stderr = run_flasher(["-hex", HEX_FILE, "-v", "on"], timeout=120)
    if rc == 0 and "Pass" in stdout:
        return "PASS", stdout[-300:]
    return "FAIL", f"rc={rc}, stdout={stdout[:200]}, stderr={stderr[:200]}"


# ============ Main ============

def main():
    print("=" * 60)
    print("  AURIX MCP Server - Full Integration Test")
    print(f"  Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  wiggle.exe: {WIGGLE_EXE}")
    print(f"  AURIXFlasher: {FLASHER_EXE}")
    print(f"  Hex file: {HEX_FILE}")
    print("=" * 60)
    print()

    # Check prerequisites
    print("[Prerequisites]")
    print(f"  wiggle.exe exists: {os.path.exists(WIGGLE_EXE)}")
    print(f"  AURIXFlasher exists: {os.path.exists(FLASHER_EXE)}")
    print(f"  Hex file exists: {os.path.exists(HEX_FILE)}")
    print()

    # Run tests
    print("[Running Tests]")
    
    test("wiggle_list - List targets", test_list)
    test("wiggle_status - Flash status register", test_status)
    test("wiggle_info - Device info & memory layout", test_info)
    test("wiggle_reg (read) - Read DMU_HF_STATUS", test_reg_read)
    test("wiggle_reg (list) - List DMU registers", test_reg_list)
    test("wiggle_dump - Hex dump DFlash 16 bytes", test_dump)
    test("wiggle_read - Read DFlash", test_read)
    test("wiggle_search - Search pattern in DFlash", test_search)
    test("wiggle_poke - Write SRAM (0x70000000)", test_poke)
    test("wiggle_erase - Erase 1 DFlash sector", test_erase)
    test("wiggle_reset - Reset MCU", test_reset)
    test("wiggle_pflash - PFlash program + verify", test_wiggle_pflash)

    # Summary
    print()
    print("[Summary]")
    passed = sum(1 for r in results if r["status"] == "PASS")
    failed = sum(1 for r in results if r["status"] == "FAIL")
    skipped = sum(1 for r in results if r["status"] == "SKIP")
    errors = sum(1 for r in results if r["status"] == "ERROR")
    total = len(results)
    print(f"  Total: {total}  PASS: {passed}  FAIL: {failed}  SKIP: {skipped}  ERROR: {errors}")
    print()

    # Generate report
    report_path = Path(r"D:\workspace\tas-dflash_-erase\wiggle\mcp_test_report.txt")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("=" * 70 + "\n")
        f.write("  AURIX MCP Server - Full Integration Test Report\n")
        f.write("=" * 70 + "\n\n")
        f.write(f"Date:           {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"wiggle.exe:     {WIGGLE_EXE}\n")
        f.write(f"AURIXFlasher:   {FLASHER_EXE}\n")
        f.write(f"Hex file:       {HEX_FILE}\n")
        f.write(f"Server:         {SERVER}\n\n")
        f.write("-" * 70 + "\n")
        f.write(f"{'#':<4} {'Status':<8} {'Time':<8} {'Test Name'}\n")
        f.write("-" * 70 + "\n")
        for r in results:
            f.write(f"{r['id']:<4} {r['status']:<8} {r['time_ms']:>5}ms  {r['name']}\n")
        f.write("-" * 70 + "\n")
        f.write(f"\nSummary: {passed} PASS / {failed} FAIL / {skipped} SKIP / {errors} ERROR (Total: {total})\n\n")
        
        f.write("=" * 70 + "\n")
        f.write("  Detailed Results\n")
        f.write("=" * 70 + "\n\n")
        for r in results:
            f.write(f"[{r['id']:02d}] {r['name']}\n")
            f.write(f"     Status: {r['status']} | Time: {r['time_ms']}ms\n")
            f.write(f"     Detail: {r['detail']}\n\n")

    print(f"  Report saved: {report_path}")
    return 0 if failed == 0 and errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
