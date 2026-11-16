# 微雪 AXS15231B 三款首次整板构建

本批覆盖已有 `waveshare-esp32-s3-touch-lcd-3.5b`、`waveshare-esp32-s3-touch-lcd-3.49`、`waveshare-esp32-s3-touch-lcd-3.49-v2`。此前三款因缺AXS15231B 2.1.0组件未编译。本轮补齐固定RGB565／QSPI面板流程，保留已有整帧DMA、触摸、电源和音频，再分别构建；原版ST7796 3.5寸独立回归。

固定小智提交仍为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。两组50项既有板级／厂家／驱动来源重新核对SHA256，原表未改。AXS 2.1.0源固定于 `espressif/esp-iot-solution@e8ae944284047eb1a3a89e2538dd35f487396fa6`，本轮通过只读gh api核对Git blob，源文件与旧归档逐字一致。命令、blob、摘要见 `evidence/axs-native-20261112/verified-reference.json`，其余来源在source-references.json。未安装或复制整套组件。

新增 `axs_qspi_panel.cpp/.h`，保留Espressif版权和Apache-2.0全文。接口只支持当前原生320×480或172×640、RGB565缓冲、gap0、原生轴及顺序行写。SLPOUT等待100ms、MADCTL00／COLMOD55前导后发送原表及全部延时；不调用SDK ST7789初始化、RAMCTRL或AXS默认表。原生panel draw使用相同02参数／32像素封装、CASET含end-1、首行RAMWR／后续RAMWRC，无RASET；真实产品仍由既有整帧flush负责顺序、转换、完成等待和DMA生命周期。任意硬件mirror／swap、非顺序矩形、18位、普通SPI／MIPI及通用默认表不在本接口范围，产品现有软件映射未删减。

| 差异 | 3.5B | 3.49原版 | 3.49 V2 |
| --- | --- | --- | --- |
| 画布／原生屏 | 480×320／320×480，软件90度映射 | 原生172×640，无CPU旋转 | 同原版 |
| 表／复位 | 原34条；板级TCA复位后软件01等120ms | 原11／29各100ms；GPIO21高30／低250／高30ms，不再软件复位 | 同两条表；复位由TCA P5承担，不写GPIO21 |
| 总线 | QSPI2 mode3／40MHz，CS12／CLK5／D0..3=1/2/3/4 | QSPI3 mode3／40MHz，CS9／CLK10／D0..3=11/12/13/14 | 同原版 |
| 内存／刷新 | 307200字节PSRAM帧，12800字节内部DMA，320×20行条带 | 220160字节PSRAM帧，22016字节DMA，172×64行共10条 | 同原版 |
| 背光 | GPIO6高有效 | GPIO8低有效 | GPIO42低有效加TCA P1门控；GPIO8是中断 |
| 触摸 | 11字节命令请求8字节，停止后另读8 | 独立I2C1，组合请求／读14字节 | 同原版 |
| 音频 | 单ES8311／24kHz | ES8311＋ES7210／24kHz TDM4取MIC1 slot0 | 同原版 |

每帧仍发送完整原生画面，CASET返回检查、RAMWR／RAMWRC、每条包括最后一条完成等待、转换时一次字节交换、超时后暗屏／禁止继续复用DMA缓冲均原文保持。一秒完成等待窗口不包含所有可能的总线排队阻塞，不能称严格端到端期限；真实帧率、IRQ／DMA时序、默认行窗口和部分无效区域刷新仍需硬件验收。

触摸读取与坐标校验／初始释放／多指／越界／错误取消原文保持，3.49仍为rawX640／rawY172、`x=rawY,y=639-rawX`，3.5B保留双镜像后换轴意图。原34条命令逐项核对一致；3.49两版各两条表按实际发送长度核对一致。审阅脚本首次误以为两条源表使用NULL，实际是零发送长度的占位00；失败脚本及说明保存在initial-source-review，改用已有按发送长度比较的解析器后通过，未改表或重跑固件。

板级TCA／AXP、电源键、PA读回、未拥有位保留、GPIO／采样率、音频、完整唤醒／AEC、自然插话／媒体人工决定、绑定／NVS／OTA源码摘要保持。3.49两版PWR16先松手后2秒长按请求关P6／P7，USB供电时不推断已经物理掉电。AXP仍仅原ALDO1操作，不扩展充电策略。许可导出补充3.5B原小智MIT和三款AXS Apache-2.0，未实际运行Docker导出。

只有微雪1.8实物。三款真实Flash／PSRAM、面板／40MHz／复位／原表／格式／整帧吞吐、触摸包兼容性／坐标、TCA／PA／PWR／V2门控、24kHz时钟／槽位／增益／静音、AEC／完整唤醒／自然插话、媒体、绑定／提醒／同板OTA均未实机验收。相机、SD、RTC、IMU、电池／充电、睡眠、原厂MCP和硬件参考AEC等既有外围缺项保留，不能据终端构建声称整板外围完成。

证据目录为 `evidence/axs-native-20261112/`。三个新目标各有新stage，原版3.5回归同步其已有stage。环境为现有SDK5.5.6-dirty／Xtensa14.2.0／17份本地override／Python联网保护，各2jobs，未安装依赖。

已执行：

```text
python -B evidence/axs-native-20261112/ws35b/build_cached.py
python -B evidence/axs-native-20261112/ws349/build_cached.py
python -B evidence/axs-native-20261112/ws349v2/build_cached.py
python -B evidence/axs-native-20261112/ws35/build_cached.py
python -B evidence/axs-native-20261112/ws35/collect_build.py
python -B evidence/axs-native-20261112/review_sources.py
python -B evidence/axs-native-20261112/ws35b/collect_build.py
python -B evidence/axs-native-20261112/ws349/collect_build.py
python -B evidence/axs-native-20261112/ws349v2/collect_build.py
```

| 目标（IDF_TARGET=esp32s3） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| waveshare-esp32-s3-touch-lcd-3.5b build | 2540608 | 2203819 | 首次退出0，3MiB应用槽剩605120字节 |
| waveshare-esp32-s3-touch-lcd-3.49 build | 2259616 | 2203819 | 首次退出0，剩886112字节 |
| waveshare-esp32-s3-touch-lcd-3.49-v2 build | 2260160 | 2203819 | 首次退出0，剩885568字节 |
| waveshare-esp32-s3-touch-lcd-3.5 reconfigure build | 2546768 | 2203819 | 退出0 |

只读直接依赖报告命令及结果：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/axs-native-20261112/direct-dependencies.json
```

退出0：153本地／152官方，935条直接声明，100本地／99官方满足直接条件，缺组件声明129项。直接条件不证明传递解析、API、整板／启动或实机通过。原版回归已归档290项当前输入／24份产物，不替AXS三板证明实际运行。

三新目标各290项当前输入与stage一致、26份产物快照；原版回归为290输入／24产物。实际readelf／nm／size、esptool image_info、分区解码和SDK Git只读查询均退出0；独立项目身份assistant_ws35b／assistant_ws349／assistant_ws349v2、板型宏、checksum／validation hash、双3MiB OTA和4MiB模型分区均核对。ELF保留真实full-frame flush、原生AXS入口、AEC／完整唤醒、HTTP／WebSocket；3.5B含单ES8311与原35板级PA接口，3.49两版含ES7210／ES8311与349电源／PA接口，无外部AXS工厂。3.5B音频位于audio_io.cpp，349用waveshare_spi_audio.cpp，对象快照按真实选择保存。

四目标均一次构建退出0，未出现此前的GCC内部错误；原LVGL弃用等警告保留。四份ELF仍含SDK既有四个强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。真实nm、链接器-u参数、SDK来源和配置保留，未改已安装SDK，不称零UND、可启动或干净发行构建。

最终归档命令：

```text
python -B evidence/axs-native-20261112/inspect_sdk_limits.py
python -B evidence/axs-native-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/axs-native-20261112/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增或修改的未跟踪源码／许可>
```

三AXS板依赖改为resolved_local_overrides，历史预检状态保存在prior字段与旧证据；原版ST7796单独记录回归，不能将原版与B版旧共组研究视为同一硬件。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。4 passed_cached_sources／95 stale_cached_sources／0 failed_cached_sources／89 not_run，累计99个官方变体曾缓存构建通过。95项历史成功需按当前源码重建，不能算当前通过；188项本轮实机均not_run，148项未被显式关闭规则标出缺口也不等于完整功能已验收。

最终audit核对main仅修改共享35显示和CMake，新增AXS panel源／头及Apache许可；原表、full-frame DMA／转换／超时、触摸、板级电源和音频、控制人工决定、绑定／OTA及其它main摘要保持。普通git diff --check退出0；未跟踪源码／许可no-index检查记录真实差异退出码与无空白诊断。共享root_managed_components锁事后快照另存，不能称所有构建写入仅限stage。未运行独立测试套件、类型检查、服务、声学／触摸／DMA运行或故障注入；未安装依赖、Git写入、刷机、OTA、上传或发布。
