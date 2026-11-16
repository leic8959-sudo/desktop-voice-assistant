# T-Circle 时钟修正后十五款 S3／C5 独立重建

上批修正两款 T-Circle 的 MAX98357A 物理播放率24kHz→16kHz，并改用现有stereo16复制mono路径。共享输入改变后，其他旧记录被如实标为过期。本批为七款微雪AMOLED修订、三款BOX、CoreS3和四款C5补齐当前输入的整板构建证据；不再修改固件源码、注册、manifest、配置、分区或人工决策。

固定上游仍为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围 **154目录／188变体**；138／171为旧README统计。当前仍为152个官方源码入口、153个本地profile（含历史入口），36项无入口、4项已知必需功能缺口保留。源码、历史通过、当前输入构建和实机验收分别记录。

## 本批逐板选择

| profile | 芯片／唤醒 | 保留的显示、触摸和音频差异 |
| --- | --- | --- |
| waveshare-amoled-1.8-sh8601 | S3／你好，小燕子 | 原版368×448 SH8601、FT5x06、TCA9554复位、单ES8311、SD |
| waveshare-amoled-1.8-v2 | S3／你好，小燕子 | CO5300、BSP触摸探测与gap、原单ES8311路径、SD |
| waveshare-amoled-1.32 | S3／你好，小燕子 | 历史466×466 SH8601／gap6、CST820、单ES8311、无SD、8MB |
| waveshare-amoled-2.06 | S3／你好，小燕子 | 410×502 SH8601、FT5x06、ES7210＋ES8311、SD、32MB声明 |
| waveshare-amoled-2.16 | S3／你好，小燕子 | 480×480 CO5300、CST9217、A0方向、双codec、SD |
| waveshare-amoled-1.75 | S3／你好，小燕子 | 466×466 CO5300、CST9217、双codec、SD |
| waveshare-amoled-1.75c | S3／你好，小燕子 | 独立复位／MCLK配置、466×466 CO5300、CST9217、双codec、无SD |
| esp32-s3-box | S3／你好，小燕子 | 320×240专用面板命令／SDK DCS，TT21100，ES7210＋ES8311，无SD |
| esp32-s3-box-lite | S3／你好，小燕子 | 独立方向／背光极性，无触摸，ES7243E＋ES8156，无SD |
| esp32-s3-box-3 | S3／你好，小燕子 | TT21100／GT911选择，ES7210＋ES8311，独立底座SD电源 |
| m5stack-core-s3 | S3／你好，小燕子 | FT5x06、ES7210＋AW88298、SD；Quad PSRAM，DC／SD-MISO复用 |
| waveshare-esp32-c5-touch-lcd-1.69 | C5／你好小智 | 240×280 ST7789、CST816S、TCA9554、单ES8311 |
| movecall-moji2-esp32c5 | C5／你好小智 | 360×360 ST77916 QSPI、无声明触摸、单ES8311，USB控制台 |
| espressif-esp-sensairshuttle | C5／你好小智 | 284×240板级命令／SDK DCS，ADC16k＋PDM24k，独立ADC owner |
| espressif-esp-spot-c5 | C5／你好小智 | 无屏热点网页绑定，单ES8311 mono16k／BCLK时钟，8MB |

七款AMOLED没有按同尺寸或同系列合并硬件修订；原版1.8 SH8601、CO5300 v2和历史1.32的配置分别核对。BOX及CoreS3的SDK ST7789符号用于DCS操作，不据符号名改变实际面板命令或型号。CoreS3检查原SPI前后回调符号，但不能替代DC／SD-MISO复用的物理验收。

C5四项保留已接受的固定 `wn9s_nihaoxiaozhi`／“你好小智”，不以手动按键代替唤醒；S3十一项保留自定义唤醒。CoreS3和四款C5使用Quad PSRAM，其他十项为Octal。Spot无屏入口仍选择headless UI／portal，六位码在手机连接设备热点后读取，绑定后才初始化音频；本批不宣称真实手机、访问边界或呈现行为已通过。

## 实际环境和命令

复用十五个现有独立stage，分别同步当前main、根构建元数据、芯片／板配置和分区，再重新配置构建。没有共用同一活动build目录，没有复制其他板的对象或镜像。环境为已安装ESP-IDF5.5.6-dirty、现有IDF Python、Xtensa／RISC-V `esp-14.2.0_20260121`、CMake3.30.2、Ninja1.12.1、ROM20260528，以及各stage已有组件override和offline_guard，不下载或安装依赖。

本批沿原stage构建设置，不新增ccache模式或修改代码生成优化；最多两个板同时构建，每项 `IDF_PY_BUILD_JOBS=2`。上批Genjutech的GCC内部错误、诊断与缓存旁路记录继续保留，不能据本批其他板成功就认定其根因已解决。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/clockfix-cross-chip-rebuild-20261113/build_batch.py
```

每项实际命令为 `idf.py -DIDF_TARGET=<esp32s3或esp32c5> -DASSISTANT_BOARD=<profile> reconfigure build`。准确解释器路径、cwd、开始结束时间、同步文件、工具版本、日志摘要和退出码分别保存在各板 `build.json`，进程结果汇总在 `batch-build-results.json`。

完成后分次调用 `collect_batch.py` 的 `collect` 核对实际产物，最后汇总。每板八条真实命令包括对应架构 `readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`，以及 `esptool image_info --version 2`、实际分区二进制解析、SDK HEAD／status只读查询。完整命令、输出、退出码和摘要保存在各板 `commands.json`。随后执行：

```powershell
# python指上述已安装的IDF Python。
python -B evidence/clockfix-cross-chip-rebuild-20261113/collect_batch.py
python -B evidence/clockfix-cross-chip-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/clockfix-cross-chip-rebuild-20261113/finalize.py
git diff --check
```

当前源文件摘要、编译宏和实际选中main源码用于核对每板身份；正反符号期望保存于 `symbol-expectations.json`，由此前已核验记录并对照当前驱动／CMake选择复核。实际ELF中的codec、面板、触摸、SD及固定／自定义唤醒选择分别检查，不能以缓存目录含有某组件代替目标链接证据。

## 最终结果

十五项真实整板构建和每项八条产物核对命令全部退出0；每项保留330个当前输入摘要，共归档632个产物。

| profile | 芯片 | app字节 | app分区剩余 | 模型字节 | 产物数 |
| --- | --- | ---: | ---: | ---: | ---: |
| waveshare-amoled-1.8-sh8601 | esp32s3 | 2,610,752 | 534,976 | 2,203,819 | 43 |
| waveshare-amoled-1.8-v2 | esp32s3 | 2,617,792 | 527,936 | 2,203,819 | 39 |
| waveshare-amoled-1.32 | esp32s3 | 2,535,808 | 216,704 | 2,203,819 | 43 |
| waveshare-amoled-2.06 | esp32s3 | 2,614,784 | 530,944 | 2,203,819 | 43 |
| waveshare-amoled-2.16 | esp32s3 | 2,611,008 | 534,720 | 2,203,819 | 43 |
| waveshare-amoled-1.75 | esp32s3 | 2,611,232 | 534,496 | 2,203,819 | 43 |
| waveshare-amoled-1.75c | esp32s3 | 2,541,856 | 603,872 | 2,203,819 | 43 |
| esp32-s3-box | esp32s3 | 2,544,592 | 601,136 | 2,203,819 | 42 |
| esp32-s3-box-lite | esp32s3 | 2,536,800 | 608,928 | 2,203,819 | 41 |
| esp32-s3-box-3 | esp32s3 | 2,617,328 | 528,400 | 2,203,819 | 43 |
| m5stack-core-s3 | esp32s3 | 2,604,496 | 541,232 | 2,203,819 | 41 |
| waveshare-esp32-c5-touch-lcd-1.69 | esp32c5 | 2,226,224 | 919,504 | 125,951 | 41 |
| movecall-moji2-esp32c5 | esp32c5 | 2,495,040 | 650,688 | 125,951 | 43 |
| espressif-esp-sensairshuttle | esp32c5 | 2,492,672 | 653,056 | 125,951 | 42 |
| espressif-esp-spot-c5 | esp32c5 | 1,552,336 | 1,593,392 | 125,951 | 42 |

累计曾缓存构建通过的官方变体仍为115个。本批为十五个过期项补齐新输入构建，之前十三项当前通过保持，矩阵为 **28项当前通过／87项历史通过但输入已过期／73项未执行构建**。不是新增源码入口、首次通过或实机完成。


S3模型均为2,203,819字节，使用MN5及自定义唤醒源码。C5模型均为125,951字节，SHA-256为 `fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b`，配置和源码选用固定唤醒，未选MN5。C5应用真实ELF为RISC-V，S3为Xtensa；不把一种架构的回归当作另一种架构的编译证据。

每板核对项目身份、芯片、Flash声明、镜像checksum／validation hash和真实分区，逐项比较分区名称、偏移、大小及标志，检查app／model实际容纳。1.32和Spot的8MB分区分别保持双0x2A0000 app＋0x240000 model，以及双3MiB app＋1MiB model，不混用。其他项沿各自既有16／32MB声明及分区，保留NVS。

本批归档当前输入、ELF、镜像、map、模型、分区、配置、编译命令及实际选择的main对象。最终审计核对旧main、注册、manifest、配置、工具和旧构建账本未变；研究只追加本批后续证据，前批十三项当前通过仍须保持当前，旧功能缺口／实机边界不删除。

SDK强制UND继续按架构记录：C5为 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`，S3另有 `start_app_other_cores`。原警告、SDK dirty状态和本地override边界保留，不称干净发行、零UND、启动或声学验收。

188项实机仍全部 `not_run`，唯一现有微雪1.8的具体修订仍待确认。本批未运行独立测试套件／类型检查、启动服务、安装依赖、Git写操作、刷机、OTA、上传或发布。物理显示／触摸／SD、麦克风时钟、PA、唤醒／AEC／自然插话、网络绑定和同板OTA回滚仍待对应实机；全部厂商外围不据编译计为完成。

## 证据入口

- [最终审计](../evidence/clockfix-cross-chip-rebuild-20261113/final-audit.json)
- [逐板构建结果](../evidence/clockfix-cross-chip-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/clockfix-cross-chip-rebuild-20261113/collection-summary.json)
- [板型与独立stage](../evidence/clockfix-cross-chip-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/clockfix-cross-chip-rebuild-20261113/symbol-expectations.json)
- [上批T-Circle时钟修正](remaining_s3_rebuild_20261113.md)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
