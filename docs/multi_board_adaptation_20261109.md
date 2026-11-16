# 首批多板型固件源码适配

后续更新：BOX-3已接入本地ST7789/ILI9341及GT911/TT21100分支并首次整板缓存构建通过；1.75共享回归及2.16方向修正重建通过，实机未验收。见[本批说明](box3_native_20261113.md)。

后续更新：1.75及1.75C已按各自14命令、复位/音频引脚、16/32MB及SD差异接入共享本地CO5300/LVGL/CST9217路径，两款首次整板缓存构建与2.16共享回归通过，实机未验收。见[本批说明](amoled175_family_native_20261113.md)。

后续金属版离散键接入见[本轮记录](xingzhi_metal_adaptation_20261109.md)：28个本地profile对应27个官方变体，161个待源码；以下保留本页对应阶段的数量和证据。

最新星智阶段见[ABS2.0／NV3023方块记录](xingzhi_adaptation_20261109.md)：27个本地profile对应26个官方变体、162个待源码；以下保留本页对应阶段的数量和证据。

最新TFT进展见[本轮记录](tft154_adaptation_20261109.md)：25个本地profile对应24个官方变体，164个待源码，10个当前缓存构建通过；以下保留本页对应阶段的数量和证据。

最新阶段见[1.85系列源码记录](waveshare_st77916_adaptation_20261109.md)：21个本地profile对应20个官方变体，168个待源码；新增三板缺驱动未编译。AMOLED1.75C已依据独立厂家页更正32MB容量。以下保留本页对应阶段的数量和证据。

最新阶段见[1.46／0.85及数字音频记录](waveshare_digital_adaptation_20261109.md)：18个本地profile对应17个官方变体，171个待源码，6个当前缓存构建通过；以下保留本页对应阶段的数量和证据。

后续LCD进展见[1.54／1.83板级记录](waveshare_lcd_adaptation_20261109.md)：当前16个本地profile对应15个官方变体、173个待源码。以下保留本页对应阶段的数量和结果。

最新B2进展见[微雪1.32／2.06／2.16源码记录](waveshare_b2_adaptation_20261109.md)：当前14个本地profile对应13个官方变体，175个仍待源码；新三板缺BSP未编译。以下保留本页对应阶段的数量和构建记录。

后续进展：两款T-Circle现已增加实际源码入口，当前11个本地profile对应10个官方变体，178个待适配；32kHz PDM已有板型实例化。最新构建／实机边界见[T-Circle适配记录](tcircle_adaptation_20261109.md)。本页以下保留上一阶段的实现与执行记录，其中8／180及“尚未实例化”等描述是该阶段状态。

2026-11-09，Asia/Shanghai。牛牛确认先适配 ESP32-S3-BOX-3、微雪 ESP32-S3-Touch-AMOLED-1.75，并保留现有微雪1.8。本轮完成源码候选及构建／烧录入口修改，未安装依赖、构建、测试、类型检查、刷机、OTA或实机验收。

本轮追加全量范围登记：官方 `78/xiaozhi-esp32` `v2.5.2` 的 README 声明138个目录／171个变体，但该版本实际发布清单为 **154个目录／188个变体**；固定源码另有170个 `BOARD_TYPE` 和156个含 `config.json` 的目录。此前138／171的回答只引用了README，不能作为实际发行总数。全部188个发布变体及170个配置选项均纳入 [`board_matrix_v2.5.2.json`](../firmware/assistant_espidf/board_matrix_v2.5.2.json)，2个仅在源码出现、未进入该发布包的目录另列。详见[全量批次执行记录](all_boards_adaptation_20261109.md)。矩阵中的 `source_profile_present` 仅表示本仓库已有板型分支；`inventory_only` 表示已登记但尚未接入本项目构建，不代表已经适配。

## 当前范围与状态

| `-Board`／`--board` | 屏幕／布局 | 板级组件 | 本轮状态 |
| --- | --- | --- | --- |
| `waveshare-amoled-1.8`（旧默认名） | 368×448，原竖屏布局 | `waveshare/esp32_s3_touch_amoled_1_8` 2.0.3，CO5300 | 历史兼容入口，不能视为小智SH8601原版支持；保留原行为，新板使用下面明确屏控的入口 |
| `esp32-s3-box-3` | 320×240，紧凑横屏布局 | `espressif/esp-box-3` 3.2.1 | 新增源码候选；依赖解析、编译及实机均待验收，不自动等同BOX、BOX-Lite等其他板型 |
| `waveshare-amoled-1.75` | 466×466，圆屏安全区域 | `waveshare/esp32_s3_touch_amoled_1_75` 3.0.1 | 新增源码候选，按该版本CO5300／CST9217接口编写；不同屏控／硬件修订需先核对，不能承诺SH8601版本直接兼容 |
| `m5stack-core-s3` | 320×240，横屏＋触摸按住说话 | `espressif/m5stack_core_s3` 3.0.2 | 后续B1推进新增源码候选；AW88298输出／ES7210输入由官方BSP提供；QUAD PSRAM，GPIO0不作按键；未解析依赖、构建或实机验收 |
| `waveshare-amoled-1.75c` | 466×466，圆屏安全区域 | `waveshare/esp32_s3_touch_amoled_1_75c` 3.0.0 | C专用MCLK16／LCD复位1／触摸复位2，ES7210＋ES8311，adapter锁按ESP_OK；无SD接口，不启用本地快照缓存；源码候选 |
| `waveshare-amoled-1.8-sh8601` | 368×448，竖屏 | `waveshare/esp32_s3_touch_amoled_1_8` 1.1.4 | 对应官方原版SH8601／FT5x06；独立QSPI补丁分支及应用名，未构建／实机验收 |
| `waveshare-amoled-1.8-v2` | 368×448，竖屏 | `waveshare/esp32_s3_touch_amoled_1_8` 2.0.3 | 对应CO5300 v2；触摸探测与gap由固定BSP处理，独立应用名与产物目录，未构建／实机验收 |
| `esp32-s3-box` | 320×240，横屏触摸 | `espressif/esp-box` 3.1.0~2，codec1.1.0 | 旧BOX专用依赖；ES7210输入／ES8311输出；无SD，不视为BOX-3别名；源码候选 |
| `esp32-s3-box-lite` | 320×240，横屏无触摸 | `espressif/esp-box-lite` 2.1.0~2，codec1.1.0 | ES7243E输入／ES8156输出；无SD；卡片按BOOT短按选项、长按松手确认；源码候选 |

原首批之后，B1推进补上CoreS3、三种明确微雪修订及旧BOX／BOX-Lite入口。共9个本地入口，其中1个是保留的旧兼容名，其余8个分别对应8个官方发布变体；共用现有对话协议、配对、提醒、点云、软件AEC和唤醒流程。相近名称不代表不同硬件可互刷。

当前发布变体矩阵为 **8 个 `source_profile_present`、180 个 `inventory_only`**。此前把旧“1.8”入口计入上游SH8601原版不准确；现已改为专用SH8601入口，旧兼容名不重复计入。源码入口、可构建和实机验收分别记录，不把任何一项推断为另两项。

## 修改内容

- `firmware/assistant_espidf/main/board_profile.h`：选择唯一BSP头文件，声明板型、分辨率、屏幕形状、输入／输出编解码器关系、按键；统一显示锁的成功语义。1.75 BSP返回`esp_err_t`，`ESP_OK==0`，不能按其他BSP的布尔值处理。
- `main/display_layout.h`、`main/display_ui.cpp`：分开三种布局，调整状态文字、配对页、提醒／计时／信息卡、按钮与导航箭头；横屏缩小点云画布并按比例映射粒子与图标坐标，字体保持16px。圆屏的文字／按钮采用内收区域。启动时核对BSP实际报告的分辨率，异常明确停止。
- `main/audio_io.cpp`：1.8保持ES8311同一收放句柄；新板通过BSP分别初始化ES7210输入和ES8311输出，增益只作用于输入、音量和静音只作用于输出。新板采用双I2S槽、选择第0槽，向现有流程提供单声道，不宣称双麦波束成形、硬件回采或自然双讲已完成。
- 1.8的电源寄存器写入和显示扩展器复位均限制在1.8编译分支；新板按选定BSP初始化，不向BOX-3写入1.8的AXP2101寄存器，不照搬1.8显示复位掩码。新板电源／音频状态仍需用实物核验。
- `main/main.cpp`、`main/storage.cpp`、`main/control_protocol.cpp`：按板型选择按键和BSP存储接口；hello增加`board`、`display.width`、`display.height`、`display.round`。电池状态继续为unknown，不制造电量读数。现有“完整唤醒词可暂停媒体，普通回答语音打断关闭”的人工决定未修改。
- 顶层及main的CMake：按板型选择BSP和编译宏；分别使用`assistant_espidf`、`assistant_box3`、`assistant_amoled175`应用名。`main/ota_update.cpp`在选择启动分区前核对待升级镜像和当前应用的`project_name`，不因SHA正确就接受其他板型固件。
- `boards/<板型>/idf_component.yml`、`sdkconfig.defaults`：新增板型独立直接依赖与默认配置。1.75使用`esp_lvgl_adapter` 0.6.0，BOX-3使用`esp_lvgl_port` 2.9.0；不把两套显示任务装进同一板型。保留新BSP自身的显示缓冲，不对其应用旧板的DMA缓冲替换。禁止SD／SPIFFS挂载失败后自动格式化。
- `tools/build_firmware.ps1`：新增`-Board`，隔离默认volume／产物目录、检查volume板型标记；新板在构建volume中替换manifest及合并默认配置，不改写工作区主manifest。1.8把原锁文件作为构建输入；新板后续真实解析生成的锁导出到自己的产物目录，本轮没有伪造传递依赖锁。构建配置和锁不再回写工作区根文件。
- `tools/flash_native.py`：新增`--board`，读取对应目录及`board-target.json`，新板缺少或不匹配标记就拒绝。保留完整16MB备份、写入范围与不覆盖NVS检查；烧录日志按板型分开。脚本不能自动识别连接实物的整板型号，执行前仍须核对实物和备份。

## 构建入口（本轮没有执行）

依赖和Docker镜像由牛牛准备；当前脚本要求已有`espressif/idf:v5.5.2`，同步源码还使用`ubuntu:24.04`。后续执行构建会解析／下载组件并写构建目录，不属于本轮已验证结果。普通浏览器用户不需要构建硬件固件。

从仓库根目录分别构建，不能同时让同一个volume服务两块板：

```powershell
# 默认现有板型
powershell -File tools/build_firmware.ps1 -Board waveshare-amoled-1.8
# BOX-3
powershell -File tools/build_firmware.ps1 -Board esp32-s3-box-3
# 微雪1.75，先核对实际屏幕控制器与硬件修订
powershell -File tools/build_firmware.ps1 -Board waveshare-amoled-1.75
# M5Stack CoreS3，源码候选，先准备依赖与实物验收环境
powershell -File tools/build_firmware.ps1 -Board m5stack-core-s3
```

| 板型 | 默认构建volume | 产物目录 |
| --- | --- | --- |
| 1.8 | `assistant-idf-sr247-v1` | `firmware/assistant_espidf/build/` |
| BOX-3 | `assistant-idf-esp32-s3-box-3-v1` | `firmware/assistant_espidf/build/esp32-s3-box-3/` |
| 1.75 | `assistant-idf-waveshare-amoled-1.75-v1` | `firmware/assistant_espidf/build/waveshare-amoled-1.75/` |
| CoreS3 | `assistant-idf-m5stack-core-s3-v1` | `firmware/assistant_espidf/build/m5stack-core-s3/` |
| 1.75C | `assistant-idf-waveshare-amoled-1.75c-v1` | `firmware/assistant_espidf/build/waveshare-amoled-1.75c/` |
| 1.8 SH8601 | `assistant-idf-waveshare-amoled-1.8-sh8601-v1` | `firmware/assistant_espidf/build/waveshare-amoled-1.8-sh8601/` |
| 1.8-v2 CO5300 | `assistant-idf-waveshare-amoled-1.8-v2-v1` | `firmware/assistant_espidf/build/waveshare-amoled-1.8-v2/` |
| BOX | `assistant-idf-esp32-s3-box-v1` | `firmware/assistant_espidf/build/esp32-s3-box/` |
| BOX-Lite | `assistant-idf-esp32-s3-box-lite-v1` | `firmware/assistant_espidf/build/esp32-s3-box-lite/` |

每份产物应包含该目录自己的`flasher_args.json`、镜像、`project_description.json`、`board-target.json`、`sdkconfig`和解析后的`dependencies.lock`，后续构建后再核对与冻结。新板首次依赖解析仍有传递版本漂移风险，不能称为完整可复现构建。

手动在原工程直接加`idf.py -DASSISTANT_BOARD=...`并不会替换默认1.8依赖清单；新增板型应使用上述入口，不要只改一个CMake参数。`main/version.txt`仍为源码候选版本0.3.21，本轮未生成新发行版本或修改历史运行记录。

新增1.75C／SH8601／v2的固定来源、屏控身份更正和对应 `-Board` 命令见[微雪修订版适配记录](waveshare_revisions_adaptation_20261109.md)。上表是产物规划目录，不表示目录中已经有本轮构建结果。

旧BOX与BOX-Lite的codec版本隔离、单键卡片输入和未验证采样率见[BOX系列适配记录](box_family_adaptation_20261109.md)。旧BSP不再由上游维护，不因此从全量范围删去，也不能据兼容声明推断当前SDK下已经可编译。

本轮不提供实际串口烧录指令：应在后续编译及分区检查完成、明确获准刷机且持有该实物完整备份后，才使用`flash_native.py --board <板型>`及本人确认的串口／备份路径。所有新板均按16MB Flash工程处理，不适用于容量不符的模块。

## 后续验收顺序与限制

1. 先在全新且互不混用的volume解析和构建三个板型，记录组件锁、SDK配置、产物摘要、应用名、实际屏控和模型分区；检查新板3MB OTA分区余量。SDK／驱动API兼容性目前只做静态对照，不能当作编译通过。
2. 对各实物单独核对型号／修订、恢复备份、电源、屏幕方向、触摸坐标、BOOT按键、配网与配对；SD卡及可选底座缺失不得变成数据丢失。BOX-3的扩展／SD能力取决于底座，未假定基础套装都有可用卡槽。
3. 分别验收新板麦克风槽映射、采样率、增益、扬声器输出、软件参考延迟、回声及唤醒。当前7ms参考提前量、30／12dB增益沿用既有方案，新板尚未做声学校准；不要承诺原板效果能自动迁移。
4. 检查长中文、配对码、两种提醒按钮、计时卡、离线状态和圆屏边缘；几何布局已编写，不等于已经看过真实屏幕或确认触摸命中。
5. 真实网关连接和一次真实模型对话、到时提醒／本人确认分别验收；第三方账号、模型费用和家居动作须另有明确范围。
6. 固件初刷和同板OTA／失败回滚分别验收；跨板镜像即使摘要正确也不得选择启动。新增应用名检查只有刷入本轮代码后才生效，旧运行固件不会自动获得保护。本轮没有修改Python网关按板型选择OTA资源的流程，不支持据hello字段自动群发不同固件；不得向旧设备推送新板镜像。

## 全量适配批次

全量工作按能力和风险分批，不按厂商目录逐个复制：

| 批次 | 范围 | 主要新增工作 | 退出条件 |
| --- | --- | --- | --- |
| B0 | 154个发布目录／188个发布变体、170个 `BOARD_TYPE`、2个仅源码目录 | 固定提交、发布清单与源码路径、芯片目标及批次；能力未经查证用null，名称仅用于排队 | 发布清单数量与条目一致、无重复变体，全部配置选项映射至源码目录；不强行让过时README计数等于发布数 |
| B1 | 已有三板及同协议同BSP近邻 | 1.8、1.8-v2、1.75／1.75C、BOX／BOX-Lite／BOX-3、CoreS3、T-Circle-S3等逐板建立BSP profile；不把相似外观视为兼容 | 每板独立依赖锁、构建产物、分辨率／触摸／音频 smoke 记录 |
| B2 | 常规有屏＋音频板 | 其他 S3 LCD／AMOLED／TFT、LILYGO、M5Stack、DFRobot等；增加布局族、旋转、字体覆盖、触摸和音频 codec 适配 | 每板至少有屏幕、触摸、麦克风、扬声器、配网、提醒显示验收 |
| B3 | 无屏／音频专用板 | 把显示层变成可选能力；保留配对、唤醒、对话、提醒音；禁止调用屏幕 API | 无屏构建不链接 LVGL 显示依赖；语音、按键、升级和错误反馈独立通过 |
| B4 | C3／C5／C6／P4／S31 及电子纸 | 独立 IDF／ESP-SR／模型／分区与功耗约束；不把 S3 的 PSRAM、模型和 DMA 假设带过去 | 每个芯片族至少一块参考板完整构建和启动；其余同族板需逐板 pin／屏控核对 |
| B5 | 摄像头、4G／ML307、机器人、传感器和特殊执行器 | 明确是否属于小燕子终端；拆分摄像头／蜂窝／运动控制能力，不把普通语音固件冒充完整设备适配 | 设备专属功能合同、权限、功耗、升级和实机验收齐全；否则保持实验／未适配 |

B0 之后仍须按板型实际硬件取得构建输入；不能仅根据小智有目录就自动生成本项目固件。小智的目录包含不同产品目标和专用功能，本项目只复用其板级资料和协议研究，不复制其服务器、账号体系或特殊设备业务。

## 固定版本官方来源

本轮只读官方版本页及固定提交的头文件／实现，不下载、安装组件包：

- BOX-3 BSP 3.2.1：[官方组件页](https://components.espressif.com/components/espressif/esp-box-3/versions/3.2.1/readme)，[固定源码](https://github.com/espressif/esp-bsp/tree/c9a0f385cd1bf86e1bd07f14dc2cb77b86f20a9a/bsp/esp-box-3)，[Apache-2.0许可](https://components.espressif.com/components/espressif/esp-box-3/versions/3.2.1/license)。
- 微雪1.75 BSP 3.0.1：[官方组件页](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1/readme)，[固定源码](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/bfeb6e6d5737178cdde78b630c8118074da0a657/bsp/esp32_s3_touch_amoled_1_75)，[Apache-2.0许可](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1/license)。该版本README仍有历史`^1`说明，不能仅靠该行认定所有硬件修订兼容，需核对具体控制器。
- esp_lvgl_adapter 0.6.0：[官方组件页](https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.6.0/readme)，[固定源码](https://github.com/espressif/esp-iot-solution/tree/0ac5319a9862bb6742980bbaa51f20b6a6f3bd00/components/display/tools/esp_lvgl_adapter)，[Apache-2.0许可](https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.6.0/license)。本轮核对了`esp_lv_adapter_lock`返回`esp_err_t`的接口。

其他字体、ESP-SR及传递依赖沿用各自许可，见[此前许可初表](third_party_inventory_20261109.md)，新增实际产物仍需补齐SBOM与通知；本轮未决定整体LICENSE。

## 实际审查与交付边界

实际使用`git status --short`、`rg --files`／`rg -n`、`Get-Content`读取规则、源码和差异；`Invoke-WebRequest -TimeoutSec 20 -UseBasicParsing`／`Invoke-RestMethod`只读官方版本资料；编辑后使用`git diff`、`git diff --stat`和`git diff --check`审查。它们不构成编译／类型检查或行为测试。未执行被读取的构建／烧录脚本。

已跟踪文件的`git diff --check`返回0；新增文件分别以`git diff --no-index --check -- NUL <路径>`检查，没有报告空白错误，返回1表示与空文件存在新增差异。Git出现LF将转换为CRLF提示，未因此改写整仓库换行。

新增的板型清单、头文件及本说明为未跟踪文件，须用单文件空文件diff查看，不能仅用普通`git diff`推断全部新增内容。此前两份开源评估文档保留，本轮未覆盖它们。没有Git暂存／提交／推送、依赖安装、数据库／网关／账号操作、固件发布或设备状态变更。
