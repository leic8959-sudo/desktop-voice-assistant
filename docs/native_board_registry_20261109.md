# 全量适配基础：统一原生板型登记

后续金属版离散键接入见[本轮记录](xingzhi_metal_adaptation_20261109.md)：28个本地profile对应27个官方变体，161个待源码；以下保留本页对应阶段的数量和证据。

最新星智阶段见[ABS2.0／NV3023方块记录](xingzhi_adaptation_20261109.md)：27个本地profile对应26个官方变体、162个待源码；以下保留本页对应阶段的数量和证据。

最新TFT进展见[本轮记录](tft154_adaptation_20261109.md)：25个本地profile对应24个官方变体，164个待源码，10个当前缓存构建通过；以下保留本页对应阶段的数量和证据。

最新阶段见[1.85系列源码记录](waveshare_st77916_adaptation_20261109.md)：21个本地profile对应20个官方变体，168个待源码；新增三板缺驱动未编译。AMOLED1.75C已依据独立厂家页更正32MB容量。以下保留本页对应阶段的数量和证据。

最新阶段见[1.46／0.85及数字音频记录](waveshare_digital_adaptation_20261109.md)：18个本地profile对应17个官方变体，171个待源码，6个当前缓存构建通过；以下保留本页对应阶段的数量和证据。

后续LCD进展见[1.54／1.83板级记录](waveshare_lcd_adaptation_20261109.md)：当前16个本地profile对应15个官方变体、173个待源码。以下保留本页对应阶段的数量和结果。

最新B2进展见[微雪1.32／2.06／2.16源码记录](waveshare_b2_adaptation_20261109.md)：当前14个本地profile对应13个官方变体，175个仍待源码；新三板缺BSP未编译。以下保留本页对应阶段的数量和构建记录。

后续进展：两款T-Circle现已增加实际源码入口，当前11个本地profile对应10个官方变体，178个待适配；32kHz PDM已有板型实例化。最新构建／实机边界见[T-Circle适配记录](tcircle_adaptation_20261109.md)。本页以下保留上一阶段的实现与执行记录，其中8／180及“尚未实例化”等描述是该阶段状态。

后续历史CO5300入口已在隔离的IDF5.5.6-dirty环境完成[缓存构建](cached_firmware_build_idf556_20261109.md)，实际用到了本登记表和CMake读取器；只证明该入口本次配置／编译路径，不覆盖其他profile、PowerShell包装器或实机。本页保留登记表刚收拢时的执行边界。

2026-11-09，Asia/Shanghai。继续全量板型目标，消除现有原生入口在多个工具中的重复硬编码身份映射，同时补齐两款T-Circle的固定源码调查。**本轮没有新增已完成板型**：仍是9个本地入口（含1个历史兼容名），对应8个官方发布变体，180个发布变体尚无源码入口。

## 为什么需要统一

此前每增加一块板，要同时改顶层CMake应用名、main组件BSP／宏、PowerShell构建白名单、内嵌Python产物映射、烧录choices／项目名、批次白名单、矩阵映射。遗漏其中一处可能导致源码存在却不能构建，或产物身份与板型错配。

现在仅在 `firmware/assistant_espidf/native_board_profiles.json` 登记原生身份，包括profile ID、应用名、芯片目标、BSP组件、实现宏、上游变体、历史兼容标记和QSPI补丁分支。以下消费者读取同一文件：

| 消费者 | 读取后的用途 |
| --- | --- |
| 顶层CMake、main/CMake | `board_profiles.cmake`选择应用名、BSP及编译宏；移除重复if链 |
| 单板构建脚本 | `tools/board_profiles.ps1`校验板型ID／唯一应用名及映射，构建前确认所选manifest与默认配置存在，再选择芯片与补丁；不从188项库存生成硬件实现 |
| 产物导出 | 在构建volume读取同一JSON，核对实际`project_name`后输出board/project_name/target标记；历史根目录与新板子目录规则保持 |
| 烧录脚本 | `tools/board_profiles.py`提供choices和预期应用身份，继续检查备份、chip、范围、NVS与产物标记 |
| 批次入口 | 核对每个已登记源码项的native_profile确实对应当前full_name；过时／错配矩阵在启动任何构建前拒绝 |
| 上游矩阵整理 | 同一Python读取器导出8个上游映射及历史兼容项，并记录登记表SHA256；不再维护另一份SOURCE_PROFILES常量 |

9个现有应用名保持为 `assistant_espidf`、`assistant_box3`、`assistant_amoled175`、`assistant_cores3`、`assistant_amoled175c`、`assistant_sh8601`、`assistant_amoled18v2`、`assistant_box`、`assistant_boxlite`。没有因重构而修改它们的板级引脚、原始依赖版本、应用版本、备份或运行设备。

## 失败边界与构建输入

- 登记表只放本项目已有源码入口。188项的上游库存仍独立保存；只在库存加一行不能进入原生构建。
- ID、应用身份、目标、实现宏及上游映射在读取边界校验，应用身份和上游映射不允许重复。当前原生引擎仍只实现ESP32-S3，其他芯片继续留待独立移植，不能只改target字段就当完成。
- `board_profile.h`对未知实现宏明确编译报错，不再默默采用1.8默认BSP。新增真实板型仍需写BSP／音频／显示等实现及专用依赖文件。
- 构建volume同步时复制登记JSON和CMake读取器；`.assistant-board`标记供主配置及IDF组件依赖预展开读取，同一volume的板型与命令行选择不一致就拒绝，保留各板的独立构建目录。
- CMake配置依赖包含该JSON，避免修改登记后增量构建继续沿用旧身份。由于采用`string(JSON ...)`，本工程最低CMake版本调整为3.19。[官方IDF v5.5.2工具清单](https://github.com/espressif/esp-idf/blob/v5.5.2/tools/tools.json)本轮只读确认推荐CMake **3.30.2**，满足该要求；本轮没有安装／升级本机CMake。
- Python读取器同时支持直接脚本与包内调用；PowerShell工具先读登记表，再检查Docker或产生构建目录。内嵌导出脚本读取构建volume内的同一文件，不持有第二份项目名列表。

这些是源码约束和静态审查结果，不能据它们推断真实IDF配置、依赖预展开、增量构建、异常场景或烧录行为已测试通过。

## T-Circle新增调查结果

两款板在固定上游提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 均为160×160圆屏，触摸CST816D使用SDA11／SCL14／IRQ12；LCD使用MOSI17／SCLK15／DC16／CS13／背光18，两个修订不是466×466圆屏的直接别名。

- V1.0：麦克风BCLK7／WS9／DATA8，独立于MAX98357A扬声器的BCLK5／LRCLK4／DATA6／使能45；需要独立I2S采集、输出、PCM／通道处理、软件音量及功放时序。
- V1.1：麦克风变为PDM CLK9／DATA8，采集32kHz，扬声器仍24kHz。固定README明确说明PDM时钟最低要求，16kHz采集会让该麦克风不能正常工作；不能直接复用当前24→16kHz FIR。硬件参考、软件参考和输入通道不能混为一谈。
- [固定V1.0 pin_config.h](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/lilygo/t-circle-s3/pin_config.h)头部写有 `License: GPL 3.0`；原厂仓库API报告GPL-3.0。不能把整个小智仓库顶层MIT推定为该文件的许可，也不能未经核对给其他借用驱动贴MIT标签。电气引脚事实与纳入GPL代码的分发条件分别处理，整体项目许可证仍未选择。
- GC9D01N实现在该固定提交的板型目录中找到 `.c`／`.h`，文件本身未声明SPDX／版权头；其来源及适用许可仍待核对，不能因为源码可读就直接冒称可自由再许可。查询 `espressif/esp_lcd_gc9d01n`、`lilygo/t_circle_s3` 组件路径返回404，只说明这些路径未找到，不证明不存在其他可用实现。

上述事实已写入 `board_research_v2.5.2.json`，两款T-Circle继续保持 `inventory_only`；本轮没有复制原厂GPL文件或GC9D01N驱动，也没有写占位BSP来抬高完成数量。

## 实际执行与仍未完成

执行了本地只读源码／Git状态读取、官方公开元数据／固定源码HTTP读取，并运行 `python -B tools/sync_board_matrix.py --source-directory <此前下载的公开元数据缓存>` 进行实际矩阵整理：退出0，154目录／188变体／170配置项不变，登记为8个官方变体源码入口及1个历史兼容入口，记录登记表摘要。该命令使用了新的Python读取器，但它不是固件构建、类型检查或测试套件。

编辑后查看diff及空白检查；新增未跟踪文件单独与空文件比较，不为此暂存。没有执行CMake、PowerShell构建／烧录／批次脚本、安装／解析依赖、测试、类型检查、启动服务、Git写入、上传、发布或任何设备／账号操作；没有把此前不可用Docker环境推测为当前已恢复。

下一步仍需两款T-Circle的小圆屏与独立音频真实实现，以及全部候选的依赖解析、构建和实机验收。统一登记只减少全量适配的配置遗漏，不缩小目标或替代这些要求。
