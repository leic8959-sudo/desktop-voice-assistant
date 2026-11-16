# Moji2 C5 的 QSPI 接入与首次整板构建

SDK链接后续：本页这份镜像已单独核对ELF／镜像入口、实际启动函数表和显式链接输入，未发现对象引用所列强制UND；不能单凭这些名称判定缺少启动代码。实机启动仍未验收。见[只读审阅](sdk_forced_und_audit_20261113.md)。

本批推进已有 `movecall-moji2-esp32c5` 源码入口。此前该板因ST77916 2.0.2缺失未暂存、未编译；本次复用现有360×360／RGB565／QSPI面板流程，保留原184条表、GPIO1复位、无触摸、单ES8311音频及固定唤醒词差异，再构建真实C5目标。VoCat S3单独重建，核对共享显示路径回归。

## 固定来源与改动范围

小智v2.5.2仍固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。本轮重新核对前批25份Moji2板类／config／README、驱动及SDK参考的SHA256，有Git blob字段的同时核对blob内容；记录在 `evidence/moji2-qspi-native-20261112/source-references.json`。没有安装或下载SDK、驱动组件或预编译语音库。

板类虽然叫 `movecall_moji2_esp32s3.cc`，实际目标和本次IDF参数均为esp32c5，不能据文件名当成S3。Moji2的184条命令与既有 `VOCAT_PANEL_COMMANDS` 实际发送字节、长度、顺序、延时逐项相同，继续复用原表及其MIT通知，不复制第二份数据。结尾是零参数21、零参数11及NOP等待120ms；F0回到user bank，没有user bank下MADCTL／COLMOD覆盖。

源码改动限于共享QSPI显示层的面板创建、C5对应CMake选择、Moji2精确依赖声明和profile实现说明。显示层统一调用 `st77916_qspi_panel_create`，无需保留Moji2外部ST77916 vendor结构或工厂。公共panel源／头、原命令表、Moji2板级供电、音频、固定唤醒、控制协议与人工决定、UI、绑定和OTA均保持本批开始时原文。

导出许可条件扩展到Moji2，使用既有 `xiaozhi_vocat` MIT和 `st77916` Apache-2.0目录；没有运行Docker或实际导出。整体项目LICENSE和规则文件未改。

## 逐板差异

| 模块 | Moji2 C5保持的路径 |
| --- | --- |
| 显示 | 360×360 RGB565，QSPI2 mode0／40MHz，CS3／CLK0／D0..3=9／8／7／6，RST1低有效；无mirror／swap、gap0 |
| 协议／复位 | 原02参数、32／2C像素封装、2A／2B含end-1、MADCTL00／COLMOD55前导后原184表；GPIO1低10ms／高后120ms |
| 刷新 | 原两像素X边界、双20行DMA条带28,800字节、同panel IO完成通知、LVGL core0 |
| 触摸 | 固定板类没有触摸路径，BSP_CAPS_TOUCH=0；不创建CST816、I2C触摸或LVGL触摸指针，不据源码断言未知硬件修订没有触摸芯片 |
| 音频／功放 | ES8311 BOTH、单checked IN_OUT设备、mono24kHz；I2C0 SDA26／SCL27，MCLK25／BCLK11／WS24／DOUT23／DIN12；PA5由原INPUT_OUTPUT读回／100ms互斥控制，实际DAC解除静音后启用，背光2先暗 |
| 唤醒／AEC | 用户已接受固定“你好小智”／wn9s_nihaoxiaozhi，保留单模型完整性门槛、16kHz前端、C5 SR_LOW_COST AEC filter2和软件播放参考；没有改成纯按键 |
| 操作／网络 | BOOT28先松手再PTT／卡片；保留原Wi-Fi绑定、WebSocket语音与媒体、自然插话调度、凭据保存和HTTPS双OTA |
| 控制台 | USB Serial/JTAG13／14，CMake继续拒绝UART默认配置；UART0 TX11／RX12与I2S BCLK11／DIN12冲突，不能静默恢复UART |

公共panel没有在本批扩大API范围，也没有调用SDK普通SPI初始化或追加Waveshare屏幕ID探测／TCA复位。VoCat继续它自己的动态PCB复位、双codec和触摸；不能把两板表相同推断为外围相同。

## 未完成的验收

只有现有微雪1.8实物。Moji2的Flash16MB／QUAD PSRAM容量、PCB／屏控身份、40MHz可靠性、复位波形、gamma／颜色／圆屏可视区、BOOT／卡片、USB接线、PA极性／实际发声、24kHz时钟／增益／静音均未实机验收。

C5单核下LVGL、Wi-Fi、TLS和音频并发的CPU、堆／栈和峰值预算，以及固定词识别、播放期唤醒、AEC、自然插话、媒体、绑定和同板HTTPS OTA仍需实测。C5 SR AEC与S3 FD／NLP接口不同，构建或API存在不证明声学等价。LED10、电池ADC4／分压遥测、sleep和原厂MCP未实现；电量不作推断。

## 实际执行与证据

目录为 `evidence/moji2-qspi-native-20261112/`。Moji2新stage：`I:/xy_board_builds_20261109/moji2_qspi_native_20261112`；VoCat复用并同步自身stage。环境为现有SDK5.5.6-dirty，C5使用RISC-V14.2.0及16份兼容组件override，S3回归使用Xtensa14.2.0及17份override，各2jobs和Python联网保护，没有安装依赖。

实际执行：

```text
python -B evidence/moji2-qspi-native-20261112/moji2/build_cached.py
python -B evidence/moji2-qspi-native-20261112/vocat/build_cached.py
python -B evidence/moji2-qspi-native-20261112/review_sources.py
python -B evidence/moji2-qspi-native-20261112/vocat/collect_build.py
python -B evidence/moji2-qspi-native-20261112/moji2/collect_build.py
```

| 实际目标命令 | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| `idf.py -DIDF_TARGET=esp32c5 -DASSISTANT_BOARD=movecall-moji2-esp32c5 build` | 2495040 | 125951 | 首次退出0，3MiB应用槽剩650688字节 |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=espressif-esp-vocat reconfigure build` | 2558080 | 2203819 | 退出0 |

VoCat回归归档283项当前输入及27份产物，项目身份、镜像、分区和关键符号检查通过；它不验证Moji2固定词／单codec／无触摸／C5资源。

只读直接依赖报告已执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/moji2-qspi-native-20261112/direct-dependencies.json
```

退出0：153本地／152官方、944条直接声明，92本地／91官方满足直接条件，缺组件声明138项。该报告不等于传递解析、API、目标构建或硬件验收。

Moji2归档283项当前输入、28份产物快照，stage与工作区摘要一致。实际RISC-V readelf／nm／size、esptool image_info、分区解码及SDK Git只读查询均退出0；项目名assistant_moji2c5、目标esp32c5、真实MOJI2_C5／FIXED_WAKE宏均核对，checksum／validation hash有效。双3MiB OTA与4MiB模型分区保留；生成模型125951字节，SHA256 `fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b`，与前批同一单固定词包一致。

ELF含Moji2电源／PA、共享panel、ES8311、AEC／固定唤醒、播放入口、HTTP／WebSocket；无ES7210、CST816触摸工厂和外部ST77916工厂。20,496字节的软件播放参考缓冲仍存在。实际sdkconfig保留QUAD、固定模型和USB Serial/JTAG，UART默认未启用。源码／配置／符号核对不执行推理或音频，也不证明资源峰值和声学已通过。

两目标均一次退出0，未出现此前的GCC内部错误。Moji2仍有既存BSP_DISPLAY_LVGL_BUF_HEIGHT未知配置项及LVGL弃用等警告，保留日志，未扩展本轮配置清理。Moji2 ELF有三个既有SDK强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`；VoCat另有`start_app_other_cores`。真实nm结果、链接器-u参数与SDK来源保留；未改SDK、不称零UND、可启动或干净发行构建。

最终归档命令：

```text
python -B evidence/moji2-qspi-native-20261112/inspect_sdk_limits.py
python -B evidence/moji2-qspi-native-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/moji2-qspi-native-20261112/finalize.py
git diff --check
git diff --no-index --check -- NUL firmware/assistant_espidf/main/waveshare_qspi_display.cpp
```

Moji2依赖改为resolved_local_overrides，旧未编译／预检状态保存在prior研究字段及历史证据；VoCat只追加回归记录。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。2 passed_cached_sources／89 stale_cached_sources／0 failed_cached_sources／97 not_run；累计91个官方变体曾缓存构建通过。89项历史成功需按当前源码重建，不能算当前通过；188项本轮实机均not_run，148项未被显式关闭规则标出缺口也不等于完整功能已验收。

最终audit核对main只改共享QSPI显示和CMake，无新增main文件；公共面板、原表、板级电源、音频、固定词、协议人工决定、UI、配网绑定和OTA源码摘要保持。普通git diff --check退出0；未跟踪显示源no-index检查记录真实差异退出码1与无空白诊断。共享root_managed_components锁事后快照保留，不能称所有构建写入仅限stage。未执行独立测试套件、类型检查、服务、模型推理、声学或触摸操作；未安装依赖、Git写入、刷机、OTA、上传或发布。
