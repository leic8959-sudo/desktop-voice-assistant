# T-Circle时钟修正后二十一款语音／无屏终端重建

本批为二十一个既有S3入口补齐当前输入的整板构建证据，覆盖ATK五款、CHD两款、Freenove、全动、立创、BigSmart、Korvo2、K10、AtomS3R底座、VoCat、三款无屏板及两款OLED面包板。全部使用各自现有隔离目录，不再修改固件main、注册、manifest、配置、分区或人工决策。

固定上游为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围 **154目录／188变体**；138／171为旧README统计。仍有152个官方源码入口、153个本地profile（含历史入口），36项无入口、4项已知必需功能缺口保留。本批只更新当前构建证据，不新增源码入口或首次通过变体。

## 逐板选择与验收边界

| profile | 当前显示／音频或输入路径 |
| --- | --- |
| alientek-atk-dnesp32s3 | 320×240 ST7789 SPI、ES8388、XL9555 |
| alientek-atk-dnesp32s3-box | 320×240 ST7789 I80；运行时选择ES8311或数字双工 |
| alientek-atk-dnesp32s3-box0 | 240×240 ST7789、单ES8311 16k、无MCLK、独立按键极性 |
| alientek-atk-dnesp32s3-box2-wifi | 240×320 ST7789、ES8389、板级功放及ADC按键；仅Wi-Fi入口 |
| alientek-atk-dnesp32s3-box3 | 320×240 ST7789、ES7210＋ES8311、独立功放／背光／按键 |
| chd-esp32s3-box | 320×240 SPI、ES7210＋ES8311、GT911／FT5x06选择 |
| chd-esp32s3-box-lcd35 | 480×320，独立ST7796默认命令／SDK DCS操作，双codec |
| freenove-esp32s3-display-2.8-lcd | 320×240 ILI9341命令／SDK DCS操作、FT5x06、单ES8311 |
| quandong-s3-dev | 320×240专用面板命令／SDK DCS操作、单ES8311 |
| lckfb-lichuang-dev | 320×240 SPI、FT5x06、PCA9557、双codec |
| rymcu-bigsmart | 320×240 SPI、GT911、PCA9557、双codec |
| espressif-esp32s3-korvo2-v3 | 280×240 SPI、ADC按键、双codec；不是RNDIS变体 |
| dfrobot-df-k10 | 240×320 ILI9341命令／SDK DCS操作、ES7243E输入＋数字输出、16k |
| m5stack-atoms3r-echo-base | 128×128 GC9107／ST7735 ID选择、单ES8311、Atomic底座功放 |
| m5stack-atoms3r-echo-pyramid | 128×128独立面板方向／窗口、双codec、外部Si5351时钟 |
| espressif-esp-vocat | 360×360 ST77916 QSPI／CST816S、双codec、板级复位／电源选择 |
| doit-s3-aibox | 无屏热点网页绑定，PDM16k输入／数字24k输出，独立按键和LED |
| m5stack-atom-echos3r | 无屏热点网页绑定，单ES8311、BCLK时钟、PA18、8MB |
| m5stack-atoms3r-cam-m12-echo-base | 无屏热点网页绑定，单ES8311、PI4IOE功放，无应用键、8MB |
| bread-compact-wifi | SSD1306 128×32 OLED、数字音频 |
| bread-compact-wifi-128x64 | SSD1306 128×64 OLED、数字音频 |

尺寸及选择来自当前板级头、CMake和具体实现，实际编译源文件与ELF另行检查，不能代替真实屏控、布局、触摸、颜色或实体按键验收。借用SDK ST7789对象的DCS操作不等于更改实际面板命令或型号。

ATK BOX保留同一固件内的两条音频分支，板级检测成功后才允许初始化；构建不能确定实物选择结果。K10保留ES7243E和厂家标准采样，不套普通ES7210＋ES8311。Echo Base／Pyramid不按屏幕相同合并方向或音频接线；Pyramid外部时钟与底座发现路径单独核对。BOX2 Wi-Fi与Korvo2 Wi-Fi的通过不计入4G或RNDIS入口。

三款无屏板选择headless UI／portal，未创建虚构LVGL屏幕；手机连接设备热点后读取六位码，绑定成功后再初始化音频。接口地址检查、短期会话、查看窗口和提醒呈现沿原实现。本批没有真实手机、AP隔离或网页绑定验收；已链接相关函数不能当作人已看到提示或音频已正常工作。CAM/M12没有应用键，重启打开有界查看窗口的差异保留。

二十一项均保留自定义“你好，小燕子”、AFE／软件AEC、媒体、网络绑定、提醒和双OTA。没有用纯按键语音、关闭AEC或单应用分区换取通过；摄像头、传感器、电池、原厂全部手势等外围仍按既有记录，不据语音终端构建计为完成。

## 独立构建和产物核对

复用每板原stage，同步当前main、根构建元数据、芯片／板配置及分区，再独立重新配置构建。使用已安装ESP-IDF5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake3.30.2、Ninja1.12.1、ROM20260528及各stage原有组件override／offline_guard；不下载、安装或升级SDK／依赖。

最多两个独立stage同时构建，每项 `IDF_PY_BUILD_JOBS=2`，不改代码生成优化或复制其他板对象／镜像。准确命令、工作目录、开始结束时间、同步文件、工具版本、日志摘要和退出码在各板 `build.json`。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/clockfix-voice-headless-rebuild-20261113/build_batch.py
```

每项实际调用 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。构建完成后分次调用 `collect_batch.py` 的 `collect` 核对产物，最后汇总；每板执行八条真实命令：`readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、实际分区二进制解析、SDK HEAD／status只读查询。命令、输出、退出码及摘要均在 `commands.json`。

```powershell
# python指上述已安装的IDF Python。
python -B evidence/clockfix-voice-headless-rebuild-20261113/collect_batch.py
python -B evidence/clockfix-voice-headless-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/clockfix-voice-headless-rebuild-20261113/finalize.py
git diff --check
```

`symbol-expectations.json` 保存逐板codec、面板、I80、按键／功放和无屏接口的正反期望，并对照当前源码及此前取证核对。实际宏、所选main文件、模型与ELF一起检查；包括区分headless／display、OLED／TFT、标准／PDM、单／双codec。符号存在只证明选中并链接，不证明真实交互、声音或硬件时钟通过。

## 最终结果

二十一项整板构建和每项八条真实产物核对命令均退出0；每项保留330个当前输入摘要，共归档880个产物。

| profile | app字节 | app分区剩余 | model分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| alientek-atk-dnesp32s3 | 2,241,664 | 904,064 | 1,990,485 | 42 |
| alientek-atk-dnesp32s3-box | 2,227,648 | 918,080 | 1,990,485 | 43 |
| alientek-atk-dnesp32s3-box0 | 2,247,776 | 897,952 | 1,990,485 | 40 |
| alientek-atk-dnesp32s3-box2-wifi | 2,236,352 | 909,376 | 1,990,485 | 43 |
| alientek-atk-dnesp32s3-box3 | 2,252,512 | 893,216 | 1,990,485 | 42 |
| chd-esp32s3-box | 2,266,816 | 878,912 | 1,990,485 | 42 |
| chd-esp32s3-box-lcd35 | 2,267,696 | 878,032 | 1,990,485 | 42 |
| freenove-esp32s3-display-2.8-lcd | 2,258,560 | 887,168 | 1,990,485 | 41 |
| quandong-s3-dev | 2,249,168 | 896,560 | 1,990,485 | 40 |
| lckfb-lichuang-dev | 2,265,648 | 880,080 | 1,990,485 | 42 |
| rymcu-bigsmart | 2,260,528 | 885,200 | 1,990,485 | 43 |
| espressif-esp32s3-korvo2-v3 | 2,262,176 | 883,552 | 1,990,485 | 42 |
| dfrobot-df-k10 | 2,227,024 | 918,704 | 1,990,485 | 42 |
| m5stack-atoms3r-echo-base | 2,246,496 | 506,016 | 155,477 | 43 |
| m5stack-atoms3r-echo-pyramid | 2,256,976 | 495,536 | 155,477 | 44 |
| espressif-esp-vocat | 2,558,080 | 587,648 | 1,990,485 | 43 |
| doit-s3-aibox | 1,583,216 | 1,562,512 | 1,990,485 | 41 |
| m5stack-atom-echos3r | 1,618,144 | 1,134,368 | 155,477 | 41 |
| m5stack-atoms3r-cam-m12-echo-base | 1,619,808 | 1,132,704 | 155,477 | 42 |
| bread-compact-wifi | 2,158,816 | 986,912 | 1,990,485 | 41 |
| bread-compact-wifi-128x64 | 2,158,624 | 987,104 | 1,990,485 | 41 |

累计曾缓存构建通过仍为115个官方变体。本批为二十一项补齐当前输入构建，之前56项仍当前，矩阵为 **77项当前通过／38项历史通过但输入已过期／73项未执行构建**。不是新增源码入口、首次通过或实机完成。


每份模型为2,203,819字节，MN5及自定义唤醒源码保留，未选固定词或手动唤醒替代实现。核对真实镜像项目身份、Xtensa／ESP32-S3、Flash设置、checksum／validation hash和分区二进制；按各板CSV比较分区名称、偏移、长度与标志，app／model均须实际容纳。

两款有屏AtomS3R底座和两款无屏Atom入口各保留独立8MB布局：双app各0x2A0000、model0x240000。其余沿既有16MB配置，本批全部为Octal PSRAM。NVS保留原0x9000／0x4000，不因同为S3或相同屏幕尺寸互用镜像；物理容量仍待确认。

每板归档330个当前输入摘要、ELF、镜像、map、模型、分区、配置、编译命令和每个实际选中的main对象。最终审计核对旧main、注册、manifest、配置、工具及旧构建账本未被改动；研究只追加本批后续，之前56项当前通过须保持，旧功能缺口不删除。

SDK四个既有强制UND继续记录：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。原警告、dirty状态和本地override边界保留，不称零UND、干净发行、启动或声学验收。历史GCC内部错误仍未确定根因，其他板型成功不改变该限制。

188项实机仍全部`not_run`，唯一现有微雪1.8的具体修订仍待确认。本批未运行独立测试套件／类型检查、启动服务、安装依赖、Git写操作、刷机、OTA、上传或发布。物理选型、触摸／按键、实际时钟、PA、唤醒／AEC／自然插话、绑定／提醒和同板OTA回滚仍需对应实机验收，全量目标继续保留。

## 证据入口

- [最终审计](../evidence/clockfix-voice-headless-rebuild-20261113/final-audit.json)
- [逐板构建结果](../evidence/clockfix-voice-headless-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/clockfix-voice-headless-rebuild-20261113/collection-summary.json)
- [板型及独立stage](../evidence/clockfix-voice-headless-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/clockfix-voice-headless-rebuild-20261113/symbol-expectations.json)
- [T-Circle时钟修正](remaining_s3_rebuild_20261113.md)
- [当前依赖分组](clockfix_display_rebuild_20261113.md)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
