# AURIX MCP Server 閰嶇疆涓庝娇鐢ㄨ鏄?

## 姒傝堪

AURIX MCP Server 鏄竴涓熀浜?[Model Context Protocol (MCP)](https://modelcontextprotocol.io/) 鐨勬湇鍔″櫒锛屽皢 `wiggle.exe`锛圖Flash 鎿嶄綔 + 璋冭瘯锛夊拰 `AURIXFlasher.exe`锛圥Flash 鐑у啓锛夊皝瑁呬负 AI 宸ュ叿锛屼娇 LLM锛堝 Claude銆丟PT锛夎兘閫氳繃鑷劧璇█鎿嶄綔 Infineon AURIX 寰帶鍒跺櫒銆?

## 鐜瑕佹眰

- **Python**: 3.10+
- **渚濊禆鍖?*: `mcp[cli]`
- **wiggle.exe**: AURIX DFlash/璋冭瘯宸ュ叿
- **AURIXFlasher.exe**: Infineon PFlash 鐑у啓宸ュ叿锛堝彲閫夛紝浠?PFlash 鐑у啓鏃堕渶瑕侊級
- **TAS Server**: 蹇呴』杩愯涓紝鎻愪緵璋冭瘯鍣ㄧ‖浠惰繛鎺?

## 瀹夎

```bash
pip install "mcp[cli]"
```

## 閰嶇疆

### 閰嶇疆鏂囦欢 (mcp_config.json)

```json
{
    "wiggle_exe": "D:\\path\\to\\wiggle\\wiggle.exe",
    "aurix_flasher_exe": "C:\\Infineon\\AURIXFlasherSoftwareTool-3.0.16\\AURIXFlasher.exe",
    "server": "localhost",
    "target": null,
    "device": null,
    "timeout": 120
}
```

| 瀛楁 | 绫诲瀷 | 璇存槑 |
|------|------|------|
| `wiggle_exe` | string | wiggle.exe 瀹屾暣璺緞锛堜笉璁惧垯鑷姩鎼滅储锛?|
| `aurix_flasher_exe` | string | AURIXFlasher.exe 瀹屾暣璺緞锛堜笉璁惧垯鑷姩鎼滅储锛?|
| `server` | string | TAS Server IP 鍦板潃锛岄粯璁?`localhost` |
| `target` | string\|null | 鐩爣鏍囪瘑绗︼紝澶氱洰鏍囨椂鎸囧畾锛沶ull=鑷姩閫夋嫨 |
| `device` | string\|null | 璁惧绫诲瀷瑕嗙洊锛堝 `TC33x`锛夛紱null=鑷姩妫€娴?|
| `timeout` | int | 鍛戒护瓒呮椂绉掓暟锛岄粯璁?120 |

### 鑷姩鎼滅储璺緞

鏈厤缃?exe 璺緞鏃讹紝MCP Server 鎸変互涓嬮『搴忔悳绱細

1. `tools/` 鐩綍锛堣剼鏈墍鍦ㄧ洰褰曪級
2. `../wiggle/` 鐩綍锛堥儴缃茬粨鏋勶級
3. `../data/` 鐩綍
4. `../AURIXFlasher/` 鐩綍

## 鍚姩

```bash
# 榛樿 stdio 浼犺緭 + 鑷姩鎼滅储璺緞
python aurix_mcp_server.py

# 鎸囧畾閰嶇疆鏂囦欢
python aurix_mcp_server.py --config mcp_config.json

# 浣跨敤 SSE 浼犺緭锛圚TTP 妯″紡锛?
python aurix_mcp_server.py --config mcp_config.json --transport sse
```

### 鍦?AI 瀹㈡埛绔腑閰嶇疆

#### Claude Desktop (claude_desktop_config.json)

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": ["D:\\path\\to\\tools\\aurix_mcp_server.py", "--config", "D:\\path\\to\\tools\\mcp_config.json"]
    }
  }
}
```

#### Cursor / VS Code MCP 閰嶇疆

```json
{
  "mcp": {
    "servers": {
      "aurix": {
        "command": "python",
        "args": ["D:\\path\\to\\tools\\aurix_mcp_server.py", "--config", "D:\\path\\to\\tools\\mcp_config.json"]
      }
    }
  }
}
```

## MCP 宸ュ叿鍒楄〃

### 璁惧淇℃伅

| 宸ュ叿 | 璇存槑 |
|------|------|
| `wiggle_list` | 鍒楀嚭宸茶繛鎺ョ殑 TAS 鐩爣璁惧 |
| `wiggle_status` | Flash 鐘舵€佸瘎瀛樺櫒锛圫VD 瀛楁瑙ｇ爜锛?|
| `wiggle_info` | 璁惧淇℃伅銆佸唴瀛樺竷灞€銆丼VD 鎽樿 |

### DFlash 鎿嶄綔

| 宸ュ叿 | 璇存槑 |
|------|------|
| `wiggle_erase` | 鎿﹂櫎 DFlash 鎵囧尯锛堝叏閮ㄦ垨鎸囧畾鍦板潃锛?|
| `wiggle_write` | 浠?HEX/BIN 鏂囦欢鍐欏叆 DFlash |
| `wiggle_read` | 璇诲彇 DFlash 鍐呭 |

### PFlash 鐑у啓

| 宸ュ叿 | 璇存槑 |
|------|------|
| `aurix_pflash` | 璋冪敤 AURIXFlasher 鐑у啓 PFlash锛堟敮鎸?HEX/ELF锛?|

### 璋冭瘯

| 宸ュ叿 | 璇存槑 |
|------|------|
| `wiggle_reg` | 鎸夊悕瀛楁垨鍦板潃璇诲啓瀵勫瓨鍣?|
| `wiggle_dump` | 鍐呭瓨 Hex Dump锛圥Flash/DFlash/SRAM锛?|
| `wiggle_poke` | 鍐欏叆鍊煎埌鍐呭瓨鍦板潃 |
| `wiggle_search` | 鎼滅储鍐呭瓨涓殑瀛楄妭妯″紡 |
| `wiggle_compare` | 鏈湴鏂囦欢涓?Flash 鍐呭瀵规瘮 |

### 鎺у埗

| 宸ュ叿 | 璇存槑 |
|------|------|
| `wiggle_reset` | 澶嶄綅 MCU |

---

## 宸ュ叿璇︾粏鍙傛暟

### wiggle_list

鍒楀嚭宸茶繛鎺ョ殑鐩爣璁惧銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `server` | string | `""` | TAS Server IP锛堣鐩栭厤缃級 |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|

---

### wiggle_status

璇诲彇 Flash 鐘舵€佸瘎瀛樺櫒锛屽甫 SVD 瀛楁瑙ｇ爜銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

---

### wiggle_info

鑾峰彇璁惧淇℃伅銆佸唴瀛樺竷灞€銆丼VD 瀵勫瓨鍣ㄦ憳瑕併€?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

---

### wiggle_erase

鎿﹂櫎 DFlash 鎵囧尯銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `erase_all` | bool | `true` | 鎿﹂櫎鏁翠釜 DFlash |
| `address` | string | `""` | DFlash 璧峰鍦板潃锛堝崄鍏繘鍒讹紝erase_all=false 鏃跺繀濉級 |
| `sectors` | int | `0` | 鎿﹂櫎鎵囧尯鏁帮紙0=1涓墖鍖猴級 |
| `verify` | bool | `false` | 鎿﹂櫎鍚庡洖璇绘牎楠?|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

**绀轰緥**锛?
```
鎿﹂櫎鍏ㄩ儴 DFlash锛歸iggle_erase(erase_all=True, verify=True)
鎿﹂櫎鎸囧畾鎵囧尯锛歸iggle_erase(erase_all=False, address="0xAF000000", sectors=2)
```

---

### wiggle_write

浠?HEX/BIN 鏂囦欢鍐欏叆 DFlash銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `file_path` | string | (蹇呭～) | HEX 鎴?BIN 鏂囦欢璺緞 |
| `verify` | bool | `true` | 鍐欏叆鍚庡洖璇绘牎楠?|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

> 娉ㄦ剰锛歸iggle_write 浠呮敮鎸?DFlash锛孭Flash 璇蜂娇鐢?aurix_pflash銆?

---

### wiggle_read

璇诲彇 DFlash 鍐呭銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `address` | string | (蹇呭～) | 璧峰鍦板潃锛堝崄鍏繘鍒讹級 |
| `length` | string | (蹇呭～) | 璇诲彇闀垮害锛堝崄鍏繘鍒讹級 |
| `output_file` | string | `""` | 淇濆瓨璺緞锛?hex 鎴?.bin锛夛紝绌?杩斿洖鏁版嵁 |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

---

### aurix_pflash

璋冪敤 AURIXFlasher.exe 鐑у啓 PFlash銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `hex_file` | string | (蹇呭～) | Intel HEX 鎴?ELF 鏂囦欢璺緞 |
| `verify` | bool | `true` | 鐑у啓鍚庢牎楠?|
| `erase` | string | `""` | 鎿﹂櫎妯″紡锛歚"all"`=鍏ㄧ墖鎿﹂櫎, `"used"`=浠呭凡鐢ㄦ墖鍖? 绌?榛樿 |
| `ucb` | bool | `false` | 鍚敤 UCB 缂栫▼ |
| `connect` | string | `""` | 杩炴帴妯″紡锛歚"0"`=鐑繛鎺?涓嶅浣?, `"1"`=澶嶄綅骞舵殏鍋?|
| `start` | string | `""` | 鐑у啓鍚庤涓猴細`"on"`=澶嶄綅杩愯, `"off"`=淇濇寔鏆傚仠, 绌?榛樿(澶嶄綅鍚姩) |
| `device_id` | string | `""` | 澶氱洰鏍囨椂鐨?DAP/璁惧 ID锛堝 `"0"`, `"1"`锛?|

**绀轰緥**锛?
```
鍩烘湰鐑у啓锛歸iggle_pflash(hex_file="firmware.hex")
鐑у啓鍚庝笉鍚姩锛歸iggle_pflash(hex_file="firmware.hex", start="off")
鍏ㄦ摝闄?鐑у啓+鐑繛鎺ワ細aurix_pflash(hex_file="app.elf", erase="all", connect="0")
```

---

### wiggle_reg

鎸?SVD 鍚嶅瓧鎴栧湴鍧€璇诲啓瀵勫瓨鍣ㄣ€?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `name_or_addr` | string | (蹇呭～*) | 瀵勫瓨鍣ㄥ悕(濡?`DMU.HF.STATUS`) 鎴栧湴鍧€(濡?`0xF8040010`) |
| `value` | string | `""` | 鍐欏叆鍊硷紙绌?璇诲彇锛?|
| `list_peripheral` | string | `""` | 鍒楀嚭澶栬瀵勫瓨鍣紙璁剧疆鏃跺拷鐣?name_or_addr锛?|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

**绀轰緥**锛?
```
璇诲彇瀵勫瓨鍣細wiggle_reg("DMU.HF.STATUS")
鍐欏叆瀵勫瓨鍣細wiggle_reg("DMU.HF.STATUS", value="0x00000001")
鎸夊湴鍧€璇诲彇锛歸iggle_reg("0xF8040010")
鍒楀嚭DMU澶栬锛歸iggle_reg("", list_peripheral="DMU")
```

---

### wiggle_dump

璇诲彇鍐呭瓨骞惰繑鍥?Hex Dump锛堟敮鎸?PFlash/DFlash/SRAM锛夈€?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `address` | string | (蹇呭～) | 璧峰鍦板潃锛堝崄鍏繘鍒讹級 |
| `length` | string | (蹇呭～) | 璇诲彇闀垮害锛堝崄鍏繘鍒讹級 |
| `output_file` | string | `""` | 淇濆瓨璺緞锛?bin 鎴?.hex锛?|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

---

### wiggle_poke

鍐欏叆鍊煎埌鍐呭瓨鍦板潃骞跺洖璇婚獙璇併€?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `address` | string | (蹇呭～) | 鐩爣鍦板潃锛堝崄鍏繘鍒讹級 |
| `value` | string | (蹇呭～) | 鍐欏叆鍊硷紙鍗佸叚杩涘埗锛?|
| `width` | int | `32` | 浣嶅锛?, 16, 32, 64 |
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

---

### wiggle_search

鎼滅储鍐呭瓨涓殑瀛楄妭妯″紡銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `address` | string | (蹇呭～) | 鎼滅储璧峰鍦板潃 |
| `length` | string | (蹇呭～) | 鎼滅储鑼冨洿闀垮害 |
| `pattern` | string | (蹇呭～) | 鍗佸叚杩涘埗瀛楄妭妯″紡锛堝 `DEADBEEF`锛?|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

---

### wiggle_compare

姣旇緝鏈湴鏂囦欢涓?Flash/鍐呭瓨鍐呭銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `file_path` | string | (蹇呭～) | 鏈湴鏂囦欢璺緞锛?hex 鎴?.bin锛?|
| `address` | string | (蹇呭～) | 姣旇緝鐨勮捣濮嬪湴鍧€ |
| `length` | string | `""` | 姣旇緝闀垮害锛堢┖=鏂囦欢鍏ㄩ暱锛?|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|
| `device` | string | `""` | 璁惧绫诲瀷瑕嗙洊 |

---

### wiggle_reset

澶嶄綅 MCU銆?

| 鍙傛暟 | 绫诲瀷 | 榛樿鍊?| 璇存槑 |
|------|------|--------|------|
| `halt` | bool | `false` | 澶嶄綅鍚庢殏鍋滐紙true=鏆傚仠, false=杩愯锛?|
| `server` | string | `""` | TAS Server IP |
| `target` | string | `""` | 鐩爣鏍囪瘑绗?|

---

## 杩斿洖鏍煎紡

鎵€鏈夊伐鍏疯繑鍥?JSON 瀛楃涓诧紝缁熶竴鏍煎紡锛?

**鎴愬姛**锛?
```json
{
  "status": "ok",
  "command": "...",
  ...
}
```

**閿欒**锛?
```json
{
  "status": "error",
  "code": -1,
  "message": "閿欒鎻忚堪"
}
```

甯歌閿欒鐮侊細
| code | 鍚箟 |
|------|------|
| -1 | 鍙墽琛屾枃浠舵湭鎵惧埌 |
| -2 | 鍛戒护瓒呮椂 |
| -3 | 杩愯鏃跺紓甯?|
| >0 | 绋嬪簭閫€鍑虹爜锛堥潪0锛?|

## 娴嬭瘯

```bash
# 杩愯鍏ㄩ噺 MCP 娴嬭瘯锛堥渶瑕?TAS Server + 璁惧杩炴帴锛?
python test_mcp_all.py

# 杈撳嚭娴嬭瘯鎶ュ憡鍒版枃浠?
# 鎶ュ憡鑷姩淇濆瓨鍒?../wiggle/mcp_test_report.txt
```

## 鍏稿瀷宸ヤ綔娴?

### 1. 鏌ョ湅璁惧鐘舵€?
```
AI 鈫?wiggle_list() 鈫?鑾峰彇杩炴帴鐨勭洰鏍?
AI 鈫?wiggle_info() 鈫?鑾峰彇璁惧鍨嬪彿銆佸唴瀛樺竷灞€
AI 鈫?wiggle_status() 鈫?妫€鏌?Flash 鐘舵€?
```

### 2. DFlash 鎿﹂櫎 + 鍐欏叆
```
AI 鈫?wiggle_erase(erase_all=True, verify=True)
AI 鈫?wiggle_write(file_path="data.hex", verify=True)
AI 鈫?wiggle_compare(file_path="data.hex", address="0xAF000000")
```

### 3. PFlash 鐑у啓鍥轰欢
```
AI 鈫?aurix_pflash(hex_file="firmware.hex", verify=True, start="off")
AI 鈫?wiggle_reset(halt=False)  # 鎵嬪姩鍚姩
```

### 4. 璋冭瘯瀵勫瓨鍣?
```
AI 鈫?wiggle_reg("", list_peripheral="DMU")  # 鍒楀嚭DMU瀵勫瓨鍣?
AI 鈫?wiggle_reg("DMU.HF.STATUS")            # 璇诲彇鐘舵€?
AI 鈫?wiggle_reg("DMU.HF.CONTROL", value="0x1")  # 鍐欏叆鎺у埗瀵勫瓨鍣?
```

### 5. 鍐呭瓨妫€鏌?
```
AI 鈫?wiggle_dump(address="0xAF000000", length="0x100")
AI 鈫?wiggle_search(address="0xAF000000", length="0x10000", pattern="DEADBEEF")
```
