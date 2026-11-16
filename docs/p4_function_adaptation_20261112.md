# P4功能评估板与P4X修订源码适配（2026-11-12）

本批新增 `espressif-esp32-p4-function-ev-board` 和 `espressif-esp32-p4x-function-ev-board` 两个独立源码候选，并把构建入口从仅S3扩展到明确实现的P4路径。**两个P4目标均缺BSP，尚未目标编译、链接或实机验收。**旧S3回归和单独模型打包不能证明P4已经可用。

## 板型、修订与来源

固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行清单154目录／188变体。P4族共有27个变体、6组源码目录；本批读取这6组的30份固定源码／配置，另25个P4变体保持inventory_only，没有套用功能评估板的引脚。

| 本地入口 | 芯片与库选择 | 独立应用名 |
| --- | --- | --- |
| espressif-esp32-p4-function-ev-board | P4旧修订，`SELECTS_REV_LESS_V3=y`，最低1.0 | assistant_p4func |
| espressif-esp32-p4x-function-ev-board | P4 v3+，最低3.0，使用对应新修订库 | assistant_p4xfunc |

SDK明确说明这两组芯片不兼容。当前SDK5.5.6与ESP-SR2.4.7分别按芯片修订选择库；旧修订在SDK6又有单独的库目录。两个应用身份和配置不得互换；芯片名相同不能证明整板或修订相同。

采用官方 `espressif/esp32_p4_function_ev_board 5.2.3` BSP，固定组件源码提交 `4b3f542031dfe2a0b955517f804a99734f2cf8bb`。这是经接口核对后为小燕子选择的BSP；固定小智原板使用自有初始化类，并非声明它使用相同BSP。公共网络组件精确声明 `esp_hosted 2.12.11` 与 `esp_wifi_remote 1.6.4`。参考包只归档于evidence，未安装、放入managed_components或链接到目标。

原板、BSP、网络／面板驱动、SDK与ESP-SR共521份参考资料的SHA256见 `evidence/p4-source-20261112/source-artifacts.json`。依赖完整解析未执行，特别是BSP所需video、触摸、面板和其他传递组件，不能仅凭直接版本声明认为已经兼容。

## 实现路径

显示复用官方BSP的1024×600 RGB565 EK79007、双通道MIPI DSI、GT911及XY镜像，背光26、复位27、MIPI PHY LDO3。I2C1 SDA7／SCL8由BSP统一管理，触摸与音频控制共享该总线。GPIO0作为固定原板真实BOOT键接入现有说话／卡片路径；不把其他板的GPIO0定义套过来。1024屏布局和触摸坐标仍需实际显示验收。

音频复用项目既有单ES8311引擎：I2S0、MCLK13、BCLK12、WS10、DIN11、DOUT9、PA53，输入／输出24kHz、共享一个BOTH编解码器句柄和PCM软件AEC。没有调用BSP默认22050Hz音频初始化，也没有同时调用其分别创建ES8311对象的speaker／microphone工厂。实际MCLK、输入通道、静音、增益、回声和并行收放音仍需实物核对。

P4没有本机Wi-Fi射频，单板配置明确选择C6、Hosted SDIO四线及功能评估板引脚预设。原有Wi-Fi AP／STA、HTTP绑定、WebSocket控制／PCM媒体和OTA通过Remote／Hosted组件接入。该组件的构造函数会初始化主机传输，Wi-Fi API转发到协处理器；C6必须已有匹配固件及正确连接。未刷写或升级C6，协处理器启动失败、复位和重连路径尚无运行证据。

BSP提供的SD缓存走现有存储层，LDO4及SDMMC由BSP管理，配置显式关闭挂载失败格式化。SDMMC与Hosted SDIO的总线／GPIO共存需实机验收。相机虽为BSP能力，本项目没有初始化为小燕子功能；蓝牙、电池等专属能力也没有完成。

## 模型与SDK配置分层

ESP-SR2.4.7的Kconfig将中文mn5q8／mn6限制为S3；P4使用mn7。本批P4专用选择 `mn7_cn`，若模型分区没有该模型则明确失败，不回退到S3模型。唤醒词仍为“你好，小燕子”，没有取消本地唤醒、自然插话、AEC或OTA；新模型对该短语和现有阈值的识别效果没有验收。

配置现在按固定顺序读取：

```text
sdkconfig.defaults
sdkconfig_target_<芯片>.defaults
sdkconfig_board.defaults（隔离构建）或 boards/<板型>/sdkconfig.defaults
```

公共文件保留共享功能。S3专用的目标名、Octal／80MHz、240MHz CPU、缓存参数和mn5q8移入S3文件；P4文件选择HEX／200MHz、400MHz CPU及mn7。板型配置最后加载，保留QUAD PSRAM、无PSRAM和各板独立选择。118个旧S3入口的“最后赋值表”与分层前逐项一致；这项静态比较不等于对118块板重新构建。

本地缓存准备与Docker同步都使用同一层次，不再把板型配置拼进公共文件。CMake使用登记表选择芯片，明确拒绝错误目标；Python／PowerShell登记读取器允许S3和P4源码入口，其他尚未移植目标继续拒绝。烧录工具的芯片参数改为选定profile的target，备份、镜像身份、NVS／保留扇区和范围检查保留；**没有执行烧录工具**。

缓存准备还按组件manifest的targets过滤不兼容组件，避免将现有S3专用微雪BSP强制注入P4。当前缓存中S3可用17份、P4兼容16份；声明不兼容的缓存不能代替目标依赖。Docker默认镜像名由5.5.2改为5.5.6以符合多数profile约束，未拉取镜像；NT26等要求SDK6的目标仍需用户另行选择兼容环境。

## 实际执行与证据边界

两项P4实际预检均退出1，报本地没有唯一的 `esp32_p4_function_ev_board` BSP；未创建P4暂存目录或镜像：

```text
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp32-p4-function-ev-board --destination I:/xy_board_builds_20261109/p4_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp32-p4x-function-ev-board --destination I:/xy_board_builds_20261109/p4x_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

真实项目CMake入口对这两个profile分别以 `-DIDF_TARGET=esp32s3 -P firmware/assistant_espidf/CMakeLists.txt` 检查，均在SDK初始化前以退出1拒绝芯片不匹配。命令完整参数和原始日志在 `wrong-chip-guard.json`。这是配置边界检查，不是P4编译。

单独模型打包的实际成功命令为：

```text
python -B firmware/assistant_espidf/managed_components/espressif__esp-sr/model/movemodel.py -d1 evidence/p4-source-20261112/mn7-candidate.sdkconfig -d2 firmware/assistant_espidf/managed_components/espressif__esp-sr -d3 evidence/p4-source-20261112/model-build-final
```

退出0，mn7_cn＋fst为2,681,351字节，小于4MB模型分区，剩余1,512,953字节。首次调用把-d2误指到model子目录，多拼了一层model路径，失败日志已保留；修正为组件根目录后成功。模型包不包含应用，也不证明P4识别效果、内存余量或实时性。

两个旧目标最终缓存回归均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=bread-compact-wifi reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

OLED32应用2,157,056字节，RLCD应用2,228,000字节，各自3MB app分区；model仍2,203,819字节／4MB。各205个main文件集合、213项输入摘要与最终工作区一致，23份产物归档。`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0；镜像身份／校验／16MB／双OTA／NVS／模型布局有效。project_description实际记录三层配置顺序，S3模型、CPU和PSRAM选择保留。

另建全新OLED32暂存目录，17份既有组件复制成功，并以 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=bread-compact-wifi reconfigure` 完成冷配置，退出0；没有生成应用镜像。不能把旧sdkconfig的保留值当作新分层正确的唯一证据，冷配置与静态118项比较分别记录。

目标过滤后的最终120项只读预检：官方119项中64通过、42首缺直接组件、11首缺BSP、1精确版本不符、1 SDK不符；历史入口另1通过。0意外失败、0个只读预检暂存目录。过滤前的120项日志另行归档，并有迁移映射和原SHA256；原有118项历史记录保留。全部均不是新增目标构建证明。

本批未运行测试套件，没有执行Docker构建、安装依赖／SDK、写Git、刷机、OTA、上传或发布。`git diff --check`退出0；控制协议人工决策、主启动、OTA和桥接端等7个保护文件保持原SHA256。P4 BSP、MIPI／触摸、C6 Hosted、P4版mn7／库、RISC-V应用和P4修订条件没有被旧S3编译覆盖。

## 当前总量

119个官方变体有源码入口，另1个历史入口，共120个本地profile；69个官方变体待源码。P4族27个中仅这两个新增源码候选，25个仍待实现。累计64个官方变体曾缓存构建通过；当前2 passed、62 stale、6历史failed、118 not_run，其中49项有源码。全部188项本轮实机not_run，现有微雪1.8不能替代P4验收。

源码、原始命令结果、来源与产物摘要、分层比较和最终diff见 `evidence/p4-source-20261112/`；正式旧板构建记录为 `cached_build_attempts_20261112_p4_*.json`。整个P4路径仍需依赖解析、目标构建、容量／启动验证和对应修订实物的声音、界面、联网、SD与同板OTA验收。
