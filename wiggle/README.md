# Wiggle - AURIX MCU Flash & Register Tool

Wiggle 鏄熀浜?TAS Client API 鐨?AURIX 寰帶鍒跺櫒璋冭瘯宸ュ叿锛屾敮鎸?DFlash 璇诲啓/鎿﹂櫎銆佸瘎瀛樺櫒鎿嶄綔銆佸唴瀛樿皟璇曠瓑鍔熻兘銆?

## 鏀寔鐨勮姱鐗?

- **TC2x 绯诲垪**: TC21x, TC22x, TC23x, TC26x, TC27x, TC29x
- **TC3x 绯诲垪**: TC33x, TC35x, TC36x, TC37x, TC38x, TC39x, TC3Ex

> 涓嶆敮鎸?TC4x 绯诲垪銆?

## 鐜瑕佹眰

- **鎿嶄綔绯荤粺**: Windows 10/11 (x64)
- **杩愯鏃?*: Visual C++ Redistributable 2015-2022锛圵in10/11 閫氬父宸插唴缃級
- **TAS Server**: 闇€瑕佸湪鏈湴鎴栬繙绋嬭繍琛?TAS Server锛堣繛鎺ヨ皟璇曞櫒纭欢锛?
- **Python 3.x**锛堜粎 MCP Server 闆嗘垚鏃堕渶瑕侊級

## 鐩綍缁撴瀯

```
wiggle/
鈹溾攢鈹€ wiggle.exe              # 涓荤▼搴?
鈹溾攢鈹€ DeviceConfigs/          # 璁惧閰嶇疆鏂囦欢锛堝繀椤伙級
鈹?  鈹溾攢鈹€ devices.json        # 璁惧绱㈠紩
鈹?  鈹溾攢鈹€ TC33x_A_step.json   # TC33x 閰嶇疆
鈹?  鈹斺攢鈹€ ...                 # 鍏朵粬鑺墖閰嶇疆
鈹溾攢鈹€ RegisterDefs/           # 瀵勫瓨鍣ㄥ畾涔夋枃浠讹紙reg 鍛戒护闇€瑕侊級
鈹?  鈹溾攢鈹€ TC33x-full.json     # TC33x 鍏ㄩ噺瀵勫瓨鍣?
鈹?  鈹斺攢鈹€ ...                 # 鍏朵粬鑺墖瀵勫瓨鍣?
鈹斺攢鈹€ README.md               # 鏈枃妗?
```

## 蹇€熷紑濮?

```bash
# 鏌ョ湅甯姪
wiggle.exe --help

# 鍒楀嚭杩炴帴鐨勭洰鏍囪澶?
wiggle.exe list

# 鏌ョ湅璁惧鐘舵€?
wiggle.exe status --server localhost

# 鏌ョ湅璁惧淇℃伅
wiggle.exe info
```

## 鍛戒护涓€瑙?

### Flash 鎿嶄綔

| 鍛戒护 | 璇存槑 |
|------|------|
| `erase` | 鎿﹂櫎 DFlash 鎵囧尯 |
| `write` | 浠?HEX/BIN 鏂囦欢鍐欏叆 DFlash |
| `rewrite` | 浠绘剰鍦板潃鍐欏叆 DFlash锛圧ead-Modify-Write锛?|
| `restore` | 浠庡浠芥仮澶?DFlash锛堟摝闄?鍐欏叆+鏍￠獙锛?|
| `read` | 璇诲彇 DFlash 鍐呭骞朵繚瀛?|

### 璋冭瘯鍛戒护

| 鍛戒护 | 璇存槑 |
|------|------|
| `reg` | 鎸夊悕瀛楁垨鍦板潃璇诲啓瀵勫瓨鍣紙SVD 椹卞姩锛?|
| `dump` | 鍐呭瓨 Hex Dump锛圥Flash/DFlash/SRAM锛?|
| `poke` | 鍐欏叆鍊煎埌鍐呭瓨鍦板潃 |
| `status` | Flash 鐘舵€佸瘎瀛樺櫒锛堝甫瀛楁瑙ｇ爜锛?|
| `info` | 璁惧淇℃伅銆佸唴瀛樺竷灞€銆丼VD 鎽樿 |
| `compare` | 鏈湴鏂囦欢涓?Flash 鍐呭瀵规瘮 |
| `search` | 鎼滅储鍐呭瓨涓殑瀛楄妭妯″紡 |
| `shell` | 浜や簰寮?REPL 妯″紡 |

### 鍏朵粬

| 鍛戒护 | 璇存槑 |
|------|------|
| `list` | 鍒楀嚭宸茶繛鎺ョ殑 TAS 鐩爣 |
| `reset` | 澶嶄綅 MCU |
| `ucb` | UCB 璇诲啓鎿嶄綔锛堜粎 TC3XX锛?|

## 浣跨敤绀轰緥

### 鎿﹂櫎 DFlash

```bash
# 鎿﹂櫎 1 涓墖鍖?
wiggle.exe erase --addr AF000000 --sectors 1

# 鎿﹂櫎鍏ㄩ儴 DFlash 骞跺浠?
wiggle.exe erase --all --backup backup.hex --verify

# 鏌ョ湅璁惧淇℃伅锛堜笉鎵ц鎿﹂櫎锛?
wiggle.exe erase --info
```

### 鍐欏叆 DFlash

```bash
# 浠?HEX 鏂囦欢鍐欏叆
wiggle.exe write -f data.hex --verify

# 浠?BIN 鏂囦欢鍐欏叆鎸囧畾鍦板潃
wiggle.exe write -f data.bin --addr 0xAF000000 --verify
```

### 璇诲彇 DFlash

```bash
# 璇诲彇骞朵繚瀛樹负 HEX 鏂囦欢
wiggle.exe read 0xAF000000 0x1000 -o output.hex

# 璇诲彇骞朵繚瀛樹负 BIN 鏂囦欢
wiggle.exe read 0xAF000000 0x1000 -o output.bin
```

### 瀵勫瓨鍣ㄦ搷浣?

```bash
# 鎸夊悕瀛楄鍙栧瘎瀛樺櫒
wiggle.exe reg HF.STATUS

# 鎸夊湴鍧€璇诲彇瀵勫瓨鍣?
wiggle.exe reg 0xF8050010

# 鍐欏叆瀵勫瓨鍣?
wiggle.exe reg HF.CONTROL 0x00000001

# 鍒楀嚭澶栬鐨勬墍鏈夊瘎瀛樺櫒
wiggle.exe reg --list DMU
```

### 鍐呭瓨璋冭瘯

```bash
# Hex Dump
wiggle.exe dump 0xAF000000 0x40

# 鍐欏叆鍐呭瓨
wiggle.exe poke 0xAF000000 0xDEADBEEF

# 鎼滅储鍐呭瓨
wiggle.exe search 0xAF000000 0xAF001000 0xDEADBEEF
```

## 鍏ㄥ眬閫夐」

| 閫夐」 | 璇存槑 |
|------|------|
| `--json` | 杈撳嚭缁撴瀯鍖?JSON锛堢敤浜?MCP/AI 闆嗘垚锛?|
| `--server <ip>` | TAS Server IP 鍦板潃锛堥粯璁? localhost锛?|
| `--target <id>` | 鐩爣鏍囪瘑绗︼紙澶氱洰鏍囨椂閫夋嫨锛?|
| `--device <name>` | 鎵嬪姩鎸囧畾璁惧绫诲瀷锛堣烦杩囪嚜鍔ㄦ娴嬶級 |
| `--config-dir <path>` | DeviceConfigs 鐩綍璺緞 |

## JSON 杈撳嚭妯″紡

娣诲姞 `--json` 鍙傛暟鍙幏寰楃粨鏋勫寲 JSON 杈撳嚭锛屼究浜庤剼鏈拰 AI 宸ュ叿瑙ｆ瀽锛?

```bash
wiggle.exe status --json
```

杈撳嚭鏍煎紡锛?
```json
{
  "status": "ok",
  "command": "status",
  "register": "HF.STATUS",
  "address": "0xF8040010",
  "value": "0x02080000",
  "fields": { ... }
}
```

## MCP Server 闆嗘垚

Wiggle 鎻愪緵 Python MCP Server锛坄aurix_mcp_server.py`锛夛紝鍙泦鎴愬埌 AI 宸ュ叿涓€氳繃鑷劧璇█鎿嶄綔 AURIX 鑺墖銆?

### 閰嶇疆

缂栬緫 `mcp_config.json`锛?
```json
{
  "wiggle_exe": "/path/to/wiggle.exe",
  "aurix_flasher_exe": "/path/to/AURIXFlasher.exe",
  "server": "localhost",
  "target": null,
  "device": null,
  "timeout": 120
}
```

### 鍚姩 MCP Server

```bash
python aurix_mcp_server.py
```

### MCP 宸ュ叿鍒楄〃

| MCP Tool | 瀵瑰簲鍛戒护 |
|----------|----------|
| `wiggle_list` | `wiggle list --json` |
| `wiggle_status` | `wiggle status --json` |
| `wiggle_info` | `wiggle info --json` |
| `wiggle_reg` | `wiggle reg <name> --json` |
| `wiggle_reg_list` | `wiggle reg --list <peripheral> --json` |
| `wiggle_dump` | `wiggle dump <addr> <len> --json` |
| `wiggle_read` | `wiggle read <addr> <len> --json` |
| `wiggle_search` | `wiggle search ... --json` |
| `wiggle_poke` | `wiggle poke <addr> <val> --json` |
| `wiggle_erase` | `wiggle erase ... --json` |
| `wiggle_reset` | `wiggle reset --json` |
| `aurix_pflash` | AURIXFlasher PFlash 鐑у啓 |

### aurix_pflash 鍙傛暟璇存槑

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `hex_file` | string | (蹇呭～) | Intel HEX 鎴?ELF 鏂囦欢璺緞 |
| `verify` | bool | `true` | 鐑у啓鍚庢牎楠?|
| `erase` | string | `""` | 鎿﹂櫎妯″紡锛歚"all"`=鍏ㄧ墖鎿﹂櫎, `"used"`=浠呮摝闄ゅ凡鐢ㄦ墖鍖? 绌?AURIXFlasher 榛樿 |
| `ucb` | bool | `false` | 鍚敤 UCB 缂栫▼ |
| `connect` | string | `""` | 杩炴帴妯″紡锛歚"0"`=鐑繛鎺?涓嶅浣?, `"1"`=澶嶄綅骞舵殏鍋?|
| `start` | string | `""` | 鐑у啓鍚庤涓猴細`"on"`=澶嶄綅杩愯, `"off"`=淇濇寔鏆傚仠 |
| `device_id` | string | `""` | 澶氱洰鏍囨椂鐨?DAP/璁惧 ID锛堝 `"0"`, `"1"`锛?|

## 璁惧閰嶇疆

Wiggle 鍦ㄨ繍琛屾椂鑷姩浠?`DeviceConfigs/` 鐩綍鍔犺浇璁惧淇℃伅銆傛悳绱㈣矾寰勶細

1. `--config-dir` 鍙傛暟鎸囧畾鐨勮矾寰?
2. 鐜鍙橀噺 `TAS_DEVICE_CONFIGS`
3. wiggle.exe 鍚岀骇鐩綍涓嬬殑 `DeviceConfigs/`

姣忎釜璁惧 JSON 鍖呭惈锛欶lash 鍩哄湴鍧€銆佹墖鍖哄ぇ灏忋€侀〉澶у皬銆丣TAG ID銆乁CB 閰嶇疆绛変俊鎭€?

## 瀵勫瓨鍣ㄥ畾涔?

`RegisterDefs/` 鐩綍鍖呭惈鍚勮姱鐗囩殑瀹屾暣瀵勫瓨鍣ㄥ畾涔夛紙浠?SVD 杞崲鑰屾潵锛夛紝渚?`reg` 鍛戒护鎸夊悕瀛楁煡鎵惧瘎瀛樺櫒鍦板潃銆?

- 濡傛灉 RegisterDefs 鏂囦欢涓嶅瓨鍦ㄦ垨鍔犺浇澶辫触锛宍reg` 鍛戒护浠嶅彲閫氳繃鍦板潃鐩存帴璇诲啓
- RegisterDefs 鏂囦欢杈冨ぇ锛堝崟鏂囦欢 19-65 MB锛夛紝濡備笉闇€瑕?`reg` 鍛戒护鍙笉鍖呭惈

## 璁稿彲璇?

Apache License 2.0
