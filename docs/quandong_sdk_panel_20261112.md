# Quandong S3 复用 SDK DCS 操作与首次整板构建

本批针对已有 `quandong-s3-dev` 源码入口，移除阻止构建的外部 ILI9341 组件依赖，改为复用 SDK 的公共 DCS 操作和板级初始化／睡眠回调。**硬件仍按固定资料记录为 ILI9341**，没有因为调用 SDK ST7789 工厂就更改型号，也不以构建代替实际屏幕验收。官方源码入口数量不变。

## 驱动契约

对照先前固定的 esp-bsp 提交 `93460b932beba7022a8a4ec4186e0d6b7533d05f` 的 ILI9341 2.1.0 源码和本机 SDK 5.5.6 ST7789 实现，只复用真实相同的操作；差异由板级代码明确保留。

| 路径 | 本批行为 |
| --- | --- |
| reset | SDK 软件 reset 0x01 后等待 20ms，与原适配器相同 |
| init | 板级回调先发送原 SLPOUT／100ms、MADCTL=0、COLMOD=0x55，再发送原 19 条有效命令，每条保留零 tick 延时的让出行为 |
| 禁止混入默认 init | 不调用 SDK 默认 ST7789 init，避免它额外写 RAMCTRL |
| MADCTL 缓存 | 原 RGB 构造被表中 0x08 覆盖为 BGR；新工厂以 BGR 初始化内部缓存，回调仍按原顺序发送 RGB 前导和表内 0x08，再由同样的 swapXY／mirrorY 得到 0xA8 |
| COLMOD／绘图 | 保留 RGB565／0x55，软件 gap、0x2a／0x2b 含末坐标减一、0x2c 和 16 位像素长度；SDK 同时传播 color TX 错误 |
| sleep | 板级回调保留 0x10／0x11 后等待 120ms，避免缩短成 SDK 默认 100ms |
| 背光 | 仍由 GPIO45 PWM 控制；关闭 SDK 直接亮度回调，不增加寄存器调光路径 |

编译期检查固定表内 MADCTL／COLMOD 与缓存状态一致。19 条有效命令保持原文；原上游三个错误 NOP 数据长度条目仍按先前修正排除，没有重新解释为未知命令。SPI2 mode0／40MHz、MOSI11／CLK12／DC46／CS10／无RST、320×240、gap0／0、反色、换轴／mirrorY、LVGL缓冲和方向不变。

通过公共 `esp_lcd_panel_t` 回调表接入，未访问 SDK 私有布局或修改已安装 SDK。本板按现有单面板全生命周期初始化，回调使用其已有 IO owner；没有新增通用驱动抽象、组件安装或对其他 ILI9341 板的批量替换。

## 改动范围

修改 `main/waveshare_spi_display.cpp` 的 Quandong 编译分支、CMake 中该板的组件依赖和该板 manifest，registry 仅更新该 profile 的实现说明。新增构建／核对脚本和来源记录。其他 main 文件，包括音频、完整唤醒、自然插话与媒体人工决策、绑定、配网和双 OTA，按改动前摘要保留。

单 ES8311 仍为 24kHz、I2C0 SDA16／SCL15、I2S0 MCLK4／BCLK5／WS7／DIN6／DOUT8，PA1 低有效且检查实际静音回调，BOOT0 沿通用按住说话／卡片交互。没有添加固定配置宏中未实例化的 PCA9557／ES7210、触摸或 LED。

## 实际执行

命令、退出码、源码输入及产物摘要在 `evidence/quandong-sdk-panel-20261112/`，完整汇总为 `final-audit.json`：

| 实际执行 | 结果与边界 |
| --- | --- |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=quandong-s3-dev build` | 首次真实目标编译链接退出0，2215步；应用2249152字节、模型2203819字节；267项当前输入、24份产物快照 |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=alientek-atk-dnesp32s3-box2-wifi reconfigure build` | 共享显示文件回归退出0；应用2236352字节、模型2203819字节；267项当前输入、24份产物快照 |
| 镜像／符号／分区核对 | 实际readelf／nm／size、esptool和分区解码命令全部退出0；两板均为双3MiB OTA，项目身份、checksum／validation hash通过；不代表可启动或实机验收 |

Quandong ELF保留真实板宏、ES8311、AEC、原自定义唤醒、HTTP／WS和新的初始化／睡眠回调；没有链接外部ILI9341工厂或使用替代板宏。应用槽剩896576字节。两板实际nm均保留四个SDK强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`；与前批来源核对的限制一致，命令和SDK来源快照在 `sdk-link-limitations.json`。已有LVGL弃用警告保留，没有扩大到UI调整。

```text
python -B evidence/quandong-sdk-panel-20261112/build_cached.py
python -B evidence/quandong-sdk-panel-20261112/build_native.py
python -B evidence/quandong-sdk-panel-20261112/collect_build.py
python -B evidence/quandong-sdk-panel-20261112/collect_native.py
python -B evidence/quandong-sdk-panel-20261112/inspect_sdk_limits.py
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/quandong-sdk-panel-20261112/direct-dependencies.json
python -B evidence/quandong-sdk-panel-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/quandong-sdk-panel-20261112/finalize.py
```

新 Quandong stage 为 `I:/xy_board_builds_20261109/quandong_sdk_panel_20261112`，使用既有 SDK 5.5.6-dirty、Xtensa14.2.0、17 份现有本地组件 override 和 Python 外网保护；原 manifest 与生成的 override manifest 分开保存。没有下载或安装组件。IDF 日志显示它更新共享 `C:/Espressif/official-5.5.6/root_managed_components/idf5.5/dependencies.lock` 的缓存解析元数据，本批不将构建描述为文件写入完全局限在 stage，也没有修改 SDK 驱动源码。

另实际执行 `firmware_dependency_report.py`，输出本批新 JSON：153 本地／152 官方，969 条直接依赖声明，68 本地／67 官方满足直接条件；缺组件声明从164减为163。其余直接条件分类不变。准备工具实际暂存 Quandong 退出0，与当前 manifest 和全部工作区 main 摘要匹配；这不是干净环境安装证明。

## 未完成

只有现有微雪1.8可实机验收。Quandong 的实际控制器修订、Flash／PSRAM、19条初始化和gamma／颜色／方向、PWM／PA、24kHz音频、AEC、唤醒与自然插话、媒体、BOOT、配网／绑定、同板 OTA 仍未验证。SDK 固有强制 UND 和构建警告继续如实记录，不宣称可启动或干净发行构建通过。

原缺依赖预检和旧 EYE 回归记录保留为历史证据。当前缓存整板构建、直接依赖就绪、运行和实机验收分别记录；本批不运行测试套件、类型检查或服务，不安装依赖、写 Git、刷机、OTA、上传或发布。

本板当前依赖状态从历史 `blocked_missing_local_sources` 更新为 `resolved_local_overrides`，具体依据是本批实际本地依赖解析、完整构建和产物来源；不表示干净安装。当前152／188官方源码入口、153本地profile、36待源码。账本2 passed_cached_sources／65 stale_cached_sources／6 failed_cached_sources／115 not_run，当前通过项为Quandong与BOX2；Sensair因整个main输入集合变化成为stale，保留其历史成功记录。累计67个官方变体曾缓存构建通过，188项本轮实机全部not_run。
