# 全板型构建依赖准备清单

2026-11-13当前更正：本页后文为历史依赖／SDK快照。当前153本地／152官方入口，116本地／115官方满足本机直接条件；C5 LCD1.69已在SDK5.5.6独立构建，不再需要本页旧表所列6.1条件。BOX和多个面板也已有本地驱动接入，勿照旧缺件表重复升级安装。73项未构建中37有源码／36无入口，最新逐板缺项、SDK条件及功能缺口见[当前报告说明](clockfix_display_rebuild_20261113.md)及其直接依赖JSON；不代表传递解析、远程版本可得或实机通过。本批未安装或升级。

后续更新：虫洞BOX3.5已完成首次完整缓存构建，使用核对过的ST7796默认初始化及SDK DCS操作；BOX2.8与LilyGO回归通过。GT3.5的+80坐标偏移及实机验收仍未完成。见[本批记录](chd35_st7796_sdk_20261112.md)。

后续更新：虫洞BOX共享触摸已保留FT6336及原探测顺序并接入GT911轮询；2.8版首次整板构建通过，3.5版仅共享源码编译并仍缺ST7796 1.3.5，不能继承整板结果。最新记录见[本批说明](gt911_chd_20261112.md)。

后续更新：BigSmart现已接入共享GT911固定容量轮询，保留上电默认0x5D/noRST/noINT与原坐标变换，首次整板构建通过；立创SZPI的FT5x06路径独立回归通过。最新边界见[本批记录](gt911_bigsmart_20261112.md)。

后续更新：微雪4B现已保留板级GT911复位/0x5D地址选择并接入固定容量轮询，首次整板构建通过；其它GT911入口及功能缺口候选没有因此完成适配。最新来源、触摸状态与实机限制见[本批记录](gt911_ws4b_20261112.md)。

后续更新：微雪S3LCD3.5原版与LilyGO MVSRLoRa已移除各自外部ST7796依赖，保留原17条专用／12条默认表和复位120ms，并分别完成首次整板构建。3.5B的AXS QSPI分支未构建，见[本批记录](st7796_sdk_20261112.md)。

后续更新：本页保留此前候选范围；Magiclick2.4与星智0.85WiFi现已移除各自外部NV3023依赖，保留原默认25／专用54命令和reset时序，并分别完成首次整板构建。最新来源与实机边界见[本批记录](nv3023_sdk_20261112.md)。

后续更新：本页保留此前候选范围；AtomS3R EchoBase/Pyramid与Magiclick2.5现已移除各自外部GC9A01依赖，保留原GC9107专用表和识别，两款AtomS3R首次整板构建通过，Magiclick整板仍受GCC内部错误阻塞。最新来源与实机边界见[本批记录](gc9107_sdk_20261112.md)。无PSRAM AtomS3仍独立受阻。

后续更新：本页为历史范围记录；Spotpear1.28BOX、MovecallMoji/璀璨、Electron现已用SDK DCS操作保留原GC9A01默认42命令，各自成功完成整板构建。专用GC9107分支未替换。当前依赖963声明、74本地/73官方满足直接条件，最新来源和限制见[本批记录](gc9a01_default_sdk_20261112.md)。

后续更新：本页保留此前状态快照；Freenove2.8与K10现已移除外部ILI9341依赖并分别完成首次整板构建。当前依赖967条声明、70本地/69官方满足直接条件。最新来源、失败修正和验收边界见[本批记录](ili9341_default_sdk_20261112.md)。

后续更新：本页为生成时的依赖快照；Quandong已移除本板外部ILI9341依赖，完成首次整板构建。当前直接条件满足增至68本地/67官方、969条声明，最新报告见[本批记录](quandong_sdk_panel_20261112.md)。

本清单用于牛牛准备环境；本批只读检查和生成报告，没有执行安装、卸载、依赖解析、构建、下载组件包、Docker拉取或启动服务。它覆盖当前全部153个本地源码入口，其中152个对应固定小智v2.5.2发行变体，一个是历史入口。其余36个官方变体没有源码入口，继续列为待适配，不进入本清单的安装队列。

本批新增 `tools/firmware_dependency_report.py`，复用准备脚本的SDK版本读取和现有组件扫描。报告逐板列出全部直接声明，而原预检遇第一个错误即停止；两者153项就绪分类逐项一致。准备脚本只是提取两个已有只读函数，暂存、外网保护和构建行为不变。

## 实际结果

| 项目 | 数量 |
| --- | ---: |
| 本地源码入口 | 153 |
| 官方源码入口 | 152 |
| 直接依赖声明 | 970 |
| 所有直接条件满足的本地入口 | 67 |
| 所有直接条件满足的官方入口 | 66 |
| 有至少一项直接条件未满足的本地入口 | 86 |
| 缺组件的声明项 | 164 |
| 缺唯一BSP的入口 | 11 |
| 已有组件版本不符的声明项 | 5 |
| 固定Git提交需独立核对的声明项 | 1 |
| SDK版本不符的入口 | 7 |

这些错误数量不能相加当作板型数量：同一板可能有多个缺项，BSP本身也可能已计为缺组件。满足直接条件也不等于传递解析、编译、链接、启动或实机验收。报告使用已安装组件的真实版本与targets字段，精确版本沿原暂存工具严格比较，范围由既有IDF Component Manager语义库判断。没有评估传递条件依赖、完整源包一致性或远程registry可用性。

## SDK准备顺序

| 板型入口 | 所需范围 | 本次只读核实 |
| --- | --- | --- |
| 146个本地入口（含历史入口） | >=5.5.0或>=5.5.6，主体限定<5.6.0 | 本机5.5.6满足版本条件；Git HEAD与官方v5.5.6提交相同，但已有dirty文件和强制UND限制，不视为干净可启动SDK |
| bread-compact-nt26 | >=6.0.1,<6.2.0 | 官方v6.0.2源标签存在，尚未安装；仍需对应模组组件与整板验证 |
| m5stack-corep4x | >=6.0.2,<6.1.0 | 官方v6.0.2源标签存在，尚未安装；仍缺esp_hosted/esp_wifi_remote等 |
| S31 coreboard、S31 korvo、maza S31、C5 LCD1.69 | >=6.1.0,<6.2.0 | v6.1.0标签查询404；release/v6.1分支存在，固定快照见下文，不能称正式稳定版 |
| espressif-esp-mosaico | >=6.2.0,<6.3.0 | v6.2.0标签查询404；master快照声明6.2.0，不能称正式稳定版 |

官方源标签v6.0.2解引用提交为 `7101770dc6db2667b3c477cc31365dd1acd6db4e`。release/v6.1本次固定到 `f0114250e205e33a945e2f3e7ec40085c5a029a2`，其version.cmake声明6.1.0；master固定到 `827cc6229fb01c6ecf6362b4840ef27d62d2138e`，声明6.2.0。这两份快照都存在S31的soc_caps.h，但公共头文件存在不证明完整SDK／工具链或本项目可构建。没有移动现有manifest的版本门槛，也未选择用浮动分支取代正式发布环境。

本机另有 `C:/Espressif/frameworks/esp-idf`，version.cmake为5.5.2且目录没有Git元数据，仅作为发现的目录记录，未当作可用替代环境。当前唯一Python IDF环境仍是idf5.5_py3.12_env。Docker只读 `docker info` 退出1：DockerDesktopLinuxEngine命名管道不存在。没有替牛牛启动Docker。Docker Hub v5.5.6和v6.0.2标签元数据查询均超时，未据源码标签推断Docker镜像可拉取。

牛牛可先准备官方已存在的6.0.2独立环境，保留现有5.5.6目录；完成后再按NT26／CoreP4X各自清单准备组件。6.1／6.2目标先保留源码候选和固定来源，等正式环境或明确选定开发快照后推进，不给出尚不存在版本标签的安装命令。工具会按选中板型manifest再次检查，不能因同为P4或S31就共用未经核对的SDK。

## 全部未满足的直接组件条件

下表将不同版本、Git提交分开；同名组件不能简单替换成最新版本。registry状态未逐项联网解析，表中“缺失”表示当前本地缓存没有，不承诺对应版本可下载或其所有传递依赖可满足。每组涉及的完整板型ID在JSON报告的dependency_groups中。

| 组件 | 所需版本／提交 | 涉及入口 | 本地情况 |
| --- | --- | ---: | --- |
| 78/esp_lcd_nv3023 | 1.0.1 | 2 | 缺失 |
| 78/uart-eth-modem | 0.7.0 | 1 | 缺失 |
| esphome/esp-hub75 | 0.3.6 | 1 | 缺失 |
| espressif/esp-box | 3.1.0~2 | 1 | 缺失 |
| espressif/esp-box-3 | 3.2.1 | 1 | 缺失 |
| espressif/esp-box-lite | 2.1.0~2 | 1 | 缺失 |
| espressif/esp32_p4_function_ev_board | 5.2.3 | 2 | 缺失 |
| espressif/esp_codec_dev | 1.1.0 | 2 | 版本不符；已有1.5.11 |
| espressif/esp_codec_dev | 1.3.1 | 1 | 版本不符；已有1.5.11 |
| espressif/esp_hosted | 2.12.11 | 27 | 缺失 |
| espressif/esp_lcd_axs15231b | 2.1.0 | 3 | 缺失 |
| espressif/esp_lcd_co5300 | 2.1.0 | 1 | 版本不符；已有2.2.0 |
| espressif/esp_lcd_ek79007 | 2.0.2~1 | 2 | 缺失 |
| espressif/esp_lcd_gc9a01 | 2.0.4 | 8 | 缺失 |
| espressif/esp_lcd_ili9341 | 2.1.0 | 3 | 缺失 |
| espressif/esp_lcd_ili9881c | 1.1.0 | 4 | 缺失 |
| espressif/esp_lcd_jd9365 | 2.0.2 | 10 | 缺失 |
| espressif/esp_lcd_spd2010 | 2.0.0~1 | 2 | 缺失 |
| espressif/esp_lcd_st7701 | 2.0.2~2 | 2 | 缺失 |
| espressif/esp_lcd_st77916 | 2.0.2 | 7 | 缺失 |
| espressif/esp_lcd_st77922 | 2.0.2 | 1 | 缺失 |
| espressif/esp_lcd_st7796 | 1.3.5 | 1 | 缺失 |
| espressif/esp_lcd_st7796 | 1.4.0 | 5 | 缺失 |
| espressif/esp_lcd_touch_cst9220 | 0.1.0 | 1 | 缺失 |
| espressif/esp_lcd_touch_gt1151 | 1.0.1 | 1 | 缺失 |
| espressif/esp_lcd_touch_gt1151 | 1.1.0~1 | 1 | 缺失 |
| espressif/esp_lcd_touch_gt911 | 1.1.3 | 2 | 缺失 |
| espressif/esp_lcd_touch_gt911 | 1.2.1 | 28 | 缺失 |
| espressif/esp_lcd_touch_spd2010 | 2.1.0 | 1 | 缺失 |
| espressif/esp_lvgl_adapter | 0.6.0 | 3 | 缺失 |
| espressif/esp_wifi_remote | 1.6.4 | 27 | 缺失 |
| espressif/iot_usbh_rndis | 0.3.1 | 1 | 缺失 |
| espressif/m5stack_core_s3 | 3.0.2 | 1 | 缺失 |
| waveshare/custom_io_expander_ch32v003 | 2.0.0 | 6 | 缺失 |
| waveshare/esp32_s3_touch_amoled_1_32 | 22733e088a533378680a03e398279b2f7ab09a73 | 1 | 需固定Git检出核对 |
| waveshare/esp32_s3_touch_amoled_1_75 | 3.0.1 | 1 | 缺失 |
| waveshare/esp32_s3_touch_amoled_1_75c | 3.0.0 | 1 | 缺失 |
| waveshare/esp32_s3_touch_amoled_1_8 | 1.1.4 | 1 | 版本不符；已有2.0.3 |
| waveshare/esp32_s3_touch_amoled_2_06 | 2.0.0 | 1 | 缺失 |
| waveshare/esp32_s3_touch_amoled_2_16 | 2.0.1 | 1 | 缺失 |
| waveshare/esp_lcd_hx8394 | 2.1.0 | 2 | 缺失 |
| waveshare/esp_lcd_st7703 | 2.0.0 | 2 | 缺失 |

1.32 AMOLED的BSP不是registry版本号：manifest指定仓库 `https://github.com/waveshareteam/Waveshare-ESP32-components.git`、路径 `bsp/esp32_s3_touch_amoled_1_32`、提交 `22733e088a533378680a03e398279b2f7ab09a73`，须按该来源核对。不能把它替成同名缓存包或把SHA当语义版本。

高覆盖的缺项包括P4相关的esp_hosted2.12.11与esp_wifi_remote1.6.4（各27入口）、GT9111.2.1（28入口）、JD9365 2.0.2（10入口）、GC9A01 2.0.4（8入口）。这只是共享缺项覆盖数；不代表装完一项可完成这些板型。现有老BOX要求codec_dev1.1.0、1.32 AMOLED要求1.3.1，而其他板使用1.5.11；各板应独立解析，保留manifest精确版本，不能覆盖公共缓存后把原板也视为兼容。

## 实际执行与使用

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/dependency-readiness-20261112/direct-dependencies.json
```

命令退出0，输出153本地／152官方、970条直接声明、67本地／66官方满足直接条件；报告保留SDK版本来源、每板manifest摘要、17项现有组件manifest摘要及三个工具摘要。脚本要求输出新文件，拒绝覆盖；重跑应选择新JSON文件名。脚本只读取工程和指定SDK，不调用网络、构建、包管理器或Git写命令。

联网只读来源核实使用gh api读取官方Git标签、解引用tag、分支及固定提交公共文件；完整命令与退出码、Git blob核对记录在 `evidence/dependency-readiness-20261112/`。404和DockerHub超时均保留原结果，不把查询失败写成环境已具备。

研究记录方面，150项既有条目只追加证据；BOX-3和微雪1.75两个已有源码入口原来没有独立研究条目，本批补齐依赖检查记录，未新增profile或硬件支持结论。初次归档因错误假设152项均已有研究条目而停止，修正后再完整核对，失败说明保留在evidence。

本批main、native registry及现有构建产物均保持原文，构建账本仍2 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／116 not_run，152／188源码入口、36待源码；188项实机均not_run。未执行测试套件、类型检查、服务启动、依赖安装／卸载、固件构建、Git写入、刷机、OTA、上传或发布。
