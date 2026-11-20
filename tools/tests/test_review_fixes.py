"""Regression tests for the 2026-11-19 review fixes.

Plain Python (no pytest dependency): `py -3.15 tests/test_review_fixes.py`
Run from anywhere; paths are derived from this file's location.
"""
import json
import os
import queue
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import aurix_mcp_server as srv
from build_analysis import BuildRunner

from mcp.server.mcpserver.exceptions import ToolError

PASS, FAIL = [], []


def check(name, cond, detail=""):
    (PASS if cond else FAIL).append(name)
    print(f"  {'PASS' if cond else 'FAIL'}  {name}" + (f"  [{detail}]" if detail and not cond else ""))


# ---------------------------------------------------------------------------
# H1: reg_fields fullName matching (DMU.SF.STATUS must not return HF.STATUS)
# ---------------------------------------------------------------------------
print("[H1] reg_fields fullName matching (TC39xB, offline)")
r = json.loads(srv.reg_fields("DMU.SF.STATUS", device="TC39xB"))
check("SF.STATUS resolves to SF (0xF8060010)",
      r.get("address") == "0xF8060010" and r.get("register") == "SF.STATUS",
      json.dumps(r)[:120])
r = json.loads(srv.reg_fields("DMU.HF.STATUS", device="TC39xB"))
check("HF.STATUS resolves to HF (0xF8040010)",
      r.get("address") == "0xF8040010" and r.get("register") == "HF.STATUS",
      json.dumps(r)[:120])
r = json.loads(srv.reg_fields("DMU.STATUS", device="TC39xB"))
names = [m.get("fullName") for m in r.get("matches", [])]
check("DMU.STATUS reports ambiguity (both fullNames)",
      r.get("status") == "ambiguous" and "HF.STATUS" in names and "SF.STATUS" in names,
      json.dumps(r)[:160])

# ---------------------------------------------------------------------------
# H2: build_project invalidates parser cache on success
# ---------------------------------------------------------------------------
print("[H2] build_project cache invalidation")
srv._config = {"build": {"command": "make all", "work_dir": None, "timeout": 5}}
invalidated = []
orig_inval = srv._invalidate_parsers
srv._invalidate_parsers = lambda: invalidated.append(True)
orig_run = BuildRunner.run
BuildRunner.run = lambda self, cmd, wd, t: {"success": True, "returncode": 0}
try:
    out = json.loads(srv.build_project())
    check("success build invalidates cache", invalidated == [True] and out.get("parser_cache") == "invalidated")
    BuildRunner.run = lambda self, cmd, wd, t: {"success": False, "returncode": 2}
    try:
        srv.build_project()
        check("failed build -> ToolError (isError path)", False, "no exception")
    except ToolError as e:
        check("failed build -> ToolError (isError path)", "success" in str(e))
    check("failed build does not invalidate cache", invalidated == [True])
finally:
    BuildRunner.run = orig_run
    srv._invalidate_parsers = orig_inval
    srv._config = {}

# ---------------------------------------------------------------------------
# H3: clean derivation (make / ninja / cmake / unknown)
# ---------------------------------------------------------------------------
print("[H3] _derive_clean_command")
d = srv._derive_clean_command
check("make -C build all -> make -C build clean", d("make -C build all") == ["make", "-C", "build", "clean"])
check("make -j8 all keeps attached flag", d("make -j8 all") == ["make", "-j8", "clean"])
check("make -j 8 all keeps numeric jobs value", d("make -j 8 all") == ["make", "-j", "8", "clean"])
check("make -j all treats all as target", d("make -j all") == ["make", "-j", "clean"])
check("bare make -> make clean", d("make") == ["make", "clean"])
check("ninja -C out -> ninja -C out -t clean", d("ninja -C out") == ["ninja", "-C", "out", "-t", "clean"])
check("cmake --build b -> + --target clean", d("cmake --build b") == ["cmake", "--build", "b", "--target", "clean"])
check("cmake with --target refuses to guess", d("cmake --build b --target app") is None)
check("unknown builder returns None", d("foo_build.sh --all") is None)
check("quoted make path works",
      d('"C:\\Program Files\\make.exe" -C b all') == ["C:\\Program Files\\make.exe", "-C", "b", "clean"])
check("argv-list build command accepted", d(["make", "-C", "b", "all"]) == ["make", "-C", "b", "clean"])

# clean=True actually runs the derived clean first
print("[H3b] build_project(clean=True) runs clean before build")
srv._config = {"build": {"command": "make -C b all", "work_dir": None, "timeout": 5}}
calls = []
BuildRunner.run = lambda self, cmd, wd, t: (calls.append(cmd), {"success": True, "returncode": 0})[1]
try:
    json.loads(srv.build_project(clean=True))
    check("two runs: clean argv first, build string second",
          calls and calls[0] == ["make", "-C", "b", "clean"] and calls[1] == "make -C b all",
          str(calls))
    calls.clear()
    srv._config["build"]["command"] = "my_builder.bat"
    srv._config["build"]["clean_command"] = "clean.bat"
    json.loads(srv.build_project(clean=True))
    check("configured clean_command used verbatim",
          calls and calls[0] == "clean.bat" and calls[1] == "my_builder.bat", str(calls))
finally:
    BuildRunner.run = orig_run
    srv._config = {}

# ---------------------------------------------------------------------------
# M7: BuildRunner._prepare_cmd (quoted .bat with spaces)
# ---------------------------------------------------------------------------
print("[M7] BuildRunner._prepare_cmd")
p = BuildRunner._prepare_cmd
if sys.platform == "win32":
    check("quoted .bat with space -> cmd.exe /c wrapper",
          p('"C:\\Program Files\\build.bat" --fast') == ["cmd.exe", "/c", "C:\\Program Files\\build.bat", "--fast"],
          str(p('"C:\\Program Files\\build.bat" --fast')))
    check("bare quoted .bat detected", p('"b.bat"') == ["cmd.exe", "/c", "b.bat"])
check("plain command untouched", p("make all") == ["make", "all"])
check("argv list passthrough", p(["make", "clean"]) == ["make", "clean"])
check("quoted non-bat args unquoted", p('gcc "my file.c" -o out') == ["gcc", "my file.c", "-o", "out"])

# ---------------------------------------------------------------------------
# M8/M10: config validation and relative-path resolution
# ---------------------------------------------------------------------------
print("[M8/M10] _load_config")
with tempfile.TemporaryDirectory() as td:
    # make the relative wiggle_exe target exist so the bundled fallback doesn't kick in
    os.makedirs(os.path.join(td, "wiggle"), exist_ok=True)
    open(os.path.join(td, "wiggle", "wiggle.exe"), "w").close()
    cfgf = os.path.join(td, "mcp_config.json")
    with open(cfgf, "w", encoding="utf-8") as f:
        json.dump({"wiggle_exe": "wiggle/wiggle.exe",
                   "das_home": os.path.join(td, "no_such_das"),
                   "project": {"elf_file": "out/x.elf"}}, f)
    cfg = srv.load_config(cfgf)
    check("relative wiggle_exe resolved vs config dir",
          cfg["wiggle_exe"].lower() == os.path.join(td, "wiggle", "wiggle.exe").lower(), cfg["wiggle_exe"])
    check("relative project path resolved vs config dir",
          cfg["project"]["elf_file"].lower() == os.path.join(td, "out", "x.elf").lower(), cfg["project"]["elf_file"])
    check("nonexistent das_home dropped", cfg.get("das_home") is None or os.path.isdir(cfg["das_home"]))

# ---------------------------------------------------------------------------
# M5/M6: WiggleSession timeout / EOF / kill (no real subprocess)
# ---------------------------------------------------------------------------
print("[M5/M6] WiggleSession behaviour")


class FakeProc:
    def __init__(self):
        self.stdin = open(os.devnull, "w")
        self.terminated = False

    def terminate(self):
        self.terminated = True

    def wait(self, timeout=None):
        return 0


def bare_session():
    s = object.__new__(srv.WiggleSession)
    s.proc = FakeProc()
    s.lock = threading.Lock()
    s.next_id = 0
    s.dead = False
    s._stdout = None
    s._queue = queue.Queue()
    return s


class FakeStdout:
    def __init__(self, lines):
        self._lines = list(lines)

    def __iter__(self):
        return iter(self._lines)


s = bare_session()
s._stdout = FakeStdout(['{"id":1,"status":"ok"}'])
s._reader = threading.Thread(target=s._read_loop, daemon=True)
s._reader.start()
time.sleep(0.2)
check("request returns queued response (id match)",
      s.request("ping", timeout=1) == {"id": 1, "status": "ok"})

s2 = bare_session()
s2._reader = threading.Thread(target=lambda: None, daemon=True)
t0 = time.monotonic()
try:
    s2.request("ping", timeout=0.3)
    check("timeout raises and marks dead", False, "no exception")
except RuntimeError as e:
    check("timeout raises and marks dead", "timeout" in str(e) and s2.dead, str(e))
check("timeout did not hang (fast)", time.monotonic() - t0 < 2)

s3 = bare_session()
s3._queue.put(None)
s3._reader = threading.Thread(target=lambda: None, daemon=True)
try:
    s3.request("ping", timeout=1)
    check("EOF sentinel raises and marks dead", False, "no exception")
except RuntimeError as e:
    check("EOF sentinel raises and marks dead", "EOF" in str(e) and s3.dead, str(e))

s4 = bare_session()
s4._reader = threading.Thread(target=lambda: None, daemon=True)
fake4 = s4.proc
s4.kill()
check("kill marks dead, terminates and detaches proc",
      s4.dead and fake4.terminated and s4.proc is None)

# ---------------------------------------------------------------------------
# N5: _read_loop must use the cached stream (proc already None, no race)
# ---------------------------------------------------------------------------
print("[N5] reader uses cached _stdout")
s5 = bare_session()
s5._stdout = FakeStdout(['{"id":1,"status":"ok"}'])
s5.proc = None  # as kill() leaves it
s5._read_loop()  # must not raise despite proc being None
line5, eof5 = s5._queue.get_nowait(), s5._queue.get_nowait()
check("reader survives proc=None and still puts EOF sentinel",
      json.loads(line5)["status"] == "ok" and eof5 is None)

# ---------------------------------------------------------------------------
# N6: response validation vs the REAL serve protocol
# wiggle serve responses carry status/message only — no request id echo
# (verified against the binary). A no-id response MUST be accepted; a
# mismatched id (if a peer ever echoes ids) or stray output MUST kill.
# ---------------------------------------------------------------------------
print("[N6] session response validation (real protocol shape)")
s6 = bare_session()
s6._reader = threading.Thread(target=lambda: None, daemon=True)
s6._queue.put('{"status": "ok"}')
try:
    r6 = s6.request("ping", timeout=1)
    check("no-id response accepted (real protocol shape)",
          r6 == {"status": "ok"} and not s6.dead, str(r6))
except RuntimeError as e:
    check("no-id response accepted (real protocol shape)", False, str(e))

s6b = bare_session()
s6b._reader = threading.Thread(target=lambda: None, daemon=True)
s6b._queue.put('{"id": 99, "status": "ok"}')
try:
    s6b.request("ping", timeout=1)
    check("mismatched id echo kills session", False, "no exception")
except RuntimeError as e:
    check("mismatched id echo kills session", "mismatch" in str(e) and s6b.dead, str(e))

s6c = bare_session()
s6c._reader = threading.Thread(target=lambda: None, daemon=True)
s6c._queue.put('123\n')
try:
    s6c.request("ping", timeout=1)
    check("non-object JSON line kills session", False, "no exception")
except RuntimeError as e:
    check("non-object JSON line kills session", "non-object" in str(e) and s6c.dead, str(e))

s7 = bare_session()
s7._reader = threading.Thread(target=lambda: None, daemon=True)
s7._queue.put('garbage not json\n')
try:
    s7.request("ping", timeout=1)
    check("non-JSON line kills session", False, "no exception")
except RuntimeError as e:
    check("non-JSON line kills session", "non-JSON" in str(e) and s7.dead, str(e))

# ---------------------------------------------------------------------------
# N4: config "timeout": null must fall back to 120 (never queue.get(None))
# ---------------------------------------------------------------------------
print("[N4] session_request timeout normalization")


class RecSession:
    def request(self, cmd, timeout=120, **kw):
        self.seen_timeout = timeout
        return {"status": "ok"}


srv._config = {"timeout": None}
rec = RecSession()
orig_gs = srv._get_session
srv._get_session = lambda: rec
try:
    srv.session_request("cli")
    check("null config timeout falls back to 120", rec.seen_timeout == 120, str(rec.seen_timeout))
finally:
    srv._get_session = orig_gs
    srv._config = {}

# ---------------------------------------------------------------------------
# N2: override call kills the persistent session and reaches the CLI path
# ---------------------------------------------------------------------------
print("[N2] override call releases session, uses CLI")


class FakeCP:
    stdout = ""
    stderr = ""
    returncode = -1


srv._config = {"wiggle_exe": "fake.exe", "server": "base"}
sn = bare_session()
srv._session = sn
cli_calls = []
orig_run2 = srv.subprocess.run
srv.subprocess.run = lambda *a, **k: (cli_calls.append(1), FakeCP())[1]
try:
    r = srv.run_wiggle(["read"], {"wiggle_exe": "fake.exe", "server": "other"})
    check("override call reaches CLI path", cli_calls == [1] and r.get("status") == "error",
          f"calls={len(cli_calls)} r={str(r)[:60]}")
    check("override call killed persistent session (hardware released)",
          sn.dead and srv._session is None)

    sm = bare_session()
    srv._session = sm
    orig_sr = srv.session_request
    srv.session_request = lambda *a, **kw: {"status": "ok", "via": "session"}
    r2 = srv.run_wiggle(["read"], {"wiggle_exe": "fake.exe", "server": "base"})
    check("matching params keep the session", r2.get("via") == "session" and not sm.dead)
    srv.session_request = orig_sr
finally:
    srv.subprocess.run = orig_run2
    srv._session = None
    srv._config = {}

# ---------------------------------------------------------------------------
# X1: session 'cli' dialect {"exit_code": N, "id": M, "message": ...} — no status
# ---------------------------------------------------------------------------
print("[X1] session cli dialect handling")


@srv.tool
def _scratch_exit_code_tool(fail: bool = True) -> str:
    return json.dumps({"exit_code": 2, "message": "bad args"} if fail
                      else {"exit_code": 0, "message": "done"})


try:
    _scratch_exit_code_tool(True)
    check("shim flags exit_code != 0 (JSON text)", False, "no exception")
except ToolError:
    check("shim flags exit_code != 0 (JSON text)", True)
check("shim passes exit_code == 0",
      json.loads(_scratch_exit_code_tool(False))["exit_code"] == 0)

srv._config = {"wiggle_exe": "fake.exe"}
orig_sr3 = srv.session_request
try:
    srv.session_request = lambda *a, **kw: {"exit_code": 1, "id": 1, "message": "bad args"}
    r = srv.run_wiggle(["erase"], {"wiggle_exe": "fake.exe"})
    check("session exit_code!=0 normalized to status error",
          r.get("status") == "error" and r.get("exit_code") == 1, str(r))
    srv.session_request = lambda *a, **kw: {"exit_code": 0, "id": 2, "message": "ok"}
    r2 = srv.run_wiggle(["read"], {"wiggle_exe": "fake.exe"})
    check("session exit_code==0 normalized to status ok", r2.get("status") == "ok", str(r2))
finally:
    srv.session_request = orig_sr3
    srv._config = {}

s8 = bare_session()
s8._reader = threading.Thread(target=lambda: None, daemon=True)
s8._queue.put('{"exit_code": 0, "id": 1, "message": "ok"}')
check("real cli shape (id echo) passes request id check",
      s8.request("cli", timeout=1) == {"exit_code": 0, "id": 1, "message": "ok"})

# ---------------------------------------------------------------------------
# X2: _detect_device parses the serve 'cli' output from 'message' (live-verified)
# ---------------------------------------------------------------------------
print("[X2] _detect_device message field")
orig_rw = srv.run_wiggle
try:
    srv.run_wiggle = lambda args, cfg, timeout=None: {
        "status": "ok", "exit_code": 0,
        "message": "TAS target:\n  Device: TC39xB_A_step\n  Server: localhost",
    }
    got = srv._detect_device({})
    check("device parsed from message field", got == "TC39xB", str(got))
    srv.run_wiggle = lambda args, cfg, timeout=None: {
        "status": "error", "exit_code": 4, "message": "initTool failed",
    }
    check("failed info yields no device", srv._detect_device({}) is None)
finally:
    srv.run_wiggle = orig_rw

# ---------------------------------------------------------------------------
# X3: "timeout": null in config must not become subprocess timeout=None
# ---------------------------------------------------------------------------
print("[X3] timeout null normalization")


class FakeCP2:
    stdout = '{"status": "ok"}'
    stderr = ""
    returncode = 0


captured = {}
orig_run3 = srv.subprocess.run
srv.subprocess.run = lambda *a, **k: (captured.update(timeout=k.get("timeout")), FakeCP2())[1]
srv._session = None
try:
    srv._config = {}
    srv.run_wiggle(["read"], {"wiggle_exe": "fake.exe", "timeout": None})
    check("run_wiggle CLI: null timeout -> 120", captured.get("timeout") == 120, str(captured))
    srv.run_flasher(["--version"], {"aurix_flasher_exe": "fake.exe", "timeout": None})
    check("run_flasher: null timeout -> 300", captured.get("timeout") == 300, str(captured))
finally:
    srv.subprocess.run = orig_run3
    srv._session = None
    srv._config = {}

# ---------------------------------------------------------------------------
# N9/M9: device-required error lists available devices
# ---------------------------------------------------------------------------
print("[M9] _device_required_error")
srv._config = {"wiggle_exe": os.path.join(os.path.dirname(srv.__file__), "wiggle", "wiggle.exe")}
err = json.loads(srv._device_required_error(srv._config, "reg_search"))
check("error lists available devices", "TC39xB" in err.get("available_devices", []) and "TC23x" in err.get("available_devices", []), str(err.get("available_devices"))[:80])
srv._config = {}

print("[PFlash] diagnostics and non-replayed execution (host mocks)")
orig_run4, orig_kill4 = srv.subprocess.run, srv._kill_session
order = []
calls = []
srv._kill_session = lambda: order.append("release")

def flasher_fail(cmd, **kwargs):
    order.append("launch")
    calls.append((cmd, kwargs))
    return type("Result", (), {"returncode": 2, "stdout": "Programming Flash memory Flax : Not Responding", "stderr": "vendor detail"})()

try:
    srv.subprocess.run = flasher_fail
    result = srv.run_flasher(["-e", "all", "-log", "diagnostic.xml"], {"aurix_flasher_exe": "fake.exe"})
    check("release owned MCD session before launch", order == ["release", "launch"])
    check("retain both vendor streams and log path", result["stdout"].endswith("Not Responding") and result["stderr"] == "vendor detail" and result["log_file"] == "diagnostic.xml")
    check("all failure is not retried as on", len(calls) == 1 and calls[0][0][2] == "all")
    def expire(cmd, **kwargs):
        calls.append((cmd, kwargs))
        raise srv.subprocess.TimeoutExpired(cmd, 3, output=b"erase started", stderr=b"timeout detail")
    srv.subprocess.run = expire
    result = srv.run_flasher(["-e", "all"], {"aurix_flasher_exe": "fake.exe"}, timeout=3)
    check("process timeout retains output and unknown completion", result.get("completion_unknown") and result["stdout"] == "erase started" and result["stderr"] == "timeout detail")
    srv.subprocess.run = flasher_fail
    with tempfile.TemporaryDirectory() as directory:
        hex_path = os.path.join(directory, "input.hex")
        with open(hex_path, "w", encoding="ascii") as stream:
            stream.write(":00000001FF\n")
        srv._config = {"aurix_flasher_exe": "fake.exe"}
        log_path = os.path.join(directory, "capture.xml")
        try:
            srv.aurix_pflash(hex_file=hex_path, erase="all", connect="6", flax_timeout_seconds=60, log_file=log_path)
        except ToolError:
            pass
        argv = calls[-1][0]
        check("inner timeout forwarded separately from outer process", argv[argv.index("-timeout")+1] == "60" and calls[-1][1]["timeout"] == 300)
        check("requested all erase retained", argv[argv.index("-e")+1] == "all")
        check("vendor decoding cannot abort result capture", calls[-1][1].get("errors") == "replace")
        old_cwd = os.getcwd()
        try:
            os.chdir(directory)
            try:
                srv.aurix_pflash(hex_file="input.hex", log_file="relative.xml")
            except ToolError:
                pass
        finally:
            os.chdir(old_cwd)
        relative_argv = calls[-1][0]
        check("relative HEX becomes verified absolute path before cwd change",
              relative_argv[relative_argv.index("-hex")+1] == hex_path)
        count = len(calls)
        for kwargs in ({"erase": "used"}, {"connect": "1"}, {"flax_timeout_seconds": 0}, {"log_file": log_path}, {"ucb": True}):
            try:
                srv.aurix_pflash(hex_file=hex_path, **kwargs)
                check("invalid/unsafe request rejected before launch", False)
            except ToolError:
                check("invalid/unsafe request rejected before launch", len(calls) == count)
finally:
    srv.subprocess.run, srv._kill_session = orig_run4, orig_kill4
    srv._config = {}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
print(f"\n{len(PASS)} passed, {len(FAIL)} failed")
if FAIL:
    print("FAILED:", *FAIL, sep="\n  - ")
    sys.exit(1)
