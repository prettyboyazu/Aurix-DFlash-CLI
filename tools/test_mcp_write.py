#!/usr/bin/env python3
"""Quick MCP integration test - calls wiggle_write to flash a hex file."""
import sys
import os
import json
import subprocess

# Test by directly running wiggle.exe (same as MCP server would do internally)
WIGGLE_EXE = r"D:\workspace\tas-dflash_-erase\wiggle\wiggle.exe"
HEX_FILE = r"D:\workspace\tas-dflash_-erase\TC334_Lite_Kit_FreeRTOS_Basic.hex"

def test_wiggle_write():
    """Test wiggle write command with hex file (simulates MCP wiggle_write call)."""
    if not os.path.exists(WIGGLE_EXE):
        print(f"ERROR: {WIGGLE_EXE} not found")
        return 1
    if not os.path.exists(HEX_FILE):
        print(f"ERROR: {HEX_FILE} not found")
        return 1

    print(f"=== MCP Write Test ===")
    print(f"Executable: {WIGGLE_EXE}")
    print(f"Hex file:   {HEX_FILE}")
    print(f"File size:  {os.path.getsize(HEX_FILE)} bytes")
    print()

    # Build command (same as MCP server's run_wiggle would do)
    cmd = [WIGGLE_EXE, "write", "-f", HEX_FILE, "--verify", "--json", "--server", "localhost"]
    
    print(f"Command: {' '.join(cmd)}")
    print(f"Working dir: {os.path.dirname(WIGGLE_EXE)}")
    print()
    print("--- Executing ---")
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=120,
            cwd=os.path.dirname(WIGGLE_EXE),
        )
        
        print(f"Return code: {result.returncode}")
        print()
        
        if result.stdout:
            print("STDOUT:")
            # Try to parse as JSON
            try:
                data = json.loads(result.stdout)
                print(json.dumps(data, indent=2))
            except json.JSONDecodeError:
                print(result.stdout)
        
        if result.stderr:
            print("\nSTDERR:")
            print(result.stderr)
            
        return result.returncode
        
    except subprocess.TimeoutExpired:
        print("ERROR: Command timed out after 120s")
        return -2
    except Exception as e:
        print(f"ERROR: {e}")
        return -1


if __name__ == "__main__":
    sys.exit(test_wiggle_write())
