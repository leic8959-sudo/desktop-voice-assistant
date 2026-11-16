# Freenove 2.8／K10 默认 ILI9341 路径构建

本批继续推进已登记源码但缺组件未构建的两块板：`freenove-esp32s3-display-2.8-lcd` 和 `dfrobot-df-k10`。复用已有 SDK 公共 DCS 操作，保留原 ILI9341 2.1.0 默认初始化。硬件仍记录为 ILI9341，不因 SDK 工厂函数名改写型号；没有用其它板配置替代目标构建。

## 显示接入

源自先前已核对的 esp-bsp 固定提交 `93460b932beba7022a8a4ec4186e0d6b7533d05f`，ILI9341 2.1.0 的 18 条默认命令转为 `main/ili9341_default_commands.h`，完整保留顺序和有效载荷，包括两条重复的 F7；没有采用 Quandong 的不同 gamma／电源表。

默认初始化回调保留 SLPOUT／100ms、BGR MADCTL=0x08、RGB565 COLMOD=0x55，再逐条发送 18 条命令并保留零 tick 让出。SDK 默认 ST7789 init 不被调用，避免额外写 RAMCTRL。SDK 内部 BGR／16位状态与这份表一致，编译期检查表不覆盖 MADCTL／COLMOD 且数据长度不越界。

软件复位仍为 0x01／20ms；绘图复用相同的 gap、0x2a／0x2b 末坐标减一、0x2c 和 16 位长度，并传播 SDK color TX 错误。共用睡眠回调保留 ILI9341 的 0x10／0x11／120ms，同时供此前 Quandong 使用；该板只更换这份回调的共用名称，其独立19条初始化表仍保持原文。SDK 直接亮度回调禁用，继续使用实际板级 PWM／扩展口控制背光。

| 板型 | 保留的显示／输入 | 保留的音频 |
| --- | --- | --- |
| Freenove 2.8 | SPI3 mode0／20MHz；320×240横屏，反色、swapXY、不镜像，gap0；GPIO45 PWM；BOOT0和FT6336U触摸按下状态，仍无LVGL坐标指针 | 单ES8311，24kHz mono；PA1低有效，实际解除静音后使能 |
| K10 | SPI3 mode0／40MHz；240×320竖屏，不反色、不换轴、mirrorY，gap0；TCA9555 P0二值背光、P12／P2 A/B键 | ES7243E输入＋数字输出；16kHz标准双声道帧，左输入抽mono／输出复制mono；网络24→16kHz和AEC参考保留 |

两板各自的SPI引脚、触摸／扩展口、音频地址和格式不互换。没有改音频、按键、完整自定义唤醒、自然插话、媒体、绑定、凭据保存或双 OTA 源码。新增表保留 Espressif Apache-2.0 标识，并在 `main/third_party/espressif_ili9341/LICENSE` 附同一许可全文；未复制、安装或链接外部组件二进制。

## 验证与证据

本批源码／命令／产物在 `evidence/ili9341-default-sdk-20261112/`，按 `freenove/`、`k10/`、`quandong/` 三个真实目标分开保存。构建使用现有 SDK5.5.6-dirty、Xtensa14.2.0、17份现有本地组件override及Python外网保护。两新板使用全新stage，Quandong复用原stage并先同步main、根registry和配置；旧产物在前批evidence保留。

实际入口命令：

| 实际目标命令 | 最终结果与边界 |
| --- | --- |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=freenove-esp32s3-display-2.8-lcd build` | 最终退出0，应用2258560字节，3MiB应用槽剩887168字节 |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=dfrobot-df-k10 build` | 首次整板及最终源码同步后重跑均退出0，应用2227024字节，应用槽剩918704字节 |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=quandong-s3-dev reconfigure build`，随后当前源码 `build` | 两次退出0，应用2249152字节，应用槽剩896576字节 |

三板模型均为2203819字节，双3MiB应用槽／4MiB模型分区、镜像项目身份、checksum／validation hash均检查通过。每板269项当前源码／配置摘要、24份产物快照；实际readelf／nm／size、esptool和分区解码命令均退出0。Freenove保留ES8311和触摸键路径，K10保留实际ES7243E／板级IO入口，三板均保留AEC／唤醒、HTTP／WS和对应显示回调；不是空入口组件工程。

```text
python -B evidence/ili9341-default-sdk-20261112/freenove/build_cached.py
python -B evidence/ili9341-default-sdk-20261112/k10/build_cached.py
python -B evidence/ili9341-default-sdk-20261112/quandong/build_cached.py
python -B evidence/ili9341-default-sdk-20261112/freenove/rebuild_cached.py
python -B evidence/ili9341-default-sdk-20261112/k10/rebuild_cached.py
python -B evidence/ili9341-default-sdk-20261112/quandong/rebuild_cached.py
python -B evidence/ili9341-default-sdk-20261112/freenove/collect_build.py
python -B evidence/ili9341-default-sdk-20261112/k10/collect_build.py
python -B evidence/ili9341-default-sdk-20261112/quandong/collect_build.py
python -B evidence/ili9341-default-sdk-20261112/inspect_sdk_limits.py
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/ili9341-default-sdk-20261112/direct-dependencies.json
python -B evidence/ili9341-default-sdk-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ili9341-default-sdk-20261112/finalize.py
```

直接依赖报告为153本地／152官方、967条声明，70本地／69官方满足当前直接条件，缺组件声明减为161。它不代表传递依赖、编译或实机已通过；真实构建分别记录。没有下载或安装缺失组件，也没有用 `idf.py add-dependency` 改项目清单。

Freenove首轮实际退出2：ESP-DSP既有C源码触发GCC内部Segmentation fault；未修改该库源码或优化选项。设置当前SDK支持的 `IDF_PY_BUILD_JOBS=2` 重跑后，出现独立的真实板级错误：FT5x06宏指定初始化器把scl_speed_hz放在control_phase_bytes前，违反SDK5.5.6结构体的C++声明顺序。改为零初始化和原字段值逐项赋值，保留0x38／400kHz／50ms、control_phase_bytes=1、dc_bit_offset=0、8位命令和disable_control_phase=1；随后重跑退出0。降低并发后编译器未再报内部段错误，但没有测量资源或确定其根因。

同类检索仅发现另一个宏调用在 `waveshare_35_display.cpp`，同步改为原值赋值，保留该板100ms超时；该变更只影响标准微雪3.5的FT5x06分支，3.5B／3.49仍走独立触摸路径。微雪3.5整板仍缺自己的屏幕组件，本批没有构建它，不给它增加通过计数。两次失败的完整日志／命令和此前K10、Quandong成功产物分别保存；最终三板输入已同步本次触摸源修正后重新构建／核对。

第一次调用重试包装脚本时，其继承模板错误要求前次构建成功，断言在启动IDF前停止；修正为读取已记录的失败尝试，并核对SDK实际并发环境变量后再执行。此脚本准备错误与两次真实固件构建失败分开记录，未覆盖原日志。

三板实际nm均有四个SDK既有强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`；均在链接参数有对应 `-u`，来源和实际配置归档，未修改SDK。IDF仍可能更新安装目录下共享root_managed_components缓存锁文件，本批保留事后快照，不宣称所有写入仅在stage。既有LVGL弃用警告不扩大修复。

## 实机与运行限制

当前只有微雪1.8实物。默认表的静态逐字核对不证明实际两块板的gamma、颜色、窗口和旋转；Freenove触摸按下／多指／按键冲突、PA门控和24kHz音频，K10扩展口极性、ADC修订／地址／帧时钟／左声道及16kHz播放，均需实物验收。

两块板的真实Flash／PSRAM容量、模型创建、AFE／AEC／自然插话、音量／静音／重开、配网绑定、媒体及同板双 OTA 也未实机验收。SDK既有强制UND和构建警告保留，不把缓存构建退出0当成零UND、可启动或干净发行环境通过。

原缺依赖预检和其它板回归记录继续保留为历史；本批没有新增profile、运行独立测试套件或类型检查、启动服务、安装依赖、Git写入、刷机、OTA、上传或发布。

两板当前依赖状态由历史blocked_missing_local_sources更新为resolved_local_overrides，以真实本地解析／构建和产物摘要为依据，仍不代表干净环境。当前152／188官方源码入口、153本地profile、36待源码；3 passed_cached_sources／66 stale_cached_sources／6 failed_cached_sources／113 not_run，当前通过为Freenove、K10和Quandong。此前BOX2因完整main输入变化转stale，历史成功保留；累计69官方变体曾缓存构建通过，188项本轮实机均not_run。
