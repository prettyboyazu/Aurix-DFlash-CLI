// 同一翻译单元替换动态接口，以免测试桩侵入生产接口。
#include "../debug_session.cpp"
#include "../mcd_ctrl.cpp"

namespace {
using wiggle_dbg::nljson;
constexpr uint32_t TEST_PC = 0x80000024U;
constexpr uint32_t TEST_TRIGGER = 0xDA500000U;
uint32_t s_passed = 0U;
uint32_t s_failed = 0U;
mcd_core_st s_core = {};
mcd_server_st s_server = {};

struct Mock {
    std::vector<mcd_return_et> stateResults = {MCD_RET_ACT_NONE};
    std::vector<mcd_return_et> captureResults = {MCD_RET_ACT_NONE};
    uint32_t state = MCD_CORE_STATE_DEBUG;
    uint32_t pc = TEST_PC;
    uint32_t stateCalls = 0U;
    uint32_t captureCalls = 0U;
    uint32_t runCalls = 0U;
    uint32_t stopCalls = 0U;
    uint32_t stepCalls = 0U;
    uint32_t resetCalls = 0U;
    uint32_t txCalls = 0U;
    uint32_t createCalls = 0U;
    uint32_t removeCalls = 0U;
    uint32_t target = TEST_PC;
    uint32_t stateDelayMs = 0U;
    uint32_t failTxAfter = UINT32_MAX;
    mcd_return_et actionResult = MCD_RET_ACT_NONE;
    mcd_return_et txResult = MCD_RET_ACT_NONE;
    mcd_return_et removeResult = MCD_RET_ACT_NONE;
    mcd_return_et activateResult = MCD_RET_ACT_NONE;
    bool captureValid = true;
    bool captured = true;
    bool failStateAfterRun = false;
    bool stopAtOtherAddress = false;
    bool partialTx = false;
};
Mock s_mock;

void Check(bool condition, const char* name)
{
    if (condition) ++s_passed;
    else ++s_failed;
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
}

mcd_return_et Next(const std::vector<mcd_return_et>& results, uint32_t call)
{
    return results[call < results.size() ? call : results.size() - 1U];
}

void QueryError(const mcd_core_st*, mcd_error_info_st* info)
{
    info->error_code = 0x102U;
    std::snprintf(info->error_str, sizeof(info->error_str), "mock diagnostic failure");
}

mcd_return_et QueryState(const mcd_core_st*, mcd_core_state_st* value)
{
    if (s_mock.stateDelayMs != 0U)
        std::this_thread::sleep_for(std::chrono::milliseconds(s_mock.stateDelayMs));
    value->state = s_mock.state;
    value->trig_id = TEST_TRIGGER;
    return Next(s_mock.stateResults, s_mock.stateCalls++);
}

mcd_return_et QueryCapture(const mcd_core_st*, uint32_t, mcd_trig_state_st* value)
{
    value->captured_valid = s_mock.captureValid;
    value->captured = s_mock.captured;
    return Next(s_mock.captureResults, s_mock.captureCalls++);
}

mcd_return_et Run(const mcd_core_st*, mcd_bool_t)
{
    ++s_mock.runCalls;
    if (s_mock.failStateAfterRun) s_mock.stateResults = {MCD_RET_ACT_HANDLE_ERROR};
    s_mock.pc = s_mock.stopAtOtherAddress ? TEST_PC + 0x100U : s_mock.target;
    return s_mock.actionResult;
}

mcd_return_et Stop(const mcd_core_st*, mcd_bool_t)
{
    ++s_mock.stopCalls;
    s_mock.state = MCD_CORE_STATE_DEBUG;
    return s_mock.actionResult;
}

mcd_return_et Step(const mcd_core_st*, mcd_bool_t, mcd_core_step_type_et, uint32_t)
{
    ++s_mock.stepCalls;
    return s_mock.actionResult;
}

mcd_return_et Reset(const mcd_core_st*, uint32_t, mcd_bool_t)
{
    ++s_mock.resetCalls;
    return s_mock.actionResult;
}

mcd_return_et Transactions(const mcd_core_st*, mcd_txlist_st* list)
{
    ++s_mock.txCalls;
    if (s_mock.txCalls > s_mock.failTxAfter) return MCD_RET_ACT_HANDLE_ERROR;
    for (uint32_t i = 0U; i < list->num_tx; ++i) {
        mcd_tx_st& tx = list->tx[i];
        if (tx.access_type == MCD_TX_AT_R) {
            for (uint32_t j = 0U; j < tx.num_bytes; ++j)
                tx.data[j] = static_cast<uint8_t>(s_mock.pc >> ((j % 4U) * 8U));
        }
        tx.num_bytes_ok = s_mock.partialTx ? 0U : tx.num_bytes;
    }
    list->num_tx_ok = s_mock.partialTx ? 0U : list->num_tx;
    return s_mock.txResult;
}

mcd_return_et CreateTrigger(const mcd_core_st*, void* trigger, uint32_t* id)
{
    ++s_mock.createCalls;
    s_mock.target = static_cast<uint32_t>(
        static_cast<mcd_trig_simple_core_st*>(trigger)->addr_start.address);
    *id = TEST_TRIGGER;
    return MCD_RET_ACT_NONE;
}

mcd_return_et RemoveTrigger(const mcd_core_st*, uint32_t)
{
    ++s_mock.removeCalls;
    return s_mock.removeResult;
}

mcd_return_et Activate(const mcd_core_st*) { return s_mock.activateResult; }
mcd_return_et CloseCore(const mcd_core_st*) { return MCD_RET_ACT_NONE; }
mcd_return_et CloseServer(const mcd_server_st*) { return MCD_RET_ACT_NONE; }

mcd_return_et QueryCores(const mcd_core_con_info_st*, uint32_t,
                        uint32_t* count, mcd_core_con_info_st* info)
{
    *count = 1U;
    if (info) std::snprintf(info->core, sizeof(info->core), "CPU0");
    return MCD_RET_ACT_NONE;
}

mcd_return_et OpenCore(const mcd_core_con_info_st*, mcd_core_st** core)
{
    *core = &s_core;
    return MCD_RET_ACT_NONE;
}

void ResetMock()
{
    s_mock = Mock{};
    wiggle_mcd::g_api = {};
    wiggle_mcd::g_loaded = true;
    wiggle_mcd::g_initialized = true;
    wiggle_mcd::g_server = &s_server;
    auto& api = wiggle_mcd::g_api;
    api.qry_error_info = QueryError;
    api.qry_state = QueryState;
    api.qry_trig_state = QueryCapture;
    api.run = Run;
    api.stop = Stop;
    api.step = Step;
    api.rst = Reset;
    api.execute_txlist = Transactions;
    api.create_trig = CreateTrigger;
    api.remove_trig = RemoveTrigger;
    api.activate_trig_set = Activate;
    api.qry_cores = QueryCores;
    api.open_core = OpenCore;
    api.close_core = CloseCore;
    api.close_server = CloseServer;
}

struct Fixture {
    wiggle_dbg::DebugSession debug;
    Fixture() { ResetMock(); }
    ~Fixture() { debug.close(); }
    nljson Call(nljson request)
    {
        nljson response;
        debug.dispatch(request, response);
        return response;
    }
};

void TestQueryRetry()
{
    ResetMock();
    wiggle_mcd::Session session{&s_core, 0};
    uint32_t state = 0U, trigger = 0U;
    std::string info;
    s_mock.stateResults = {MCD_RET_ACT_AGAIN, MCD_RET_ACT_AGAIN, MCD_RET_ACT_NONE};
    Check(wiggle_mcd::stateFull(session, state, trigger, info) &&
          state == MCD_CORE_STATE_DEBUG && trigger == TEST_TRIGGER && s_mock.stateCalls == 3U,
          "state AGAIN then success");
    for (const auto result : {MCD_RET_ACT_HANDLE_ERROR, MCD_RET_ACT_HANDLE_EVENT}) {
        ResetMock();
        s_mock.stateResults = {static_cast<mcd_return_et>(result)};
        Check(!wiggle_mcd::stateFull(session, state, trigger, info) &&
              state == MCD_CORE_STATE_UNKNOWN && trigger == 0U && s_mock.stateCalls == 1U,
              "ERROR/EVENT not retried and poisoned output rejected");
    }
    ResetMock();
    s_mock.stateResults = {MCD_RET_ACT_AGAIN};
    Check(!wiggle_mcd::stateFull(session, state, trigger, info) &&
          s_mock.stateCalls == wiggle_mcd::QUERY_MAX_ATTEMPTS &&
          info.find("budget exhausted") != std::string::npos, "AGAIN attempt cap");
    ResetMock();
    s_mock.stateResults = {MCD_RET_ACT_AGAIN};
    s_mock.stateDelayMs = wiggle_mcd::QUERY_RETRY_TIMEOUT_MS + 10U;
    Check(!wiggle_mcd::stateFull(session, state, trigger, info) &&
          s_mock.stateCalls == 1U, "deadline prevents another DLL call");
    ResetMock();
    s_mock.state = MCD_CORE_STATE_UNKNOWN;
    Check(!wiggle_mcd::stateFull(session, state, trigger, info) &&
          info.find("unknown") != std::string::npos, "unknown state is not success");
    session.core = nullptr;
    Check(!wiggle_mcd::stateFull(session, state, trigger, info) && !info.empty(),
          "missing core has an explicit error");
}

void TestCapture()
{
    for (uint32_t validity = 0U; validity < 2U; ++validity) {
        for (uint32_t capture = 0U; capture < 2U; ++capture) {
            Fixture fixture;
            s_mock.captureValid = validity != 0U;
            s_mock.captured = capture != 0U;
            fixture.Call({{"cmd", "break"}, {"action", "add"}, {"addr", TEST_PC}});
            const auto out = fixture.Call({{"cmd", "break"}, {"action", "list"}});
            const auto& entry = out["trigs"][0];
            Check(out["status"] == "ok" && entry["captured_valid"] == s_mock.captureValid &&
                  (validity ? entry["captured"] == s_mock.captured : entry["captured"].is_null()),
                  "capture validity/captured combination");
        }
    }
    Fixture fixture;
    fixture.Call({{"cmd", "watch"}, {"action", "add"}, {"addr", 0x70000000U}});
    s_mock.captureResults = {MCD_RET_ACT_AGAIN, MCD_RET_ACT_NONE};
    Check(fixture.Call({{"cmd", "watch"}, {"action", "list"}})["status"] == "ok" &&
          s_mock.captureCalls == 2U, "capture AGAIN retry");
    s_mock.captureCalls = 0U;
    s_mock.captureResults = {MCD_RET_ACT_HANDLE_ERROR};
    auto out = fixture.Call({{"cmd", "watch"}, {"action", "list"}});
    Check(out["status"] == "error" && out["trigs"][0]["captured"].is_null() &&
          out["trigs"][0].contains("capture_error") && s_mock.captureCalls == 1U,
          "capture query error is not reported as false");
}

void TestCommandFailures()
{
    Fixture fixture;
    s_mock.stateResults = {MCD_RET_ACT_HANDLE_ERROR};
    auto out = fixture.Call({{"cmd", "state"}});
    Check(out["status"] == "error" && !out.contains("pc") && !out.contains("state") &&
          s_mock.txCalls == 0U, "state failure has no fabricated PC or state");
    s_mock.stateResults = {MCD_RET_ACT_NONE};
    s_mock.txResult = MCD_RET_ACT_HANDLE_ERROR;
    out = fixture.Call({{"cmd", "state"}});
    Check(out["status"] == "error" && !out.contains("pc"), "PC read failure is observable");
    s_mock.actionResult = MCD_RET_ACT_HANDLE_ERROR;
    const auto reads = s_mock.txCalls;
    out = fixture.Call({{"cmd", "go"}});
    Check(out["status"] == "error" && s_mock.txCalls == reads &&
          out["message"].get<std::string>().find("run:") != std::string::npos,
          "action failure preserves original error");
    s_mock.actionResult = MCD_RET_ACT_NONE;
    s_mock.txResult = MCD_RET_ACT_NONE;
    s_mock.failStateAfterRun = true;
    out = fixture.Call({{"cmd", "go"}});
    Check(out["status"] == "error" && out["action_applied"] == true && !out.contains("pc"),
          "successful run with failed observation is marked applied");
    out = fixture.Call({{"cmd", "reset"}});
    Check(out["status"] == "error" && out["action_applied"] == true &&
          !out.contains("state"), "reset observation failure is not false success");
}

void TestNoTransactionReplay()
{
    ResetMock();
    wiggle_mcd::Session session{&s_core, 0};
    std::string err;
    bool applied = false;
    s_mock.actionResult = MCD_RET_ACT_AGAIN;
    Check(!wiggle_mcd::run(session, err) && s_mock.runCalls == 1U, "run AGAIN is not replayed");
    Check(!wiggle_mcd::runGlobal(session, err) && s_mock.runCalls == 2U, "global run is not replayed");
    Check(!wiggle_mcd::step(session, 1U, applied, err) && s_mock.stepCalls == 1U, "step is not replayed");
    Check(!wiggle_mcd::reset(session, 2U, false, err) && s_mock.resetCalls == 1U, "reset is not replayed");
    s_mock.state = MCD_CORE_STATE_RUNNING;
    Check(!wiggle_mcd::stop(session, applied, err) && s_mock.stopCalls == 1U, "stop AGAIN is not replayed");
    s_mock.txResult = MCD_RET_ACT_AGAIN;
    uint8_t data[4] = {};
    Check(!wiggle_mcd::readMem(session, 0xF881FD30U, data, 4U, err) && s_mock.txCalls == 1U,
          "read-clear MMIO is not replayed");
    Check(!wiggle_mcd::writeMem(session, 0x70000000U, data, 4U, err) && s_mock.txCalls == 2U,
          "write transaction is not replayed");
    s_mock.txResult = MCD_RET_ACT_NONE;
    s_mock.partialTx = true;
    Check(!wiggle_mcd::readMem(session, 0x70000000U, data, 4U, err) && s_mock.txCalls == 3U,
          "partial transaction is not replayed");
    Check(err.find("addr=0x70000000 length=4 bytes_ok=0") != std::string::npos,
          "read failure includes exact transaction and valid byte count");
    s_mock.txResult = MCD_RET_ACT_HANDLE_ERROR;
    Check(!wiggle_mcd::readMem(session, 0xA0003000U, data, 4U, err) &&
          err.find("addr=0xA0003000") != std::string::npos &&
          err.find("error=0x102") != std::string::npos && s_mock.txCalls == 4U,
          "flash read error is not silently filled with zero or retried");
}

void TestAppliedWaitFailure()
{
    for (const char* command : {"halt", "step"}) {
        Fixture fixture;
        s_mock.stateResults = {MCD_RET_ACT_HANDLE_ERROR};
        const auto out = fixture.Call({{"cmd", command}});
        Check(out["status"] == "error" && out.value("action_applied", false) &&
              s_mock.stopCalls + s_mock.stepCalls == 1U,
              "halt/step wait failure records one applied action");
    }
    Fixture fixture;
    s_mock.stateResults = {MCD_RET_ACT_HANDLE_EVENT};
    const auto out = fixture.Call({{"cmd", "halt"}, {"global", true}});
    Check(out["status"] == "error" && out.value("action_applied", false) &&
          s_mock.stopCalls == 1U && s_mock.stateCalls == 1U,
          "global halt wait EVENT is not retried or mistaken for no action");
}

void TestTemporaryBreakpoints()
{
    for (const char* command : {"stepover", "stepout"}) {
        {
            Fixture fixture;
            s_mock.stateResults = {MCD_RET_ACT_HANDLE_ERROR};
            const auto out = fixture.Call({{"cmd", command}});
            Check(out["status"] == "error" && s_mock.createCalls == 0U,
                  "step operation rejects state query failure");
        }
        {
            Fixture fixture;
            s_mock.txResult = MCD_RET_ACT_HANDLE_ERROR;
            const auto out = fixture.Call({{"cmd", command}});
            Check(out["status"] == "error" && s_mock.createCalls == 0U,
                  "step operation rejects PC/A11 read failure");
        }
        {
            Fixture fixture;
            s_mock.failStateAfterRun = true;
            const auto out = fixture.Call({{"cmd", command}});
            Check(out["status"] == "error" && s_mock.runCalls == 1U && s_mock.removeCalls == 1U &&
                  s_mock.stopCalls == 1U && out.value("action_applied", false),
                  "wait failure attempts one stop, cleans up and reports applied action");
        }
        {
            Fixture fixture;
            s_mock.removeResult = MCD_RET_ACT_HANDLE_ERROR;
            const auto out = fixture.Call({{"cmd", command}});
            Check(out["status"] == "error", "temporary breakpoint removal error is visible");
        }
        {
            Fixture fixture;
            s_mock.stopAtOtherAddress = true;
            const auto out = fixture.Call({{"cmd", command}});
            Check(out["status"] == "ok" && out["hit"] == false,
                  "another stop address is not a temporary breakpoint hit");
        }
    }
}
}

int main()
{
    std::puts("HOST MOCK TESTS: not hardware coverage");
    TestQueryRetry();
    TestCapture();
    TestCommandFailures();
    TestNoTransactionReplay();
    TestAppliedWaitFailure();
    TestTemporaryBreakpoints();
    std::printf("%u passed, %u failed\n", s_passed, s_failed);
    return s_failed == 0U ? 0 : 1;
}
