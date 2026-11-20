# wiggle MCP 全功能实测报告

目标: TC36x / Blinky_LED_1_KIT_TC367_TFT  时间: 2026-11-20 15:21:03
工具总数: 42  调用记录: 88

| 工具 | 结果 |
|---|---|
| aurix_pflash | PASS |
| build_project | PASS |
| lookup_address | PASS |
| lookup_symbol | PASS |
| parse_elf | PASS |
| parse_symbols | PASS |
| reg_fields | PASS |
| reg_peripherals | PASS |
| reg_search | PASS |
| reload_parsers | PASS |
| wiggle_break | PASS |
| wiggle_bt | PASS |
| wiggle_compare | PASS |
| wiggle_cpu_reg | PASS |
| wiggle_disasm | PASS |
| wiggle_dump | PASS |
| wiggle_erase | PASS |
| wiggle_go | PASS |
| wiggle_halt | PASS |
| wiggle_info | PASS |
| wiggle_list | PASS |
| wiggle_mem_read | PASS |
| wiggle_mem_write | PASS |
| wiggle_poke | PASS |
| wiggle_read | 部分 |
| wiggle_reg | PASS |
| wiggle_reset | PASS |
| wiggle_restore | PASS |
| wiggle_rewrite | PASS |
| wiggle_rtos | PASS |
| wiggle_sample | PASS |
| wiggle_search | PASS |
| wiggle_state | PASS |
| wiggle_status | PASS |
| wiggle_step | PASS |
| wiggle_stepout | PASS |
| wiggle_stepover | PASS |
| wiggle_sym | PASS |
| wiggle_ucb | PASS |
| wiggle_var | PASS |
| wiggle_watch | PASS |
| wiggle_write | PASS |

## 关键证据
- DFlash 备份 SHA256: f86354d2e3885cc006b4780a276369780daccb1a8be3782743fc02b0b188665d
- break 命中: True
- watch 命中: True
- 最终 state: 1
- 原始请求/响应: raw_calls.jsonl；备份/产物: 本目录。
- 说明: build_project 为运行器冒烟测试（用户指定不重编固件）；aurix_pflash erase=all 在本setup编程失败，erase=on 成功。