# wiggle MCP 全功能实测 · 逐项证据

生成: 2026-11-20 15:22:51  目标: TC36x / Blinky_LED_1_KIT_TC367_TFT

判据: 硬件回读/状态/命中/校验一致为 PASS；预期拒绝(UCB无confirm/裸机RTOS)为 PASS(预期错误)；其余 FAIL。

## 同步与备份
- 源码 commit: 2c0d7c2 (MCD可靠性) / 119f1bd (实装回同步)，已推送 gitee+github。
- 实装 exe SHA256: ae38a4c1ced391cc7bb5679dd04b5e107a8dea32d23f0a6847952a76425bb5e3 (两实装库一致)。
- 备份: backup_dflash.bin / backup_pflash_4k.bin / backup_ucb_bmhd0.bin。
- 恢复: DFlash sector0 回擦除态；PFlash erase=on 重烧原 HEX compare match=true；最终 state=1。

## aurix_pflash
- [PASS] args={"hex_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.hex", "verify": true, "erase": "on", "connect": "6", "start": "on"} -> {"status": "ok", "returncode": 0}

## build_project
- [PASS] args={} -> {"status": "ok", "returncode": 0}
- [PASS] args={"clean": true} -> {"status": "ok", "returncode": 0}

## lookup_address
- [PASS] args={"address": "0x80000024"} -> {"status": "ok"}

## lookup_symbol
- [PASS] args={"name": "blinkLED"} -> {"status": "ok"}

## parse_elf
- [PASS] args={"sections": true} -> {"symbols": [{"name": ".\\Blinky_LED_1_KIT_TC367_TFT.elf", "address": 0, "size": 0, "type": "STT_FILE", "bind": "STB_LOCAL", "section_idx": "SHN_ABS"}, {"name": "IfxAsclin_PinMap.src", "address": 0, "size": 0, "type": "STT_FILE", "bind": "STB_LOCAL", "section_idx": "SHN_ABS"}, {"name": "IfxCan_PinMa
- [PASS] args={"functions": true, "search": "blinkLED"} -> {"symbols": [{"name": "blinkLED", "address": 2147483684, "size": 86, "type": "STT_FUNC", "bind": "STB_GLOBAL", "section_idx": 78}]}
- [PASS] args={"variables": true, "search": "cpuSync"} -> {"symbols": [{"name": "g_cpuSyncEvent", "address": 1879048340, "size": 4, "type": "STT_OBJECT", "bind": "STB_GLOBAL", "section_idx": 80}]}

## parse_symbols
- [PASS] args={"summary": true} -> {"symbols": [{"name": "__INDIRECT__", "address": 0, "space": "-"}, {"name": "IfxCpu_Trap_assertion", "address": 2147484528, "space": "mpe:vtc:linear"}, {"name": "IfxCpu_Trap_busError", "address": 2147484596, "space": "mpe:vtc:linear"}, {"name": "IfxCpu_Trap_contextManagementError", "address": 214748
- [PASS] args={"summary": false, "search": "blinkLED"} -> {"symbols": [{"name": "blinkLED", "address": 2147483684, "space": "mpe:vtc:linear"}]}

## reg_fields
- [PASS] args={"name": "DMU.HF.STATUS", "device": "TC36x"} -> {"status": "ok"}

## reg_peripherals
- [PASS] args={"device": "TC36x"} -> {"status": "ok"}

## reg_search
- [PASS] args={"keyword": "watchdog", "device": "TC36x", "max_results": 10} -> {"status": "ok"}

## reload_parsers
- [PASS] args={} -> {"status": "ok"}

## wiggle_break
- [PASS] args={"action": "add", "address": "0x80000024"} -> {"status": "ok"}
- [PASS] args={"action": "list"} -> {"status": "ok", "trigs": [{"addr": 2147483684, "captured": true, "captured_valid": true, "id_trig": 3662675968, "kind": "break", "range": 0, "type": 1}]}
- [PASS] args={"action": "del", "address": "0x80000024"} -> {"status": "ok"}
- [PASS] args={"action": "clear"} -> {"status": "ok"}

## wiggle_bt
- [PASS] args={"elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok"}

## wiggle_compare
- [PASS] args={"file_path": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.hex", "address": "0xa0000000"} -> {"status": "ok", "exit_code": 0, "match": true, "mismatches": 0}
- [PASS] args={"file_path": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\dflash_pattern.hex", "address": "0xaf000000"} -> {"status": "ok", "exit_code": 0, "match": true, "mismatches": 0}
- [PASS] args={"file_path": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\dflash_restore.hex", "address": "0xaf000000"} -> {"status": "ok", "exit_code": 0, "match": true, "mismatches": 0}
- [PASS] args={"file_path": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.hex", "address": "0xa0000000"} -> {"status": "ok", "exit_code": 0, "match": true, "mismatches": 0}

## wiggle_cpu_reg
- [PASS] args={"name": "PC"} -> {"status": "ok", "value": 2147484326}
- [PASS] args={"name": "D0"} -> {"status": "ok", "value": 4294967295}
- [PASS] args={"name": "D0", "value": "0x12345678"} -> {"status": "ok", "value": 305419896}
- [PASS] args={"name": "D0", "value": "0xffffffff"} -> {"status": "ok", "value": 4294967295}

## wiggle_disasm
- [PASS] args={"address": "0x80000024", "count": 8, "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok"}

## wiggle_dump
- [PASS] args={"address": "0x80000024", "length": "0x20", "output_file": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\dump_blink.bin"} -> {"status": "ok", "exit_code": 0}

## wiggle_erase
- [PASS] args={"erase_all": false, "address": "0xaf000000", "sectors": 1, "verify": true} -> {"status": "ok", "exit_code": 0}
- [PASS] args={"erase_all": false, "address": "0xaf000000", "sectors": 1, "verify": true} -> {"status": "ok", "exit_code": 0}

## wiggle_go
- [PASS] args={} -> {"status": "ok", "pc": 2147483684, "state": 3}
- [PASS] args={} -> {"status": "ok", "pc": 2147484220, "state": 3}
- [PASS] args={} -> {"status": "ok", "pc": 2147484276, "state": 1}
- [PASS] args={} -> {"status": "ok", "pc": 2147484326, "state": 1}

## wiggle_halt
- [PASS] args={"confirm_risk": true, "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "pc": 2147484326, "state": 3}
- [PASS] args={"confirm_risk": true} -> {"status": "ok", "pc": 2147483684, "state": 3}
- [PASS] args={"confirm_risk": true} -> {"status": "ok", "pc": 2147484220, "state": 3}

## wiggle_info
- [PASS] args={} -> {"status": "ok", "exit_code": 0}

## wiggle_list
- [PASS] args={} -> {"status": "ok", "exit_code": 0, "targets": [{"device": "TC36x", "identifier": "TriBoard TC2XX V2.0 TB77RANE", "index": 0}]}
- [PASS] args={} -> {"status": "ok", "exit_code": 0, "targets": [{"device": "TC36x", "identifier": "TriBoard TC2XX V2.0 TB77RANE", "index": 0}]}
- [PASS] args={} -> {"status": "ok", "exit_code": 0, "targets": [{"device": "TC36x", "identifier": "TriBoard TC2XX V2.0 TB77RANE", "index": 0}]}

## wiggle_mem_read
- [PASS] args={"address": "0x7000f000", "length": "0x4"} -> {"status": "ok"}
- [PASS] args={"address": "0x7000f000", "length": "0x4"} -> {"status": "ok"}
- [PASS] args={"address": "0x7000f000", "length": "0x4"} -> {"status": "ok"}

## wiggle_mem_write
- [PASS] args={"address": "0x7000f000", "data": "A5A5A5A5"} -> {"status": "ok"}
- [PASS] args={"address": "0x7000f000", "data": "00000000"} -> {"status": "ok"}

## wiggle_poke
- [PASS] args={"address": "0x7000f000", "value": "0x5A5A5A5A", "width": 32} -> {"status": "ok", "exit_code": 0, "match": true, "value": 1515870810}
- [PASS] args={"address": "0x7000f000", "value": "0x00000000", "width": 32} -> {"status": "ok", "exit_code": 0, "match": true, "value": 0}

## wiggle_read
- [PASS] args={"address": "0xaf000000", "length": "0x20000", "output_file": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\backup_dflash.bin"} -> {"status": "ok", "exit_code": 0}
- [FAIL] args={"address": "0xa0000000", "length": "0x10000", "output_file": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\backup_pflash_head.bin"} -> {}
- [PASS] args={"address": "0xa0000000", "length": "0x1000", "output_file": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\backup_pflash_4k.bin"} -> {"status": "ok", "exit_code": 0}

## wiggle_reg
- [PASS] args={"name_or_addr": "DMU.HF.STATUS"} -> {"status": "ok", "exit_code": 0, "value": 34078720}
- [PASS] args={"name_or_addr": "", "list_peripheral": "DMU"} -> {"status": "ok", "exit_code": 0}
- [PASS] args={"name_or_addr": "P13.OUT"} -> {"status": "ok", "exit_code": 0, "value": 1}
- [PASS] args={"name_or_addr": "P13.OUT", "value": "0x0"} -> {"status": "ok", "exit_code": 0, "value": 0}
- [PASS] args={"name_or_addr": "P13.OUT", "value": "0x1"} -> {"status": "ok", "exit_code": 0, "value": 1}

## wiggle_reset
- [PASS] args={"halt": true} -> {"status": "ok", "exit_code": 0, "state": 3}

## wiggle_restore
- [PASS] args={"file_path": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\dflash_restore.hex"} -> {"status": "ok", "exit_code": 0}

## wiggle_rewrite
- [PASS] args={"address": "0xaf000100", "data": "1122334455667788", "backup": true, "backup_path": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\rewrite_backup.bin", "verify": true} -> {"status": "ok", "exit_code": 0}

## wiggle_rtos
- [FAIL] args={"action": "tasks", "type": "auto", "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {}

## wiggle_sample
- [PASS] args={"address": "0xf0001010", "size": 4, "interval_ms": 100} -> {"status": "ok", "consistent": false, "value0": 1509966336, "value1": 1520779311}

## wiggle_search
- [PASS] args={"address": "0xa0000000", "length": "0x1000", "pattern": "914000FF"} -> {"status": "ok", "exit_code": 0}

## wiggle_state
- [PASS] args={} -> {"status": "ok", "pc": 2147484326, "state": 3}
- [PASS] args={} -> {"status": "ok", "pc": 2147483684, "state": 3}
- [PASS] args={} -> {"status": "ok", "pc": 2147484220, "state": 3}
- [PASS] args={} -> {"status": "ok", "pc": 2147484326, "state": 1}

## wiggle_status
- [PASS] args={} -> {"status": "ok", "exit_code": 0, "value": 34078720}

## wiggle_step
- [PASS] args={"n": 1, "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "pc": 2147484330, "state": 3}

## wiggle_stepout
- [PASS] args={"elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "pc": 2147483768, "hit": true}

## wiggle_stepover
- [PASS] args={"elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "pc": 2147484332, "hit": true}

## wiggle_sym
- [PASS] args={"action": "load", "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "symbols": 103}
- [PASS] args={"action": "name", "arg": "blinkLED"} -> {"status": "ok", "value": 2147483684}
- [PASS] args={"action": "addr", "arg": "0x80000024"} -> {"status": "ok"}

## wiggle_ucb
- [PASS] args={"action": "read", "address": "0xAF400000", "length": "0x200", "output_file": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\backup_ucb_bmhd0.bin"} -> {"status": "ok", "exit_code": 0}
- [PASS] args={"action": "read", "address": "0xAF400000", "length": "0x20"} -> {"status": "ok", "exit_code": 0}
- [FAIL] args={"action": "write", "file_path": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\backup_ucb_bmhd0.bin", "confirm_risk": false} -> {}
- [FAIL] args={"action": "erase", "address": "0xAF400000", "sectors": 1, "confirm_risk": false} -> {}

## wiggle_var
- [PASS] args={"action": "read", "name": "g_cpuSyncEvent", "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "value": 3}
- [PASS] args={"action": "write", "name": "g_cpuSyncEvent", "value": "0x4", "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "value": 4}
- [PASS] args={"action": "write", "name": "g_cpuSyncEvent", "value": "0x3", "elf_file": "D:/workspace/Autosar Port/Blinky_LED_1_KIT_TC367_TFT/TriCore Debug (TASKING)/Blinky_LED_1_KIT_TC367_TFT.elf"} -> {"status": "ok", "value": 3}

## wiggle_watch
- [PASS] args={"action": "add", "address": "0xf0001010", "length": 4, "type": "read"} -> {"status": "ok"}
- [PASS] args={"action": "list"} -> {"status": "ok", "trigs": [{"addr": 4026535952, "captured": true, "captured_valid": true, "id_trig": 3662675968, "kind": "watch", "range": 3, "type": 2}]}
- [PASS] args={"action": "clear"} -> {"status": "ok"}

## wiggle_write
- [PASS] args={"file_path": "D:\\workspace\\tas-dflash_-erase\\docs\\mcp_full_test\\dflash_pattern.hex", "verify": true} -> {"status": "ok", "exit_code": 0}

## 已知限制 / 失败项
- wiggle_read 大段 PFlash(0x10000) 经 MCD 读返回 0x2100；小段正常，建议分块。
- aurix_pflash erase=all 编程阶段 Flax Not Responding；erase=on 成功并校验通过。
- build_project 仅运行器冒烟(echo)，按用户要求未重编固件；解析用现有 TASKING 产物。
- wiggle_rtos 裸机无 RTOS 符号，返回错误为预期(N/A)。
- UCB write/erase 无 confirm_risk 被拒绝为预期安全门槛；未实际写 UCB。
