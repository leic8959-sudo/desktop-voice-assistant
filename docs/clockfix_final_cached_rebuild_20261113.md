# T-Circle时钟修正后三十七项缓存目标重建

本批为剩余37项有可复用环境、且未登记必需功能缺口的S3入口补齐当前源码整板构建证据。另一个过期项Cardputer ADV仍缺本地唤醒、唤醒片段、自然插话、媒体唤醒和AFE／AEC，不用编译成功替代功能完成。本批不修改固件main、注册、manifest、配置、分区或人工决策。

固定小智v2.5.2提交为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围为 **154目录／188变体**，README的138／171为旧统计。仍有152个官方源码入口、153个本地profile（含历史入口）、36项无入口、4项已知必需功能缺口。本批不是新增源码入口或首次构建通过变体。

## 范围和保留差异

本批包括便携／小屏／OLED、电子纸、摄像头板、无屏板和机器人基础语音入口。完整逐板配置见下表和 `plan.json`。

- 5项Quad PSRAM：乐动、掌控板、两款T-Camera和MixGo；其余32项为Octal。
- 3项8MB Flash：StickS3、MixGo、Cuican，保留独立双0x2A0000 app和0x240000模型分区，其余入口沿现有16MB配置。
- Magiclick2.5沿此前单任务设置，`IDF_PY_BUILD_JOBS=1`，其余项为2；不更改优化或编译器。
- 两项无屏：DFRobot AI Cam和EDA Super Bear，选择headless UI／portal及手机热点六位码绑定，绑定后初始化音频。
- AiPi Lite保留0x9000／0x4000只读工厂区，工作NVS为0x10000／0x4000，核对专用分区而不是套通用地址。
- Otto保留CAM／非CAM两套运行时接线与采样率；SparkBot保留共用GPIO46背光／功放策略；MVSRLoRa保留PDM采集及CST226SE，默认Wi-Fi入口不等于LoRa已适配。
- Magiclick2.4的NV3023与2.5的ADC选择GC9107／ST7735分别核对，2.5还核对真实ADC调用和未知修订停止诊断；不要求被优化内联的小函数必须有独立符号。

自定义“你好，小燕子”、AFE／软件AEC、媒体、绑定、提醒和双OTA均保留。面板命令／窗口、触摸／按键、codec、数字音频和PDM选择逐板核对，不按屏幕尺寸、品牌或相同芯片合并硬件。部分专用面板借用SDK ST7789对象的DCS操作，不能据该符号改变实际屏控身份。

摄像头板构建不等于摄像头工作，机器人入口不等于机械运动／工具服务验收，纸屏刷新接口也不能证明真实可见呈现。电池、传感器、省电、全部厂商手势和未移植外围仍按原记录保留，不能据本批终端构建宣布整板全部完成。

## 实际构建及检查

37项各有独立stage，复制当前main、根构建元数据、芯片／板配置和分区后分别重新配置构建。使用现有ESP-IDF5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake3.30.2、Ninja1.12.1、ROM20260528，以及各stage已有组件override／offline_guard；不下载或安装依赖，不从别板复制对象或镜像。

最多两个独立目标同时构建。每项实际命令为 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，完整解释器路径、cwd、时间、jobs、同步文件及退出码保存在各板 `build.json`。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/clockfix-final-cached-rebuild-20261113/build_batch.py
```

完成后分次调用 `collect_batch.py` 的 `collect`，最后汇总。每项实际执行8条产物命令：`readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、分区二进制解析及SDK HEAD／status只读查询，真实命令、输出和摘要均在 `commands.json`。

```powershell
# python指上述现有IDF Python。
python -B evidence/clockfix-final-cached-rebuild-20261113/collect_batch.py
python -B evidence/clockfix-final-cached-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/clockfix-final-cached-rebuild-20261113/finalize.py
git diff --check
```

逐板 `symbol-expectations.json` 来自此前已核对记录，并对照当前源码保持最新的MVSRLoRa回调名和AiPi RTC电源选择。编译宏、选择的main文件、配置、模型和ELF一起检查，包括标准／PDM、单codec／双codec／数字音频、headless／显示和OLED／TFT。符号存在仅证明选中并链接，不能证明运行或声学效果。

历史Magiclick和Genjutech的GCC内部错误根因仍未确认，本批其他目标成功不说明编译器已修复。失败尝试若发生则单独留档，不能用诊断对象、降优化、关闭算法或改功能范围制造通过。

## 最终产物与矩阵

三十七项真实整板构建和每项八条产物核对命令全部退出0；每项保留330个当前输入摘要，共归档1509个产物。

| profile | app字节 | app分区剩余 | model分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| bread-compact-wifi-s3cam | 2,215,744 | 929,984 | 1,990,485 | 41 |
| chd-esp32s3-eye | 2,246,576 | 899,152 | 1,990,485 | 40 |
| dfrobot-df-s3-ai-cam | 1,581,920 | 1,563,808 | 1,990,485 | 41 |
| du-chatx | 2,209,104 | 936,624 | 1,990,485 | 41 |
| electron-bot | 2,209,424 | 936,304 | 1,990,485 | 41 |
| espressif-esp-sparkbot | 2,239,840 | 905,888 | 1,990,485 | 40 |
| hu-087 | 2,157,456 | 988,272 | 1,990,485 | 41 |
| jiuchuan-s3 | 2,250,000 | 895,728 | 1,990,485 | 41 |
| kevin-box-2 | 2,190,960 | 954,768 | 1,990,485 | 41 |
| kevin-sp-v4-dev | 2,246,304 | 899,424 | 1,990,485 | 40 |
| labplus-ledong-v2 | 2,244,176 | 901,552 | 1,990,485 | 41 |
| labplus-mpython-v3 | 2,243,904 | 901,824 | 1,990,485 | 41 |
| lceda-course-examples-eda-robot-pro | 2,157,456 | 988,272 | 1,990,485 | 41 |
| lceda-course-examples-eda-super-bear | 1,578,912 | 1,566,816 | 1,990,485 | 41 |
| lceda-course-examples-eda-tv-pro | 2,208,144 | 937,584 | 1,990,485 | 41 |
| lilygo-t-cameraplus-s3 | 2,228,016 | 917,712 | 1,990,485 | 41 |
| lilygo-t-cameraplus-s3-v1.2 | 2,231,920 | 913,808 | 1,990,485 | 41 |
| lilygo-t-display-s3-pro-mvsrlora | 2,233,088 | 912,640 | 1,990,485 | 42 |
| m5stack-stick-s3 | 2,249,824 | 502,688 | 155,477 | 41 |
| m5stack-stopwatch | 2,544,592 | 601,136 | 1,990,485 | 41 |
| magiclick-2p4 | 2,249,776 | 895,952 | 1,990,485 | 40 |
| magiclick-2p5 | 2,256,864 | 888,864 | 1,990,485 | 40 |
| maza-ai-esp32-s3 | 2,197,344 | 948,384 | 1,990,485 | 40 |
| minsi-k08-dual | 2,209,184 | 936,544 | 1,990,485 | 41 |
| mixgo-nova | 2,243,120 | 509,392 | 155,477 | 41 |
| movecall-cuican-esp32s3 | 2,088,176 | 664,336 | 155,477 | 40 |
| movecall-moji-esp32s3 | 2,247,984 | 897,744 | 1,990,485 | 40 |
| nologo-xingzhi-cube-0.85tft-wifi | 2,210,672 | 935,056 | 1,990,485 | 41 |
| nologo-xingzhi-cube-0.96oled-wifi | 2,159,696 | 986,032 | 1,990,485 | 41 |
| nulllab-ai-vox-v3 | 2,248,416 | 897,312 | 1,990,485 | 40 |
| otto-robot | 2,209,232 | 936,496 | 1,990,485 | 42 |
| spotpear-sp-esp32-s3-1.28-box | 2,259,072 | 886,656 | 1,990,485 | 40 |
| waveshare-esp32-s3-touch-lcd-3.5 | 2,546,768 | 598,960 | 1,990,485 | 41 |
| wk-esp32s3-dev | 2,158,768 | 986,960 | 1,990,485 | 41 |
| xorigin-aipi-lite | 2,248,816 | 896,912 | 1,990,485 | 40 |
| yunliao-s3 | 2,247,696 | 898,032 | 1,990,485 | 41 |
| zhengchen-cam | 2,258,272 | 887,456 | 1,990,485 | 42 |

累计曾缓存构建通过仍为115个官方变体。本批补齐三十七项新输入证据，之前77项仍当前，矩阵为 **114项当前通过／1项历史通过但输入过期／73项未执行构建**。剩余唯一过期项为有明确必需功能缺口的Cardputer ADV；它不能由重建直接算作完整适配。


每板模型均为2,203,819字节，保留MN5和自定义唤醒源码，排除固定词或手动唤醒替代实现。实际镜像核对项目名、Xtensa／ESP32-S3、Flash设置、checksum／validation hash；真实分区按各板CSV比较名称、偏移、长度及标志，核对app／model确实容纳。NVS与双OTA保留，不从别板结果推断物理容量。

每板归档330个当前输入摘要、镜像、ELF、map、模型、分区、配置、编译命令和全部实际选择的main对象。最终审计核对旧main、注册、manifest、配置、工具和历史构建账本未改；研究只追加本批后续，之前77项当前通过须保持，已知缺口不删除。

## 全部当前构建证据统一复核

本批另实际运行 `python -B evidence/clockfix-final-cached-rebuild-20261113/audit_release_evidence.py`，退出0。它读取最终矩阵，为全部114项选择真实当前输入匹配的成功记录，逐项核对330个输入及归档源码副本，流式重算全部4,757份产物摘要、尺寸和命令日志摘要，检查镜像／ELF／map／模型／分区／配置等必需产物存在。累计核对8,793,015,643字节（约8.79GB），包含110个S3、4个C5、6个无屏入口，不将一种架构或UI路径的构建推广到另一种。

它还核对实际配置、自定义／固定唤醒源文件、headless／display源码选择、模型大小、SDK强制UND、每项app／model余量及镜像检查日志。T-Circle两项包含此前实际编译参数预处理的第九条命令，其他项各八条。完整逐板记录和当前矩阵／注册摘要保存在 `release-evidence-audit.json`。

这是对当前构建记录和归档字节的复核，不是重新执行固件、干净依赖解析或实物测试；没有审计完整第三方依赖供应链或物理容量，不能据此证明所有功能、外围、内存峰值或声学结果。`no_known_omissions_in_selectors` 仅表示现有选择器未登记必需功能删减，不能作为完整支持认证。

## 仍需推进的74项

| 范围 | 数量 | 当前状态 |
| --- | ---: | --- |
| 已有源码但未构建 | 37 | 27个P4／P4X、4个S31、6个S3；缺SDK／组件或其他功能条件，逐板清单保留 |
| 无源码入口 | 36 | C3十项、C6十项、ESP32七项、S3九项；固定唤醒词许可不等于内部内存和完整管道已实现 |
| Cardputer ADV | 1 | 历史候选编译过，当前输入已过期；缺本地唤醒、唤醒片段、自然插话、媒体唤醒和AFE／AEC |

74项中共有4项已知必需功能缺口：Cardputer ADV、无PSRAM AtomS3 Echo Base和两款Guition。其余34项已有源码且没有登记选择器删减，但仍无目标构建证据；36项尚无入口。不能把安装依赖或重建旧候选单独当成这些功能已经完成。当前本机依赖分组见[前批最新报告](clockfix_display_rebuild_20261113.md)，没有自行安装SDK或依赖。

剩余清单按最终矩阵导出为 `remaining-74-variants.json`，保持完整目标。全部188项仍待对应实机验收，不因114项当前构建通过而收窄原目标。

SDK四个既有强制UND继续记录：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。原警告、dirty状态和本地override边界保留，不称零UND、干净发行或启动通过。

188项实机仍全部`not_run`，唯一现有微雪1.8的具体修订仍待确认。本批未运行独立测试套件／类型检查、启动服务、安装依赖、Git写操作、刷机、OTA、上传或发布。实际时钟、显示／触摸／按键、PA、唤醒／AEC／自然插话、手机绑定、联网媒体、资源峰值和OTA回滚仍需对应实机验收，全量目标未完成。

## 证据入口

- [最终审计](../evidence/clockfix-final-cached-rebuild-20261113/final-audit.json)
- [114项当前构建归档统一复核](../evidence/clockfix-final-cached-rebuild-20261113/release-evidence-audit.json)
- [剩余74项逐板清单](../evidence/clockfix-final-cached-rebuild-20261113/remaining-74-variants.json)
- [构建进程结果](../evidence/clockfix-final-cached-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/clockfix-final-cached-rebuild-20261113/collection-summary.json)
- [逐板配置／独立stage／jobs](../evidence/clockfix-final-cached-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/clockfix-final-cached-rebuild-20261113/symbol-expectations.json)
- [未构建项依赖分组](clockfix_display_rebuild_20261113.md)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
