# AURIX MCP Server 鈥?鎵撳寘璇存槑 (v2.2)

> 灏?Infineon AURIX TC2x / TC3x 寰帶鍒跺櫒鐨?DFlash 璋冭瘯 + PFlash 鐑у啓鑳藉姏锛屽皝瑁呬负
> [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) 宸ュ叿锛岃 Claude / GPT / Cursor /
> Qoder / VS Code (Continue/Cline) 绛?AI 瀹㈡埛绔兘閫氳繃鑷劧璇█鎿嶄綔纭欢銆?
---

## 鐩綍

1. [Quick Start 鈥?30 绉掕窇璧锋潵](#1-quick-start--30-绉掕窇璧锋潵)
2. [鍖呯洰褰曠粨鏋刔(#2-鍖呯洰褰曠粨鏋?
   - 2.5 [宸ュ叿鏉ユ簮锛坵iggle vs AURIXFlasher 鐨勬湰璐ㄥ尯鍒級](#25-宸ュ叿鏉ユ簮wiggle-vs-aurixflasher-鐨勬湰璐ㄥ尯鍒?
   - 2.6 [AURIXFlasher 瀹夎涓庨厤缃甝(#26-aurixflasher-瀹夎涓庨厤缃?
3. [涓夊眰鏋舵瀯 / 鏁版嵁娴乚(#3-涓夊眰鏋舵瀯--鏁版嵁娴?
4. [鐜瑕佹眰](#4-鐜瑕佹眰)
5. [閰嶇疆 `mcp_config.json`](#5-閰嶇疆-mcp_configjson)
6. [鍚姩 MCP Server](#6-鍚姩-mcp-server)
7. [鍦?AI 瀹㈡埛绔腑娉ㄥ唽](#7-鍦?ai-瀹㈡埛绔腑娉ㄥ唽)
8. [19 涓?MCP 宸ュ叿閫熸煡](#8-19-涓?mcp-宸ュ叿閫熸煡)
9. [宸ュ叿璇︾粏鍙傛暟](#9-宸ュ叿璇︾粏鍙傛暟)
10. [杩斿洖 JSON 鏍煎紡](#10-杩斿洖-json-鏍煎紡)
11. [鍏稿瀷宸ヤ綔娴乚(#11-鍏稿瀷宸ヤ綔娴?
12. [UCB 鍦板潃鍥?(TC3xx)](#12-ucb-鍦板潃鍥?tc3xx)
13. [閫€鍑虹爜 / 閿欒鐮乚(#13-閫€鍑虹爜--閿欒鐮?
14. [娴嬭瘯](#14-娴嬭瘯)
15. [甯歌闂](#15-甯歌闂)
16. [閰嶅鎵嬪唽 (Excel)](#16-閰嶅鎵嬪唽-excel)
17. [鏂囨。鐗堟湰](#17-鏂囨。鐗堟湰)

---

## 1. Quick Start 鈥?30 绉掕窇璧锋潵

```bash
# 鈶?瑁?Python 渚濊禆锛圥ython 3.10+锛?pip install "mcp[cli]"

# 鈶?鍚姩 Infineon TAS Server锛坢iniWiggler 璋冭瘯鍣ㄥ悗绔紝淇濇寔鍚庡彴杩愯锛?
# 鈶?AURIX 鏉夸笂鐢?+ miniWiggler USB 鎻掑叆

# 鈶?鍚姩 MCP server锛坰tdio 妯″紡锛岀粰 Claude / Cursor / Qoder 鐢級
python D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json
```

鍚姩鎴愬姛鍚庯紝AI 瀹㈡埛绔氨鑳借皟鐢?**19 涓伐鍏?*锛?3 涓‖浠舵搷浣?+ 6 涓瀯寤轰骇鐗╁垎鏋愶級鎿嶄綔 AURIX銆?
**5 绉掔儫闆炬祴璇?*锛堜笉寮€ AI 瀹㈡埛绔紝鍏堢‘璁ら摼璺€氾級锛?
```bash
# wiggle 鏄惁璇嗗埆鍒扮洰鏍囷紵
D:\wiggle_MCP\wiggle\wiggle.exe list

# AURIXFlasher 鏄惁鑳借繛涓婏紵锛堝鏋滃凡瀹夎锛?AURIXFlasher.exe -l   # 鎴栧畬鏁磋矾寰?
# 璺戝叏閲?MCP 闆嗘垚娴嬭瘯
cd D:\wiggle_MCP
python test_package.py
# 棰勬湡锛? PASS / 0 FAIL
```

---

## 2. 鍖呯洰褰曠粨鏋?
```
D:\wiggle_MCP\                            鈫?鏈寘鏍圭洰褰?鈹溾攢鈹€ aurix_mcp_server.py                   鈫?MCP server 鍏ュ彛锛圥ython锛?鈹溾攢鈹€ build_analysis.py                     鈫?鏋勫缓浜х墿瑙ｆ瀽搴擄紙MAP/ELF/LST/MDF锛?鈹溾攢鈹€ mcp_config.json                       鈫?閰嶇疆鏂囦欢
鈹溾攢鈹€ requirements.txt                      鈫?Python 渚濊禆鍒楄〃
鈹溾攢鈹€ start_mcp.bat                         鈫?Windows 鍚姩鑴氭湰
鈹溾攢鈹€ start_mcp.sh                          鈫?Linux/Mac 鍚姩鑴氭湰
鈹溾攢鈹€ test_package.py                       鈫?瀹屾暣鎬ч獙璇佽剼鏈?鈹溾攢鈹€ README.md                             鈫?鏈枃妗?鈹?鈹溾攢鈹€ wiggle\                               鈫?鑷爺 DFlash 璋冭瘯宸ュ叿
鈹?  鈹溾攢鈹€ wiggle.exe
鈹?  鈹溾攢鈹€ DeviceConfigs\                    鈫?21 涓姱鐗囩殑璁惧閰嶇疆 JSON
鈹?  鈹?  鈹溾攢鈹€ devices.json
鈹?  鈹?  鈹斺攢鈹€ TC{21x..39x,A_step,...}.json
鈹?  鈹溾攢鈹€ RegisterDefs\                     鈫?8 涓姱鐗囩殑 SVD 娲剧敓瀹屾暣瀵勫瓨鍣ㄥ畾涔?鈹?  鈹?  鈹斺攢鈹€ TC{23x,27xD,29xB,33x,36x,37x,38x,39xB}-full.json
鈹?  鈹斺攢鈹€ wiggle-Command-Reference.xlsx     鈫?閰嶅 CLI 鎵嬪唽
鈹?鈹斺攢鈹€ docs\
    鈹溾攢鈹€ MCP-Command-Reference.xlsx        鈫?19 涓?MCP 宸ュ叿鐨勫畬鏁存墜鍐?    鈹溾攢鈹€ AURIXFlasher-Command-Reference.xlsx 鈫?AURIXFlasher CLI 鎵嬪唽
    鈹斺攢鈹€ mcp_config_guide.md               鈫?閰嶇疆鏂囦欢濉啓鎸囧崡

> **娉ㄦ剰**锛歚AURIXFlasher.exe` 鏄?Infineon 闂簮宸ュ叿锛?*涓嶅寘鍚湪鏈寘鍐?*锛岄渶瑕佽嚜琛屽畨瑁咃紙瑙?搂2.6锛夈€?
**鎬荤鐩樺崰鐢?*锛氱害 430 MB锛堝叾涓?8 涓?RegisterDefs JSON 鍗?~290 MB锛夈€?
---

## 2.5 宸ュ叿鏉ユ簮锛坵iggle vs AURIXFlasher 鐨勬湰璐ㄥ尯鍒級

鏈寘浣跨敤涓や釜鏍稿績 exe锛?*鏉ユ簮瀹屽叏涓嶅悓**锛屾贩娣嗕細瀵艰嚧鐗堟潈 / 缁存姢 / 鍗囩骇棰勬湡鍑洪敊锛?
|  | `wiggle.exe`锛堟湰鍖呭唴鍚級 | `AURIXFlasher.exe`锛堥渶鑷瀹夎锛?|
|--|---------------|---------------------|
| **鎬ц川** | 鏈」鐩?*鑷爺** | Infineon **瀹樻柟鍟嗕笟浜у搧** |
| **浣滆€?* | 鏈」鐩紙鍩轰簬 Infineon 寮€婧愮殑 TAS Client API锛?| Infineon Technologies AG |
| **寮€婧?* | Apache License 2.0 | **闂簮**鍟嗕笟宸ュ叿 |
| **鍒嗗彂** | **鍖呭惈鍦ㄦ湰鍖呭唴** | **涓嶅寘鍚?*锛岄渶鑷浠?Infineon 瀹樼綉涓嬭浇 |
| **璇█/鏋勫缓** | C++17锛孋Make + Conan 2锛?*闈欐€侀摼鎺?*锛堟棤 VC++ 杩愯渚濊禆锛墊 .NET Framework 4.8锛堢湅 `AURIXFlasher.exe.config`锛墊
| **鏇存柊鏂瑰紡** | 鏀规簮鐮?鈫?`build.bat` 鈫?閲嶆柊閮ㄧ讲鍒?`wiggle_MCP\wiggle\` | 涓嬭浇 Infineon 鏂扮増瀹夎鍖?|
| **鑳藉姏鍩?* | DFlash 鎿嶄綔 + 璋冭瘯锛?2 涓?MCP 宸ュ叿锛墊 PFlash 鏁寸墖鐑у啓锛? 涓?MCP 宸ュ叿锛歚aurix_pflash`锛墊
| **渚濊禆 DLL** | 鏃狅紙闈欐€侀摼鎺ワ級| `ConversionLayerdll.dll` / `LATTE.dll` / `Newtonsoft.Json.dll` / `tricore-objcopy.exe` / 澶氫釜 `flax_TC*.hex`锛團lash Loader锛墊
| **鍗忚** | TAS Client API锛堝紑婧?C++ 搴擄紝Apache 2.0锛墊 绉佹湁 DAS 鍗忚锛圛nfineon 鍐呴儴锛墊
| **瀵规帴纭欢** | TAS Server (TCP:2000) 鈫?miniWiggler 鈫?AURIX | DAS Server 鈫?miniWiggler 鈫?AURIX锛堜篃鏀寔 DAP/JTAG/SPD/SWD锛墊

**涓€鍙ヨ瘽鎬荤粨**锛?- **wiggle = 鎴戜滑鍐欑殑 DFlash 璋冭瘯鍣?*锛圓pache 2.0锛屾湰鍖呯洿鎺ュ寘鍚級
- **AURIXFlasher = Infineon 鐨?PFlash 鐑у啓鍣?*锛堥棴婧愶紝闇€鑷瀹夎锛岃 搂2.6锛?
鎵€浠?MCP server 閲岀殑 19 涓伐鍏蜂腑锛?- **12 涓?*锛坄wiggle_list` / `wiggle_info` / `wiggle_status` / `wiggle_read` / `wiggle_erase` / `wiggle_write` / `wiggle_reg` / `wiggle_dump` / `wiggle_poke` / `wiggle_search` / `wiggle_compare` / `wiggle_reset`锛夆啋 璋?wiggle.exe
- **1 涓?*锛坄aurix_pflash`锛夆啋 璋?AURIXFlasher.exe
- **6 涓?*锛坄build_project` / `reload_parsers` / `parse_symbols` / `parse_elf` / `lookup_symbol` / `lookup_address`锛夆啋 瑙ｆ瀽缂栬瘧浜х墿锛圱ASKING MAP / GCC LST / ELF / MDF锛?
瑕佹墿灞曞姛鑳斤紙姣斿鏀寔鏂拌姱鐗囧瀷鍙枫€佸姞鏂拌皟璇曞懡浠わ級鈫?鏀?wiggle 婧愮爜 + 閲嶆柊 build銆?瑕佹洿鏂?AURIXFlasher 鐗堟湰锛堟瘮濡傜瓑 TC4x 鏀寔锛夆啋 绛?Infineon 鍙戞柊鐗堛€?
> `wiggle\RegisterDefs\*.json` 涔熶笉灞炰簬 Infineon锛氭槸鐢ㄩ」鐩嚜甯︾殑 `svd-to-full-json` skill 浠?Infineon 鍏紑鐨?`data/SVD\*.svd` 娲剧敓鐨?JSON锛堣 [搂15 甯歌闂 / 鎬庝箞鏇存柊 SVD 瀵勫瓨鍣ㄥ畾涔塢(#15-甯歌闂)锛夈€?
---

## 2.6 AURIXFlasher 瀹夎涓庨厤缃?
AURIXFlasher 鏄?Infineon 鎻愪緵鐨?*闂簮鍟嗕笟宸ュ叿**锛屽彈鐗堟潈闄愬埗**涓嶅寘鍚湪鏈寘鍐?*銆?
### 绗竴姝ワ細涓嬭浇瀹夎

1. 鍓嶅線 Infineon 瀹樼綉涓嬭浇 [AURIX Flasher Software Tool](https://www.infineon.com/cms/en/tools/landing/aurix-tools/aurix-flasher-software-tool/)
2. 瀹夎锛堝厤璐癸紝鏃犻渶璐拱 license锛?3. 榛樿瀹夎璺緞锛歚C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\`

### 绗簩姝ワ細MCP Server 鑷姩鏌ユ壘

`aurix_mcp_server.py` 鍚姩鏃舵寜浠ヤ笅浼樺厛绾ц嚜鍔ㄦ悳绱?AURIXFlasher.exe锛?
| 浼樺厛绾?| 鏉ユ簮 | 璇存槑 |
|--------|------|------|
| 1 | `mcp_config.json` 鐨?`aurix_flasher_exe` 瀛楁 | 鏄惧紡閰嶇疆锛屾渶浼樺厛 |
| 2 | 鐜鍙橀噺 `AURIX_FLASHER_EXE` | 瀹屾暣璺緞 |
| 3 | 鐜鍙橀噺 `AURIX_FLASHER_DIR` | 鐩綍锛岃嚜鍔ㄦ嫾 `AURIXFlasher.exe` |
| 4 | 鑴氭湰鍚岀洰褰?| `D:\wiggle_MCP\AURIXFlasher.exe` |
| 5 | `D:\wiggle_MCP\AURIXFlasher\` | 鍚岀骇 AURIXFlasher 瀛愮洰褰?|
| 6 | Infineon 榛樿瀹夎璺緞 | `C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>`C:\Program Files\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>`C:\Program Files (x86)\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>锛坓lob 鍖归厤浠绘剰鐗堟湰鍙凤級|
| 7 | PATH 鐜鍙橀噺 | scoop / choco / 鎵嬪姩鍔犲叆 PATH 鐨勮兘鎵惧埌 |

**閫氬父鎯呭喌涓?*锛屾寜榛樿璺緞瀹夎鍚?MCP Server 浼氳嚜鍔ㄦ壘鍒帮紝鏃犻渶鎵嬪姩閰嶇疆銆?
### 绗笁姝ワ細濡傛灉鑷姩鏌ユ壘澶辫触

鍦?`mcp_config.json` 涓墜鍔ㄦ寚瀹氾細

```json
{
    "aurix_flasher_exe": "C:\\Infineon\\AURIXFlasherSoftwareTool-3.0.16\\AURIXFlasher.exe",
    ...
}
```

鎴栬€呰缃幆澧冨彉閲忥紙浼樺厛绾ч珮浜庨厤缃枃浠讹級锛?
```powershell
# 鏂规硶 A锛氬畬鏁磋矾寰?set AURIX_FLASHER_EXE=C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\AURIXFlasher.exe

# 鏂规硶 B锛氬彧缁欑洰褰?set AURIX_FLASHER_DIR=C:\Infineon\AURIXFlasherSoftwareTool-3.0.16
```

### 楠岃瘉

```bash
# 濡傛灉 AURIXFlasher 鍦?PATH 鎴栧凡閰嶇疆濂?AURIXFlasher.exe -l

# 鎴栧畬鏁磋矾寰?C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\AURIXFlasher.exe -l
```

鎴愬姛搴斿垪鍑哄凡杩炴帴鐨?AURIX 鐩爣銆?
---

## 3. 涓夊眰鏋舵瀯 / 鏁版嵁娴?
```
鈹屸攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?L7  AI 瀹㈡埛绔?(Claude / Cursor / Qoder / Continue)              鈹?鈹?     鈫?stdio (JSON-RPC 2.0)  鎴? SSE (HTTP)                    鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?L6  MCP Server (aurix_mcp_server.py, FastMCP)                   鈹?鈹?     鈥?13 涓?wiggle_* 宸ュ叿 鈫?缈昏瘧鎴?wiggle / AURIXFlasher 璋冪敤  鈹?鈹?     鈥?5 涓?build_* 宸ュ叿 鈫?瑙ｆ瀽 TASKING/GCC 缂栬瘧浜х墿            鈹?鈹?     鈥?--json 瑙ｆ瀽 / 閿欒鐮佺粺涓€                                  鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?L5  CLI 宸ュ叿 (wiggle.exe / AURIXFlasher.exe)                     鈹?鈹?     鈥?DFlash: 璇?/ 鎿?/ 鍐?/ rewrite / restore / 璋冭瘯瀵勫瓨鍣?   鈹?鈹?     鈥?PFlash: 鏁寸墖鎿?/ 缂栫▼ / 鏍￠獙 (AURIXFlasher)              鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?L4  TAS Client API (C++ 闈欐€佸簱, src/tas_client/)                鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?L3  TAS Server (Infineon 瀹樻柟, 鍚庡彴杩涚▼, TCP:2000)             鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?L2  miniWiggler 璋冭瘯鎺㈤拡 (USB 鈫?DAP/JTAG)                       鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?L1  AURIX TC2x / TC3x 鐩爣鏉?                                   鈹?鈹?     DFlash 0xAF000000 / PFlash 0xA0000000 / SRAM 0x70000000    鈹?鈹斺攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?```

**鑱岃矗鍒掑垎**锛?- **DFlash 鎿嶄綔**锛堣 / 鎿?/ 鍐?/ 璋冭瘯锛夆啋 `wiggle.exe`锛孧CP 宸ュ叿 12 涓?- **PFlash 鐑у啓**锛堟暣鐗囨摝 + 缂栫▼ + 鏍￠獙锛夆啋 `AURIXFlasher.exe`锛孧CP 宸ュ叿 1 涓細`aurix_pflash`
- **涓よ€呬簰琛ヤ笉閲嶅彔** 鈥斺€?涓嶈鎶?wiggle 鐢ㄥ埌 PFlash锛屼篃涓嶈鎶?AURIXFlasher 鐢ㄥ埌 DFlash

---

## 4. 鐜瑕佹眰

| 缁勪欢 | 鏈€浣庣増鏈?| 澶囨敞 |
|------|----------|------|
| OS | Windows 10/11 x64 鎴?Linux | 32 浣?DLL 璺緞鏈夊樊寮?|
| **Python** | **3.10+锛堢‖鎬ц姹傦級** | `mcp[cli]` 鍖呬笉鏀寔 3.9 鍙婁互涓嬨€俙python -V` 鏌ョ湅 |
| `mcp[cli]` | latest | `pip install "mcp[cli]"` |
| TAS Server | 浠绘剰 Infineon 鍙楁敮鎸佺増鏈?| 鍚庡彴杩涚▼锛岀洃鍚?TCP:2000 |
| miniWiggler | 椹卞姩瑁呭ソ | USB 鎺?PC锛孞TAG/DAP 鎺?AURIX |
| AURIX 鐩爣 | TC2x / TC3x | TC4x **涓嶆敮鎸?*锛坆ase 鍦板潃銆佸懡浠ゅ簭鍒椾笉鍚岋級 |

> **Python 3.8/3.9 鐢ㄦ埛**锛氱洿鎺?`pip install "mcp[cli]"` 浼氭姤 `Could not find a version that satisfies the requirement mcp[cli] (from versions: none)`銆?*涓や釜瑙ｅ喅鏂瑰紡**锛?> 1. 瑁?Python 3.10+锛堟帹鑽愶級锛歔python.org/downloads](https://www.python.org/downloads/)
> 2. 涓嶆兂鎹?Python锛氱洿鎺ョ敤 wiggle CLI锛?3 涓?MCP 宸ュ叿鏈川閮芥槸 `wiggle.exe --json` 鐨勫皝瑁咃紝璺宠繃 MCP server 涔熺収鏍风敤锛?
---

## 5. 閰嶇疆 `mcp_config.json`

```json
{
    "wiggle_exe":         "D:\\wiggle_MCP\\wiggle\\wiggle.exe",
    "aurix_flasher_exe":  null,
    "server":             "localhost",
    "target":             null,
    "device":             null,
    "timeout":            120,

    "build": {
        "command":        null,
        "work_dir":       null,
        "timeout":        300
    },
    "project": {
        "elf_file":       null,
        "map_file":       null,
        "lst_file":       null,
        "mdf_file":       null,
        "hex_file":       null,
        "toolchain":      null
    }
}
```

### 5.1 鍩虹閰嶇疆锛堢‖浠舵搷浣滐級

| 瀛楁 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `wiggle_exe` | string\|null | `null`=鑷姩鎼?| wiggle.exe 瀹屾暣璺緞 |
| `aurix_flasher_exe` | string\|null | `null`=鑷姩鎼?| AURIXFlasher.exe 瀹屾暣璺緞 |
| `server` | string | `localhost` | TAS Server IP |
| `target` | string\|null | `null`=棣栦釜 | 鐩爣 ID锛屽鐩爣鏃舵寚瀹?|
| `device` | string\|null | `null`=鑷姩 | 璁惧鍚嶈鐩栵紙濡?`TC33x_A_step`锛?|
| `timeout` | int | `120` | 鍗曞伐鍏疯秴鏃剁锛坄aurix_pflash` 鍥哄畾 300 s锛?|

### 5.2 鏋勫缓閰嶇疆锛坆uild_project 宸ュ叿锛?
| 瀛楁 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `build.command` | string\|null | `null` | 缂栬瘧鍛戒护锛堝 `"make all"` 鎴?`"build.bat"`锛?|
| `build.work_dir` | string\|null | `null` | 缂栬瘧宸ヤ綔鐩綍 |
| `build.timeout` | int | `300` | 缂栬瘧瓒呮椂绉掓暟 |

### 5.3 椤圭洰閰嶇疆锛堢鍙峰垎鏋愬伐鍏凤級

| 瀛楁 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `project.elf_file` | string\|null | `null` | ELF 鏂囦欢璺緞锛圱ASKING/GCC 鍧囧彲锛?|
| `project.map_file` | string\|null | `null` | TASKING MAP 鏂囦欢璺緞 |
| `project.lst_file` | string\|null | `null` | GCC LST 鏂囦欢璺緞锛坥bjdump 杈撳嚭锛?|
| `project.mdf_file` | string\|null | `null` | TASKING MDF 鏂囦欢璺緞锛堝湴鍧€绌洪棿缈昏瘧锛?|
| `project.hex_file` | string\|null | `null` | HEX 鏂囦欢璺緞 |
| `project.toolchain` | string\|null | `null`=鑷姩 | 宸ュ叿閾撅細`"tasking"` / `"gcc"` / `null` |

**鍏稿瀷閰嶇疆绀轰緥**锛圱ASKING 椤圭洰锛夛細
```json
"project": {
    "elf_file": "D:/workspace/MyProject/TriCore Debug (TASKING)/MyApp.elf",
    "map_file": "D:/workspace/MyProject/TriCore Debug (TASKING)/MyApp.map",
    "mdf_file": "D:/workspace/MyProject/TriCore Debug (TASKING)/MyApp.mdf",
    "toolchain": "tasking"
}
```

**鍏稿瀷閰嶇疆绀轰緥**锛圙CC 椤圭洰锛夛細
```json
"project": {
    "elf_file": "D:/workspace/MyProject/TriCore Debug (GCC)/MyApp.elf",
    "lst_file": "D:/workspace/MyProject/TriCore Debug (GCC)/MyApp.lst",
    "toolchain": "gcc"
}
```

### 5.4 鑷姩鎼滅储璺緞

**浼樺厛绾?*锛堥珮 鈫?浣庯紝鍛戒腑鍗冲仠锛夛細

| # | 鏉ユ簮 | 绀轰緥 / 澶囨敞 |
|---|------|------------|
| 0 | **`mcp_config.json` 鏄惧紡閰嶇疆** | `wiggle_exe` / `aurix_flasher_exe` 鍐欐缁濆璺緞 |
| 1 | **`AURIX_FLASHER_EXE` 鐜鍙橀噺**锛堝畬鏁磋矾寰勶級| `set AURIX_FLASHER_EXE=C:\Infineon\...\AURIXFlasher.exe` |
| 2 | **`AURIX_FLASHER_DIR` 鐜鍙橀噺**锛堢洰褰曪紝鑷姩鎷?`AURIXFlasher.exe`锛墊 `set AURIX_FLASHER_DIR=C:\Infineon\AURIXFlasherSoftwareTool-3.0.16` |
| 3 | **`WIGGLE_EXE` 鐜鍙橀噺** | `set WIGGLE_EXE=D:\wiggle_MCP\wiggle\wiggle.exe` |
| 4 | **`WIGGLE_DIR` 鐜鍙橀噺** | `set WIGGLE_DIR=D:\wiggle_MCP\wiggle` |
| 5 | 鑴氭湰鎵€鍦ㄧ洰褰曪紙`D:\wiggle_MCP\`锛墊 鎵?`wiggle.exe` / `AURIXFlasher.exe` |
| 6 | `./wiggle/`锛堝悓绾?wiggle 瀛愮洰褰曪級| 鎵?`wiggle.exe` / `wiggle` |
| 7 | `./data/`锛堝悓绾?data 瀛愮洰褰?鈥?鍏煎寮€鍙戜粨搴撳竷灞€锛墊 |
| 8 | `./AURIXFlasher/`锛堝悓绾?AURIXFlasher 瀛愮洰褰曪級| 鎵?`AURIXFlasher.exe` |
| 9 | **Infineon 榛樿瀹夎浣嶇疆**锛堜粎 Windows锛夛細<br>鈥?`C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>鈥?`C:\Program Files\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`<br>鈥?`C:\Program Files (x86)\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe` | glob 鍖归厤浠讳綍鐗堟湰鍙?|
| 10 | **PATH 鍏滃簳**锛坄shutil.which`锛墊 scoop / choco / 鎵嬪姩 PATH 瑁呯殑鑳芥壘鍒?|
| 鈥?| **閮芥壘涓嶅埌** 鈫?鍚姩鏃舵姤閿?`Executable not found: ...` | |

**瀹炴祴缁撴灉**锛堜綘鏈哄櫒涓婄殑褰撳墠閰嶇疆锛夛細
- `find_exe("AURIXFlasher.exe")` 鈫?鍛戒腑绗?9 鏉★細`C:\Infineon\AURIXFlasherSoftwareTool-3.0.16\AURIXFlasher.exe`
- `find_exe("wiggle.exe")` 鈫?鍛戒腑绗?6 鏉★細`D:\wiggle_MCP\wiggle\wiggle.exe`

> **CI / 瀹瑰櫒鍖栭儴缃叉帹鑽愮敤 env var**锛氫笉鐢ㄥ姩 `mcp_config.json` 灏辫兘鎸囧悜涓嶅悓鏈哄櫒鐨?exe锛?> ```bash
> # Linux / WSL 绀轰緥
> export AURIX_FLASHER_EXE=/opt/infineon/aurixflasher/AURIXFlasher.exe
> export WIGGLE_EXE=/opt/wiggle/wiggle
> python aurix_mcp_server.py
> ```
>
> **鎵句笉鍒版椂鐨勫鐞?*锛氱洿鎺ュ惎鍔ㄥけ璐?鈫?浼樺厛鐢?env var锛堢 1-4 鏉★級锛沞nv var 涔熶笉鎯宠灏辨敼 `mcp_config.json` 鍐欐璺緞銆?*涓嶈鎶?exe 澶嶅埗鍒?`D:\wiggle_MCP\wiggle\` 鎴?`AURIXFlasher\`**锛堝弬瑙?搂2.5锛欰URIXFlasher 鏄?Infineon 鍟嗕笟浜у搧锛孍ULA 绂佹浜屾鍒嗗彂锛夈€?
---

## 6. 鍚姩 MCP Server

```bash
# 鏂瑰紡 1锛氱敤閰嶇疆鏂囦欢锛堟帹鑽愶級
python D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json

# 鏂瑰紡 2锛氳嚜鍔ㄦ悳绱紙涓嶄紶 --config锛?python D:\wiggle_MCP\aurix_mcp_server.py

# 鏂瑰紡 3锛歋SE 杩滅▼妯″紡锛圚TTP锛岄粯璁ょ鍙?8000锛?python D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json --transport sse
```

> 鍚姩鍚庤繘绋嬪父椹伙紱瀹㈡埛绔柇寮€鍚庤繘绋嬩笉浼氳嚜鍔ㄩ€€鍑猴紝闇€瑕?Ctrl+C 鎵嬪姩缁撴潫銆?
---

## 7. 鍦?AI 瀹㈡埛绔腑娉ㄥ唽

### 7.1 Claude Desktop

鏂囦欢浣嶇疆锛圵indows锛夛細`%APPDATA%\Claude\claude_desktop_config.json`

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": [
        "D:/wiggle_MCP/aurix_mcp_server.py",
        "--config",
        "D:/wiggle_MCP/mcp_config.json"
      ]
    }
  }
}
```

### 7.2 Cursor

鏂囦欢浣嶇疆锛歚~/.cursor/mcp.json`

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": [
        "D:/wiggle_MCP/aurix_mcp_server.py",
        "--config",
        "D:/wiggle_MCP/mcp_config.json"
      ]
    }
  }
}
```

### 7.3 VS Code (Continue / Cline / Roo Code)

VS Code 鍘熺敓涓嶆敮鎸?MCP锛岄渶鍏堣涓€涓?MCP 鎵╁睍锛岀劧鍚庡湪鎵╁睍鐨勯厤缃噷鍐欙細

```json
{
  "mcpServers": {
    "aurix": {
      "command": "python",
      "args": [
        "D:/wiggle_MCP/aurix_mcp_server.py",
        "--config",
        "D:/wiggle_MCP/mcp_config.json"
      ]
    }
  }
}
```

### 7.4 Qoder IDE

Qoder 璁剧疆 鈫?MCP 鈫?娣诲姞锛?
- **Name**: `aurix`
- **Command**: `python`
- **Args**: `D:\wiggle_MCP\aurix_mcp_server.py --config D:\wiggle_MCP\mcp_config.json`

### 7.5 娉ㄥ唽鍚庢€庝箞楠岃瘉

鍦?AI 瀹㈡埛绔噷鐩存帴闂細

```
浣犳湁鍝簺 aurix 鐩稿叧鐨勫伐鍏凤紵
```

搴旇兘鐪嬪埌 19 涓伐鍏峰垪琛紙13 涓?`wiggle_*` + 6 涓?build 鍒嗘瀽宸ュ叿锛夈€傜劧鍚庯細

```
甯垜鏌ヤ竴涓嬪綋鍓嶈繛鎺ョ殑 AURIX 璁惧鍨嬪彿鍜?DFlash 鐘舵€?```

AI 搴旇嚜鍔ㄨ皟鐢?`wiggle_list()` + `wiggle_status()` + `wiggle_info()` 缁欏嚭绛旀銆?
---

## 8. 19 涓?MCP 宸ュ叿閫熸煡

鎸夊姛鑳藉垎缁勩€傝缁嗗弬鏁拌 [搂9](#9-宸ュ叿璇︾粏鍙傛暟)銆?
### 8.1 璁惧淇℃伅锛? 涓級

| 宸ュ叿 | 鐢ㄩ€?|
|------|------|
| `wiggle_list` | 鍒楀凡杩?TAS 鐩爣 |
| `wiggle_status` | Flash 鐘舵€佸瘎瀛樺櫒锛圫VD 瀛楁瑙ｇ爜锛?|
| `wiggle_info` | 璁惧淇℃伅 / 鍐呭瓨甯冨眬 / SVD 鎽樿 |

### 8.2 DFlash 鎿嶄綔锛? 涓級

| 宸ュ叿 | 鐢ㄩ€?|
|------|------|
| `wiggle_erase` | 鎿?DFlash 鎵囧尯锛堝叏閮?or 鎸囧畾鍦板潃锛?|
| `wiggle_write` | 浠?HEX/BIN 鏂囦欢鍐?DFlash |
| `wiggle_read` | 璇?DFlash 鍐呭 |

### 8.3 PFlash 鐑у啓锛? 涓級

| 宸ュ叿 | 鐢ㄩ€?|
|------|------|
| `aurix_pflash` | 璋?AURIXFlasher 鐑?PFlash锛堜粎 hex_file 涓€涓弬鏁帮級 |

### 8.4 璋冭瘯锛? 涓級

| 宸ュ叿 | 鐢ㄩ€?|
|------|------|
| `wiggle_reg` | 鎸?SVD 鍚嶅瓧/鍦板潃璇诲啓瀵勫瓨鍣紙鏀寔鏋氫妇澶栬瀵勫瓨鍣級 |
| `wiggle_dump` | 鍐呭瓨 Hex Dump锛圥Flash/DFlash/SRAM锛?|
| `wiggle_poke` | 鍐欏€煎埌鍐呭瓨锛堝甫 readback 鏍￠獙锛?|
| `wiggle_search` | 鎼滅储鍐呭瓨瀛楄妭妯″紡 |
| `wiggle_compare` | 鏈湴鏂囦欢 vs Flash 姣斿 |

### 8.5 鎺у埗锛? 涓級

| 宸ュ叿 | 鐢ㄩ€?|
|------|------|
| `wiggle_reset` | 澶嶄綅 MCU锛堝彲閫?halt锛?|

### 8.6 鏋勫缓浜х墿鍒嗘瀽锛? 涓級

| 宸ュ叿 | 鐢ㄩ€?|
|------|------|
| `build_project` | 鎵ц缂栬瘧鍛戒护锛岃繑鍥?errors/warnings/output_files锛屾垚鍔熷悗鑷姩鍒锋柊 parser 缂撳瓨 |
| `reload_parsers` | 寮哄埗閲嶆柊鍔犺浇 MAP/LST/ELF/MDF 瑙ｆ瀽鍣紙澶栭儴缂栬瘧鍚庝娇鐢級 |
| `parse_symbols` | 瑙ｆ瀽 MAP/LST 绗﹀彿琛紙鑷姩妫€娴?TASKING/GCC锛?|
| `parse_elf` | 瑙ｆ瀽 ELF 鏂囦欢绗﹀彿銆佹銆丏WARF 璋冭瘯淇℃伅 |
| `lookup_symbol` | 鎸夊悕绉拌法婧愭煡鎵剧鍙凤紙MAP + ELF锛?|
| `lookup_address` | 鎸夊湴鍧€鍙嶆煡鏈€杩戠鍙?+ MDF 鍦板潃缈昏瘧 |

---

## 9. 宸ュ叿璇︾粏鍙傛暟

鍙傛暟鍛藉悕閬靛惊 Python 椋庢牸锛坰nake_case锛夛紝鎵€鏈夊崄鍏繘鍒舵帴鍙?`0x` 鍓嶇紑鎴栬８ hex锛岀┖涓?`""` 琛ㄧず鐢ㄩ粯璁ゃ€?
### wiggle_list

```
wiggle_list(server='', target='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `server` | string | `""` | TAS server IP锛堣鐩栭厤缃級 |
| `target` | string | `""` | 鐩爣 ID |

### wiggle_status

```
wiggle_status(server='', target='', device='') 鈫?JSON
```

璇?`DMU.HF.STATUS` 绛?Flash 鐘舵€佸瘎瀛樺櫒锛屾寜 SVD 鑷姩瑙ｅ瓧娈碉紙D0BUSY/D1BUSY/P0BUSY/...锛夈€?
### wiggle_info

```
wiggle_info(server='', target='', device='') 鈫?JSON
```

杩斿洖 `{device, family, dflash:{base,sector_size,sectors,size}, memory_regions:[]}`銆?
### wiggle_read

```
wiggle_read(address, length, output_file='', server='', target='', device='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `address` | string | **蹇呭～** | 璧峰鍦板潃锛坔ex锛?|
| `length` | string | **蹇呭～** | 闀垮害锛坔ex锛?|
| `output_file` | string | `""` | `.bin`=鍘熷浜岃繘鍒?/ `.hex`=Intel HEX锛涚┖=stdout hex dump |

```
绀轰緥锛?  wiggle_read('0xAF000000', '0x100')
  wiggle_read('0xAF000000', '0x2000', output_file='dump.bin')
```

### wiggle_erase

```
wiggle_erase(erase_all=True, address='', sectors=0, verify=False,
             server='', target='', device='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `erase_all` | bool | `true` | 鏁寸墖鎿︼紙榛樿锛?|
| `address` | string | `""` | 璧峰鍦板潃锛坄erase_all=false` 鏃跺繀濉紝hex锛?|
| `sectors` | int | `0` | 鎵囧尯鏁帮紙0=1 涓級 |
| `verify` | bool | `false` | 鎿﹀悗鍥炶鏍￠獙锛堟帹鑽?`true`锛?|

```
绀轰緥锛?  wiggle_erase(erase_all=True, verify=True)                                    # 鏁寸墖鎿?  wiggle_erase(erase_all=False, address='0xAF004000', sectors=2, verify=True)  # 灞€閮ㄦ摝
```

### wiggle_write

```
wiggle_write(file_path, verify=True, server='', target='', device='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `file_path` | string | **蹇呭～** | `.hex` 鎴?`.bin` 璺緞 |
| `verify` | bool | `true` | 鍐欏悗鍥炶鏍￠獙锛堟帹鑽愶級 |

> 浠呮敮鎸?DFlash銆侾Flash 璇风敤 `aurix_pflash`銆?
### aurix_pflash

```
aurix_pflash(hex_file) 鈫?str
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `hex_file` | string | **蹇呭～** | Intel HEX 鏂囦欢璺緞锛堟殏涓嶆敮鎸?ELF锛?|

**琛屼负**锛氳秴鏃?300 s锛涜蛋 AURIXFlasher 榛樿娴佺▼锛堟摝宸茬敤鎵囧尯 + 缂栫▼ + 澶嶄綅鍚姩锛夈€傝繑鍥?AURIXFlasher 鐨?stdout + `Pass`/`Fail` 鏍囪銆?
濡傞渶 `erase all` / `verify on` / `-ucb on` / `-connect 0` / `-start off` 绛夌簿缁嗘帶鍒讹紝**璇风洿鎺ョ敤 AURIXFlasher.exe CLI**锛岃瑙?`AURIXFlasher-Command-Reference.xlsx`銆?
### wiggle_reg

```
wiggle_reg(name_or_addr, value='', list_peripheral='',
           server='', target='', device='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `name_or_addr` | string | 鐪嬩笅 | 瀵勫瓨鍣ㄥ悕锛堝 `DMU.HF.STATUS`锛夋垨鍦板潃锛堝 `0xF8040010`锛夛紱list 妯″紡鍙┖ |
| `value` | string | `""` | 鍐欏叆鍊硷紙绌?璇伙級 |
| `list_peripheral` | string | `""` | 璁剧疆鏃惰繘鍏?list 妯″紡锛屽垪鍑哄璁惧叏閮ㄥ瘎瀛樺櫒 |

```
绀轰緥锛?  wiggle_reg('DMU.HF.STATUS')                    # 璇?  wiggle_reg('0xF8040010')                       # 鎸夊湴鍧€璇?  wiggle_reg('DMU.HF.OPERATION', value='0x1')    # 鍐?  wiggle_reg('', list_peripheral='DMU')          # 鍒?74 涓?DMU 瀵勫瓨鍣?```

### wiggle_dump

```
wiggle_dump(address, length, output_file='', server='', target='', device='') 鈫?JSON
```

浠绘剰鍐呭瓨 hex dump锛坄0xAF...`=DFlash / `0xA0...`=PFlash / `0x70...`=SRAM锛夈€?
### wiggle_poke

```
wiggle_poke(address, value, width=32, server='', target='', device='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `width` | int | `32` | 浣嶅 8 / 16 / 32 / 64 |

甯?readback 鏍￠獙锛岃繑鍥?`match: true/false`銆?
### wiggle_search

```
wiggle_search(address, length, pattern, server='', target='', device='') 鈫?JSON
```

`pattern` 鏄?hex 瀛楃涓诧紙濡?`DEADBEEF`锛屾棤 `0x`锛夛紝杩斿洖鍖归厤鍦板潃鍒楄〃銆?
### wiggle_compare

```
wiggle_compare(file_path, address, length='', server='', target='', device='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `file_path` | string | **蹇呭～** | `.hex` 鎴?`.bin` 璺緞 |
| `address` | string | **蹇呭～** | 璧峰鍦板潃锛坔ex锛?|
| `length` | string | `""` | 姣旇緝闀垮害锛堢┖=鏂囦欢鍏ㄩ暱锛?|

### wiggle_reset

```
wiggle_reset(halt=False, server='', target='') 鈫?JSON
```

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `halt` | bool | `false` | `true` = 澶嶄綅鍚庡仠 CPU锛堢敤浜?debug锛?|

### build_project

```
build_project(clean=False) 鈫?JSON
```

鎵ц鐢ㄦ埛鏋勫缓鍛戒护锛岃繑鍥炵紪璇戠粨鏋溿€?
| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `clean` | bool | `false` | 鎵ц clean 鍚庡啀 build |

**閰嶇疆瑕佹眰**锛氬湪 `mcp_config.json` 涓缃?`build.command` 鍜?`build.work_dir`銆?
### reload_parsers

```
reload_parsers() 鈫?JSON
```

寮哄埗閲嶆柊鍔犺浇鎵€鏈夌紪璇戜骇鐗╄В鏋愬櫒锛圡AP/LST/ELF/MDF锛夈€傚湪浠ヤ笅鍦烘櫙浣跨敤锛?- 閫氳繃 IDE 鎴栧叾浠栧伐鍏峰湪澶栭儴瀹屾垚浜嗙紪璇?- `build_project` 浠ュ鐨勬柟寮忎慨鏀逛簡缂栬瘧浜х墿鏂囦欢
- 鍒囨崲浜嗛」鐩厤缃腑鐨?`elf_file` / `map_file` 璺緞

| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| 锛堟棤鍙傛暟锛?| - | - | 鑷姩閲嶆柊鍔犺浇鎵€鏈夊凡閰嶇疆鐨勮В鏋愬櫒 |

**杩斿洖**锛歚loaded` 鏁扮粍鍒楀嚭鏈鍔犺浇鐨勮В鏋愬櫒鍜岀鍙锋暟閲忋€?
### parse_symbols

```
parse_symbols(summary=True, search='', max_results=100) 鈫?JSON
```

瑙ｆ瀽 MAP/LST 鏂囦欢鐨勭鍙疯〃鍜屽唴瀛樺竷灞€銆?
| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `summary` | bool | `true` | 杩斿洖鍐呭瓨鐢ㄩ噺鎽樿锛圱ASKING MAP 涓撶敤锛?|
| `search` | string | `""` | 鎸夊悕绉拌繃婊ょ鍙凤紙涓嶅尯鍒嗗ぇ灏忓啓锛?|
| `max_results` | int | `100` | 鏈€澶氳繑鍥炵鍙锋暟 |

**閰嶇疆瑕佹眰**锛氬湪 `mcp_config.json` 涓缃?`project.map_file`锛圱ASKING锛夋垨 `project.lst_file`锛圙CC锛夈€?
### parse_elf

```
parse_elf(functions=False, variables=False, sections=True, search='', max_results=100) 鈫?JSON
```

瑙ｆ瀽 ELF 鏂囦欢鐨勭鍙枫€佹銆丏WARF 璋冭瘯淇℃伅銆?
| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `functions` | bool | `false` | 鍙繑鍥炲嚱鏁扮鍙?|
| `variables` | bool | `false` | 鍙繑鍥炲彉閲忕鍙?|
| `sections` | bool | `true` | 杩斿洖娈典俊鎭?|
| `search` | string | `""` | 鎸夊悕绉拌繃婊ょ鍙?|
| `max_results` | int | `100` | 鏈€澶氳繑鍥炵鍙锋暟 |

**閰嶇疆瑕佹眰**锛氬湪 `mcp_config.json` 涓缃?`project.elf_file`銆?
### lookup_symbol

```
lookup_symbol(name, source='all') 鈫?JSON
```

鎸夊悕绉拌法婧愭煡鎵剧鍙凤紙MAP + ELF锛夈€?
| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `name` | string | 蹇呭～ | 绗﹀彿鍚嶇О |
| `source` | string | `"all"` | 鏌ユ壘鑼冨洿锛歚"map"` / `"elf"` / `"all"` |

### lookup_address

```
lookup_address(address, source='all') 鈫?JSON
```

鎸夊湴鍧€鍙嶆煡鏈€杩戠鍙?+ MDF 鍦板潃缈昏瘧銆?
| 鍙傛暟 | 绫诲瀷 | 榛樿 | 璇存槑 |
|------|------|------|------|
| `address` | string | 蹇呭～ | 鍗佸叚杩涘埗鍦板潃锛堝 `"0x800019de"`锛?|
| `source` | string | `"all"` | 鏌ユ壘鑼冨洿锛歚"map"` / `"elf"` / `"all"` |

**閰嶇疆瑕佹眰**锛堝彲閫夛級锛氳缃?`project.mdf_file` 鍙惎鐢?TASKING 鍦板潃绌洪棿缈昏瘧銆?
---

## 10. 杩斿洖 JSON 鏍煎紡

**鎵€鏈夊伐鍏疯繑鍥炲瓧绗︿覆褰㈠紡鐨?JSON**銆侫gent 搴旇В鏋愬悗鐢?`status` 瀛楁鍒ゆ柇鎴愬姛銆?
**鎴愬姛**锛?```json
{
  "status": "ok",
  "command": "...",
  "...": "鍏朵粬瀛楁渚濆伐鍏疯€屽畾"
}
```

**閿欒**锛?```json
{
  "status": "error",
  "code": -1,
  "message": "閿欒鎻忚堪"
}
```

### 甯歌閿欒鐮侊紙MCP 灞傦級

| code | 鍚箟 | 澶勭悊寤鸿 |
|------|------|----------|
| `-1` | 鍙墽琛屾枃浠舵湭鎵惧埌 | 妫€鏌?`mcp_config.json` 鐨?`wiggle_exe` / `aurix_flasher_exe` 璺緞 |
| `-2` | 鍛戒护瓒呮椂 | `timeout` 璋冨ぇ锛屾垨妫€鏌?TAS Server 鏄惁鍗′綇 |
| `-3` | 杩愯鏃跺紓甯?| 鏌?stderr 瀹屾暣鍫嗘爤 |
| `>0` | wiggle / AURIXFlasher 杩涚▼閫€鍑虹爜 | 鏌?[搂13 閫€鍑虹爜](#13-閫€鍑虹爜--閿欒鐮? |

### SVD 瀛楁瑙ｇ爜鏍蜂緥

`wiggle_reg("DMU.HF.STATUS")` 杩斿洖锛?
```json
{
  "status": "ok",
  "register": "HF.STATUS",
  "address": 4161011728,
  "value": 34078720,
  "fields": {
    "D0BUSY": {"value": 0, "lsb": 0, "msb": 0, "desc": "DF0 ready, not busy..."},
    "D1BUSY": {"value": 0, "lsb": 1, "msb": 1, "desc": "DF1 ready, not busy..."},
    "P0BUSY": {"value": 0, "lsb": 8, "msb": 8, "desc": "PF0 ready, not busy..."}
  }
}
```

瀛楁瀹氫箟鏉ヨ嚜 `wiggle/RegisterDefs/<chip>-full.json`锛堣嚜鍔ㄤ粠 `data/SVD/` 娲剧敓锛夈€?
---

## 11. 鍏稿瀷宸ヤ綔娴?
### 11.1 涓婄數妫€鏌?
```
AI 鈫?wiggle_list()                       # 纭璁惧宸茶繛
AI 鈫?wiggle_info()                       # 璇昏姱鐗囧瀷鍙枫€佸唴瀛樺竷灞€
AI 鈫?wiggle_status()                     # Flash 鏄惁蹇?/ 閿?```

### 11.2 DFlash 澶囦唤 鈫?鏁寸墖鎿?鈫?鏍￠獙

```
AI 鈫?wiggle_read('0xAF000000', '0x20000', output_file='backup.bin')   # 1. 澶囦唤
AI 鈫?wiggle_erase(erase_all=True, verify=True)                        # 2. 鏁寸墖鎿?+ 鏍￠獙
AI 鈫?wiggle_read('0xAF000000', '0x10')                                # 3. 鎶芥煡锛岀‘璁ゅ叏 0
```

### 11.3 DFlash 鐑ф暟鎹?+ 鍥炶鏍￠獙

```
AI 鈫?wiggle_erase(erase_all=False, address='0xAF004000', sectors=2, verify=True)
AI 鈫?wiggle_write(file_path='patch.hex', verify=True)
AI 鈫?wiggle_compare(file_path='patch.hex', address='0xAF004000', length='0x1000')
```

### 11.4 PFlash 鐑у浐浠讹紙鐢?AURIXFlasher锛?
```
AI 鈫?aurix_pflash(hex_file='D:/firmware.hex')
# 棰勬湡杩斿洖 "AURIXFlasher Exit Status: Pass"
```

濡傞渶 `erase all` / `verify on` / UCB 缂栫▼ / hot-attach / 鐑у悗涓嶅惎鍔ㄧ瓑绮剧粏鎺у埗锛?*鐩存帴鐢?AURIXFlasher CLI**锛堣瑙?`AURIXFlasher-Command-Reference.xlsx`锛夈€?
### 11.5 璋冭瘯锛氳 / 鍐?/ 鏋氫妇瀵勫瓨鍣?
```
AI 鈫?wiggle_reg('', list_peripheral='DMU')          # 1. 鍒楀嚭 DMU 鍏ㄩ儴 74 涓瘎瀛樺櫒
AI 鈫?wiggle_reg('DMU.HF.STATUS')                     # 2. 璇?Flash 鐘舵€侊紙鍚瓧娈佃В鐮侊級
AI 鈫?wiggle_reg('0xF8040010')                        # 3. 鎸夌粷瀵瑰湴鍧€璇?AI 鈫?wiggle_reg('DMU.HF.OPERATION', value='0x1')     # 4. 鍐欒Е鍙戝瘎瀛樺櫒
AI 鈫?wiggle_status()                                 # 5. 鍐嶈涓€娆★紝鐪嬪彉鍖?```

### 11.6 鍐呭瓨妫€鏌?/ 妯″紡鎼滅储

```
AI 鈫?wiggle_dump('0xAF000000', '0x100')                                    # hex dump
AI 鈫?wiggle_search('0xAF000000', '0x10000', 'DEADBEEF')                    # 鎵?0xDEADBEEF
AI 鈫?wiggle_poke('0x70000000', '0xDEADBEEF')                               # 鍐?SRAM (CPU0 DSPR)
AI 鈫?wiggle_read('0x70000000', '0x10', output_file='sram.bin')             # 璇诲洖
```

### 11.7 瀹屾暣鐑у啓 + 澶嶄綅

```
AI 鈫?wiggle_erase(erase_all=True, verify=True)        # 1. 鎿?DFlash
AI 鈫?wiggle_write(file_path='data.hex', verify=True)  # 2. 鍐?DFlash
AI 鈫?wiggle_compare(file_path='data.hex', address='0xAF000000')  # 3. 瀵规瘮
AI 鈫?wiggle_reset()                                   # 4. 澶嶄綅璺戣捣鏉?```

---

## 12. UCB 鍦板潃鍥?(TC3xx)

UCB = User Configuration Block锛屽瓨 BMHD銆佸畨鍏ㄩ厤缃€丗lash 淇濇姢绛夈€?*鍐欓敊浼?brick 鑺墖** 鈥斺€?鍐欏叆鎿嶄綔閮借鏁?`yes` 纭銆?
| 鍖哄煙 | 鍦板潃鑼冨洿 | 澶у皬 | 鎻忚堪 | 鍙惁鎿﹂櫎 |
|------|----------|------|------|----------|
| BMHD0-3 | `AF400000-AF4007FF` | 2 KB | Boot Mode Headers (0-3) | 鉁?|
| **Security** | `AF400800-AF400FFF` | 2 KB | OTP/DFLASH/DBG/HSM | 鉂?**LOCKED** |
| BMHD COPY | `AF401000-AF4017FF` | 2 KB | BMHD 澶囦唤 | 鉁?|
| **Security COPY** | `AF401800-AF401FFF` | 2 KB | Security 澶囦唤 | 鉂?**LOCKED** |
| PFLASH | `AF402000-AF4027FF` | 2 KB | PFlash 淇濇姢 | 鉁?|
| SWAP | `AF402800-AF4037FF` | 4 KB | Flash swap | 鉁?|
| LBIST | `AF403800-AF4047FF` | 4 KB | Logic BIST | 鉁?|
| SSW | `AF404800-AF4057FF` | 4 KB | Startup Software | 鉁?|

MCP 鏆傛湭灏佽 UCB 宸ュ叿锛岄渶瑕佹椂璇风洿鎺ョ敤 wiggle CLI锛?
```bash
# 璇绘暣鐗?UCB
D:\wiggle_MCP\wiggle\wiggle.exe ucb read

# 璇?BMHD 鍖?D:\wiggle_MCP\wiggle\wiggle.exe ucb read --addr AF400000 --length 800

# 澶囦唤鍒?Intel HEX
D:\wiggle_MCP\wiggle\wiggle.exe ucb read -a AF400000 -l 6000 -o ucb_backup.hex
```

---

## 13. 閫€鍑虹爜 / 閿欒鐮?
### 13.1 wiggle 閫€鍑虹爜

| Code | 鍚箟 | Code | 鍚箟 |
|------|------|------|------|
| 0 | Success | 12 | Erase failed |
| 1 | Invalid args / addr OOR | 13 | EndInit restore failed |
| 2 | Server connection failed | 14 | Erase timeout |
| 3 | Get targets failed / none | 15 | Error flags (PVER/EVER/PROER/SQER/OPER) |
| 5 | Session start failed | 16 | Read mode failed |
| 6 | Device connect failed | 17 | Verification failed |
| 7 | Unsupported device (e.g. TC4x) | 18 | Read failed |
| 8 | Backup failed | 19 | Write failed |
| 9 | Safety Watchdog failed | 20 | File I/O failed |
| 10 | EndInit failed | 21 | Restore failed |
| 11 | Flash status failed | 22 | UCB locked |
| | | 23 | Unsupported device for UCB |

### 13.2 AURIXFlasher 杈撳嚭鏍囪瘑

AURIXFlasher 涓嶈繑鍥炴暟瀛楅€€鍑虹爜锛岃€屾槸 stdout 鏈熬涓€琛岋細

```
AURIXFlasher Exit Status: Pass    鈫?鎴愬姛
AURIXFlasher Exit Status: Fail    鈫?澶辫触锛岀湅涓婇潰 :: 琛?```

---

## 14. 娴嬭瘯

```bash
# 鍏ㄩ噺 MCP 闆嗘垚娴嬭瘯锛?2 渚嬶紝~7 s锛?# 闇€瑕侊細TAS Server 杩愯涓?+ AURIX 宸茶繛
cd D:\wiggle_MCP
python test_mcp_all.py
```

鎶ュ憡鑷姩鍐欏埌 `D:\wiggle_MCP\wiggle\mcp_test_report.txt`锛屾瘡琛屼竴鏉★細

```
1    PASS       462ms  wiggle_list - List targets
2    PASS       321ms  wiggle_status - Flash status register
...
12   PASS      1859ms  aurix_pflash - PFlash program + verify
```

**褰撳墠鍩虹嚎**锛?026-06-12 13:56:31锛夛細**12 PASS / 0 FAIL**銆?
---

## 15. 甯歌闂

### Q: AI 瀹㈡埛绔繛涓嶄笂 MCP server

A: 涓夋鎺掓煡锛?1. `python aurix_mcp_server.py --config ...` 鍦ㄧ粓绔兘璺戣捣鏉ュ悧锛熻窇涓嶈捣鏉ヨ鏄庤矾寰勬垨 Python 渚濊禆鏈夐棶棰?2. 瀹㈡埛绔殑 `args` 璺緞鍜?`cwd` 鏄惁姝ｇ‘锛焀indows 璺緞鐢?`D:/...` 姝ｆ枩鏉犳垨 `"D:\\\\..."` 鍙屽弽鏂滄潬
3. 瀹㈡埛绔槸鍚﹂渶瑕侀噸鍚紵Claude Desktop / Cursor 鏀逛簡 MCP 閰嶇疆蹇呴』閲嶅惎

### Q: `wiggle_exe not found` / `aurix_flasher_exe not found`

A: 涓や釜鍘熷洜锛?- `mcp_config.json` 鐨勮矾寰勫啓閿欙紙鐢?`\\` 杞箟鎴?`/` 涓嶇敤 `\`锛?- 鐢ㄤ簡 `null` 浣嗙洰褰曞竷灞€涓嶆槸 `D:\wiggle_MCP\` 榛樿甯冨眬 鈥斺€?鎶婅矾寰勫啓姝?
### Q: 宸ュ叿璋冪敤瓒呮椂

A: `mcp_config.json` 璋冨ぇ `timeout`銆俙aurix_pflash` 鍥哄畾 300 s銆傚鏋?erase/write 鐪熷疄闇€瑕佹洿涔咃紝鐪嬫槸涓嶆槸璁惧 busy銆?
### Q: TC4x 璁惧鎶?"unsupported"

A: **鏈寘涓嶆敮鎸?TC4x**锛圓URIX 2G锛夈€侱Flash base 鍦板潃锛坄0xC...` 鑰屼笉鏄?`0xAF...`锛夊拰鍛戒护搴忓垪閮戒笉鍚屻€?
### Q: 鐑у畬 PFlash 鍚庤澶囦笉鍚姩

A: `aurix_pflash` 璧?AURIXFlasher 榛樿琛屼负锛堟渶鍚庡浣嶅惎鍔級銆傚瑕佺儳瀹屼繚鎸?halt锛岃嚜宸辫窇锛?
```bash
D:\wiggle_MCP\AURIXFlasher\AURIXFlasher.exe -hex fw.hex -start off
```

### Q: UCB 鍐欏叆鎶ラ敊 "UCB locked"

A: Security / Security COPY 鍖猴紙`AF400800-...` 鍜?`AF401800-...`锛夋槸 OTP锛?*浠讳綍宸ュ叿閮芥摝涓嶆帀**銆傝繖鏄?Infineon 璁捐鐨勫畨鍏ㄦ満鍒讹紝涓嶆槸 bug銆?
### Q: 鎬庝箞鏇存柊 SVD 瀵勫瓨鍣ㄥ畾涔夛紵

A: 8 涓?`wiggle/RegisterDefs/<chip>-full.json` 鏄粠 `data/SVD/` 鐢?`svd-to-full-json` skill 鑷姩鐢熸垚鐨勩€?
```bash
# 鎶婃柊 SVD 鏀惧埌 D:\workspace\...\data\SVD\<chip>\<ver>\device.svd
# 鐒跺悗閲嶆柊璺?skill
python ~/.mavis/agents/mavis/skills/svd-to-full-json/scripts/svd-to-full-json.py \
    D:\workspace\...\data\SVD\<chip>\<ver>\device.svd \
    D:\wiggle_MCP\wiggle\RegisterDefs\<chip>-full.json \
    --device <chip> --svd-name <chip>
```

---

## 16. 閰嶅鎵嬪唽 (Excel)

鏈?README 鏄叆鍙ｏ紝瀹屾暣鎸囦护鍙傝€冪湅 3 涓?Excel锛堟瘡涓惈绗﹀彿绾﹀畾 + 瀹屾暣鍛戒护琛?+ 绀轰緥锛夛細

| Excel | 璺緞 | 鍐呭 |
|-------|------|------|
| **MCP 宸ュ叿鎸囦护** | `D:\wiggle_MCP\docs\MCP-Command-Reference.xlsx` | 19 涓?MCP 宸ュ叿鐨勫畬鏁寸鍚?+ 璋冪敤绀轰緥 |
| **AURIXFlasher CLI** | `D:\wiggle_MCP\AURIXFlasher\AURIXFlasher-Command-Reference.xlsx` | 12 涓?flag + 11 鍏稿瀷鍦烘櫙 + 9 涓?script 瀛愬懡浠?|
| **wiggle CLI** | `D:\wiggle_MCP\wiggle\wiggle-Command-Reference.xlsx` | 17 涓瓙鍛戒护 + 8 鍏ㄥ眬閫夐」 + UCB 琛?+ 24 閫€鍑虹爜 |

姣忎釜 Excel 鐨?**Sheet 1 = 绗﹀彿绾﹀畾**锛堣В閲?`< > [ ] ( ) { } |` 鍦ㄦ枃妗ｄ腑鐨勫惈涔夛級锛?*Sheet 2 = 鎸囦护琛?*锛堝甫鍙鍒剁殑鍛戒护绀轰緥锛夈€?
---

## 17. 鏂囨。鐗堟湰

| 鐗堟湰 | 鏃ユ湡 | 涓昏鍙樻洿 |
|------|------|----------|
| **v2.5** | 2026-06-12 | 鏂板 `reload_parsers` 宸ュ叿锛?9 涓伐鍏凤級锛歚build_project` 鎴愬姛鍚庤嚜鍔ㄥ埛鏂?parser 缂撳瓨锛沗reload_parsers` 渚涘閮ㄧ紪璇戝悗鎵嬪姩鍒锋柊锛沗build_project(clean=True)` 澧炲己锛氬尯鍒?make/cmake/batch/ps1 鐨?clean 璇箟 |
| **v2.4** | 2026-06-12 | `find_exe()` 鏂板 4 涓幆澧冨彉閲忓叆鍙ｏ紙`AURIX_FLASHER_EXE` / `AURIX_FLASHER_DIR` / `WIGGLE_EXE` / `WIGGLE_DIR`锛夛紝浼樺厛绾ф渶楂橈紱鏂逛究 CI / 瀹瑰櫒鍖栭儴缃层€傚垹闄や笂涓€鐗?find_exe() 娈嬬暀鐨勬浠ｇ爜銆俁EADME 搂5.4 鏀瑰啓鎴?11 绾т紭鍏堢骇琛?+ Linux/WSL env var 绀轰緥 |
| **v2.3** | 2026-06-12 | `find_exe()` 澧炲己锛氳嚜鍔ㄦ悳 `C:\Infineon\AURIXFlasherSoftwareTool-*\AURIXFlasher.exe`锛坓lob 鍖归厤浠讳綍鐗堟湰锛夊拰 PATH 鍏滃簳锛坰coop/choco锛夈€傚疄娴嬶細褰撳墠鏈哄櫒 AURIXFlasher 鑷姩鍛戒腑 Infineon 瀹夎璺緞銆俁EADME 搂5.4 / 搂4 / 搂1 鍔?Python 3.10+ 纭€ц姹傛彁绀?+ 鎵句笉鍒?exe 鏃剁殑鍏滃簳閰嶇疆鎸囧紩 |
| **v2.2** | 2026-06-12 | 鏂板 5 涓瀯寤轰骇鐗╁垎鏋愬伐鍏凤紙build_project / parse_symbols / parse_elf / lookup_symbol / lookup_address锛夛紝鏀寔 TASKING MAP + GCC LST + ELF + MDF 瑙ｆ瀽锛涙柊澧?requirements.txt / start_mcp.bat / start_mcp.sh锛涢厤缃枃浠舵柊澧?build/project 娈?|
| **v2.1** | 2026-06-12 | 鏂板 搂2.5 宸ュ叿鏉ユ簮璇存槑锛坵iggle = 鑷爺 Apache 2.0锛汚URIXFlasher = Infineon 闂簮鍟嗕笟浜у搧锛夛紝鏄庣‘涓よ€呯殑浠ｇ爜浣嶇疆 / 鏋勫缓鏂瑰紡 / 鏇存柊娓犻亾 / 鍗忚宸紓 |
| v2.0 | 2026-06-12 | 澶у箙閲嶅啓锛氬姞 Quick Start / 鏋舵瀯鍥?/ 瀹屾暣宸ヤ綔娴?/ UCB 琛?/ 閫€鍑虹爜 / FAQ / 閰嶅 Excel 寮曠敤 / 鏂囨。鐗堟湰琛紱淇鑷姩鎼滅储璺緞锛?鈫? 鏉★級锛涗慨姝?`aurix_pflash` 璇︾粏鍙傛暟锛? 涓?鈫?瀹為檯 1 涓級锛涗慨姝?Cursor 閰嶇疆閿悕 |
| v1.0 | 2026-05 | 鍒濈増 |