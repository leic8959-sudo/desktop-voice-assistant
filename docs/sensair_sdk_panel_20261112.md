# Sensair C5 复用 SDK 屏幕操作与首次整板构建

本批只调整已登记 Sensair C5 的屏幕驱动接入，使用本机 SDK 的 ST7789 操作表和本板初始化回调，移除该板对未安装 ILI9341 组件的依赖。其他使用 ILI9341 的板型不受此变更影响。官方源码入口数量不变，没有以替代板配置或空入口组件工程登记通过。

## 源码依据和改动

固定小智 v2.5.2 板源码把控制器声明为 ST7789，但使用 ILI9341 公共 API 传入 19 条自定义命令。此前候选照此接入，现有缓存缺 ILI9341 2.0.2，无法进行整板构建。本批对已经归档的 2.0.2 驱动和本机 SDK 5.5.6 ST7789 实现逐项核对了实际使用的操作：

| 路径 | 本批接入 |
| --- | --- |
| reset | 复用 SDK 软件复位 0x01、等待 20ms，与旧适配器相同 |
| init | 替换公共 panel 接口的 init 回调；保留旧适配器的 0x11／100ms、MADCTL=0、COLMOD=0x55 前导，再逐条发送原 19 条命令及延时，包括表内 COLMOD=0x05 |
| 窗口／绘图 | 复用 SDK 的 gap、0x2a／0x2b 末坐标减一、0x2c 和 16 位像素字节数；SDK 会传播 color TX 错误 |
| 旋转／反色／开关 | 复用同样的 MADCTL 位和 0x20／0x21、0x28／0x29；RGB 初始缓存为零，固定表同样写零，编译期约束这项关系 |

没有调用 SDK 默认 init，因为它额外写 RAMCTRL，不能把默认初始化混入原命令序列。没有安装驱动或改 SDK 私有结构；通过公开 `esp_lcd_panel_t` 接口设置初始化回调，只服务已有单面板的板级生命周期。该接入仍依赖已核对的 SDK 行为，升级 SDK 后需要重新核对。

屏幕仍为 284×240 RGB565、SPI2 mode0／40MHz、MOSI23／CLK24／DC26／CS25、无复位 GPIO、gap36／0、mirrorY、swapXY、反色、双 20 行 DMA。命令表保持原文；没有改变触摸、PA、ADC1 channel5／16kHz、24kHz 差分 PDM、固定“你好小智”模型或双 OTA。

实际修改 `main/sensair_board.cpp`、CMake 的 Sensair 分支、该板组件清单和 registry 内该板的实现说明；未新增公共封装或通用配置。两驱动源码和命令表快照／摘要、逐项比较记录在 `evidence/sensair-sdk-panel-20261112/driver-contract-review.json`。这些是源码证据，没有执行 SPI 命令录制或显示验收。

## 验证和边界

实际命令、退出码、源码与产物摘要保存在同批 evidence，汇总至 `final-audit.json`：

| 实际构建／核对 | 结果 |
| --- | --- |
| `idf.py -DIDF_TARGET=esp32c5 -DASSISTANT_BOARD=espressif-esp-sensairshuttle build` | 退出 0，2226 步构建完成；应用 2492640 字节、模型 125951 字节，双 3 MiB 应用槽内剩 653088 字节；267 项当前源码／配置摘要、23 份产物快照 |
| `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=alientek-atk-dnesp32s3-box2-wifi reconfigure build` | 最终退出 0；应用 2236320 字节、模型 2203819 字节；267 项当前输入摘要、24 份产物快照 |
| 两板镜像／ELF／分区核对 | 实际 readelf、nm、size、esptool、分区解码命令全部退出 0；镜像项目身份、checksum／validation hash、双 OTA 核对通过；不是启动或物理验收 |

Sensair 最终 ELF 保留真实 `main.cpp`、板级 init、ADC owner、PDM／codec、固定词、AEC、HTTP／WS 等路径；没有空 app_main 或使用 Spot 宏代替本板。固定模型 SHA256 为 `fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b`。源码静态占用为 DRAM data 24064、BSS 103728 字节，参考缓存对象仍为 20496 字节；这些数字不代表运行堆／栈峰值。

```text
python -B evidence/sensair-sdk-panel-20261112/build_cached.py
python -B evidence/sensair-sdk-panel-20261112/collect_build.py
python -B evidence/sensair-sdk-panel-20261112/preflight/run_preflight.py
python -B evidence/sensair-sdk-panel-20261112/build_native.py
python -B evidence/sensair-sdk-panel-20261112/collect_native.py
python -B evidence/sensair-sdk-panel-20261112/inspect_sdk_limits.py
python -B evidence/sensair-sdk-panel-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/sensair-sdk-panel-20261112/finalize.py
```

构建使用新隔离目录 `I:/xy_board_builds_20261109/sensair_sdk_panel_20261112`、既有 SDK 5.5.6-dirty／RISC-V 14.2.0、16 份本地组件 override 和 Python 外网保护。没有下载或安装构建依赖。只读预检覆盖当前全部 153 个本地入口，区分官方 152 项及一个历史入口，不创建指定的预检 stage。

BOX2 复用原 stage，首轮构建成功后发现模板只同步 main、遗漏根 registry 内刚更新的说明文字，未据此生成通过记录。保留首轮日志／registry 并补齐根输入同步，重新 reconfigure/build 后逐项比较 stage 与工作区，最终记录只引用重跑产物。两个工程的原始组件 manifest 与生成的本地 override manifest 分别保存，不把改写的依赖输入当作原始锁文件。

| 官方入口的首个直接预检结果 | 数量 |
| --- | ---: |
| 通过现有直接缓存检查 | 66 |
| 缺直接组件 | 67 |
| 缺唯一 BSP | 11 |
| SDK 版本不满足 | 7 |
| 精确组件版本不符 | 1 |
| 合计 | 152 |

历史入口另有一项通过，全部本地共 67 项通过／86 项受阻，无意外失败，153 个指定预检目录均未创建。每项完整命令、退出码、第一项错误和日志摘要在 `preflight/results.json`。预检不验证传递解析、所有版本范围／条件依赖、API、链接、尺寸或实机状态，66 项不能读成新增 66 次构建通过。

本机 SDK 的强制链接符号限制继续如实保留：实际 nm 显示 Sensair 有 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app` 三个 UND，BOX2 另有 `start_app_other_cores`。四个名称均能在对应链接参数找到 `-u`；CXX 名来自异常关闭时的 CMake 强制项，coredump 名的定义受关闭配置保护，启动旧名与实际 `esp_startup_start_app` 名称差异仍在。实际命令、SDK 来源和配置记录见 `sdk-link-limitations.json`；没有修改 SDK 或把当前结果描述为零 UND、干净 SDK 发行构建或可启动验收。构建还保留原 `BSP_DISPLAY_LVGL_BUF_HEIGHT` 未知符号、gpio 配置缺省字段及 LVGL 弃用警告。

实际硬件控制器／修订、窗口和旋转、颜色、闪屏、触摸、ADC 同任务生命周期、PDM 反相信号和幅度、内存／任务栈、AEC、唤醒、自然插话、媒体及同板 OTA 仍需实机验收。已有 ADC owner 在清理超时或错误后要求重启，其完整 teardown／join 限制未在本批消除。当前只有微雪 1.8 实物，Sensair 不能由源码或构建结果代替物理验收。

未运行独立测试套件、类型检查或服务；未 Git 写入、刷机、OTA、上传或发布。

官方源码入口仍为 152／188，含历史入口共 153 个本地 profile，36 项待源码。当前构建账本 2 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／116 not_run；当前两项通过为 Sensair C5 和 BOX2 Wi-Fi，累计曾构建通过的官方变体增至 66。188 项本轮实机全部 not_run。
