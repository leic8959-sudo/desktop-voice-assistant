# 小燕子全量板型适配：范围校正与批次执行

当前源码范围纠正：152个官方变体有本地入口，但其中4项（Guition两款、AtomS3 EchoBase、Cardputer ADV）明确缺少完整终端必需功能，仍为实验候选；其余148项仅未被本次显式关闭规则标出，不代表完整验收。另36项无源码，至少40项仍未达到完整源码范围门槛，不能将152个入口等同152款完整适配。最新[功能缺口记录](terminal_feature_scope_20261112.md)与矩阵字段为准，以下历史批次数字保留。

日期：2026-11-09，Asia/Shanghai；当前进展更新至2026-11-12的ML307原始发送补丁候选批次。牛牛选择“官方全部纳入清单，按批次适配”。本文记录已完成的全量登记和可审阅的批次工具，以及尚未实现的板型，不把矩阵等同固件支持。

## 实际总数与固定来源

此前引用官方README的138／171作为总数不够准确。进一步读取该版本的实际发布清单后，采用以下口径：

| 来源 | 数量 | 含义 |
| --- | --- | --- |
| v2.5.2 README | 138个目录／171个变体 | 文档声明，已落后于实际发布清单，保留作差异说明 |
| v2.5.2 release-manifest.json | **154个目录／188个变体** | 本项目全量发行适配范围；发布附件也有188个ZIP，本轮未下载固件 |
| 同一提交的Kconfig | 170个BOARD_TYPE | 硬件选择项，与发布变体不是一一对应 |
| 同一提交源码树 | 156个含config.json的目录 | 除154个发布目录，另有 `alientek/atk-dnesp32s3m-4g`、`alientek/atk-dnesp32s3m-wifi` 两个源码目录，继续登记待研究 |

固定提交：`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，标签 `v2.5.2`。

- [官方发布清单](https://github.com/78/xiaozhi-esp32/releases/download/v2.5.2/release-manifest.json)：记录source_sha、IDF 6.1.0、每个board/name/full_name/target。
- [固定Kconfig](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/Kconfig.projbuild)、[固定CMake](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/CMakeLists.txt)：映射全部170个选择项至源码目录，不按名称猜引脚。
- 本项目自有固件当前缓存构建覆盖 **ESP32-S3**，另有ESP32-P4及ESP32-S31源码候选待目标编译，本批缓存构建使用本机既有 **ESP-IDF 5.5.6-dirty**（原5.5.2基础已在前批迁移）；上游IDF 6.1.0不意味着本轮已升级至6.1。跨芯片／跨SDK迁移单独处理，不自动安装工具链。

## 已落地的文件

1. `firmware/assistant_espidf/board_matrix_v2.5.2.json`：schema_version=2。188个发布变体逐项含源码URL、芯片目标、对应Kconfig选项、批次、native_profile、源码／依赖解析／构建／实机状态；170个Kconfig选项另列，保留2个源码独有目录。公开来源有摘要和提交号。
2. `firmware/assistant_espidf/board_research_v2.5.2.json`：原7个近邻变体加SH8601原版的固定源码调查、实施进展和具体阻塞。区分硬件事实与验收，不把相近尺寸推断为同一硬件。
3. `tools/sync_board_matrix.py`：仅读取四份公开元数据，校验发布清单source_sha／条目计数／唯一变体／源码目录后原子更新矩阵；网络或解析失败保留旧文件。不导入或执行上游脚本，不下载固件／SDK，不改Git或设备。可用 `--source-directory` 读取事先下载的公开元数据。
4. `tools/build_firmware_batch.ps1`：默认输出计划，按B1–B5或all选择。只有显式 `-Execute` 才调用既有构建入口；所选批次存在未适配项时在任何构建前整批拒绝。明确增加 `-SourceProfilesOnly` 才允许只处理已有入口并报告跳过数量，不将跳过项记完成。
5. `firmware/assistant_espidf/native_board_profiles.json`及各语言读取器：统一维护当前149个本地入口的应用身份、BSP、实现宏、QSPI分支与148个上游变体映射，取代CMake／构建／烧录／矩阵内的重复硬编码名单；详见[统一登记实现记录](native_board_registry_20261109.md)。条目存在仍不代表编译或实机通过。

批次工具受显式源码profile白名单约束：当前148个官方变体有对应源码入口，另保留1个历史兼容入口，不重复计入188项。**登记188项不等于允许188项进入S3构建脚本**，没有BSP／引脚／编解码器实现的项不能只填一个名字变为支持。

以下保留批次工具的入口示例，本轮未执行批次脚本；已实际执行的独立缓存构建见下表及专门记录：

```powershell
# 只展示全部登记项，默认不会启动Docker或构建
powershell -File tools/build_firmware_batch.ps1 -Batch all
# 只展示近邻板批次
powershell -File tools/build_firmware_batch.ps1 -Batch B1
```

后续干净构建可能需要补充依赖，由牛牛准备；本次缓存构建复用本机现有SDK和组件，没有下载安装。未经实物确认不运行烧录／OTA。

## 当前状态

| 状态 | 数量 | 证据边界 |
| --- | --- | --- |
| 有对应自有固件源码入口的官方发布变体 | 129 | 新增微雪P4／P4X 5寸HX8394候选及公共MIPI实现（见[本批记录](waveshare_p4_hx8394_adaptation_20261112.md)）， 新增微雪P4／P4X 4.3寸ST7701源码候选（见[本批及ILI纠正](waveshare_p4_st7701_adaptation_20261112.md)）， 新增微雪P4／P4X 7寸ILI9881C源码候选（见[本批记录](waveshare_p4_mipi7_adaptation_20261112.md)）， 新增微雪P4／P4X MIPI7B源码候选（见[本批记录](waveshare_p4_7b_adaptation_20261112.md)）， 新增微雪P4／P4X SPI3.5源码候选（见[本批记录](waveshare_p4_spi35_adaptation_20261112.md)）， 新增P4功能评估板／P4X修订源码候选（见[跨芯片与模型记录](p4_function_adaptation_20261112.md)）， 新增NT26 UART Ethernet蜂窝网络源码候选（见[本批记录](nt26_cellular_adaptation_20261112.md)）， 新增Korvo2 V3 RNDIS实际USB网络源码候选（见[本批记录](korvo_rndis_adaptation_20261112.md)）， 微雪1.8 SH8601、1.8-v2 CO5300、1.75、1.75C、BOX、BOX-Lite、BOX-3、CoreS3、两款T-Circle、微雪1.32／2.06／2.16及LCD0.85／1.46／1.54／1.83／1.85／1.85B／1.85C，以及LC／星智／正辰1.54和默认面包板LCD、星智ABS2.0、NV3023方块0.85及金属1.54，另新增Stopwatch的16MB／466圆形CO5300／CST820B／M5IOE1／单ES8311源码及目标缓存构建（见[引脚编号、供电接管及验收边界](stopwatch_adaptation_20261110.md)），以及Cardputer Adv的8MB无PSRAM／240×135 ST7789／TCA8418按键／单ES8311源码与缓存目标构建（见[音频引脚修正、键盘及验收边界](cardputer_adv_adaptation_20261110.md)），以及AtomS3＋Echo Base的8MB无PSRAM／128文本屏／单ES8311／PI4IOE底座源码候选（见[双屏控识别、布局与缺依赖边界](atoms3_base_adaptation_20261110.md)），以及微雪AMOLED1.43C的8MB／466圆形CO5300／CST820／双codec源码与目标缓存构建（见[驱动命名、偏移与8MB容量边界](waveshare_143c_adaptation_20261110.md)），以及微雪3.49原版／V2的172×640原生AXS／独立触摸I2C1／双codec源码候选（见[修订接线、坐标及电源边界](waveshare_349_adaptation_20261110.md)），以及微雪3.5原版ST7796 SPI和3.5B AXS15231B QSPI的480×320／ALDO1／单ES8311源码候选（见[整帧传输、坐标修正及缺依赖边界](waveshare_35_adaptation_20261110.md)），以及微雪4B的480×480 ST7701 RGB／TCA9554／GT911／AXP2101 ALDO1／双codec源码候选（见[触摸复位、供电及缺依赖边界](waveshare_4b_adaptation_20261110.md)），以及微雪4.3C／7C的800×480 RGB／GT911／CH32／ES8311或ES8389双codec源码候选（见[GPIO0冲突与缺依赖边界](waveshare_rgb_c_adaptation_20261110.md)），以及Kevin 376×960 GC9503V原生RGB／三线IO释放／单codec16kHz源码（见[方向修正与帧同步边界](kevin_rgb_adaptation_20261110.md)），以及LCDWiki ES3C35P的ST77922 QSPI／0x55 TDDI触摸／单codec源码候选（见[缺依赖、色序与RGB后续边界](lcdwiki_adaptation_20261110.md)），以及Otto单发布变体的两套运行时硬件接线／16kHz全双工或24kHz单工源码（见[探测、CS12冲突与K10待实现边界](otto_adaptation_20261110.md)），以及立创课程Super Bear无屏／Robot Pro OLED64／TV Pro ST7789 mode2三款基础语音源码（见[引脚冲突、无执行器及实机边界](eda_family_adaptation_20261110.md)），以及微雪S3 CAM 1.83／2／2.8／3.5四屏／CH32V003／双codec源码候选（见[整端口初始化、缺依赖及屏幕边界](waveshare_cam_family_adaptation_20261110.md)），以及Guition JC4827W543普通／外接功放两款4MB单应用／无模型／NV3041A／GT911源码候选（见[缺依赖、无OTA及手动语音边界](guition_adaptation_20261110.md)），以及面包板S3 CAM默认240×320／数字音频／UART0及Zhengchen CAM默认320×240／双codec／PCA9557基础语音源码（见[屏幕、USB及相机边界](s3cam_adaptation_20261110.md)），以及Korvo2 V3默认280×240 ST7789／TCA9554／ADC按键／双codec源码（见[屏幕选项与实机边界](korvo2_adaptation_20261110.md)），以及Quandong S3 Dev的SPI2／ILI9341自定义19命令／低有效PA1源码候选（见[三项越界记录修正和缺依赖边界](quandong_adaptation_20261110.md)），以及Freenove 2.8的ILI9341横屏／24kHz单ES8311／低有效PA1／FT6336U按下状态源码候选（见[缺依赖、触摸与功能边界](freenove_adaptation_20261110.md)），以及DFRobot S3 AI CAM的16MB无屏PDM／10dBm／手机查看源码候选（见[本批PDM直读、BOOT及相机边界](dfaicam_adaptation_20261110.md)），以及虫洞EYE的240×240／I2C1／16kHz单ES8311源码（见[本批采样率、功放及未完成相机边界](chd_eye_adaptation_20261110.md)），以及StickS3的135×240 ST7789P3／PM1 G2-G3／60%硬件音量源码（见[本批厂家屏控更正和未完成按键边界](sticks3_adaptation_20261110.md)），以及Echo Pyramid外部时钟／STM32／独立功放／双codec源码候选（见[本批精确分频、缺依赖及硬件边界](echo_pyramid_adaptation_20261110.md)），以及带屏AtomS3R＋Echo Base的双屏ID识别／LP5562／I2C1源码候选（见[本批缺依赖及外围边界](atom_s3r_base_adaptation_20261110.md)），以及AtomS3R CAM/M12＋Atomic Echo Base的8MB无屏／ES8311／P0单独功放控制候选（见[本批厂家原理图、寄存器及手机入口边界](atomic_m12_base_adaptation_20261110.md)），以及虫洞BOX2.8／3.5的双codec／可选FT与GT触摸源码候选（见[本批ST7796、缺依赖及坐标标定边界](chd_box_adaptation_20261110.md)），以及乐动掌控V2／掌控板V3的QUAD／ES8388／320×172基础Wi-Fi候选（见[本批STM8复位、接线及未完成外围边界](labplus_adaptation_20261110.md)），以及MixGo Nova的ES8374／8MB QUAD与Magiclick2.4／2.5屏幕修订源码候选（见[本批ADC、输入选择及缺依赖边界](mixgo_magiclick_adaptation_20261110.md)），以及敏思K08数字音频及NULLLAB AI-VOX3的128fs ES8311基础Wi-Fi候选（见[本批引脚冲突、时钟及外围边界](k08_aivox3_adaptation_20261110.md)），以及微雪Audio Board默认JD9853 320×172／双codec／TCA9555候选（见[本批默认屏幕及可选硬件边界](waveshare_audio_board_adaptation_20261110.md)）、AiPi-Lite的128×128／ES8311／原厂区域保留候选（见[本批分区及恢复边界](aipi_lite_adaptation_20261110.md)）、ATK BOX3的AW9523B协议／双codec／背光开关候选（见[本批厂家协议与实机边界](atk_box3_adaptation_20261110.md)）、ATK BOX2 Wi-Fi的ES8389／240×320 I80／扩展口三键候选（见[本批TX时钟与充电边界](atk_box2_wifi_adaptation_20261110.md)）、ATK BOX的I80／运行时ES8311或数字音频候选（见[本批格式转换、选型及实机边界](atk_box_i80_adaptation_20261110.md)）、ATK BOX0的16kHz单codec／混合电平按键候选（见[本批供电与社区PA21证据边界](atk_box0_adaptation_20261110.md)）、正点原子ATK-DNESP32S3的ES8388／XL9555开发板候选（见[本批背光／功放事实及BOX差异](atk_developer_adaptation_20261110.md)）、立创SZPI／RYMCU BigSmart的PCA9557／双codec源码候选（见[本批触摸依赖、功放及硬件参考边界](pcabox_adaptation_20261110.md)，BigSmart因缺GT911尚未编译）、Jiuchuan240×296专用面板／ES8311及电源按键候选（见[本批记录及未完成功能](jiuchuan_adaptation_20261110.md)）、DuChatX128×160数字音频／Kevin SP V4 240×280单codec16kHz源码候选（见[本批记录及未完成功能](duchat_kevinspv4_adaptation_20261110.md)）、AtomEchoS3R的8MB无屏／ES8311源码候选（见[本批记录及实机边界](atom_echos3r_adaptation_20261110.md)）、Doit无屏／手机配对候选（见[无屏呈现边界](doit_headless_adaptation_20261110.md)）、太极派STD／PDM源码候选（见[缺依赖及批次差异](taiji_adaptation_20261110.md)）、云聊S3基础Wi-Fi/ES8388候选（见[未完成功能及构建记录](yunliao_adaptation_20261110.md)）、WK默认OLED32／无电机配置（见[默认配置范围](wk_default_adaptation_20261109.md)）、Maza默认A01 2.9电子纸候选（见[面板范围及验收边界](maza_epaper_adaptation_20261109.md)）、Kevin Box-2基础Wi-Fi源码（见[范围及未完成功能](kevin_box2_adaptation_20261109.md)）、HU-087（见[接入及静音修正](hu087_mute_adaptation_20261109.md)）、面包板OLED32／64和星智0.96 OLED（见[OLED记录](oled_adaptation_20261109.md)），以及Movecall Moji／璀璨和GenJuTech1.54（见[本批记录](movecall_genjutech_adaptation_20261109.md)），以及Spotpear1.28 BOX／1.54 MUMA源码候选（见[本批记录](spotpear_adaptation_20261109.md)）；见[原入口记录](multi_board_adaptation_20261109.md)及[T-Circle记录](tcircle_adaptation_20261109.md)；源码不等于实机验收 |
| 已登记、尚无自有固件入口 | 59 | 保留真实未完成状态；部分已有固定源码调查 |
| 曾各自通过的官方变体缓存编译／实机验收 | 64／0（累计） | 新增微雪RLCD4.2（见[本批记录](waveshare_rlcd42_adaptation_20261112.md)）， 新增微雪3.97电子纸（见[本批记录](waveshare_epaper397_adaptation_20261112.md)）， 新增ZECTRIX NOTE4黑白4.2（见[本批记录](zectrix_adaptation_20261112.md)）， 新增微雪1.54电子纸V2（见[本批记录](waveshare_epaper_adaptation_20261111.md)）， 新增LilyGO T-CameraPlus S3两版（见[本批时钟更正与构建](tcamera_adaptation_20261110.md)），以及ESP-SparkBot（见[共用背光／功放及本批构建](sparkbot_adaptation_20261110.md)），以及LCD EV Board V1.4／V1.5（见[LCD EV本批构建](lcd_ev_adaptation_20261110.md)），以及Stopwatch（见[本批目标构建](stopwatch_adaptation_20261110.md)），以及Cardputer Adv（见[本批实际目标构建](cardputer_adv_adaptation_20261110.md)），以及微雪AMOLED1.43C（见[8MB目标构建记录](waveshare_143c_adaptation_20261110.md)），以及Kevin RGB（见[本批记录](kevin_rgb_adaptation_20261110.md)），以及Otto（见[本批记录](otto_adaptation_20261110.md)），以及立创课程三板（见[本批记录](eda_family_adaptation_20261110.md)），以及面包板S3 CAM和Zhengchen CAM（见[本批记录](s3cam_adaptation_20261110.md)），以及Korvo2默认ST7789（见[本批记录](korvo2_adaptation_20261110.md)），以及两款T-Circle、明确CO5300的1.8-v2、微雪LCD0.85／1.54／1.83，以及LC／星智／正辰1.54、面包板默认LCD和星智ABS2.0和金属1.54基础Wi-Fi入口，新增Spotpear MUMA1.54、GenJuTech1.54，以及面包板OLED32／64、星智0.96 OLED、HU-087、Kevin Box-2基础Wi-Fi入口、Maza默认A01电子纸候选、WK默认OLED32配置、云聊基础Wi-Fi候选、Doit无屏候选、AtomEchoS3R无屏候选、DuChatX及Kevin SP V4基础语音候选、Jiuchuan专用面板候选、立创SZPI基础语音候选、正点原子ATK开发板候选、ATK BOX0基础语音候选、ATK BOX I80双音频候选、ATK BOX2 Wi-Fi候选、ATK BOX3候选、AiPi-Lite候选、微雪Audio Board默认JD9853候选，以及敏思K08、NULLLAB AI-VOX3、MixGo Nova、乐动掌控V2／掌控板V3和AtomS3R CAM/M12＋Echo Base、StickS3、虫洞EYE和DFRobot S3 AI CAM基础Wi-Fi候选，均在IDF5.5.6-dirty＋17个既有组件override下编译／链接／尺寸／镜像身份检查通过；见[最新DFRobot记录](dfaicam_adaptation_20261110.md)及[虫洞EYE记录](chd_eye_adaptation_20261110.md)及[StickS3记录](sticks3_adaptation_20261110.md)及[CAM/M12记录](atomic_m12_base_adaptation_20261110.md)及[Labplus记录](labplus_adaptation_20261110.md)及[MixGo／Magiclick记录](mixgo_magiclick_adaptation_20261110.md)及[K08／VOX3记录](k08_aivox3_adaptation_20261110.md)及[微雪Audio Board记录](waveshare_audio_board_adaptation_20261110.md)及各批历史记录；BigSmart未编译，不列入累计通过。不是干净发行构建或实机证据；历史兼容入口另记，不重复计数 |

HX8394 5寸批次当前2项官方缓存构建通过（旧立创双codec／FT与RLCD4.2），62项历史通过已过期，6项历史failed，118项not_run（其中59项有源码）。累计64个官方变体曾缓存通过；59项待源码。新增5寸P4／P4X HX8394候选，四种面板共用MIPI runtime，原三种时序／极性／管脚和命令字节已静态对应比较。新驱动2.1.0按厂家BSP1.0.4跳过重复I2C1初始化；8项真实预检均首缺各自面板组件，无目标stage或P4编译。全部188项本轮实机not_run，见[本批记录](waveshare_p4_hx8394_adaptation_20261112.md)。

所有能力字段（显示、触摸、麦克风、扬声器）在没有该变体确切证据时为null；首批已有profile也不依据上游目录宣称硬件已验收。批次是工作队列，名称中含“audio”“camera”等仅帮助分组，不能用来宣称硬件能力。未来带证据的能力／构建／验收记录在同一固定提交重新同步时会保留。

按官方发布清单的芯片目标统计：S3 126、P4 27、C3 10、C6 10、ESP32 7、C5 4、S31 4，共188个变体。这是适配工作量范围，不是本项目已支持的芯片清单。

## 批次与退出条件

| 批次 | 变体数 | 范围／下一步 | 进入完成状态所需证据 |
| --- | --- | --- | --- |
| B0 | 188＋170配置项 | 全量登记、固定来源、排队和状态边界；本轮已完成登记 | 清单与实际发行条目一致、无漏项，所有选择项能追溯源码路径；不以README旧数字作硬校验 |
| B1 | 10 | 全部10个已有实际源码入口；两款T-Circle及明确微雪1.8-v2已通过各自缓存构建，全部仍需干净依赖与实机验收 | 原创入口／必要驱动、许可证、独立依赖锁／构建／实机记录；不能只写profile |
| B2 | 79 | 微雪AMOLED1.32／2.06／2.16及LCD1.54／1.83已有实际源码入口，各板构建状态单独记录；其余继续硬件调查，目录名不代表一定有触摸 | 屏幕／输入方式／麦克风／扬声器／配网／提醒逐项验收，缺失硬件要明确产品降级 |
| B3 | 6 | 音频专用／Echo／Korvo候选，先查是否无屏，再做可选显示层 | 无屏型号不依赖LVGL屏幕；配对、错误提示、提醒确认有可用入口 |
| B4 | 66 | 其他芯片及电子纸／反射屏／矩阵屏候选，分开SDK／模型／RAM与刷新约束 | 按芯片族建立参考实现，再逐板核对；电子纸不强制点云动画；非S3不能使用当前16MB／PSRAM假设 |
| B5 | 27 | 蜂窝／摄像头／机器人等特殊候选；仅先纳入终端范围，不自动扩展控制功能 | 明确基础语音支持与设备专属能力的区别；运动／执行器不默认启用，需单独授权和实物验收 |

B1–B5总计188。按真实硬件调查可以调整排队，但不能从清单删除难适配板来抬高完成率。

## B1已核实的差异与实施阻塞

| 待实现变体 | 固定源码发现 | 最小实施范围 |
| --- | --- | --- |
| 乐鑫BOX | 旧BSP声明不再维护，codec依赖 `~1.1`；有触摸、无SD；ES7210＋ES8311 | 已以专用BSP3.1.0~2／codec1.1.0建立源码入口；精确1.1.0源码包已静态核接口，不把其他板型降版；横屏、GPIO0、无SD及独立OTA身份仍待构建／实机 |
| 乐鑫BOX-Lite | 无触摸、无SD；ES7243E输入＋ES8156输出；上游配置为16kHz | 已接BSP2.1.0~2／codec1.1.0与单键选项／确认；当前24kHz共用管道为源码候选，实际时钟／采样及单键取消等必须实物验收，不把它说成上游16kHz已验证方案 |
| M5Stack CoreS3 | ES7210＋AW88298，320×240；GPIO0是音频MCLK | 已以官方BSP 3.0.2接入源码、电源／音频、QUAD PSRAM和根页面触摸按住说话；独立应用名`assistant_cores3`；依赖解析／编译／触摸松手／24kHz收放音／回声／OTA仍待验收，相机未接入 |
| T-Circle-S3 | 160×160圆屏、CST816D；独立I2S麦克风与MAX98357A输出；固定原版pin_config标GPL3.0 | 已实现本地GC9D01N／CST816D、160圆屏布局、I2S0 32位槽转PCM16／独立I2S1输出、软件音量及PA45静音，保留嵌套TFT_eSPI来源许可；构建及实机边界见专门记录 |
| T-Circle-S3 V1.1 | 同160×160屏幕，麦克风改为32kHz PDM、输出24kHz I2S，无硬件参考 | 已建立独立PDM32kHz采集／24kHz播放入口，实例化32→16kHz FIR并接小圆屏与触摸；不调用数字麦不支持的模拟增益；仍待干净依赖及实机验收 |
| 微雪1.8原版／v2 | 原版SH8601，v2 CO5300；同368×448，触摸／offset不同 | 现已拆为BSP1.1.4与2.0.3、独立应用名／产物目录，BSP处理触摸探测和gap；旧默认CO5300兼容名不再当作SH8601证据，均待构建／实机 |
| 微雪1.75C | 与1.75共目录，但MCLK16、LCD复位1、触摸复位2；官方专用BSP无SD | 已接专用3.0.0 BSP／adapter0.6.0、圆屏布局及无SD能力分支；不套非C版引脚，仍待构建／实机 |

固定URL及细节见 `board_research_v2.5.2.json`，静态研究后已推进CoreS3、三种明确微雪修订及旧BOX／BOX-Lite入口；B1的10个官方变体现已有源码入口，各自编译证据分别记录，全部仍没有本轮物理验收。物理验收还需要对应板卡、硬件修订信息、本人恢复备份和明确刷机范围；缺少实物时不制造验收记录。

## 本轮实际操作

- 只读Git状态、源码、上游release API、发布manifest、固定提交Kconfig／CMake／源码树及B1相关板级资料；未读取私人配置／运行数据。
- 首次运行 `python -B tools/sync_board_matrix.py` 时公开网络连接被远端关闭；旧矩阵未被覆盖。改用PowerShell下载四份公开元数据到系统临时目录，首次下载CMake遇EOF，补取成功后使用缓存。
- 实际成功命令：`python -B tools/sync_board_matrix.py --source-directory <临时公开元数据目录>`，输出 `Recorded 154 released directories, 188 variants, 170 Kconfig options; no firmware was built.`；补充B1研究后再次生成矩阵成功。该命令是元数据整理，不是固件测试、构建或安装。
- 文档、工具和矩阵做 `git diff`／新增文件空文件diff审查及空白检查；普通diff不显示未跟踪文件。未为审查暂存。
- 收尾时观察到 `assistant_runtime.py`、`gateway.py` 和 `tests/test_natural_barge_in.py` 的并发改动，本轮未编辑、覆盖或回滚这些文件；已有三板源码改动和此前评估文档也保留。
- B1后续推进新增[CoreS3源码入口](cores3_adaptation_20261109.md)，矩阵刷新为4个源码入口／184个待实现。只读构建环境检查发现Docker引擎管道不可连接、当前PATH没有idf.py，未启动构建或安装依赖；该环境阻塞与具体板型的源码／实机状态分别记录。
- 随后进一步核对标准安装目录，发现本机已有IDF5.5.2、匹配的GCC／CMake／Ninja和IDF Python环境；不能由PATH结果推断工具完全不存在。最新推进[麦克风／参考采样率分离](capture_rate_separation_20261109.md)后，使用已有17个组件的隔离副本尝试本机构建，因SDK缺SPIFFS子模块在CMake阶段失败。详见[实际缓存构建记录](cached_firmware_build_20261109.md)。没有下载或安装依赖、混入另一个SDK或操作设备。
- 继续用另一套已安装SDK **5.5.6-dirty** 及其匹配工具独立构建，历史CO5300兼容入口的完整build与补齐ROM ELF后的reconfigure build均退出0，生成2,614,640字节应用，3MB分区余17%；镜像只读检查通过。详见[该次构建记录](cached_firmware_build_idf556_20261109.md)。不将结果套到5.5.2、8个官方变体或实机；两组记录均在矩阵的legacy_profiles下引用。

全量适配目标仍在进行中；完成B0登记／批次入口后，B1已继续推进CoreS3、三种明确微雪修订和旧BOX／BOX-Lite，并修正旧“1.8”屏控对应关系。161个尚无入口的发布变体仍待源码实施。历史兼容入口在5.5.2失败、在独立5.5.6-dirty缓存路径通过构建，但仍没有本轮实机记录；没有安装依赖、测试套件、独立类型检查、服务启动、Git写入、上传、发布、刷机或OTA，不宣称全量或可交付设备已经就绪。

后续T-Circle源码交付见[专门记录](tcircle_adaptation_20261109.md)。牛牛已确认只有现有微雪1.8实物；其他板型不记录实机通过，也不把一个构建结果扩展到同批全部型号。

最新实际结果：两款T-Circle各自完整构建及最终增量构建退出0；微雪1.8-v2的公共代码回归构建退出0，三份镜像校验和身份检查通过。矩阵根据源码摘要区分passed_cached_sources与过期证据，未把缓存解析当作干净发行构建；当前尚余10个已有源码的官方变体待构建。B2新增四款微雪固定源码调查，均未提升源码或实机状态。

B2最新源码交付见[微雪B2记录](waveshare_b2_adaptation_20261109.md)：新增三入口，14个本地profile对应13个官方变体，175个仍待源码。1.32固定历史SH8601 Git提交，注册表同版本已变为CO5300；容量按8／32／16MB分别处理。三板预检因缺BSP停止，不计为编译失败或成功；最终公共代码回归结果另附证据。

B2公共代码收尾：两款T-Circle及明确微雪1.8-v2在本次最终源码下的reconfigure build、镜像检查全部退出0，40份输入摘要匹配；此前临时副本缺文件和旧CMake失败规则已恢复，失败日志保留。新增三板仍缺BSP，build保持not_run、dependency_resolution为blocked_missing_local_sources。具体命令／大小／边界见[微雪B2交付记录](waveshare_b2_adaptation_20261109.md)。

LCD后续：新增本地ST7789／CST816S／ES7210＋ES8311的1.54和1.83入口，16个本地profile对应15个官方变体，173个待源码；1.54不将音量减GPIO0作说话键，1.83保留保守PMIC供电和禁止格式化的SD缓存。实际命令、首次编译错误与修复、最终证据见[LCD适配记录](waveshare_lcd_adaptation_20261109.md)。

最新LCD收尾：两款新LCD及三款旧入口的最终源码缓存构建、尺寸及镜像检查全部退出0，43份输入摘要匹配；5份最新记录均记passed_cached_sources。当前15个有源码／173个待源码、10个已有源码仍未编译，全部本轮实机验收未执行。源码及验收限制见[LCD交付记录](waveshare_lcd_adaptation_20261109.md)。

数字音频／小屏后续：新增LCD1.46数字音频＋SPD2010源码入口和LCD0.85无触摸8MB入口，当前18个本地profile对应17个官方变体、171个待源码；0.85完整构建及六板最终增量／镜像检查通过，1.46缺驱动仍未编译。当前源码有6份匹配的缓存构建证据，11个已有入口待编译，所有实机未执行。见[本轮记录](waveshare_digital_adaptation_20261109.md)。

ST77916后续：新增原版1.85、B和C默认2.0的实际源码入口，共21个本地profile对应20个官方变体，168个待源码；三项因缺ST77916停止在依赖预检，未编译。AMOLED1.75C根据独立厂家页修正32MB容量。面板ID选择、两套表及许可、接线差异和最终回归证据见[1.85系列记录](waveshare_st77916_adaptation_20261109.md)。

1.85系列最终边界：20个有源码／168个待源码、6个已有入口通过当前源码缓存构建／14个已有源码仍未编译，实机验收0。新三板缺ST77916驱动，未创建构建目录；初始化表181／184条有效命令已核对，容量更正和许可纳入最终源码快照，详见[交付记录](waveshare_st77916_adaptation_20261109.md)。

TFT后续：LC、星智和正辰1.54、官方默认ST7789 240×320面包板LCD新增实际源码入口，当前25个本地profile对应24个官方变体，164个仍待源码；独立构建、首次回归失败与修复、最终输入和镜像证据见[TFT适配记录](tft154_adaptation_20261109.md)。容量采用固定上游发布默认16MB／Octal PSRAM，不能据此假定任意DIY模组容量。

TFT最终结果：四新板完整缓存构建通过，十份最终源码构建及镜像检查退出0，每份50输入摘要匹配。当前24个有源码／164个待源码、10个当前缓存构建通过／14个已有源码未编译、实机验收0。16kHz直通采集已在三新数字板自身profile实例化编译，不再仅作为未编译参数分支；声学／容量实测仍待硬件。见[TFT记录](tft154_adaptation_20261109.md)。

星智后续：ABS2.0基础Wi-Fi终端和NV3023方块0.85新增实际源码入口，当前27个本地profile对应26个官方变体、162个待源码；0.85缺驱动停止在预检，ABS独立构建及十款旧入口公共回归另记。金属版触摸是虚拟键码且电源接线不同，继续inventory_only。ABS蜂窝／关机／SD／马达等专属能力仍未实现，不能把基础语音入口当作设备整体完成。详见[本轮记录](xingzhi_adaptation_20261109.md)。

星智最终结果：ABS首次在既有ESP-DSP处遭GCC内部段错误，同SDK／源码／原目录降低并行度重试通过；旧十入口回归和十一份镜像检查通过，每份53输入摘要匹配。当前26个有源码／162个待源码，11个当前缓存构建通过／15个已有源码未编译，全部本轮实机验收0。NV3023仍缺驱动；ABS蜂窝等专属功能不能由基础Wi-Fi构建推断完成。见[交付记录](xingzhi_adaptation_20261109.md)。

金属版后续：真实CST816三个离散键、故障／跨键取消、对话音量和正确48供电／47输入已接入，28个本地profile对应27个官方变体，161个待源码。完整实机和复杂关机仍未完成；金属独立构建及十一旧入口回归见[本轮记录](xingzhi_metal_adaptation_20261109.md)。

金属最终结果：金属版完整构建和十二份实际镜像检查通过，旧十一profile公共输入／音频回归退出0，每份56输入摘要及main文件集合匹配。当前27个有源码／161个待源码、12个当前缓存构建通过／15个已有源码未编译，实机验收0；离散键故障取消和金属供电波形仍需硬件，不由编译代替。见[本轮交付记录](xingzhi_metal_adaptation_20261109.md)。


K08／VOX3后续：新增敏思K08数字音频16kHz录音／24kHz播放／ST7789偏移80-0，以及NULLLAB AI-VOX3的128fs ES8311／可选ST7789基础Wi-Fi入口。两新板完整build及AiPi／DuChatX回归退出0，四份镜像及105输入／98个main文件／编译宏和板型身份匹配。当前58官方源码变体／59本地profile／130待源码；累计37项曾缓存通过，当前4 passed／33 stale／6依赖预检失败／145 not_run，全部188实机not_run。蜂窝和外围能力未完成，K08不启用与麦克风DIN18冲突的灯控示例，VOX3时钟和增益待实测。详见[本批完整记录](k08_aivox3_adaptation_20261110.md)。


MixGo／Magiclick后续：新增MixGo Nova的8MB／QUAD／ES8374板级输入选择修正及Magiclick2.4默认NV3023、2.5按ADC识别GC9107／ST7735双屏源码。MixGo首次编译的可写缓冲区类型错误已修正，原日志保留；最终续编与ATK开发板／VOX3／K08回归退出0，四份镜像及108输入／101个main文件／编译宏和板型身份匹配。两款Magiclick缺屏驱动，仅完成源码和预检，不计编译通过。当前61官方源码变体／62本地profile／127待源码；累计38项曾缓存通过，当前4 passed／34 stale／6历史依赖预检失败／144 not_run，全部188实机not_run。详见[本批完整记录](mixgo_magiclick_adaptation_20261110.md)。


Labplus后续：新增乐动掌控V2和掌控板V3的16MB／QUAD／ES8388／320×172基础Wi-Fi源码。区分CS与背光，乐动独有STM8地址0x11单字节0x04 LCD复位；按实际板类保持ST7789默认初始化、mirrorXY+swapXY、gap0-34、24dB模拟输入与软件AEC。补齐短屏16px字体后两板最终增量及微雪Audio Board／MixGo回归均退出0，四份镜像及108输入／101个main文件／编译宏与字体身份匹配。当前63官方源码变体／64本地profile／125待源码；累计40项曾缓存通过，当前4 passed／36 stale／6历史依赖预检失败／142 not_run，全部188实机not_run。电机等原厂外设未实现，详见[本批完整记录](labplus_adaptation_20261110.md)。


CHD后续：虫洞BOX2.8 ST7789与BOX3.5专用ST7796／480×320布局源码候选已接入，I2C1／TDM4 MIC1单声道、PA46及可选FT／GT探测保留独立控制路径。两新板因缺GT911未编译，3.5另缺ST7796；GT3.5沿用+80偏移，目前只映射Y80..319，全高触摸未完成。立创／微雪Audio Board公共回归和两镜像检查通过，108输入／101个main文件匹配，不能替代CHD构建。当前65官方源码变体／66本地profile／123待源码，累计40项曾缓存通过，当前2 passed／38 stale／6历史依赖预检失败／142 not_run，全部188实机not_run。全动S3面板表的三条长度异常已归档，未猜测移植，详见[本批记录](chd_box_adaptation_20261110.md)。


CAM/M12后续：新增AtomS3R CAM/M12＋Atomic Echo Base的8MB／OctalPSRAM无屏语音入口。按厂家原理图和PI4IOE手册确认仅P0接功放、方向1=输出／高阻0=驱动，使用ID校验／单P0读改写回读／3.3V硬件增益／BCLK派生ES8311；不将GPIO41当作实际按键。绑定后手机查看暂按每次重启600秒窗口实现，待可选偏好回复，未标成人工决策。完整及最终增量构建、原AtomEchoS3R／K08回归和三镜像检查退出0，110输入／103个main文件匹配。当前66官方源码变体／67本地profile／122待源码；累计41项曾缓存通过，当前3 passed／38 stale／6历史依赖预检失败／141 not_run，全部188实机not_run。相机与其他M5板不继承支持，详见[本批记录](atomic_m12_base_adaptation_20261110.md)。


带屏AtomS3R后续：新增AtomS3R＋Echo Base的8MB／Octal／128×128双屏ID／LP5562背光源码候选，音频I2C1与内部I2C0分开；BOOT41实际按键／BCLK派生ES8311／Atomic Base单P0功放控制复用既有路径。缺GC9A01，未编译目标板；19命令和MIT文本已与固定来源对照。CAM/M12与AiPi公共回归、两镜像及115输入／108个main文件匹配，不把旧板通过套到新屏幕。当前67官方源码变体／68本地profile／121待源码，累计41项曾缓存通过；当前2 passed／39 stale／6历史依赖预检失败／141 not_run，全部188实机not_run。详见[本批记录](atom_s3r_base_adaptation_20261110.md)。


Echo Pyramid后续：新增8MB／Octal的Pyramid外部Si5351／STM32扬声器复位／AW87559与ES8311＋ES7210 STD2源码候选。27MHz×(32+96/125)÷144精确得到6.144MHz，规避上游32位溢出；寄存器回读／锁定检查不等于物理频率证明。有限五地址探测、初始PA关闭与实际codec取消静音后的PA使能，启动显示／电源失败记录错误并停止，不引入panic重启环。共享AtomS3R显示但保留Pyramid相反镜像与gap。目标仍缺GC9A01未编译。现有微雪Audio Board／CAM-M12回归及两镜像／117输入／110个main文件匹配。当前68官方源码变体／69本地profile／120待源码；累计41项曾缓存通过，当前2 passed／39 stale／6历史依赖预检失败／141 not_run，全部188实机not_run。详见[本批记录](echo_pyramid_adaptation_20261110.md)。


StickS3后续：新增8MB／Octal／135×240 ST7789P3语音源码。厂家资料纠正上游README的CO5300错误；PM1仅用现有限时读改写回读处理G2供电和G3功放，保留充电／升压／关机策略；60%硬件音量统一覆盖各写入路径，BOOT11有输入，USER12动作尚未实现。新板完整缓存构建、CAM/M12／K08回归及三镜像检查退出0，119输入／112个main文件匹配。当前69官方源码变体／70本地profile／119待源码，累计42项曾缓存通过；当前3 passed／39 stale／6历史依赖预检失败／140 not_run，全部188实机not_run。详见[本批记录](sticks3_adaptation_20261110.md)。


虫洞EYE后续：新增240×240 ST7789／16kHz输入输出／单ES8311基础Wi-Fi入口，实际总线与codec端口统一I2C1，PA40先关／实际codec取消静音后再开，BOOT0按住说话与卡片。相机及五击清NVS未移植。新板完整构建、StickS3／微雪Audio Board回归和三镜像检查退出0，119输入／112个main文件匹配。DFRobot AI CAM的PDM／无屏和Freenove2.8的ILI9341／FT6336U只追加固定与厂家调查，未登记源码支持。当前70官方源码变体／71本地profile／118待源码，累计43项曾缓存通过；当前3 passed／40 stale／6历史依赖预检失败／139 not_run，全部188实机not_run。详见[本批记录](chd_eye_adaptation_20261110.md)。


DFRobot后续：新增DFR1154 S3 AI CAM的16MB／Octal／无屏PDM16kHz采集＋24kHz I2S播放基础语音入口。PDM收到PCM16直接读，不套32位I2S麦克风转换；PHY两项上限10dBm，GAIN41不猜成功放使能。BOOT0长按3秒开现有手机查看窗、任务在音频前可用，LED3按独立正极性状态显示；相机和短按聊天未实现。首次及最终增量构建、Doit／EYE回归和三镜像检查退出0，119输入／112个main文件匹配。当前71官方源码变体／72本地profile／117待源码，累计44项曾缓存通过；当前3 passed／41 stale／6历史依赖预检失败／138 not_run，全部188实机not_run。详见[本批记录](dfaicam_adaptation_20261110.md)。

Freenove后续：新增320×240横屏ILI9341／BGR／SPI3 20MHz／24kHz单ES8311／I2C0／低有效PA1基础语音源码入口，BOOT0与FT6336U按下状态复用通用按键，初始释放、多指／组合／按住切换来源／读取失败取消，不注册屏幕坐标指针。真实预检缺ILI9341 2.1.0，退出1且没有stage／编译／镜像；已有EYE和立创两款回归退出0，两镜像身份／校验及各121输入／114个main文件匹配。当前72官方源码变体／73本地profile／116待源码，累计44项曾缓存通过；当前2 passed／42 stale／6历史依赖预检失败／138 not_run，全部188实机not_run。详见[本批记录](freenove_adaptation_20261110.md)。

Quandong后续：新增SPI2 40MHz／ILI9341 320×240／单ES8311 24kHz／PA1低有效／BOOT0基础语音源码入口。保留实际有效19条vendor命令；三条NOP记录只有1字节却声明17／17／41字节，移除越界读取且不猜测原意；SLPOUT100ms由固定2.1.0驱动提供，亮屏用API。构造RGB被vendorMADCTL08覆盖为有效BGR位，mirrorY／swapXY名义最终A8，仍待真实面板验证。不因闲置宏初始化PCA9557或ES7210。新板真实预检缺ILI9341，退出1且没有stage／编译；旧EYE回归／镜像检查退出0，124输入／117个main文件匹配。当前73官方源码变体／74本地profile／115待源码；累计44项曾缓存通过，当前1 passed／43 stale／6历史依赖预检失败／138 not_run，全部188实机not_run。详见[本批记录](quandong_adaptation_20261110.md)。

Korvo2 V3后续：新增固定默认280×240 ST7789／SPI3 60MHz／RGB／反色／swapXY镜像Y／gap20-0基础语音入口。官方BSP虚拟GPIO50-51从49起映射P1背光／P2复位，P3为LCD使能，保留P0；I2C1只探测20或38。PA48先关、实际DAC取消静音后再开；ES8311＋ES7210 TDM4取MIC1 slot0、24kHz与软件AEC。GPIO5是ADC1CH4电阻梯，不做数字BOOT上拉；曲线校准毫伏窗口识别REC／音量，其他键／间隙取消。新板完整及最终增量构建、立创初始与最终回归、两镜像检查退出0；清理本板未使用PWM变量后126输入／119个main文件匹配。ILI9341／RNDIS／相机／SD／电池／完整原厂手势未实现；AtomS3无PSRAM前置项单独记录，未伪建profile。当前74官方源码变体／75本地profile／114待源码；累计45项曾缓存通过，当前2 passed／43 stale／6历史依赖预检失败／137 not_run，全部188实机not_run。详见[本批记录](korvo2_adaptation_20261110.md)。

S3 CAM后续：新增面包板S3 CAM默认240×320 ST7789／SPI3 40MHz／16kHz数字麦＋24kHz播放与Zhengchen CAM默认320×240 ST7789／SPI3 80MHz／24kHz双codec-TDM4／PCA9557基础语音入口。前者LCD占USB19-20，板级改UART0并禁主次USB控制台，核对SDK GPIO接管；后者P0只称LCD控制，不假定物理复位，P1初始关PA且实际DAC取消静音后使能，P2保持未改。BOOT0及实际音量3-46按键分别路由。两新板完整与最终配置重构、旧立创初始与最终回归、三镜像检查退出0，各126输入／119个main文件匹配；原厂摄像头／LED／电池／动态横竖屏及完整手势未实现。当前76官方源码变体／77本地profile／112待源码；累计47项曾缓存通过，当前3 passed／44 stale／6历史依赖预检失败／135 not_run，全部188实机not_run。详见[本批记录](s3cam_adaptation_20261110.md)。

Guition后续：新增JC4827W543普通／外接功放两款4MB／OctalPSRAM源码候选，独立应用身份；单factory应用0x20000-0x400000、无model／otadata、拒绝OTA且不下载擦写、无本地唤醒，AFE空模型WebRTC VAD按公开API接入。480×272 NV3041A保留固定板92条表与QSPI02／32-3C封装，GT911driver_data复位选5D、读取失败／多指取消；触摸按住说话，BOOT0共享TE暂不映射。外接版先停放板载NS4168三线低电平。两款真实预检缺GT9111.2.1，均退出1且没有stage或目标编译；新驱动／4MB／无模型／无OTA分支不能以旧板回归作通过依据。现有面包板S3 CAM／Korvo2两轮回归、镜像检查退出0，各135输入／128个main文件匹配。当前78官方源码变体／79本地profile／110待源码；累计47项曾缓存通过，当前2 passed／45 stale／6历史依赖预检失败／135 not_run，全部188实机not_run。详见[本批记录](guition_adaptation_20261110.md)。

微雪S3 CAM四屏后续：新增1.83的240×284 RGB-ST7789、2／2.8的240×320 RGB-ST7789及3.5的320×480 BGR-ST7796镜像X，统一SPI2 40MHz但布局／驱动独立。I2C1CH32V003地址24、P0触摸复位／P1LCD复位／P4NS4150B功放／P6电源LED已核官方原理图。复用正式2.0.0组件，整端口FF／00初始化及缓存读取边界保留，P2SD／P3camera／P5BAT／P7CHG不宣称被保留；P4等实际DAC取消静音后才使能，背光走反向CH32PWM。24kHz双codec TDM4取MIC1 slot0、BOOT0共用交互。四款实际预检缺CH32组件退出1，新目标未编译；3.5还静态缺ST7796。旧Korvo2／微雪Audio Board回归和两镜像检查退出0，各137输入／130个main文件匹配。当前82官方源码变体／83本地profile／106待源码；累计47项曾缓存通过，当前2 passed／45 stale／6历史依赖预检失败／135 not_run，全部188实机not_run。详见[本批记录](waveshare_cam_family_adaptation_20261110.md)。

立创课程三板后续：新增Super Bear无屏／GPIO38手机查看键、Robot Pro SSD1306-128×64／I2C0-12-13／无镜像／键14、TV Pro ST7789-240×240／SPI3 mode2／BOOT0三款语音入口。三套数字I2S音频独立接线，16kHz输入／24kHz输出／LEFT32位槽，软件静音与现有AEC／唤醒。Super Bear不读取与右脚舵机同脚的TOUCH14宏；Robot不初始化普通OLED音量39-40以免占用扬声器；不编译控制器／movement／oscillator，不宣称机械安全或机器人功能。三新板完整构建、DFRobot旧无屏回归及四镜像检查退出0，各137输入／130个main文件匹配。当前85官方源码变体／86本地profile／103待源码；累计50项曾缓存通过，当前4 passed／46 stale／6历史依赖预检失败／132 not_run，全部188实机not_run。详见[本批记录](eda_family_adaptation_20261110.md)。

Otto后续：新增官方otto-robot单入口，在SPI／音频初始化前检测GPIO15-16弱下拉稳定高识别CAM硬件，释放检测引脚后赋两套LCD和音频引脚；两套240×240 RGB-ST7789均SPI3mode3。CAM共享I2S0LEFT32位16kHz输入输出，NON标准I2S0MIC16kHz＋I2S1扬声器24kHz，复用现有24to16FIR及相同播放参考；其余profile保留原编译期采样率。BOOT0共用说话／卡片；不接相机／运动／WS8080／电池，NONLCDCS12与右手舵机12源配置重叠如实列为硬件风险而非机械安全通过。新板完整构建、EYE16kHz模拟／TV Pro24kHz数字回归及三镜像检查退出0，各139输入／132个main文件匹配。K10实际ES7243E输入＋直接数字输出、0x23地址右移合同和MCLK疑点只更新研究，仍inventory_only。当前86官方源码变体／87本地profile／102待源码；累计51项曾缓存通过，当前3 passed／48 stale／6历史依赖预检失败／131 not_run，全部188实机not_run。详见[本批记录](otto_adaptation_20261110.md)。

LCDWiki后续：新增ES3C35P单入口，ST77922 320×480 QSPI2 80MHz、63条固定vendor数据／延时与4像素X对齐；driver不支持swap(false)，采用物理固定竖屏＋port软件旋转标志避开硬件swap；构造BGR被CMD1MADCTL00覆盖，色序不靠参数猜验收。0x55触摸按16位BE寄存器读最大点数1..5／7字节点，初始释放／多指／越界／读取错误取消；BOOT0说话与触摸卡片，PA1低有效等实际DAC解除静音后才开。真实预检缺ST779222.0.2退出1，新板无stage／编译／镜像；旧EYE与立创回归、两镜像校验退出0，各143输入／136个main文件匹配。Kevin376×960RGB的三线IO生命周期、19-20USB冲突／缓冲同步／方向只记研究未伪建profile。当前87官方源码变体／88本地profile／101待源码；累计51项曾缓存通过，当前2 passed／49 stale／6历史依赖预检失败／131 not_run，全部188实机not_run。详见[本批记录](lcdwiki_adaptation_20261110.md)。

Kevin RGB后续：新增kevin-yuying-313lcd原生376×960 RGB565入口，16MHz像素时钟／H8-30-30 V8-16-16名义36.036Hz；两份PSRAMframebuffer＋10行bounce，复用LVGL port RGB直接帧与完成事件同步。固定GC9503V36条有效vendor数据／延时保留，三线CS48SCL17SDA16写完无论成功失败均清理，RGB复用17-16前不得有悬空IO句柄。上游尺寸376×960与swapXY／mirrorX在直接帧缓冲下冲突，本地保持原生扫描不宣称90度效果；19-20RGB数据关闭USB控制台改UART0。16kHz单ES8311／I2C0／PA45高有效门控／既有24to16FIR播放参考，BOOT0共用交互。新板完整构建、旧EYE16kHz模拟与TV Pro24kHz数字回归、三镜像检查退出0，各147输入／140个main文件匹配。当前88官方源码变体／89本地profile／100待源码；累计52项曾缓存通过，当前3 passed／49 stale／6历史依赖预检失败／130 not_run，全部188实机not_run。详见[本批记录](kevin_rgb_adaptation_20261110.md)。

微雪RGB-C后续：新增4.3C16MB和7C32MB两款800×480原生RGB源码候选，各自数据脚／时序／I2C接线明确，2帧PSRAM＋10行bounce与framefinish同步；GPIO0为RGB DATA6，屏幕触摸作为说话／卡片入口。CH32P1外置复位配合INT4拉低选GT9115D，P2背光使能＋反向PWM，P3PA等实际DAC解除静音后才开；整端口FF／00和缓存状态边界保留，7CP6高作用未独立确认。ES7210 TDM4取MIC1／软件AEC，4.3CES8311mono，7CES8389DAC stereo以固定缓冲在写边界扩展mono；7C32MB仍沿用16MB范围内分区。两目标缺CH32／GT911未编译；旧Korvo2与Audio Board回归、两镜像检查退出0，各150输入／143个main文件匹配。4B的ST7701／TCA三线／AXP2101最小供电只记前置项。当前90官方源码变体／91本地profile／98待源码；累计52项曾缓存通过，当前2 passed／50 stale／6历史依赖预检失败／130 not_run，全部188实机not_run。详见[本批记录](waveshare_rgb_c_adaptation_20261110.md)。

乐鑫LCD EV后续：新增espressif-esp-s3-lcd-ev-board-1p4／1p5／2三个入口，SUB2的GC9503CV480×480＋FT5x06采用16kHz双codec，Board2按厂家更正为ST7262E43直接RGB800×480＋GT1151／24kHz。两套I2C及RGBD6/D7接线独立选择；TCA9554公开接口适配既有三线IO并掩码读回保留未拥有位，PA等实际DAC解除静音后开启；43条有效GC9503命令不变，临时IO删除后不交LVGL。GPIO19是USB_D-且上游背光宏未使用，遵从厂家固定背光限制，不伪报亮度／熄屏应用。两新SUB2完整build及最终增量、旧1.43C回归和三份镜像／链接／分区检查退出0，177个main文件／184份输入摘要匹配；Board2缺GT1151在预检退出1，无stage／编译。当前102官方源码变体／103本地profile／86待源码；累计57项曾缓存通过，当前3 passed／54 stale／6历史依赖预检失败／125 not_run，全部188实机not_run。详见[本批记录](lcd_ev_adaptation_20261110.md)。

ESP-SparkBot后续：新增espressif-esp-sparkbot基础Wi-Fi入口，ST7789RGB240×240／SPI3mode0-40MHz／GPIO46背光与功放共用，避免PWM和codecPA双重驱动。共享使能显示完成后拉高，实际DAC静音／输入增益检查；关闭会话保留I2S时钟并静音，DCS28/29只面板消隐，背光仍可发光；只接受0／100亮度且不支持夜间模式，拒绝前不改恢复缓存。ES8311I2C0／外部MCLK／16kHz，通过既有24to16FIR和相同实际PCM软件AEC；BOOT0说话与卡片。未接摄像头／电容触摸／BMI270／C2底盘移动或灯光／电池充电。新目标完整及最终增量、旧Cardputer Adv无PSRAM回归、两镜像／ELF／分区检查退出0，各177个main文件／184输入摘要匹配。当前103官方源码变体／104本地profile／85待源码；累计58项曾缓存通过，当前2 passed／56 stale／6历史依赖预检失败／124 not_run，全部188实机not_run。VoCat的V1.0／1.2互斥引脚及184条初始化序列已调查，仍无profile。见[本批记录](sparkbot_adaptation_20261110.md)。

ESP-VoCat后续：新增espressif-esp-vocat源码候选，运行期区分V1.0DIN15／PA4／LCDRST3低有效和V1.2DIN3／PA15／LCDRST47高有效。冷启动GPIO48低100ms，配对探测ES831118及ES721040，同ACK选V1.0，同NACK才使能48并等双ACK选V1.2；混合应答／总线错误不回退默认。16字节RTC_NOINIT校验缓存用于非上电复位，V1.2预装48高避免软件主动拉低；未知缓存暖启动保持供电、PA候选不驱动并提示真正断电重启，无NVS板型／绑定写入。只配置已识别PA，实际pad读回／互斥且等实际DAC解除静音后开。共享QSPI2／40MHz／360圆屏、184条MIT命令数据完全匹配固定来源、动态reset极性、PWM44；CST81615原始点数事件／错误取消；24kHzES8311＋ES7210 TDM4 MIC1软件AEC。真实预检缺ST779162.0.2，未创建stage／未目标编译；旧SparkBot及1.43C公共回归和两镜像／ELF／分区检查退出0，各182个main文件／189输入摘要匹配，但未选择新vocat_board或共享QSPI源，不能视作新板编译。当前104官方源码变体／105本地profile／84待源码，累计58项曾缓存通过；当前2 passed／56 stale／6历史依赖预检失败／124 not_run，全部188实机not_run。见[本批记录](vocat_adaptation_20261110.md)。

T-CameraPlus S3后续：新增lilygo-t-cameraplus-s3（V1.0／1.1）及v1.2两个QUAD PSRAM入口，ST7789RGB240×240／SPI3mode0-60MHz／各自I2C和LCD接线／CST816原始点数事件与错误取消／BOOT0卡片交互。旧版MSM261取RIGHT32位槽16kHzBCLK1.024MHz，新版MP34PDM32kHzCLK2.048MHz，均保留20x软件PCM放大。V1.2的GPIO18通过Q4/Q6同时给MIC和MAX供电，低有效且静音不关闭；扬声器16kHz立体声复制mono，复用24to16FIR及实际PCM软件AEC。MAX98357规格第19页明确不支持固定源声明24kHz及实际11025Hz，敏思K08和Guition扩展MAX入口同步更正16kHz，原板NS4168不扩大更改；Guition缺GT911预检退出1，无stage／编译。两新目标首次clamp模板类型推导失败，显式int32_t并补DUMMY选项后最终构建退出0；旧T-Circle缓存缺目录／文件已从本机已有来源补回，最终与敏思回归、四镜像／ELF／分区检查均退出0，各182个main文件／189输入摘要匹配。当前106官方源码变体／107本地profile／82待源码，累计60项曾缓存通过；当前4 passed／56 stale／6历史依赖预检失败／122 not_run，全部188实机not_run。未接SY6970充电策略／相机／IR／SD／睡眠；MVSRLoRa7份源码前置调查保持inventory_only。见[本批记录](tcamera_adaptation_20261110.md)。

MVSRLoRa后续：新增lilygo-t-display-s3-pro-mvsrlora默认Wi-Fi变体，ST7796BGR222×480／SPI3mode0-40MHz／mirrorX-gap49及独立布局；CST226SE原始7字节报告、AB标记／单点event6／正确X低四位／检查00AB同步／错误取消及有效零触点后重新接收。厂家两只MP34共享PDM线，不把第二只当硬件回声参考；32kHzPDM单相位取MIC2-LRGND，16kHz MAX98357立体声复制mono，复用两路FIR及软件AEC。MIC3低有效，RT908042保持高，功放41高有效且静音关闭升压与SD_MODE；GPIOpad读回和mute回调实际结果单独检查，SD14／LoRa7片选预先拉高不启动射频。没有移植充电／BATFET无电池策略、RTC、IMU、马达、SD存储或睡眠。目标预检缺ST77961.4.0退出1，无stage／编译／镜像；旧T-CameraPlus V1.2和敏思回归、两镜像／ELF／分区检查退出0，各186个main文件／193输入摘要匹配，但新mvsrlora_board及专用触摸／功放分支未选择编译。当前107官方源码变体／108本地profile／81待源码；累计60项曾缓存通过，当前2 passed／58 stale／6历史依赖预检失败／122 not_run，全部188实机not_run。见[本批记录](mvsrlora_adaptation_20261110.md)。

DFRobot K10后续：新增dfrobot-df-k10源码候选，厂家框架固定9fabfd6d6324dbb304ad47947675baf7cf1ab29e及只读静态库核对确认ES7243E物理地址15／11、MCLK3／BCLK0／WS38／DIN39／DOUT45标准双声道16位16kHz。TCA955520掩码读回只接管P0背光／P15低增益／P12A及P2B输入，不脉冲相机P1，不把GPIO0当按键；A/B均共用说话卡片，原厂长按音量未移植。复用数字后端ES7243E输入codec＋dummy数字输出，固定缓冲抽左／复制双声道，实际37.5dB增益回调及PGA寄存器读回；ILI9341BGR240×320／SPI3mode0-40MHz／mirrorY／二值背光与竖屏布局。新板真实预检缺ILI93412.1.0退出1，无stage／目标编译；旧T-CameraPlus V1.2及敏思公共回归、两镜像／ELF／分区检查退出0，各188个main文件／195输入摘要匹配，但K10板级／ES7243E／双工启动／显示分支未选择编译。当前108官方源码变体／109本地profile／80待源码；累计60项曾缓存通过，当前2 passed／58 stale／6历史依赖预检失败／122 not_run，全部188实机not_run。见[本批记录](k10_adaptation_20261110.md)。

electronBot后续：新增electron-bot基础终端候选，作者固定BOM确认N16R8／GC9A01／MAX98357A／ICS-43434及ZTS6672替代。圆屏240BGR／SPI3mode0-40MHz／finalmirrorY与LVGL一致／44px卡片按钮，BOOT0初始释放说话卡片；复用既有数字I2S0LEFT32bit16kHz采集和软件提升、I2S1LEFT32bit16kHz播放／24to16网络FIR／相同实际PCM软件AEC，修正MAX不支持的源24kHz。仅配置BL46，不初始化六路舵机4-5-6-7-15-16、不创建动作MCP／WS8080、不读写校准／不伪报电量；运动和整机功能尚未完成。预检缺GC9A012.0.4退出1，无stage／目标编译；两旧入口回归、两镜像／ELF／分区检查退出0，各188个main文件／195输入摘要匹配但无新ELECTRON分支或GC构造编译。当前109官方源码变体／110本地profile／79待源码；累计60项曾缓存通过，当前2 passed／58 stale／6历史依赖预检失败／122 not_run，全部188实机not_run。原C盘清单缓存缺失已从官方固定来源恢复到I盘，摘要与旧矩阵一致；下一批1.54电子纸8来源及159字节波形仅研究，保持inventory_only。见[本批记录](electron_adaptation_20261111.md)。


微雪1.54电子纸V2后续：厂家迁移新文档独立确认N8R8／Octal以及两版供电接线，固定厂商15文件与小智8文件归档；两份159字节全刷／局刷波形一致。V2复用有界纸屏worker，以200×200直接MSB单色映射、24+26全刷base／C7-CF、Y199递减及独立尾部寄存器处理，BUSY高忙必须实测边沿完成generation，超时停后续传输并保留长payload；184px正文分页／44px按钮，BOOT0初始松开交互。单ES8311 24kHz／I2C0／PA46，6-42低供电／17高锁存且readback，显示启动失败保持供电与PA关闭。未接PWR18关机、深睡眠、RTC／SHTC3／SD／触摸／ADC伪电量；反射屏熄屏偏好保留图像，无真实黑屏。新V2完整及最终构建、Maza公共回归、两镜像／ELF／分区检查均退出0；新app2196400字节，model2203819字节，各189个main／196输入摘要匹配。当前110官方源码变体／111本地profile／78待源码，累计61项曾缓存通过；当前2 passed／59 stale／6历史依赖预检失败／121 not_run，188实机全部not_run。V1受限方案等待用户决策；Zectrix4.2的15来源已研究，BUSY低忙、EPD/audio高有效、温度SPI读回及2bit前后帧编码与微雪不同，仍inventory_only。见[本批记录](waveshare_epaper_adaptation_20261111.md)。


ZECTRIX NOTE4后续：新增zectrix-s3-epaper-4.2黑白入口，厂家固定ca285c98ed0641f86780edb1f5ec77b0335fe649确认SSD2683／16MB Flash-Octal及独立供电极性，27来源归档并保留MIT许可。复用纸屏worker，新加20MHzTX／8MHz温度RX的IDF3WIRE半双工，持久100字节DMA行与前后像素2bit编码；每次reset/OTP2F0E，全刷实际温度／局刷83窗口，04-1200-BUSY低到高-0200完成后才提交generation，超时不发后继SPI。单ES8311 16kHz与既有24to16FIR／实际PCM软件AEC，电池锁存17、EPD6、audio42均HIGH，解除旧hold后读回并保持；PA46不加hold由codec控制。384px分页正文／44px按钮／BOOT0初始释放交互，UP39-DOWN18保留，未接关机、睡眠、RTC、NFC、充电LED或ADC。纠正前研究：源双键为设置另一个OTA启动分区而非擦除NVS，本地不移入。新目标与旧微雪V2/Maza各自构建、三镜像／ELF／分区检查退出0；新app2196928字节、model2203819字节，每项191main／198输入摘要匹配，63产物hash一致。当前111官方源码／112本地profile／77待源码，累计62曾通过；当前3 passed／59 stale／6历史依赖预检失败／120 not_run，188实机全部not_run。微雪3.97另归档6源文件及2厂家页，N16R8／800×480／AXP2101三ALDO、窗口地址单位和源充电注释冲突仍待独立核对，保持inventory_only。见[本批记录](zectrix_adaptation_20261112.md)。


微雪3.97电子纸后续：厂家原理图／SSD1677屏幕与TG28规格书渲染核对，19来源归档；N16R8／800×480／SPI3mode0-20MHz／BUSY3高忙与单ES8311 24kHz独立接线。SSD1677按16位像素X0..799、Y479..0、cursor479修正旧helper，0C五字节boost保留，全刷24+26/F7基准，局刷不reset破坏RAM／FF后等待真实BUSY，再写26下一基准。共享有界worker／48k持久DMA／768kRGB565PSRAM及3×48k单色快照，20局刷转全刷。31px项目字体＋16pxCJK缺字回退、20pxheader／768px分页正文／56px按钮；旧三纸屏正文及分页值保留。图纸ALDO2接Audio_VCC、ALDO3控EPD、ALDO1经R74NC可选PA，实际PA39经R23；93/94低5位3V3和90掩码06读改写读回，不关闭其他电源、不复制充电／DC1／自动关机策略。新板完整及最终字体修改重构建、旧V2/Maza/NOTE4公共回归、四镜像／ELF／分区检查退出0，每项191main／198输入匹配，84产物hash一致。最终app2738592、model2203819，当前112官方源码／113本地profile／76待源码，累计63曾通过；当前4 passed／59 stale／6历史依赖预检失败／119 not_run，188实机not_run。下一批反射式液晶RLCD4.2六源文件仅研究，实际2×4像素每字节映射／15000缓冲／双codec／TE6与纸屏不同，仍inventory_only。见[本批记录](waveshare_epaper397_adaptation_20261112.md)。


微雪RLCD4.2后续：新增ST7305／400×300／N16R8反射式液晶入口，与电子纸协议分开；16来源和厂家原理图／规格书已核对，SPI最小30ns周期因此修正上游40MHz为20MHz。原生2×4像素每字节映射15000字节，无360KB索引查表；27条固定命令及200ms延时保留，TE6采用IDF任务／ISR临界区计数，写前垂直消隐、写完后两轮扫描边沿才提交generation，断线500ms停更。复用有界帧任务、15k持久内部DMA、240kPSRAMRGB565与3×15k快照，reflective_display白底分页／16px文字14px标题／44px按钮，电子纸属性保持false。ES7210 TDM4 MIC1＋ES8311 DAC24kHz／I2C0／PA46独立GPIO读回，PWR18仅输入，不误用摄像头扩展器或硬件锁存。初次PA分支、随后atomiclockfree断言两次编译错误留原日志，最终改独立GPIO46和IDF临界区，新目标与旧3.97／Audio Board回归、三镜像／ELF／分区退出0，各193main／200输入摘要匹配，63产物hash一致。新app2227808、model2203819；当前113官方源码／114本地profile／75待源码，累计64曾通过；当前3 passed／61 stale／6历史依赖预检失败／118 not_run，188实机全部not_run。RGB Matrix六来源已读，HUB75外部依赖／I2S资源共存／64px界面与无按键交互仍需调查，保持inventory_only。见[本批记录](waveshare_rlcd42_adaptation_20261112.md)。


微雪RGB Matrix后续：新增默认64×64单屏源码候选，厂家确认32MBFlash/16MBPSRAM，根16MB分区范围与NVS保留。固定manifest查到esphome/esp-hub75^0.3.6，本地精确0.3.6；登记页关联a087930a2d1d4b45f43dd8a0118a8804432ed11c，S3使用LCD_CAM/GDMA而非I2S，与音频I2S0分资源；14份依赖参考仅读归档未安装。配置STANDARD_TWO_SCAN/FM6126A/20MHz/min240Hz/OE2启动高和brightness0，单缓冲8192字节RGB565PSRAM＋同步公共draw_pixels/失败后OE路由消隐保留持久对象，实际扫描率未测。独立64px文本状态／提醒滚动／六位码Montserrat14最大数字步进合计56.25px，不绘制无法操作的按钮或假BOOT手势；无应用键，使用已有语音／控制台业务。单ES8311DAC＋ES7210TDM4MIC1 16kHz/I2C0/PA11与实际PCM24to16FIR软件AEC，USBconsole避开UARTTX43-BCLK冲突，不复制音量0写70NVS覆盖。24来源／真实预检缺HUB750.3.6退出1，无Matrixstage或目标编译；旧RLCD和Audio Board回归／两镜像-ELF-分区检查退出0，每项195main／202输入摘要匹配，42归档产物hash一致，但matrix_display/64UI/32MB/GPIO11分支未选择编译。当前114官方源码／115本地profile／74待源码，累计64曾通过；当前2 passed／62 stale／6历史依赖预检失败／118 not_run，188实机全部not_run。依赖仍由牛牛准备，未安装、Git写入、刷机或发布。见[本批记录](waveshare_rgb_matrix_adaptation_20261112.md)。


SenseCAP Watcher后续研究：新增Seeed官方9份板级源、README／原理图与SPD2010依赖资料。其32MB Flash的0x9000起约200KB `nvsfactory` 保存EUI等生产信息，不能使用通用0x9000/0x4000保护范围；SPD2010 QSPI、TCA9555电源域、HX6538 SSCMA、ES7243E＋ES8311双codec均需要独立依赖和分支。当前保持inventory_only，未增加源码profile，避免在没有完整32MB备份／专用分区／依赖的情况下误刷或覆盖工厂信息。见[sensecap研究记录](sensecap_watcher_adaptation_20261112.md)。


SenseCAP基础终端后续：基于16固定来源，独立SenseCAP板级／显示／readonly工厂区guard／32MB profile接入；9000/32000完整保留200KB生产EUI，defaultNVS迁到3B000/4000、OTA3F000、双app50000和350000、modelC00000。CSV单独SDK编码解码均0；启动先验证工厂只读及分区地址再NVS或板级写入，flash profile显式保留factory＋新NVS。TCA9555@21掩码读回仅P9LCD/P10系统/P12PA/P3旋钮按键，系统保持、高电平PA等真实DAC解除静音后开，其他AI/SD/Grove/ADC位保留；开机按住非阻塞等待松手，错误取消再释放。SPD2010 RGB412圆屏／QSPI3mode3-40MHz／4像素对齐／两20行DMA／PWM8启动暗／44px旋钮确认布局。ES7243E物理14/API28标准stereo16-RIGHT24kHz、ES8311DAC，27dB实际增益与PCM软件AEC，保留共享时钟；没有套K10或ES7210TDM4。触摸、旋钮转动、HX6538AI相机、RTC/SD/Grove/LED/电池未接入，基础终端不等于整机完成。实际预检缺SPD20102.0.0~1退出1，无SenseCAPstage／目标编译。旧RLCD和Audio Board最终回归、两镜像-ELF-分区检查退出0，各199main／206输入摘要匹配，42产物hash一致；SenseCAP专用分支未编译。V1受限方案未获用户选择，本轮撤回其profile、板型文件和4处源码变更，实验资料归档到epaper-v1-withdrawal-20261112且不计入已构建；仍待用户取舍。当前115官方源码／116本地profile／73待源码，累计64曾构建通过；当前2 passed／62 stale／6历史依赖预检失败／118 not_run，188实机全部not_run。见[本批记录](sensecap_watcher_adaptation_20261112.md)。


Korvo2 V3 RNDIS后续：固定小智与厂商依赖以及SDK共29份来源，独立assistant_korvo_rndis／ST7789 280×240／ADC1CH4／ES8311＋ES7210 TDM4基础终端源码；实际USB OTG19／20 host、CDC、IOT_ETH、XY_RNDIS DHCP，默认板保留Wi-Fi。SDK5.5.6丢失IP事件改用ip_event_got_ip_t按netif过滤，连接事件按ETH句柄过滤；拔插清除地址就绪，新连接等待新DHCP。厂商驱动install就启动检测，因此先关闭root port，待netif/glue和回调以及ETH start就绪后再开放枚举；UART0替代USB控制台且编译期约束。RNDIS配网页仅要求服务地址，保留原SSID／密码，不增加AT／SIM／APN管理。真实预检缺RNDIS0.3.1退出1，无目标stage或编译。旧普通Korvo2与RLCD最终缓存构建及两镜像-ELF-分区检查退出0，各203main／210输入摘要与最终工作区一致，42产物hash归档，RNDIS事件／UART／仅server POST均未选择编译。8保护文件不变，含control_protocol人工决策和无屏手机绑定页。当前116官方源码／117本地profile／72待源码，累计64曾构建通过；当前2 passed／62 stale／6历史failed／118 not_run（46有源码），188实机全部not_run。未安装、Git写入、刷机或发布。见[本批记录](korvo_rndis_adaptation_20261112.md)。


NT26蜂窝终端后续：新增assistant_nt26独立入口，固定128×32 SSD1306／I2C0 41-42／XY镜像、数字MIC16kHz和独立扬声器24kHz、BOOT0／触摸按键47／音量40-39，复用已有完整唤醒、软件AEC和双OTA路径。UART1 3Mbps TX12RX11MRDY9SRDY10，78/uart-eth-modem0.7.0／UHCI／IOT_ETH／esp_netif，非Wi-Fi别名。先安装或复用ISR服务再异步Start，回调前注册、持久对象不从回调Stop或析构、地址依据Connected加原子IsInitialized、不跨任务借用恢复期间可销毁的GetNetif裸指针；有界TX池和重组区放PSRAM，实际内存预算待验证。仅要求服务地址的热点页保留原SSID／密码。真实预检缺0.7.0退出1且无stage；其精确manifest要求SDK>=6.0.1，本地5.5.6不能替代，单板显式锁SDK6及C++23，不修改其他板SDK。76份固定参考归档，另8个ML307变体完成配置与AT socket接口差异调查，保持inventory_only。旧OLED32及RLCD最终缓存构建和两镜像-ELF-分区检查均0，各205main／212输入摘要匹配、42产物归档，但NT26网络／IDF6／UART ISR／仅server POST分支未编译。9保护文件不变含人工决策与OTA。当前117官方源码／118本地profile／71待源码，累计64曾通过；当前2 passed／62 stale／6历史failed／118 not_run（47有源码），188实机全部not_run。未安装依赖或SDK、Git写入、刷机或发布。见[本批记录](nt26_cellular_adaptation_20261112.md)。


蜂窝传输预检后续：缓存准备工具复用单板manifest的IDF约束和已安装组件管理器的版本匹配器，创建目录前核对SDK声明基础版本，并记录版本／约束。实际NT26拒绝5.5.6退出1、RNDIS匹配SDK后报缺组件退出1，两者无stage；普通OLED32匹配5.5.6并成功暂存17既有组件退出0，205main文件摘要和板型记录一致，没有新增构建或安装。ML3073.7.5新增20份精确接口／实现／SDK参考，确认可用HTTP自定义transport与WS ext_transport，后续应保留现有协议而在原始AT TCP上补有界缓冲及ESP端TLS验证；原生SSL的auth0不能当作当前证书包验证等价实现。发现rtcp三参数访问参数3越界及FIFO溢出URC内同步AT关闭自等待，生成仅供审阅的精确依赖补丁草稿，未应用、安装或编译。MIPSEND参数含义仍缺模块AT文档，不按猜测修改。设备媒体PCM实际走同一WebSocket，非另一个HTTP媒体客户端。保护原控制协议／OTA／网络入口／板型登记不变，117官方源码／188变体计数不增加，全量目标保持。见[本批记录](cellular_transport_readiness_20261112.md)。


全部本地入口预检后续：准备工具新增--check-only，真实检查118个本地profile（117官方＋1历史），不暂存／解析依赖／构建。官方64项通过当前SDK与直接缓存检查；42项首缺组件、9项首缺BSP、1项首遇精确版本不符、1项SDK不符，合计53阻塞，0意外失败；历史入口另1通过。118暂存目录全部未创建。直接manifest合并46组要求：26组注册表组件缺失、1组固定Git源码、3组精确版本不符、12组精确匹配、4组范围未判断，完整版本与板型映射有JSON清单；esp_codec_dev／ST7796／GT911／微雪1.8 BSP的不同精确版本需要板级隔离，不能全局覆盖缓存。另确认ML307Tcp十六进制路径在上游921600下无法持续承载现有24kHz单声道PCM下行：仅数据最低960000波特，每20ms帧至少20.833ms；16kHz上行最低640000波特，双向独立判断，不把该结论套到其他调制解调器后端。需要验证更高波特率或双方压缩协议，同时保留TLS／超时／媒体功能，8个ML307变体仍inventory_only。117官方源码／71待源码／188实机not_run不变，64次累计构建与本次64项只读预检是不同证据。固件C++／人工决策／registry未改，未安装、构建、Git写入、刷机或发布。见[全部预检与带宽记录](all_profile_preflight_20261112.md)。


P4功能评估板／P4X后续：固定27个P4变体的6组30份源码及BSP／网络／模型／SDK共521份参考，新增2个独立P4修订源码候选，另25P4变体保持inventory_only。官方BSP5.2.3处理EK790071024×600DSI／GT911镜像／LDO3／非格式化SD缓存LDO4；复用现有单ES8311BOTH24kHz共享I2S0与PA53，避开BSP22050Hz及两个codec对象。Host2.12.11／Remote1.6.4C6SDIO复用现有AP绑定／WS媒体／OTA链路，未刷C6或初始化相机。旧P4min1.0与P4Xmin3.0独立project／库选择；mn7必需且无S3模型回退，词与阈值待声学验收。构建／登记／烧录芯片参数由profile一致驱动，新增错误芯片前置拒绝检查；公共-芯片-板型三层配置在缓存与Docker同步，118旧S3最后赋值表相等，芯片不兼容缓存按targets过滤。两P4预检真实缺BSP退出1且无stage／目标编译。旧OLED32／RLCD最终缓存构建及两镜像-ELF-分区检查均0，各205main／213输入摘要与最终工作区一致、23产物归档；全新OLED32暂存17源／冷配置退出0，实际三层顺序及S3参数正确但无应用镜像。mn7+FST单独打包退出0共2,681,351字节／4MB，不是P4识别或RISC-V编译证据。120项只读预检0意外失败，119官方64通过／55首阻塞（42组件、11BSP、1pin、1SDK），历史另1通过，无只读stage；历史118项与过滤前120日志保留。当前119官方源码／120本地profile／69待源码，累计64曾构建通过，当前2 passed／62 stale／6历史failed／118 not_run（49有源码），188实机全部not_run。控制协议人工决策和桥接等7保护文件不变；未安装依赖或SDK、运行Docker、Git写入、刷机、OTA或发布。见[本批记录](p4_function_adaptation_20261112.md)。


微雪P4／P4X SPI3.5后续：固定实际类而非ES7210头宏，新增ST7796SPI2mode3／80MHz320×480BGR／反色mirrorX／FT5x06I2C1 7-8／RST29IRQ50／swapXYmirrorX／单ES8311I2S0 13-12-10-11-9PA53和BOOT35源码，背光28从0开始、20行内部DMA，启动先松手再接收按住说话。复用P4 mn7、旧min1.0／P4Xmin3.0、C6 Hosted slot1上游默认配置和现有绑定／WS媒体／OTA；USB Serial JTAG24-25不与SPI20-21混用，LCD26-27占用且未启USBOTG。29来源／两目标真实缺ST7796=1.4.0退出1无stage或P4编译；旧立创FT5x06双codec和OLED32最终缓存构建／镜像-ELF-分区检查均0，各205main／213输入摘要匹配、23产物归档，不证明P4专用分支。此前120项预检输入指纹未变，本轮复用并加入2实际检查，122本地／121官方当前64预检通过、57首阻塞，不等于122次新执行或构建。当前121官方源码／122本地profile／67待源码，P4 27项中4源码／23待实现；累计64曾构建通过，当前2 passed／62 stale／6历史failed／118 not_run（51有源码），188实机全not_run。9保护文件不变，未安装、Git写入、刷机或发布；相机／SD缓存／电池／蓝牙／原厂启动开麦与重启手势未实现，仍需目标与实物验收。见[本批记录](waveshare_p4_spi35_adaptation_20261112.md)。


微雪P4／P4X 7B后续：新增独立1024×600 RGB565 EK79007 DSI双900Mbps／DPI52MHz H10-160-160 V1-23-12源码，LDO3=2500、复位33、背光32低有效且启动PWM1023暗；官方要求32MB Flash，本项目适配200KB只读nvsfactory、840KB nvs、4MB双app、18MB storage和4MB model标签分区，避免当前模型加载器仍指向model。帧缓冲1228800字节＋LVGL两20行及额外旋转缓冲122880字节PSRAM，PPA显式关闭，避开精确DPI无disp_on_off／swap_xy操作。GT911仅0x5D／0x14探测、I2C1 7-8、无变换／RST／IRQ；读错／多点／越界取消等松手，BOOT35启动松手门控。现有ES7210TDM4MIC1slot0／ES8311DAC24kHz／I2S0 13-12-10-11-9和软件AEC接入，PA53INPUT_OUTPUT读回由板级管，codecPA设NC避免重配置关闭输入，实际DACmute成功后才开启PA。主启动新增7B显示失败返回，驱动错误保留回调对象并暗屏PA关闭；原控制协议人工决策等8保护文件不变。35来源／两个目标真实缺EK79007=2.0.2~1退出1，无stage或P4编译；旧立创与RLCD最终缓存构建及镜像-ELF-分区检查均0，各207main／215输入摘要匹配、23产物归档。前122项预检条件等价复用并加2实际检查，官方123项64直接通过／59首阻塞（46组件、11BSP、1pin、1SDK），非本轮124新执行或构建。当前123官方源码／124本地profile／65待源码，P4 27项6源码／21待实现；累计64曾构建通过，当前2 passed／62 stale／6历史failed／118 not_run（53有源码），188实机全not_run。相机／SD缓存／蓝牙／电池未实现；未安装、Git写入、刷机、OTA或发布。见[本批记录](waveshare_p4_7b_adaptation_20261112.md)。


微雪P4／P4X 7寸ILI9881C后续：新增720×1280RGB565 MIPI ILI9881C双1000Mbps／DPI80MHz H50-239-33 V30-20-2／RST27／BL26／GT911 I2C1 7-8源码，固定lcd_init_cmds表直接归档；复用双ES7210＋ES8311 TDM4／MIC1／24kHz／PA53／软件AEC与P4 mn7／C6 Hosted路径，旧P4min1.0和P4Xmin3.0独立。两个目标真实预检缺ILI9881C1.1.0退出1无stage／目标编译；旧立创FT5x06双codec和RLCD最终回归均0，各210main／218输入摘要匹配、23产物归档，ILI分支未编译。当前125官方源码／126本地profile／63待源码，P4 27项8源码／19待实现；累计64曾构建通过，当前2 passed／62 stale／6历史failed／118 not_run（53有源码），188实机全not_run。见[本批记录](waveshare_p4_mipi7_adaptation_20261112.md)。


ST7701 4.3寸及源码纠正：新增P4／P4X独立480×800RGB565候选，DSI双500Mbps／DPI30MHz H12-42-42 V8-2-60，RST27／BL26低有效／GT911／双codec24kHz／mn7／Hosted；ST77012.0.2~2使用use_mipi_interface，无lane_num字段。4.3寸39条和ILI7的196条命令分别转静态C++数组、附许可并按字节长度延时核对；纠正ILI7误混8-10.1分支、误继承32MB和重复修订项、BL26高有效及日志。P4统一模型／修订／PPA检查和显示失败返回覆盖这两入口；7B保留范围覆盖200KB工厂NVS及840KB配置NVS。6项真实只读预检ST4.3／ILI7／EK7B各两项均缺驱动，未目标编译或创建目录。旧立创／RLCD最终回归和镜像ELF分区检查退出0，各214main／222输入摘要匹配、23产物归档，不覆盖P4分支。48份参考和最终快照归档；review-start含早期草稿，未冒称动手前基线。当前127官方源码／128本地入口／61待源码；P4 27项10源码候选／17待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（57有源码），188实机全not_run。未安装、Git写入、刷机、OTA或发布。见[本批记录](waveshare_p4_st7701_adaptation_20261112.md)。


HX8394 5寸及MIPI实现合并：新增P4／P4X独立720×1280RGB565候选，DSI双700Mbps／DPI58MHz H20-20-40 V4-10-24、RST27高有效、BL26高有效；HX8394驱动2.1.0按厂家5寸BSP1.0.4显式SKIP_I2C_INIT，避免重复占用共享I2C1和0x45未检查写入；该驱动无mirror，使用其默认初始化方向。将原7B／ILI7／ST4.3三个重复runtime合并为waveshare_p4_mipi_display.cpp和板级参数表，六旧文件在before完整归档后移除，三个旧面板字面值及两个vendor表不变。P4 MIPI的背光／PA准备提前到UI内存之前，其余音频、GT911、mn7、C6Hosted、WS和OTA复用；8项真实预检均缺各自面板驱动，未建目标stage。旧立创／RLCD最终缓存构建与镜像ELF分区检查退出0，各210main／218输入摘要匹配、23产物归档，不编译P4runtime。36份参考、参数比较和快照记录。当前129官方源码／130本地profile／59待源码，P4 27项12源码候选／15待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（59有源码），188实机全not_run。未安装依赖或SDK、写Git、刷机、OTA或发布；相机／SD缓存／蓝牙／电池未完成。见[本批记录](waveshare_p4_hx8394_adaptation_20261112.md)。


ST7703 4B后续：新增P4／P4X独立720×720RGB565候选，双480Mbps／DPI46MHz H20-80-80 V4-12-30、RST27低有效／BL26低有效，精确driver2.0.0使用默认表与use_mipi_interface，无lane_num。只扩展公共MIPI的面板参数和创建分支，原四面板参数、两vendor表及130旧profile不变；研究组只拆出4B两项，其余研究保留。10项真实MIPI预检全部缺各自驱动退出1，无stage／P4编译。旧立创／RLCD最终缓存构建及镜像ELF分区检查退出0，各210main／218输入摘要匹配、23产物归档，不覆盖P4分支。20份参考、8保护文件与批次快照核对。当前131官方源码／132本地profile／57待源码，P4 27项14源码候选／13待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（61有源码），188实机全not_run。未安装、写Git、刷机、OTA或发布；相机／SD缓存／蓝牙／电池未完成。见[本批记录](waveshare_p4_st7703_adaptation_20261112.md)。


JD9365四尺寸后续：新增3.4C800×800、4C720×720、8／10.1各800×1280的独立P4／P4X共8源码候选，双1500Mbps，方屏DPI46MHz H20-20-40 V4-12-24／BL26低有效，长屏DPI52MHz H20-20-40 V4-10-30／BL26高有效，均RST27低有效。driver2.0.2支持lane_num，四原始命令表197／197／174／200条各自转静态C++数组并独立逐项比较。小屏中央460px内容区及保守圆形安全布局待实物验收，厂家页面403／空正文／404未当规格依据。18MIPI入口真实预检均首缺驱动退出1，无stage或P4编译；旧立创／RLCD最终回归和镜像ELF分区检查均0，各211main／219输入摘要匹配，23产物归档，不覆盖P4。24来源、8保护文件、132旧profile、五旧面板参数／两旧表和研究分组保留。当前139官方源码／140本地入口／49待源码，P4 27项22源码候选／5待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（69有源码），188实机全not_run。相机／SD缓存／蓝牙／电池／原厂手势未完成，未安装、写Git、刷机、OTA或发布。见[本批记录](waveshare_p4_jd9365_adaptation_20261112.md)。


Nano10.1A后续：新增微雪P4 Nano／P4X Nano两源码候选，800×1280RGB565／JD9365独立200命令／双1500Mbps／DPI80MHz H20-20-40 V10-4-30，无LCD复位／背光GPIO；I2C1 7-8单0x45设备100kHz／50ms写超时，启动95=11、95=17、96=00且保留1000ms稳定等待，UI完成后percent映射0..255，ACK不冒称回读或亮度验收。厂家专用driver2.0.0会重复创建I2C1并不检查0x45写入，参考归档但不链接；候选使用espressifJD93652.0.2和原板表，替换待实物验收。单ES8311BOTH／单IN_OUT设备／STDmono16-24kHz，排除ES7210和TDM4、别名设备不重复释放；PA53读回／静音／增益实际回调、GT911100kHz／BOOT35／mn7／Hosted复用。140旧profile、九旧面板参数／三表、7保护文件不变；158项本地条件文本比较不是C++编译。20项MIPI真实预检均缺驱动退出1无stage，旧立创／RLCD最终缓存回归及镜像ELF分区检查均0，各212main／220输入摘要匹配、23产物归档，不覆盖P4。48来源及前后快照记录。当前141官方源码／142本地profile／47待源码，P4 27项24候选／3待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（71有源码），188实机全not_run。Ethernet／相机／SD缓存／蓝牙／电池／原厂手势未完成，未安装、写Git、刷机、OTA或发布。见[本批记录](waveshare_p4_nano_adaptation_20261112.md)。


Tab5后续：新增Tab5／Tab5P4X两源码候选，720×1280三面板，0x55双读固件1／3选ST7121／ST7123，只有明确未发现才选GT91114／ILI；未知／读错停止。ILI双900Mbps-60MHz-双帧／195表，ST双965Mbps-70MHz-单帧／ST712327表和ST7121默认表，STdriver24bit与RGB565DPI保留原层次。修正ST7123 A3／E8的40／15越界长度为实际39／14，并为本地精确STtouch1.0.2加读取前触点<=10检查，完整原源和许可保留，未猜补硬件字节。PI4IOE43／44只拥有天线0／PA1／LCD4／TP5／WLAN0，RMW回读保留其他位；厂家要求LCD释放输入上拉并指出C6RESET15，UserDemo确认resetactiveLOW及HEX200MHz，避免微雪默认54。ES8388DAC从模式＋ES7210TDM4MIC1slot0／24kHz／PA回读／mn7和现有绑定WS媒体OTA复用。两目标真实预检缺ILI1.1.0退出1无stage，旧立创／RLCD最终缓存回归和镜像ELF分区检查均0，各224main／232输入摘要匹配、23产物归档，不覆盖Tab5P4分支。43来源、142旧profile／旧音频本地宏文本比较、7保护文件及驱动／表精确差异归档。当前143官方源码／144本地入口／45待源码，P4 27项26候选／1待实现（CoreP4X需IDF>=6）；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（73有源码），188实机全not_run。相机／SD／RS485／USB／充电／蓝牙／电池／原厂手势未完成，未安装、写Git、刷机、OTA或发布。见[本批记录](m5stack_tab5_adaptation_20261112.md)。


CoreP4X后续：新增独立IDF>=6.0.2<6.1源码候选，ST7102480×480RGB565双600Mbps／DPI24MHz H2-40-40 V4-8-200，厂家M5GFX固定25命令＋SDKDPI、双帧921600字节及三20行57600字节缓冲、SW180旋转／CST3530输入同向。CST已知58／400kHz五命令setup，9字节头＋有界续读、总记录<=9／50字节缓冲、单指零键／ACK错误取消等松手。PM1已知6E签名2050／IOE4F RMW回读；精确PIN_1=bit0/PIN_3=bit2/PIN_8=bit7/PIN_9=bit8/PIN_10=bit9/PIN_11=bit10，纠正草稿数字名称作位号，PWM1零先于输出、1000Hz12bit，PA低字节04。ES8311DAC＋ES7210TDM4MIC1／24kHz I2S0 2-6-4-5-3／PA回读／mn7，C6SDIO44-43-45..48/reset42独立。GPIO0与IMUIRQ冲突待确认，先触摸控制；PSRAM8MB旧README与32MB新元数据冲突保留。用途限制文字冲突的KSDIYST驱动仅参考不链接，采用M5Stack公开命令和许可。新目标实际预检拒绝SDK5.5.6退出1无stage；修正后旧立创／RLCD最终回归及镜像ELF分区均0，各230main／238输入摘要匹配、23产物归档，不覆盖SDK6/P4。76来源、8保护文件、144旧profile／音频本地条件文本比较和最终快照归档。当前144官方源码／145本地profile／44待源码，P4 27项全源码候选但全待目标构建实机；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（74有源码），188实机全not_run。相机／SD／IMU／RTC／物理键／电池充电／蓝牙／原厂手势未完成，未安装、写Git、刷机、OTA或发布。见[本批记录](m5stack_corep4_adaptation_20261112.md)。


S31功能核心板后续：新增preview espressif-esp32-s31-function-coreboard-1独立IDF>=6.1<6.2源码候选，16MB/OCT200MHzPSRAM/320MHz，源码GPIO50..57／60／61与SDK固定soc_caps双核62脚范围对应。单ES8311BOTH／单IN_OUT设备／STDmono24kHz I2S0 MCLK52BCLK53WS55DIN54DOUT56/I2C0 SDA51SCL50/PA57INPUT_OUTPUT读回、codecPANC及实际静音增益回调；mn7自定义唤醒／软件AEC／绑定WS媒体OTA保留。无屏六位绑定码复用热点配网页，BOOT61先松开后长按3秒开查看窗口，不驱动WS2812GPIO60或以太网。Python/PS/CMake身份读取器同步S31白名单，145旧profile和本地音频宏文本比较保留；17来源与S31库符号/分派实现归档。目标真实预检拒绝5.5.6退出1无stage，旧立创／RLCD最终缓存回归和镜像ELF分区均0，各230main／238输入摘要匹配、23产物归档，不覆盖S31/SDK6.1。另核四C5板12原文，C3/C5/C6精确libmultinet实际反汇编入口返回NULL，当前自定义唤醒受阻；已询问固定词取舍，未擅改为按键模式或登记profile。当前145官方源码／146本地profile／43待源码，S31四项1候选／3待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（75有源码），188实机全not_run。RGB灯／Ethernet／SD／相机／电池／原厂手势未完成，未安装、写Git、刷机、OTA或发布。见[本批记录](s31_coreboard_adaptation_20261112.md)。


MazaS31后续：新增maza-ai-esp32-s31独立SDK>=6.1<6.2源码候选，复用A012.9寸128×296RAM／296×128横屏文本／C22125C全局刷各30字节波形、4736字节帧和既有有界刷新worker，保留BUSY/呈现回执/DND边界。SPI3mode0-40MHz MOSI33CLK34CS17DC35RST14BUSY13，POWER6高／PA2先关，BOOT1触摸键12音量4-61先松手多键取消。单ES8311BOTH checkedIN_OUT／STDmono24kHz I2S0 7-11-9-10-8，I2C1SDA3SCL0与实际创建总线一致，纠正原板音频构造端口0；PA2输入读回／实际静音增益回调／mn7自定义唤醒／软件AEC／WiFi绑定WS媒体OTA保留。146旧profile、146旧音频＋5纸屏本地条件文本、8保护文件和波形不变，静态比较不是目标编译。新目标实际预检拒绝SDK5.5.6退出1无stage；旧立创／RLCD最终缓存回归与镜像ELF分区检查均0，各230main／238输入摘要匹配、23产物归档，不覆盖S31分支。17来源及最终快照归档。当前146官方源码／147本地入口／42待源码，S31四项2候选／2待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（76有源码），188实机全not_run。Flash16MB源默认待实物，其他FPC／并口EPD／LED／电池断电／SD未实现；未安装、写Git、刷机、OTA或发布。见[本批记录](maza_s31_adaptation_20261112.md)。


Mosaico后续：新增espressif-esp-mosaico独立S31源码候选，按固定README要求SDK6.2提交7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe，版本与CMake只读HEAD保护分开。先读eFuseUSER_DATA0100／0101-0102选v1.0／后续布线；未知停止，无版本猜测或eFuse写入；I2C0 0-1与56-3、QSPIclock44reset42与42-44、codec电源56只在早期版区分。VCC60低／shutdown57开漏释放／PA45读回先关，CO5300480×480RGB565 QSPI40MHz CS50 D36-51-35-9／14命令唯亮度51从FF改00待UI，精确driver2.1.0／CST92200.1.0已知5A双协议和释放ACK路径，2x20行内部DMA38400字节。ES8311实际地址19的8位32、I2S54-37-49-40-52单IN_OUT mono24kHz／checkedmute-gain／AI7先松手／mn7及绑定WS媒体OTA保留。147旧profile与旧音频条件文本、8保护文件不变；49来源及SDK接口/命令差异归档，CMake精确提交保护未在目标SDK执行。新预检真实拒绝5.5.6退出1无stage，旧立创／RLCD最终缓存回归和镜像ELF分区均0，各234main／242输入摘要匹配、23产物归档，不覆盖Mosaico。当前147官方源码／148本地入口／41待源码，S31四项3候选／1待实现；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（77有源码），188实机全not_run。16MB源默认与OCT250MHz待实物；相机模块EEPROM／闪光／NAND／电量计封印配置／IMU磁传感／LED断电未实现，未安装、写Git、刷机、OTA或发布。见[本批记录](mosaico_adaptation_20261112.md)。


S31Korvo后续：新增espressif-esp32-s31-korvo-1 SDK>=6.1<6.2源码候选，800×480 RGB565 16MHz负PCLK／H4-8-8V4-8-8、双PSRAM帧1536000字节／两8行bounce25600，GT1151共享I2C0 0-1／400kHz、检查单点11字节校验与清报告结果。ES8389BOTH共享codec分IN／OUT，输入mono16kHz／输出stereo16kHz边界复制，use_mclkfalse从BCLK恢复／I2S2-3-4-6-5／PA7读回与真静音／保留TX／增益40dB和mn7／现有绑定WS媒体OTA。GPIO42ADC1CH0atten0dB复制固定Apache17位校准，四次均值／三次稳定更新／MODE与BOOT61语音卡片、音量键／SET保留取消未接重配网；只保证BOOT+ADC重叠和跨键取消，双ADC键同压组合待实物。SDK证实33/34与USBSerialJTAG冲突，KorvoRGB和MazaS31EPD显式改UART并关USJ，Maza仅四项defaults修正，历史证据保留。新目标真实预检拒绝5.5.6退出1无stage；旧立创／RLCD最终缓存回归与镜像ELF分区均0，各240main／248输入摘要匹配、23产物归档，不覆盖S31。21来源、148旧profile／音频本地条件文本、8保护摘要及最终快照归档。当前148官方源码／149本地profile／40待源码，S31四款全源码候选但全待目标构建／实机；累计64曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（78有源码），188实机全not_run。背光常供电仅DISP画面使能无PWM，相机／LED／SD／USB／电池／SET重配网未完成；未安装、写Git、刷机、OTA或发布。见[本批记录](s31_korvo_adaptation_20261112.md)。


ML307手册／ESP32后续：归档公开镜像固定提交的TCPIP V5／ML307R拨号V1／AT V1.8.1三PDF并提取全文、目视核对8关键页，确认MIPSEND第二项是进入模组栈的长度而非状态／服务器回执；原始encoding和>定长输入可用但需长度帧解析，TCP缓存MIPRD可有界拉取。ML307R PPP表为“-”，ML307A例子不能证明R的UARTPPP；IPR更高速率需实际查询，未猜3Mbps。精确3.7.5三文件补丁草稿增加发送owner互斥／逐块长度确认／超时锁存禁止续发、rtcp长度类型校验和保留原字段的无异常数值分类，保留接收OOB／FIFO自等待保护；未应用、安装、编译或测试，原始流／总deadline／连接ID复用和并发／TLS适配仍未实现。纠正早期长数字stoi推断：旧helper长度<10已挡长整数，实际是短数字HEX分类、空字段front及stod风险。另核6目录7ESP32变体24固定来源和6依赖参考，默认4MB只属配置非物理容量，MN2分派／内嵌系数符号实际读取不等于自定义口令成功；7项继续inventory_only。同步8蜂窝与7ESP32研究，240main和registry摘要与上批一致，未重跑构建或新增profile，仍148官方源码／149本地／40待源码、188实机not_run。见[本批记录](ml307_manual_and_esp32_readiness_20261112.md)。


ML307原始接收后续：在精确3.7.5的evidence五文件候选补丁增加显式独占raw模式开关和ReadMl307TcpCache：1..4096非零有界请求／socket0..5／try-lock竞争拒绝／1..60000ms请求I/O剩余deadline；MIPRD三数值头<=96字节／按声明长度消费二进制／CRLF后OK／空缓存仅OK兼容，缓存rtcp通知另发ML307_CACHE_AVAILABLE避免旧HEX误解。8KiB仅逻辑RX长度界＋4KiB固定响应数组，不冒称堆总上限。DMA池溢出与队列丢段ISR置原子loss，解析和交付前检查、错误锁存不搜索OK重同步，仍回收DMA；独立RAW_FRAME_ERROR通知owner而不在回调等待AT。raw会话普通命令超时也停止复用，esp_timer显式依赖。源码静态审阅非C++执行／测试，真实模组响应格式、原始发送／配置查询、接收ring背压、重连ID隔离、TLS-HTTP-WS和板级接入仍未完成。完整补丁未应用／安装／编译／链接，240main和registry与前批一致，8蜂窝研究追加证据、矩阵刷新；仍148官方源码／149本地／40待源码，188实机not_run，未重跑构建或刷机发布。见[本批记录](ml307_raw_receive_candidate_20261112.md)。


ML307原始发送后续：精确3.7.5六文件evidence候选补丁新增SendMl307TcpRaw，socket0..5／载荷1..4096／时限1..60000ms，命令TX-独立>提示符-原始载荷TX-接受长度+最终OK共用截止时刻，按原始字节发送不HEX或附加CTRLZ/CRLF。与MIPRD共用try命令锁和互斥读写状态，raw模式旧MIPSEND/MIPRD文本旁路拒绝；重复/错误阶段提示符-OK、连接/长度不符、超时/断开锁存UART会话，不隐式重试。精确UHCI0.4.0公开TX支持原子取消并在失败清FIFO，候选manifest精确固定该版本，13包/源码参考只读未安装；DMA丢段/poison置取消，原始读写TX均传入。没有序列号不能证明任意同长度迟到确认都相关，真实网络owner/配置查询/ring背压/TLSHTTP-WS/ID隔离仍未实现。八蜂窝研究更新，240main和registry原摘要不变，未应用/编译/测试或新增profile，仍148官方源码/149本地/40待源码，188实机not_run。见[本批记录](ml307_raw_send_candidate_20261112.md)。


ML307连接生命周期后续：六文件evidence完整候选增加Configure/Open/Close；ATE0及INITIAL后逐项设置/查询SSL关闭、原始编码与手动资源释放，打开指定TCP缓存模式2并查询MIPMODE和CONNECTED，关闭不传不支持的mode参数并核对结果/OK与状态。共享命令锁及每入口总deadline，最终发布前复核Configuring/Opening/Closing，禁止断线后复活；空闲断线保留Disconnected供显式关闭，读写中断线锁存全会话。六槽ID关闭后Retired不复用，需未来owner重启；无序列号仍不能证明任意同值迟到确认相关。raw模式禁止普通文本AT和波特率改动，读取AwaitOk分开尾界与最终OK，重复/无事务OK停止会话。仍无真实owner/硬件重启/SIM-APN-PDP/ring背压/TLS-HTTP-WS/蜂窝profile；候选未应用、安装、编译或测试。八ML307研究追加记录，240main及registry未改，仍148官方源码/149本地/40待源码，188实机not_run；未重跑构建、写Git、刷机或发布。见[本批记录](ml307_raw_tcp_lifecycle_candidate_20261112.md)。


ML307同步字节流后续：十文件evidence完整候选新增Ml307RawStream两文件，以固定4KiB本地数组先消费后MIPRD，事件通知加250ms补探测，不建后台线程或无界队列。AtErrc末尾追加Busy和映射，仅原始Read/Send登记及TX前锁/RX待解析返回；包装层仅此类退让一tick、在剩余时限重试，已发载荷失败不重发。一读一写方向锁分开，回调捕获独立共享通知状态、不等AT或访问接收数组；Stop唤醒本地等待，Close要求owner等调用结束后确认关闭，析构只注销不隐式AT。空闲断线可交付已完成的本地尾部后报ServerDisconnected；模组剩余缓存排空/真实EOF未证，HTTP Connection:close仍可能失败，不能宣称TLS/HTTP接入完成。MATREADY及畸形断线通知锁存raw错误。完整owner/ID租约/跨连接公平性/TLSHTTP-WS和物理验收仍缺，候选未应用、安装、编译或测试。八ML307研究追加，240main和registry未改，仍148官方源码/149本地/40待源码，188实机not_run，未重跑构建或写Git刷机发布。见[本批记录](ml307_raw_stream_candidate_20261112.md)。


固定唤醒词人工决定：[人工决策-2026-11-12 10:07:55] 牛牛接受C3/C5/C6采用库支持的固定唤醒词，逐板标明差异。独立清单为10项C3/4项C5/10项C6，共24变体；具体词/模型/资源和源码尚待核实，不把授权算成已实现或静默改按键模式。见[决定及逐板清单](c3_c5_c6_fixed_wake_decision_20261112.md)。

ML307认证TLS后续：十三文件evidence完整候选增加Ml307TlsStream，mbedTLS自定义BIO接原始TCP，要求证书包/时间验证配置、VERIFY_REQUIRED/原主机名/最终verify flags，SDK熵CTR-DRBG及按配置PSA初始化，最低TLS1.2，不用模块auth0替代。追加NetworkErrc::Busy独立映射和TLS上下文try串行；仅预事务忙/健康空缓存超时转WANT，写必须同原指针长度续写，到期停止上下文防新数据接旧记录；每次明文<=4096非总堆上限。只有close_notify返回干净EOF，裸TCP EOF/断线仍错误；通知和底层关闭分开报告，析构不detach全局证书包/全局PSA。真实owner/时钟/ID租约/跨任务调度/断线模组缓存排空/esp_transport/HTTP绑定OTA/WSS媒体仍缺，未应用安装编译联网或测试；八ML307研究追加，240main和registry未改，仍148官方源码/149本地/40待源码，188实机not_run，未写Git刷机发布。见[本批记录](ml307_tls_stream_candidate_20261112.md)。


C3/C5/C6固定词后续：固定v2.5.2三芯片默认均wn9s_nihaoxiaozhi/你好小智，24项逐板登记候选词与当前自定义你好小燕子差异；ESP-SR2.4.7 Kconfig有Hi乐鑫/HiESP/你好小智/HiJason，Alexa虽有文件无对应选项，HiESP/Jason元数据类型和目录后缀差异保留。九次objdump读取WakeNet/AFE/AEC实际分派，三芯片引用wn9s与单/双麦AFE，但AEC缺当前S3 FD模式/NLP接口，不能直接别名。evidence新增固定wake_word接口候选，精确模型及元数据词/16kmono单词/1024样本容量/2048输入上限，阈值只用模型默认加未校准播放margin0.05、原子意图consumer应用；旧自定义词PCM探针不套用。原组件packer独立产出125951字节单模型包并逐项核对offset/length/原文，非目标编译或识别。读取微雪C5 1.69完整GPIO/PA/触摸板类，QUAD PSRAM及Sensair资产wn9差异待真实接入。24项研究更新（4既有追加/20新增），240main和registry未改，仍148官方源码/149本地/40待源码，188实机not_run；未安装构建测试写Git刷机发布。见[本批记录](fixed_wake_backend_candidate_20261112.md)。


微雪C5LCD1.69后续：新增waveshare-esp32-c5-touch-lcd-1.69/assistant_c5lcd169原生源码候选，SDK>=6.1<6.2/上游全局Flash16MB非实测/板configQUAD80MHzPSRAM。ST7789RGB565240x280SPI2mode024MHzCS27MOSI7CLK23DC24/resetNC/gap0,20/invert/noaxesmirror/两20行DMA19200字节/LVGLcore0；I2C0SDA8SCL9/TCA9554P0PA低锁存-输入极性-方向-输入回读/P1touchreset低100高100/BL26PWM初始暗，CST816S0x15IRQ5轮询原始count-event坐标/多点错误取消等释放。ES8311BOTH单checkedIN_OUTmono24kHzI2S0 0-1-3-10-2/实际增益静音callback后才启P0；FIR16k/AFETYPE_SRcore0/C5公开SR_LOW_COST AECfilter2保留软件参考，无S3FD-NLPAPI不隐式忽略AEC失败。固定wn9s_nihaoxiaozhi/你好小智后端接入，UI实际词及profile差异明确，模型缺失拒绝后续启动；BOOT28松手PTT卡片，WiFi绑定WS媒体HTTPSOTA/NVS保留。Python-PS-CMake及缓存工具C5riscv/ROM入口增加。真实只读预检退出1拒绝SDK5.5.6且未stage，未构建测试；149旧profile保留，控制协议人工决定等保护原文不变。当前149官方源码/150本地/39待源码，旧64曾缓存通过现全stale/6历史failed/118not_run，188实机not_run。BQ27220电池写入/省电/原厂MCP未移植，RAM实时性声学及实体容量待验收；未安装写Git刷机发布。见[本批记录](c5_lcd169_adaptation_20261112.md)。


Moji2C5后续：新增movecall-moji2-esp32c5/assistant_moji2c5原生源码候选，README5.5对应SDK>=5.5.6<5.6/Flash16MB上游默认非实测/QUAD80MHzPSRAM。复用既有QSPI显示与逐条完全一致的184条VOCAT_PANEL_COMMANDS及许可证，360x360ST77916RGB565QSPI2mode040MHzCS3CLK0D9-8-7-6RST1/无镜像换轴gap0/两像素对齐/两20行DMA28800bytes/LVGLcore0；官方无触摸入口，不创建触摸对象或虚构触点，BOOT28松手PTT/卡片。PA5INPUT_OUTPUT先关回读/BL2先暗/codecPANC，I2C0 26-27/单ES8311BOTHcheckedIN_OUTmono24kHzI2S0 25-11-24-23-12，共享FIR16k/AFE_SRcore0/C5SR AEC/固定你好小智/模型启动门槛及UI，WiFi绑定WS媒体HTTPSOTA/NVS保留。核实UART0TX11RX12与I2S冲突，USBSerialJTAG13/14覆盖并CMake保护，实物USB走线仍待验。ST77916精确2.0.2提交接口源码只读未安装，真实预检SDK5.5.6过版本门槛后缺ST77916退出1无stage；150旧profile与控制协议等保护原文保留。当前150官方源码/151本地/38待源码，64旧cached全stale/6历史failed/118not_run，188实机not_run。LED电池ADC/睡眠/MCP/峰值内存/实时声学未完成，未安装构建测试写Git刷机发布。见[本批记录](moji2_c5_adaptation_20261112.md)。


ESP-SpotC5后续：新增espressif-esp-spot-c5/assistant_spotc5无屏入口，来源N8R8/8MBFlash8MBPSRAM非实测/QUAD80MHz/SDK>=5.5.6<5.6。MCU2/外设0先高锁存-旧hold释放-INPUT_OUTPUT回读-hold保持，PA23先关并实际DAC静音确认后受控；KEY5高有效松手PTT/BOOT28低有效松手后独立长按3秒手机页，两键取消。I2C0 25-26/ES8311BOTH单checkedIN_OUTmono16kHz I2S0MCLKNC BCLK7WS8DOUT9DIN6/use_mclkfalse/BCLK恢复/实际增益静音，C5SR AECfilter2/软件参考/固定你好小智模型门槛和手机提示；无屏六位码在音频前通过已有热点页面，卡片seen/DND未读保留。8MB双3MiBOTA/storage896KiB/model1MiB。真实缓存构建首轮漏GPIO头文件失败、修正后同隔离目录idf退出0；应用1550400/模型125951字节，image checksum+hash/ELF32RISC-V/分区二进制检查均0，255源输入/28产物日志摘要归档，SDK5.5.6-dirty+16本地override，不冒充干净安装/其他板编译或实机验收。151旧profile及控制协议等保护原文保留，当前151官方源码/152本地/37待源码，1currentcachedpass/64stale/6历史failed/117not_run，188实机not_run。RGB/IMU/电池/充电/睡眠/原厂关机MCP未实现；未安装测试写Git刷机发布。见[本批记录](spot_c5_adaptation_20261112.md)。


SensairC5生命周期后续：固定六文件完整板类核实输入为ADC1CH5/16k12bit12dB，输出I2S0PDM24k差分7/8PA1/up_sample_fs480及四scaleMUL4；codec set_fmt默认240且上游负端GPIO8却硬编码mux10，后续不能照搬。显示配置ST7789但调用ILI9341API/custom19命令/284x240/gap36,0/换轴mirrorY，CST816D仅触摸按键事件非已证屏幕坐标，外部wn9资产须显式替换。新增evidence两文件ADC-owner候选，SDK handle/new-codec/format/enable/read/close-deinit全在同任务，外部句柄可核对库close吞掉的stop/deinit结果；单槽/复制格式/1024PCM，50ms竞争及3000ms等待到期锁存、拒绝复用，无调用者buffer悬挂；task/bridge分引用、关闭需等公开回调结束，清理join句柄尚缺，超时需重启不猜资源释放。20来源及摘要归档，未应用/编译/执行/登记新profile，现有固件main/registry/Spot缓存输入原文不变；151官方源码/152本地/37待源码，1currentcachedpass/64stale/6历史failed/117not_run，188实机not_run。见[本批记录](sensair_c5_adc_owner_candidate_20261112.md)。


SensairShuttleC5整板后续：新增候选接入ADC1CH5/16k与I2S0PDM24k差分7/8/PA1，现有codec ADC库由同任务owner负责create/setfmt/enable/read/stop/deinit，固定槽/1024PCM/3000ms/到期锁存；PDM每次setfmt恢复up_sample_fs480与四scaleMUL4，GPIO8由矩阵反相输出，不复制上游硬编码mux10。新增284x240 ILI9341公开API+19条命令/gap36,0/换轴mirrorY/CST816D触摸键，无真实PWM背光只黑屏/显示；外部wn9资产不下载，固定wn9s你好小智。官方configN16R8/QUADPSRAM，组件ILI9341 2.0.2精确来源已归档但本机缺。依赖预检SDK版本过但缺组件退出1无stage，未构建Sensair；共享源码扩展后Spot C5隔离回归idf仍退出0。152官方源码/153本地/36待源码，1cachedpass/64stale/6failed/117notrun，188实机notrun；未安装写Git刷机发布。见[本批记录](sensair_c5_adaptation_20261112.md)。


证据修正及ESP-Hi后续：Sensair实际显示入口保持原状，研究未注册/模型名拼写与历史数字误改已纠正；当前Spot重新在相同SDK/cache隔离目录实际build退出0，262源输入+29产物摘要/镜像ELF分区检查0，新构建记录独立保存，不能由旧记录推断当前通过。152官方源码/153本地/36待源码，1currentcachedpass/64stale/6历史failed/117not_run。ESP-Hi固定10源码确认是160x80彩屏C3机器狗，ADC1CH2/PDM正6反7PA3/fs441，实际面板invertfalse/gap0,24与config不同，USB18/19和UART20/21均与舵机重叠。当前全功能五项载荷下限533376字节且PSRAM依赖，4MB上游为factory+assets非双OTA；未降级手动或单app，仍inventory_only，来源/资源/18命令证据归档。见[证据修正](board_evidence_reconcile_20261112.md)与[ESP-Hi前置核对](esp_hi_c3_readiness_20261112.md)。


Sensair ADC就绪后续：修正codec_dev open忽略set_fmt/enable返回值时，ADC对象is_open不等于已配置并启用的问题；owner独立configured/enabled和终止状态，格式/启用非法或不可执行就拒绝复用，PDM状态查询改用实际ADC就绪。只改3份Sensair文件，registry和协议人工决定等不动。现有C5工具链真实编译owner/PDM/显示/通用audio四份文件退出0，实际ILI9341头文件只供参考不安装，4对象RISC-V REL检查0；不是Sensair配置、组件驱动或整板链接通过，仍缺ILI9341组件/物理及错误注入验收。152官方源码/153本地/36待源码，现有全源码账本0currentcachedpass/65stale/6历史failed/117not_run，188实机not_run；未安装写Git刷机发布。见[本批记录](sensair_adc_readiness_20261112.md)。


ML307 S3候选编译后续：6份真实Xtensa S3/C++23源码编译通过（AT/原始流/TLS/错误/旧TCP/UHCI），首次发现bool原子is_always_lock_free在现有S3 PSRAM workaround不成立；新增内部RAM短portMUX标志，AT raw及UHCI运行/溢出/取消接口同步修正并检查owner/cancel地址，非声称无锁/任意ISR-cacheoff保证。成对13文件ML307+4文件UHCI补丁仅在evidence，ABI改变必须同编译、不安装。TLS基础S3配置无日期校验，显式开启日期宏后真实认证成功分支也编译0，完整库仍需Kconfig重建/可信时钟；6对象XtensaREL与UART无atomic辅助符号检查通过，35来源/全部命令摘要归档。固件main和registry未变、8ML307只调查不登记；152官方源码/153本地/36待源码，0currentcachedpass/65stale/6历史failed/117not_run，188实机not_run。见[本批记录](ml307_s3_compile_candidate_20261112.md)。


ML307组件完整链接后续：新隔离工程使用同一成对候选与SDK5.5.6-dirty，以真实Kconfig开启证书日期校验并完整重编mbedTLS及24份ML307/UHCI源码。首次全局no-GC引入SDK非当前配置符号而失败，保留日志；改为两组件whole-archive及767导出代码符号逐项保留、SDK正常GC后idf build退出0，767项全在Xtensa EXEC，镜像988176字节checksum/hash有效。SDK仍有两个强制旧启动名UND而实际esp_startup实现存在，如实保留不写零未定义；源码未改，空入口/单factory仅供组件检查，未移作板卡固件。628编译源摘要/35产物与SDK快照/全部检查命令归档。8ML307研究追加，main/registry不动，仍152官方源码/153本地/36待源码，0currentcachedpass/65stale/6failed/117not_run，188实机not_run；owner/时钟/认证传输接入和整板验收仍缺，未安装测试写Git刷机发布。见[本批记录](ml307_component_link_20261112.md)。


C3/C6语音组件链接后续：当前fixed_wake_word原文分别以真实C3/C6 SDK配置、ESP-SR2.4.7芯片库及四本地override实际build退出0，两个RISC-V EXEC保留后端四入口/WakeNet9s/单麦AFE表/AEC配置创建处理释放等符号，非S3别名；无PSRAM空入口工程，未执行识别。镜像386640/359520字节，单模型125951字节与既有你好小智包逐字节相同；SDK遗留start_app强制UND保留、实际esp_startup存在。两份模型/镜像ELF/分区校验0、芯片库及直接编译输入摘要归档，4MB两1856KiB槽仅为组件布局，不证明完整终端RAM/Flash或OTA。20项C3/C6只追加证据不登记profile，完整功能内部RAM流控/AEC接入与整板验收仍缺。main/registry原文不变，152官方源码/153本地/36待源码，0currentcachedpass/65stale/6failed/117not_run，188实机not_run；未安装独立测试写Git刷机发布。见[本批记录](c3_c6_speech_link_20261112.md)。


AEC参考缓存内存后续：共享audio_io将8192样本的逐点64位时间戳改为原始push起点/已消费计数的256段有界记录，保持原floor时间公式/PCM/静音/过期/容量淘汰；元数据满则播放包明确失败、清参考并返回NO_MEM，不忽略丢参考。新增头文件+修改audio仅2份，协议人工决定/所有旧main/registry保留原文。S3当前真实音频单文件编译0；SpotC5新隔离整板SDK5.5.6-dirty/16override实际build0，ELF新对象20496对旧PCM/时间戳/索引81928节省61432字节，镜像1550800/模型125951、身份和双OTA分区检查0，263源输入/23产物归档。24项固定词研究仅追加共享资源证据，C3C6完整音频与协议533376字节下限/运行验收仍缺，不注册新板。当前152官方源码/153本地/36待源码，1currentcachedpass/64stale/6failed/117not_run，188实机not_run；未安装测试写Git刷机发布。见[本批记录](audio_reference_buffer_20261112.md)。


ML307传输适配后续：evidence新增raw/TLS就绪提示及esp_transport认证TLS父层，poll不发AT、TLS等待不占SSL锁，健康等待与终止错误分开，只有close_notify映射SDK干净FIN-1；write<=64KiB入口内循环短写、部分失败停止，不将部分成功返回给WS。owner仅定义可信时钟/Acquire/Retire契约，真正模组ID复位资源尚缺。发现SDK关闭检查对无socket直接FD_SET(-1)，独立补丁改父层poll/read且共截止时间，零时限不读、错误/额外字节不当正常关闭；HTTP内置SSL ticket私有ABI不兼容时显式拒绝。15文件ML307+4UHCI+1SDK成对补丁固定来源，不改已安装SDK。新S3隔离真实HTTP/WS1.8.0 API/SDKoverride一起build最终0，634编译源记录/28快照/前767符号保留，镜像1016352字节校验有效；首次Windows CMake路径错误退出2日志保留。8项仅追加候选不注册板，main/registry/Spot当前缓存输入不变；仍152官方源码/153本地/36待源码，1currentcachedpass/64stale/6failed/117not_run，188实机not_run，未安装测试写Git刷机发布。见[本批记录](ml307_transport_adapter_20261112.md)。


ML307启动就绪后续：依赖候选相对前批7文件修正CPIN必须有效READY、CEREG单stat/可选字段越界与类型、注册独立事件；ML307仅有效激活PDP地址+注册+SIM才发布networkready，无IP不返回Ready。回调句柄由派生析构先注销、发布串行及核心原子状态，UART对象显式内部RAM/初始化检查/任务创建失败不假就绪，未知型号拒绝默认ML307；默认等待有限，但通用TX/构造清理未组成严格总deadline。固定17来源核8变体GPIO：6项TX12RX11，ATK/CAM43/44与默认控制台冲突，只有ATK扩展器12为4G使能源码候选，复位脉宽/硬件未知，其余无独立复位定义。20文件ML307+4UHCI+1SDK完整配对补丁，实际S3当前ABI库重编及767当前符号保留链接0，镜像1019824/634编译源/28快照校验归档；未运行startup或owner。8项只追加不注册，main/registry/Spot当前输入不变；152官方源码/153本地/36待源码，1currentcachedpass/64stale/6failed/117not_run，188实机not_run，未安装测试写Git刷机发布。见[本批记录](ml307_startup_readiness_20261112.md)。


ML307启动就绪后续：依赖候选修正CPIN必须有效READY、CEREG单stat/可选字段越界与类型、注册独立事件；ML307仅有效激活PDP地址+注册+SIM才发布networkready，无IP不返回Ready。回调句柄由派生析构先注销、发布串行及核心原子状态，UART对象显式内部RAM/初始化检查/任务创建失败不假就绪，未知型号拒绝默认ML307；默认等待有限，但通用TX/构造清理未组成严格总deadline。另增连接owner：启动成功后租用0-5 ID，关闭未确认置reset_required不静默复用。固定17来源核8变体GPIO：6项TX12RX11，ATK/CAM43/44与默认控制台冲突，只有ATK扩展器12为4G使能源码候选，复位脉宽/硬件未知，其余无独立复位定义。21文件ML307+4UHCI+1SDK完整配对补丁，实际S3当前ABI库重编及793当前符号保留链接0，镜像1022976/635编译源/28快照校验归档；未运行startup或owner。8项只追加不注册，main/registry/Spot当前输入不变；152官方源码/153本地/36待源码，1currentcachedpass/64stale/6failed/117not_run，188实机not_run，未安装测试写Git刷机发布。见[本批记录](ml307_startup_readiness_20261112.md)。


ML307连接owner最终候选：新增具体管理层，要求板级确认新模组会话和可信时钟；检测ML307、SIM/注册/PDP后开启raw接收，六个ID只用一次，关闭未确认保留流并要求reset，活动租约保持owner生命周期。未实现板级复位/时钟回调或固件工厂接入。此前793符号首次owner候选含ID复用/raw未开启问题，已由当前817符号、1026832字节修正版替代；635编译源/28快照，SDK组件完整build与链接退出0，未运行模组或状态机。只修改evidence候选与8项研究，main/registry和Spot当前构建输入未变；152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run。见[最终owner候选](ml307_connection_owner_20261112.md)。


ML307启动截止时间后续：新增SendStartupCommand/RunStartupCommand共享单一TX+响应deadline，波特率逐项检查剩余预算、AT+IPR后验证AT；raw owner传raw_transport=true跳过旧HTTP清理，SIM/注册/PDP后才开启raw接收，CME畸形字段也锁存startup failure。当前S3真实HTTP/WS/owner链接0，823当前符号、镜像1030240、635编译源/28产物，app_main空；没有模组超时、时序、板级reset/clock回调或实机验收。22文件ML307+4UHCI+1SDK完整补丁，8项仅追加候选，main/registry/Spot不变；152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run。见[本批记录](ml307_startup_deadline_20261112.md)。


ML307客户端工厂后续：新增HTTPS/WS包装工厂，显式配置外部WSchild路径/query/鉴权headers/443/control传播，保留4096缓冲/8192任务栈/10s网络/5s重连/10-15spingpong；URL解析复用SDK并限URI383/headers1024/握手缓冲，HTTPS/WSS不退明文。HTTP禁自动跳转、WS新局部config选项拒3xx且强制101+既有Accept校验，SDK默认false保持；源头/头文件一起构建，未改安装SDK。析构HTTPclient后parent、WSclient后child后parent，要求外部sender先join且不能在callback销毁，SDKjoin无界仍待解决。真实S3组件build0，835当前代码符号全保留，镜像1033168/636编译源/28快照，8API/调用点来源归档；未运行绑定OTA握手或接入生产调用。24ML307+4UHCI+2SDK完整补丁，8变体仅追加候选，main/registry/Spot不动；152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run。见[本批记录](ml307_client_factory_20261112.md)。


ML307默认PDP/供电时钟后续：新取固定镜像ML307R硬件及通信手册并核blob，复用TCPIP共3PDF，渲染12页实际目视7页；VBAT复位需低于1.8V连续>=100ms，RESET/PWR_ONOFF另有时序，BOX2使能不等于复位证明；CCLK指南UTC主体/四分之一小时offset未实现信任绕过，auto-baud可能无MATREADY。候选共享CID1，忽略其他CID上下线，1/2地址至少1有效；注册后查询未激活则一次MIPCALL=1,1并等地址，socket设置回读MIPCFGcid1，无写死APN/CGACT。4源文件修改，完整24ML307+4UHCI+2SDK配对，真实S3组件构建链接0/835符号，生成器首轮CRLF失败退出2已保留并修正；当前636编译源/28快照/镜像摘要见证据，未执行模组或板级回调。8项仅研究追加，main/registry/Spot不变，152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run。见[本批记录](ml307_pdp_power_time_20261112.md)。


ATK BOX2模组控制接口后续：现有板级驱动新增atk_box2_set_modem_enable(bool)，复用PA同一TCA9555设备/锁，只操作bank1bit4，输出锁存/输入极性/方向各掩码回读后检查输入引脚；普通WiFi启动无调用且继续保留4G状态。7份固定来源及4仓库tree/原PR只读调查，仍未建立BOX2 4G放电/复位电路证明，不把GPIO回读当模组新会话。2main文件最小改动，其余main/registry原文保留；新BOX2 WiFi实际SDK5.5.6缓存完整构建及镜像/ELF/双OTA检查0，新接口编译对象存在、未接4G启动。当前新BOX2记录替代过期构建，152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run；未安装测试写Git刷机发布。见[本批记录](atk_box2_modem_control_20261112.md)。


ML307时钟快照后续：AT文本拆分改为保留引号内逗号，CCLK日期/时间不再拆成两字段；64字段/65536bytes/NUL/畸形引号失败，不支持特殊转义引号。新增日历/闰年/时分秒/可选quarter-hour offset校验与ReadClockSnapshot串行启动查询，返回字段和单调收件时刻，无UTC转换/系统校时/可信时间自动放行。两手册UTC与当地时间说明差异保留，owner在PDP后raw前可调用显式accept_clock_snapshot，仍须原信任检查且预算内；未实现具体固件采用回调。8候选文件变化，26ML307+4UHCI+2SDK完整补丁，真实SDK S3完整组件build0/842当前符号、镜像1038432/637编译源/28快照，app_main空、未执行解析/CCLK/回调或测试。8项只追加研究，main/registry/BOX2当前构建输入原文不动，152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run。见[本批记录](ml307_clock_snapshot_20261112.md)。


设备HTTP调用点后续：新增DeviceHttpClient头/实现，绑定pairing_post和OTA实际使用共用创建/释放，保持SDK设置方法/Authorization/ContentType及完整响应/长度SHA板型双OTA；vTaskDelete前各HTTP错误路径显式Reset。默认现有网络栈，HTTPS补证书包/禁跳转，ML307条件分支调用真实候选工厂且必须启动先注册owner，缺owner明确失败不走WiFi。未安装候选/注册新profile/改控制协议。当前BOX2WiFi reconfigure build0，实际main/OTA/deviceHTTP重编，镜像2235344/模型2203819，265源/24快照/检查0；独立蜂窝真实当前deviceHTTP源加ML307宏与842候选符号链接0，镜像1039440/638编译源/30快照，空app_main仅组件，不是蜂窝整板。3现有main修改+2新增，其余main/registry保留。9相关变体只追加研究，152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run；未测试安装写Git刷机发布。见[本批记录](device_http_client_20261112.md)。


设备WS/蜂窝网络入口后续：新增DeviceWebSocketClient两文件，真实control_protocol仅创建段改持有包装对象并清理注册/SDK启动失败，默认4096buffer/8192stack/5s重连/10s网络/10-15spingpong，原音频/事件/人工决定区段逐字一致。owner统一网络模块注册，HTTP/WS同借；ML307 start真实Start60000/地址来自新增NetworkReady的PDP/raw健康，无owner明确失败不走WiFi。ML307 WSS服务配置页不要求SSID且保留旧WiFi字段；没有正式板型回调/注册。8main修改+2新增，candidate仅owner两文件增方法；BOX2WiFi完整build0，镜像2236000/模型2203819/267源/24产物；实际蜂窝network/config/portal/HTTP/WS组件build0，843候选符号、镜像1464016/779编译源/38快照，main/OTA/protocol3真实临时蜂窝编译退出0，非正式4G整板。9研究仅追加，152官方源码/153本地/36待源码，1passed/64stale/6failed/117not_run，188实机not_run，未安装测试写Git刷机发布。见[本批记录](device_websocket_network_20261112.md)。


绑定HTTP会话后续：只改main.cpp，创建绑定执行一次，结果查询使用第二个固定URL客户端并在完整消费响应/允许保活时复用；2047字节上限、读到结束、部分/过大/失败均Reset包括未完成TLS parent，下一次三秒查询再建客户端，不自动重发device-start。六ID耗尽、owner替换/板级复位仍未解决。BOX2 Wi-Fi当前全构建退出0，真实main.cpp显式ML307/C++23编译退出0，非蜂窝整板/联网/实机。9项研究追加，官方152源码/188变体、153本地profile、36待源码；当前1passed/64stale/6failed/117not_run，188实机not_run。未安装、测试套件、写Git、刷机或发布。见[本批记录](pairing_http_session_20261112.md)。


Sensair C5首个真实整板构建后续：复用既有SDK ST7789绘图/窗口/旋转，公共init回调保留旧ILI9341前导及原19条板级命令，不调用额外RAMCTRL默认初始化；仅移除Sensair的ILI9341依赖。Sensair完整build0、BOX2Wi-Fi默认分支回归build0，固定你好小智/ADC16k/PDM24k/PSRAM/双OTA不变，未实机。全153本地入口只读预检已重新执行，具体首个阻塞见本批结果；预检不代表传递解析或构建。仍152官方源码/188变体、153本地、36待源码；当前2passed/64stale/6failed/116not_run，188实机not_run。见[本批记录](sensair_sdk_panel_20261112.md)。


固定词端云口令后续：发现本地你好小智与云端旧口令校验/提示不一致。固件仅hello增加wake_phrase，服务端两词白名单/旧客户端默认/未知拒绝，NativeDevice每会话复制口令；前缀、重复纯唤醒、诊断、Realtime/Chat/DeepSeek系统提示及连接/媒体/结束提示统一使用会话词。完整复核/AEC/插话/媒体/双OTA及人工业务规则保留，未服务运行/ASR/部署。C5 Sensair与S3 BOX2完整构建均0，各267输入/23和24产物；应用2492672/2236352，模型125951/2203819。24项固定词研究仅追加，仍152官方源码/153本地/36待源码，2passed/64stale/6failed/116not_run，188实机not_run。Opus仅完成依赖/协议前置核对，产品取舍待回复，未安装。见[口令记录](fixed_wake_session_20261112.md)及[Opus调查](opus_no_psram_readiness_20261112.md)。


全直接依赖准备后续：新增只读firmware_dependency_report，复用既有缓存扫描/SDK版本读取；当前153本地/152官方的970条声明全部列明，与前批153项实际预检的就绪分类一致，67本地/66官方直接条件满足、86受阻。发现164缺组件声明、11缺唯一BSP、5版本冲突、1Git固定提交、7SDK不符（重叠不能相加作板数），42未满足版本/Git组保留隔离。源标签v6.0.2已核实；v6.1.0/v6.2.0标签404，开发分支固定来源单列，Docker未启动/镜像元数据超时。150既有研究只追加，BOX3/微雪1.75两个已有源码入口补齐此前缺失的独立依赖研究条目，不增加profile。不安装/解析/构建/下载组件/改main或registry，现有2passed/64stale/6failed/116not_run及188实机not_run不变。见[环境准备清单](firmware_dependency_readiness_20261112.md)。


Quandong首次整板构建后续：只移除本板外部ILI9341依赖，公共SDK DCS操作配板级init/sleep回调保留原RGB前导、19条命令、BGR缓存08/最终A8、RGB565、sleep120ms及GPIO45PWM；不调用SDK默认RAMCTRL初始化，硬件型号仍ILI9341。Quandong完整build0、应用2249152/模型2203819，各267输入/24产物；BOX2共享显示回归build0、应用2236352/模型2203819。969直接依赖声明、68本地/67官方条件满足；历史依赖阻塞改为当前resolved_local_overrides，其旧证据保留。仍152官方源码/153本地/36待源码；当前2passed/65stale/6failed/115not_run，累计67官方曾缓存构建通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](quandong_sdk_panel_20261112.md)。


Freenove/K10首次整板构建后续：公共SDK DCS操作+原ILI9341-2.1.0默认18命令（含双F7）/BGR-RGB565前导/sleep120ms/原背光，移除两板外部依赖并保留Apache许可；物理IC仍ILI9341。Freenove经历GCC内部段错与FT5x06宏C++字段顺序错误，保留两次日志，显式原值赋值并以2jobs最终build0；同类微雪3.5字段写法同步修正但其整板未构建。K10首次整板与Quandong共用sleep回归最终均build0；应用2258560/2227024/2249152，模型均2203819，各269输入/24产物。main改4文件增表/许可2文件，音频/协议人工决定/绑定/OTA保持；967直接声明、70本地/69官方条件满足，两新板当前依赖resolved_local_overrides。仍152官方源码/153本地/36待源码，当前3passed/66stale/6failed/113not_run，累计69官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](ili9341_default_sdk_20261112.md)。


四GC9A01默认入口构建后续：固定2.0.4源blob已重核对，公共SDK DCS操作+原BGR/RGB565前导/42命令/原reset/板级背光与后置调参，原无sleep/brightness仍不增加；AtomS3/S3R/Magiclick专用GC9107分支/依赖保留。Moji、璀璨、Spotpear1.28BOX、Electron各自首次成功完整build0，Freenove共享显示回归build0，各271输入/24产物，璀璨8MB独立双OTA分区保留。963直接声明，74本地/73官方条件满足；历史3项failed和1项not_run获得新通过记录，旧证据保留。仍152官方源码/153本地/36待源码，当前5passed/68stale/3failed/112not_run，累计73官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](gc9a01_default_sdk_20261112.md)。


GC9107专用三板首次构建后续：AtomS3R EchoBase/Pyramid原19条BGR/COLMOD55、Magiclick2.5原24条RGB/COLMOD55后05，16位缓冲和ID/ADC选择/未知停止/ST7735分支保持；一次板级启动改用SDK DCS操作，不增加sleep/brightness。两款AtomS3R各自完整build0，Freenove回归build0，各271输入/24产物；Magiclick整板两次GCC内部错误（含1job）仍失败，3真实board编译单元0不能当整板；双OTA与8MB两底座布局核对。960直接声明、77本地/76官方条件满足，只有无PSRAM AtomS3保留GC9A01依赖/完整内部RAM语音缺口。上批GC9A01文档colorTX错误传播措辞已按原源码纠正。仍152官方源码/153本地/36待源码，当前3passed/72stale/4failed/109not_run，累计75官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](gc9107_sdk_20261112.md)。


Magiclick GCC定位/恢复后续：未改main/registry/依赖/SDK/优化；失败与成功目标的完整DSP预处理文本和参数去路径后相同，两份预处理编译0，原始Magiclick命令同路径重放0；随后IDF/Ninja自行重编原失败DSP文件并完整构建0，无对象拷贝。首个Magiclick完整通过记录独立保存，前两次失败不覆盖；GCC根因/可靠性未解决，不归因为内存或并发。仍152官方源码/153本地/36待源码，当前4passed/72stale/3failed/109not_run，累计76官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](magiclick_gcc_recovery_20261112.md)。


NV3023双板首次构建后续：固定1.0.1源/tree/blob/Apache许可核对；Magiclick2.4原默认25命令/COLMOD05/末200ms，星智0.85WiFi原54命令/COLMOD65/末120ms，16位buffer/BGR08前导和reset10/120ms保持，SDK DCS操作+板级reset/init回调，移除两板外部依赖；复位和窗口/像素错误检查改善，无新增sleepAPI。两板各自完整build0，Freenove共享显示回归0，各273输入/24产物。958直接声明、79本地/78官方条件满足，仍152官方源码/153本地/36待源码；当前3passed/75stale/3failed/107not_run，累计78官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](nv3023_sdk_20261112.md)。


ST7796双SPI板首次构建后续：微雪S3LCD3.5原版原17条专用表、LilyGO MVSRLoRa原1.4.0默认12表，保留BGR/COLMOD05、16位buffer和reset120ms，SDK DCS操作+显式reset/init，原触摸/电源/音频保持；3.5B/3.49AXS QSPI及CHD/CAM/P4依赖不迁移。两目标各自完整build0，Freenove共享显示回归0，各275输入/24产物。956直接声明，81本地/80官方条件满足，仍152官方源码/153本地/36待源码，当前3passed/77stale/3failed/105not_run，累计80官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](st7796_sdk_20261112.md)。


微雪4B GT911后续：保留TCA9554 P5/P6复位选0x5D及原RGB/电源/音频，新增固定5点轮询复用16位panel IO，ready/payload/ACK全部成功才发布；健康未就绪仅保持已确认快照，初次未知/错误后未就绪不作松手，多点/异常/失败仍取消等待释放。不提供GPIO/IRQ/sleep复位API，其他GT911板依赖不变。微雪4B首次完整build0、Freenove回归0，各277输入/24产物；955声明、82本地/81官方直接条件满足。152官方源码/153本地/36无源码及4已知必需功能缺口仍在，至少40未达完整源码范围；当前2passed/79stale/3failed/104not_run，累计81官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](gt911_ws4b_20261112.md)。


BigSmart GT911后续：复用原Gt911Polling，固定0x5D/noRST/noINT，保留raw240x320到(y,239-x)；完整多点count/初始与故障后真实松手/异常取消和每IO100ms，不借用4B选址复位，不增加IRQ/sleep/watchdog。BigSmart首次成功完整build0，立创SZPI FT5x06/PCA9557与Freenove默认分支分别回归0，各277输入/24产物；954声明、83本地/82官方直接条件满足。共享polling/电源/音频/协议人工决定/OTA源码保持；152官方源码/153本地/36无源码及4必需功能缺口仍在，当前3passed/79stale/2failed/104not_run，累计82官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](gt911_bigsmart_20261112.md)。


虫洞BOX可选触摸后续：原FT38-GT5D-GT14探测仅NACK前进，GT复用未改轮询，FT保留正式组件；多点/错误/越界取消并初始/恢复等待真释放，原坐标变换及GTrawX+80保持，pointer按FT handle或GTInit成功判断，不能把探测标志/全NACK当GT就绪。2.8首次整板build0，BigSmart/立创回归0，各277输入/24产物；3.5只有共享单元实际compile0（2.8config+LCD35宏+1.3.5参考头），仍缺ST7796且GT全高偏移未标定，不继承整板。952声明、84本地/83官方直接条件满足；152官方源码/153本地/36无源码与4必需缺口不变，当前3passed/80stale/2failed/103not_run，累计83官方曾缓存通过，188实机not_run。未安装测试写Git刷机或发布。见[本批记录](gt911_chd_20261112.md)。


虫洞BOX3.5后续：首次整板缓存构建通过，ST7796的1.3.5/1.4.0默认12命令核对一致，复用SDK DCS操作并显式替换reset/init；BOX2.8和LilyGO完整回归通过，各277输入/24产物。GT3.5的+80坐标及硬件未验收。951直接声明、85本地/84官方直接条件满足；152官方源码/153本地/36无源码、4必需功能缺口不变；当前3passed/81stale/2failed/102not_run，累计84官方曾缓存通过，188项实机not_run。首次脚本参数错误在配置时被拒绝，纠正后记录独立保留。详见[本批说明](chd35_st7796_sdk_20261112.md)。


太极派STD/PDM后续：独立首次整板缓存构建通过，新增固定360RGB565/QSPI面板路径，保留原216/184表和02/32+2C协议、GPIO47复位，24kHz音频/触摸/完整语音源码未改；CHD2.8默认选择器回归通过。各280输入，新板各26产物/回归24产物；949声明、87本地/86官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前3passed/83stale/0failed/102not_run，累计86官方曾缓存通过，188实机not_run。其它ST77916板型未迁移，触摸/声学/SDK强制UND仍未验收。见[本批说明](taiji_qspi_native_20261112.md)。


VoCatQSPI后续：首次完整缓存build通过，184原表/两版PCB动态复位/RTC供电/双codec与原触摸完整语音保持；复用360RGB565/QSPI核心，太极派STD/PDM各整板回归通过。各282输入/27产物；948声明、88本地/87官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前3passed/84stale/0failed/101not_run，累计87官方曾缓存通过，188实机not_run。其它ST77916入口未迁移；原SDK强制UND和真实板级/声学/触摸验收仍未完成。见[本批说明](vocat_qspi_native_20261112.md)。


微雪1.85/1.85B/1.85C后续：三个真实目标首次缓存整板build通过，3MHzID清理后80MHzQSPI/216默认-181B-V1-184V2原表/扩展器或直接复位/数字或双codec原音频保持；共享core增加NC软件01+120ms，VoCat直接复位回归通过。各283输入，新板26/回归27产物；945声明、91本地/90官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前4passed/86stale/0failed/98not_run，累计90官方曾缓存通过，188实机not_run。源审阅脚本误把占位存储当发送字节的判断已纠正，B原表未改。见[本批说明](ws185_qspi_native_20261112.md)。


Moji2C5QSPI后续：首次真实C5缓存整板build通过，原184表/GPIO1复位/无触摸/USB控制台/ES8311BOTH24k/fixed你好小智/SR-AEC/绑定媒体双OTA保持；共享core/音频/固定词源码不变，仅接入现有面板，VoCatS3完整回归通过。各283输入，Moji228/VoCat27产物；944声明、92本地/91官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前2passed/89stale/0failed/97not_run，累计91官方曾缓存通过，188实机not_run。SDK强制UND/单核资源/声学/硬件未验收。见[本批说明](moji2_qspi_native_20261112.md)。


微雪CAM四屏后续：四个真实目标首次缓存整板build通过，固定CH32协议24/02FF-0300-05PWM替代缺失组件，继承整端口复位及缓存ACK限制；失败锁存需重启。3.5复用已审阅ST7796默认12表/SDKDCS，另三款ST7789，原双codec24k/AEC/完整唤醒/绑定媒体双OTA保持；CHD3.5ST7796回归通过。各284输入，CAM26/CHD24产物；939声明、96本地/95官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前5passed/90stale/0failed/93not_run，累计95官方曾缓存通过，188实机not_run。原相机/触摸/电池/SD等未完成，未据build宣称外围或物理通过。见[本批记录](ws_cam_native_20261112.md)。


LCDWiki后续：首次真实整板缓存build通过，固定320x480ST77922原63表/CMD1-2-3页/MADCTL08-COLMOD55前导后00/01/NC软件复位/拒绝硬件swap/原软件旋转和4像素刷新保持；独立TDDI触摸、PA1LOW、单ES831124k和完整语音/绑定/双OTA未改。CAM1.83选择器回归通过；各287输入/26产物；938声明、97本地/96官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前2passed/94stale/0failed/92not_run，累计96官方曾缓存通过，188实机not_run。触摸ready语义/格式颜色/SDK强制UND/外围未验收。见[本批说明](lcdwiki_native_20261112.md)。


AXS后续：3.5B/3.49/3.49V2三个目标首次完整缓存build通过，保留34/2原表与前导、原整帧DMA/旋转/触摸/板级电源/音频完整语音，新增固定AXS面板路径；原ST7796的3.5整板回归通过。各290输入，新板26/原版24产物；935声明、100本地/99官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前4passed/95stale/0failed/89not_run，累计99官方曾缓存通过，188实机not_run。所有真实面板/DMA/时序/触摸/电源/声学和SDK强制UND仍待验收。见[本批说明](axs_native_20261112.md)。


LCD-EV2/GT1151后续：首次整板缓存build通过，保留14地址/11字节产品的原非零求和与字符规则、原报告/清除/取消逻辑，RGB800x480/24k双codec/完整语音未改；Board1.5的SUB2/FT/16k路径新stage回归通过。各291输入/26产物；934声明、101本地/100官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前2passed/98stale/0failed/88not_run，累计100官方曾缓存通过，188实机not_run。ready位语义/固定背光/SDK强制UND/实机仍待验收。见[本批说明](gt1151_lcdev2_20261112.md)。


SDK强制UND只读审阅：LCD EV Board2 S3及Moji2 C5各138显式链接输入的对象引用核对、ELF/镜像入口、启动函数表和反汇编通过；所列强制UND无对象引用，实际启动/main代码存在。仅这两份镜像静态结论，不能据UND名称断言缺启动，也未证明上电可用。SDK命名不一致未修改，其它板不继承结论。main/registry/research/matrix及所有构建记录摘要保持，计数2当前/98历史待重建/88未构建、100曾通过和188实机not_run不变。见[本批说明](sdk_forced_und_audit_20261113.md)。


RGB C后续：4.3C16MB/ES8311mono与7C32MB/ES8389stereo各首次整板缓存build通过，保留800RGB时序和板级选址，复用既有GT911轮询与提取的CH32输出/PWM/整端口ACK缓存故障锁存；CAM1.83共享CH32回归通过。各293输入，新板28/CAM27产物；930声明、103本地/102官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前3passed/99stale/0failed/86not_run，累计102官方曾缓存通过，188实机not_run。CH32整端口影响、GTready/长notready、RGB/立体声/声学/外围仍未验收。见[本批说明](rgb_c_native_20261113.md)。


SenseCAP SPD后续：首次整板缓存build通过，保留402条SPD2010默认表及20ms复位/FF用户页/mirror位1-0，新增软件旋转避开swapfalse不支持和初始化就绪门槛；工厂只读9000/32000、新NVS3B000、双应用分区及TCA/旋钮/ES7243ERIGHT+ES8311/完整语音源码保持。LCDWiki回归通过；各297输入，SenseCAP27/LCDWiki26产物；929声明、104本地/103官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前2passed/101stale/0failed/85not_run，累计103官方曾缓存通过，188实机not_run。触摸/AI相机/旋钮转动/其它外围仍未完成，实机和声学未验收。见[本批说明](sensecap_spd_native_20261113.md)。


LCD1.46SPD后续：首次缓存整板build通过，共用原402表/20ms复位/用户页SPD核心，跳过硬件swapfalse改SWrotation0；原数字16kRIGHT-24kLEFT/完整语音/PWR源码保持。触摸53有界BIOS-CPU-HDP/ACK检查，续包组装缺依据仅排空取消，明确仍未完成触摸兼容性。SenseCAP/VoCat完整回归通过；各299输入/27产物；927声明、105本地/104官方直接条件满足。152官方源码/153本地、36无源码、4必需缺口不变；当前3passed/101stale/0failed/84not_run，累计104官方曾缓存通过，188实机not_run。见[本批说明](spd146_native_20261113.md)。


SPD剩余数据后续：固定厂家Arduino/IDF与Espressif实现都只解析首HDP，剩余数据排空后82清除；移除本地额外拒绝条件，保留合法首报告到实际确认后发布，原有界限与错误取消不变。1.46重建及产物核对通过，2521440应用/2203819模型/299输入27产物；不是通用组包协议或实机已验证。源码152/188、36无源码、4显式功能缺口、104曾缓存通过不变；当前1通过/103历史待重建/84未构建、188实机not_run。见[修正记录](spd_remaining_reference_20261113.md)。


ML307租约回收后续：候选使用具体owner强引用，误交其它owner的流在任何owner锁之前转交真实owner，避免真实owner遗留Active槽和失效指针；正常Close及未确认关闭保留、六ID不复用约束继续生效。组件及设备HTTP/WS/network源码隔离构建与产物检查完成，未新增正式板型或整板构建记录，8蜂窝变体仅追加研究证据。152/188有源码、36待源码，1passed/103stale/84not_run，188实机not_run。见[本批记录](ml307_lease_owner_20261113.md)。


微雪C5LCD1.69后续：首次实际C5整板缓存构建通过，仅SDK声明改为>=5.5.6<5.6及registry说明同步；main运行源码、引脚、ST7789/CST816S/TCA9554、单ES8311/固定你好小智/SRAEC/绑定媒体双OTA不变。152官方源码/153本地、36无源码、4必需缺口保留；累计105官方曾缓存通过，当前1passed/104stale/83not_run，188实机not_run。见[本批说明](c5_lcd169_build_20261113.md)。


微雪AMOLED2.16面板候选：固定18厂家来源确认CO5300/CST9217；独立候选复用现有CO53002.2.0，15命令仅亮度51从FF改00，保留MADCTLA0及SPI2接线，组件编译/链接/镜像检查通过。未接入main/profile，触摸协议与真实修订及PMIC/音频/LVGL仍需整合，不增加正式整板构建通过项。全量直接条件报告106本地/105官方满足，927声明；152官方源码/36无源码/4必需缺口、1passed/104stale/83not_run、188实机not_run不变。见[本批记录](amoled216_panel_candidate_20261113.md)。


微雪AMOLED2.16整板：本地CO5300面板/有限CST9217legacy轮询/LVGLport/共享ES7210-ES8311/PA46/SD接入，保留15命令MADCTLA0和仅启动亮度0差异；首次真实S3整板构建与C5LCD1.69独立共享回归通过，触摸身份及追加reset/实际资源和声学仍未验收。152官方源码/153本地、36无源码/4必需缺口保留；累计106官方曾缓存通过，当前2passed/104stale/82not_run，188实机not_run。见[本批记录](amoled216_board_20261113.md)。


微雪1.75/1.75C整板：按固定24厂商+4上游来源核对，复用既有CO5300/LVGL/CST9217/双codec，保留独立14表/gap6/466圆屏和mirrorXY触摸；非C16MB有SD，C32MB无SD及MCLK16/reset1/2分别选中，原ALDO1/PA46/完整语音媒体绑定双OTA保留。两款首次整板与2.16共享回归通过，实机全not_run；累计108官方曾缓存通过，当前3passed/105stale/80not_run，152源码/36无源码/4功能缺口不变。见[本批记录](amoled175_family_native_20261113.md)。


BOX-3整板：保留TT探测ST7789/其它ILI9341及GT双地址- TT触摸顺序，有限TT原始报文解析，旧ILI9341长度标记修正，双codec/PA46/SDdock/完整语音绑定媒体双OTA接入；首次实际BOX3整板与1.75共享回归通过。核对LVGL注册覆盖默认方向后修正2.16的A0对应MV/MY并重建。109官方曾缓存通过，当前3passed/106stale/79not_run；152源码/36无源码/4缺口及188实机not_run保留。见[本批记录](box3_native_20261113.md)。


CoreS3整板：复用SDK已有DC输出启停支持GPIO35与SDMISO，未加SDK补丁；本地AW9523/AXP仅改所用位、AW88298真实静音/ES7210/ILI9341默认表/FT5x06/SD/触摸说话接入，首次CoreS3整板及BOX3共享回归通过，实机未验收。110官方曾缓存通过，当前2passed/108stale/78not_run；152源码/36无源码/4缺口及188实机not_run保留。见[本批记录](cores3_native_20261113.md)。


旧BOX/BOX-Lite整板：固定小智ILI9341表与旧BSPST7789差异明确，低有效reset48、WS47-BL45、无SD；BOX24kES7210-ES8311/TT触摸，Lite16kES7243E-ES8156/背光反相/无触摸及原BOOT卡片操作分别选中。两款首次整板及BOX3/2.16标准双槽两通道约定调整后重建通过；不是硬件故障复现。112官方曾缓存通过，当前4passed/108stale/76not_run；152源码/36无源码/4缺口及188实机not_run保留。见[本批记录](box_family_native_20261113.md)。


SH8601协议基础：固定47来源与1.8/2.06/1.32各9/11/11命令表，独立RGB565候选借用真实32位QSPI IO，经02/32封装复用SDK绘图/窗口，保留SH复位和不支持变换限制；仅三处启动亮度FF改00。候选组件编译/链接/镜像检查通过，未接入main/registry或增加正式整板通过。112曾构建/4当前passed/108stale/76not_run、152源码/36无源码/4功能缺口、188实机not_run不变。见[本批记录](sh8601_protocol_candidate_20261113.md)。


微雪2.06整板：审阅过的SH8601 RGB565桥接接入真实32位QSPI/SPI2CLK11RST8，11命令仅启动亮度FF改00，410x502/gap22/noSwapMirror，FT5x06RST9IRQ38、专用双codec24k接线/ALDO1-2/PA46/SD/完整语音媒体绑定双OTA保留。首次2.06整板及2.16 CO5300共享回归通过，实机未验收。113官方曾缓存通过，当前2passed/111stale/75not_run；152源码/36无源码/4缺口及188实机not_run保留。见[本批记录](amoled206_native_20261113.md)。


微雪1.32历史SH8601整板：保留466/gap6/11表仅启动亮度0和专用QSPI引脚，固定CST820五字节原始触点数/200ms复位/ID记录未认证，单ES8311BOTH/24kmono/PA46/原电源锁存/无SD/完整语音绑定媒体双OTA接入；首次真实8MB整板和2.06共享回归通过，非后期CO5300修订。114官方曾缓存通过，当前2passed/112stale/74not_run；152源码/36无源码/4缺口及188实机not_run保留。见[本批记录](amoled132_native_20261113.md)。


微雪原版1.8 SH8601：接入本地368×448/QSPI/FT5x06/TCA9554原版HIGH100-LOW300-HIGH150ms复位和单ES8311，保留24kmono/ALDO1/SD/自定义唤醒AFE-AEC/媒体绑定双OTA。首次16MB整板和1.32共享回归通过；累计115官方曾缓存通过，当前2passed/113stale/73not_run；仍152源码/36无源码/4已知缺口，188实机not_run。不能据此判定现有1.8实物修订或实机通过。见[本批记录](amoled18_sh8601_native_20261113.md)。


原版1.8接入后的共享路径重建：CO5300 1.8v2、AMOLED2.06/2.16/1.75/1.75C、CoreS3及BOX/BOX-Lite/BOX3九项整板当前源码缓存重建和实际产物核对均通过，保留各板显示触摸/codec/SD差异及自定义唤醒AFE-AEC媒体绑定双OTA。无固件引擎、注册、manifest或配置修改；当前11passed/104stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run保留。见[本批构建记录](shared_codec_rebuild_20261113.md)。


C5／无屏／OLED九板当前源码重建：四款C5保留固定你好小智，Doit/AtomEchoS3R/CAM-M12EchoBase及两款面包板OLED保留自定义你好小燕子；全部整板最终构建和真实镜像/ELF/分区核对通过，365份产物、每项330输入摘要。四个旧暂存目录缺独立板配置的准备错误已保留，补齐暂存配置后重试通过，未修改固件源码/注册/manifest/实际配置。当前20passed/95stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run继续保留。见[本批记录](c5_headless_rebuild_20261113.md)。


微雪十二屏板当前源码重建：CAM1.83/2/2.8/3.5、LCD1.46/1.85/1.85B/1.85C、AMOLED1.43C、LCD3.49/3.49-v2/3.5B全部整板构建与实际ELF/镜像/分区核对通过，498份产物，每项330输入摘要；分别保留屏幕触摸/单或双codec/数字音频差异、自定义唤醒AFE-AEC媒体绑定双OTA。检查脚本已纠正不能用通用WakeNet查询符号判定应用唤醒选择的错误假设，固件源码/注册/manifest/配置未修改。当前32passed/83stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run保留。CAM摄像头、外围及旧硬件修订不据此计为完成。见[本批记录](waveshare_panel_rebuild_20261113.md)。


RGB／纸屏／QSPI十六项当前源码重建：微雪4B/4.3C/7C、Kevin313、LCD EV1.4/1.5/2、LCDWiki、微雪1.54V2/3.97/RLCD4.2、Zectrix4.2、SenseCAP、Audio Board及太极派STD/PDM全部整板构建与真实ELF/镜像/分区核对通过，656份产物，每项330输入摘要；保留ES8389立体声、不同RGB接线、纸屏与反射扫描、麦克风及SenseCAP只读工厂区/独立NVS差异。实际LCDWiki为QSPI，本批无I80目标。固件源码/注册/manifest/配置不修改；当前48passed/67stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run继续保留。见[本批记录](rgb_paper_rebuild_20261113.md)。


语音终端十六项当前源码重建：ATK五款、CHD两款、Freenove、全动、立创开发板、BigSmart、Korvo2 Wi-Fi、K10、AtomS3R Echo Base/Pyramid和VoCat全部整板构建及真实ELF/镜像/分区核对通过，658份产物，每项330输入摘要；保留ATK BOX I80与运行时音频选型、不同codec/按键、K10 ES7243E和Pyramid外部时钟等差异，8MB底座分区和完整唤醒AFE-AEC媒体绑定双OTA不变。固件源码/注册/manifest/配置未修改；当前64passed/51stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run保留。不能把Wi-Fi入口计入蜂窝或全部外围验收。见[本批记录](voice_terminal_rebuild_20261113.md)。


便携／小屏／OLED十六项当前源码重建：StickS3、Stopwatch、Magiclick两款、Spotpear1.28Box、Movecall两款、Jiuchuan、KevinSPV4、乐动/掌控、MixGo、AI-VOX3、云聊、KevinBox2和HU087全部整板构建及真实ELF/镜像/分区核对通过，633份产物，每项330输入摘要；保留Quad/Octal、8MB/16MB、面板ADC修订、不同codec/OLED数字音频差异。Magiclick2.5本轮单任务成功，旧GCC内部错误根因仍未确定。固件源码/注册/manifest/配置未修改；当前80passed/35stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run继续保留。见[本批记录](portable_oled_rebuild_20261113.md)。


特殊终端二十一项当前源码重建：现有完整隔离目录中的机器人基础语音、摄像头板、无屏、小屏、OLED和Maza纸屏全部整板构建及真实ELF/镜像/分区核对通过，839份产物，每项330输入摘要；保留标准/PDM、Quad/Octal、Otto动态接线、SparkBot共用背光功放、无屏热点绑定和AiPi工厂区独立NVS。检查期望已纠正MVSRLoRa旧回调名和AiPi非PCA9557电源假设，错误记录保留，固件未改。当前101passed/14stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run保留。CardputerADV功能缺口、摄像头/运动/LoRa等外围不据构建计为完成。见[本批记录](special_terminal_rebuild_20261113.md)。


T-Circle时钟修正与十三款S3当前源码构建：厂家固定README确认MAX98357A，芯片规格明确不支持24kHz；两款T-Circle物理播放改为16kHz/stereo16复制mono，复用原网络24→16FIR及相同实际播放PCM的AEC参考，标准24k/PDM32k采集不变。编译器真实参数预处理和完整目标构建通过；十三项全部最终源码构建与产物核对通过，554份产物，每项330输入摘要。新任务目录串行逐板生成配置、归档后再切换；保留Genjutech两次GCC内部错误/原参数诊断成功/旁路缓存正式成功，根因仍未明；工具环境错误和中断记录保留。共享main两文件改动使旧101项当前记录过期，当前13passed/102stale/73not_run，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run不变。未安装、写Git或刷机发布。见[本批修正与构建记录](remaining_s3_rebuild_20261113.md)。


T-Circle时钟修正后S3/C5十五项独立回归：七款微雪AMOLED、三款BOX、CoreS3和四款C5全部当前源码整板构建与实际ELF/镜像/分区核对通过，632份产物，每项330输入摘要；保留SH8601/CO5300修订、单/双codec、SD、Quad/Octal、8/16/32MB及C5固定你好小智/S3自定义你好小燕子差异。C5使用真实RISC-V目标与125951字节固定模型，Spot无屏热点绑定路径保留。固件源码/注册/manifest/配置未再修改；当前28passed/87stale/73not_run，前批13项仍当前，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run保留。见[本批记录](clockfix_cross_chip_rebuild_20261113.md)。


T-Circle时钟修正后二十八显示入口独立回归：微雪CAM/小屏修订、RGB、LCD EV、LCDWiki、电子纸/RLCD、SenseCAP、Audio Board和太极派全部当前源码整板构建及实际ELF/镜像/分区核对通过，1182份产物，每项330输入摘要；保留显示/触摸/codec/采集/8-16-32MB/NVS工厂区等逐板差异，固件源码/注册/manifest/配置不再修改。当前56passed/59stale/73not_run，之前28项仍当前，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run继续保留。另刷新本机直接依赖923条、116本地/115官方满足直接条件，未构建73项中37有源码/36无入口；旧C5 LCD1.69需SDK6.1的安装指引已标注更正，当前已用5.5.6构建，不重复安装升级。见[本批记录及当前依赖分组](clockfix_display_rebuild_20261113.md)。


T-Circle时钟修正后二十一语音/无屏/OLED入口独立回归：ATK五款、CHD两款、Freenove、全动、立创/BigSmart、Korvo2 Wi-Fi、K10、AtomS3R底座、VoCat、Doit/AtomEchoS3R/CAM-M12无屏及两款面包板OLED全部当前源码整板构建与真实ELF/镜像/分区核对通过，880份产物，每项330输入摘要；保留各板codec/按键/采集/8-16MB/headlessportal差异。固件源码/注册/manifest/配置不改；当前77passed/38stale/73not_run，原56项仍当前，累计115官方曾缓存通过；152源码/36无源码/4已知缺口/188实机not_run继续保留。见[本批记录](clockfix_voice_headless_rebuild_20261113.md)。


T-Circle时钟修正后剩余三十七缓存目标重建：全部最终源码整板构建和每项8条实际产物核对命令通过，1509份产物，每项330输入摘要；保留Quad/Octal、8/16MB、headless/OLED/TFT、codec/输入/修订和AiPi工厂区等差异。固件源码/注册/manifest/配置不改；当前114passed/1stale/73not_run。另统一复核全部114当前构建（110S3+4C5），4757份归档文件约8.79GB摘要、输入与命令记录匹配；不是实机或干净发行验收。唯一过期CardputerADV继续保留必需功能缺口；其余73未构建中37有源码、36无入口，合计74项未取得当前构建通过。152源码/153本地/4已知缺口/188实机not_run不变。见[本批记录、统一复核及剩余74项清单](clockfix_final_cached_rebuild_20261113.md)。


剩余37源码入口环境准备：27项P4／P4X、4项S31、6项S3各有独立源码工程，逐板保留原始manifest／defaults／分区／板型锁及SDK精确要求；12210份文件约210MB复制后摘要匹配，1318份既有固件和证据文件未变。未解析或安装依赖、未构建／刷机，3项已知功能缺口继续保留；矩阵114当前缓存通过／1历史过期／73未构建与188实机not_run不变。安装由牛牛执行，具体逐板命令见[本批准备清单](pending_firmware_sources_20261113.md)。


ESP32七变体MN2接口核对：官方固定提交的两库与三API文件Git blob均匹配本机；真实60字节接口有命令注册、无loader切换，model_init字面量为COEFF_NULL／NULL且未引用已导出的系数getter，不能套用S3字符串构造路径。只追加七项研究字段，保留工具读取纠错与原始证据；1314份固件／注册／配置／构建记录不变，114当前缓存通过／1历史过期／73未构建及188实机not_run保持。需先获得可核实的旧版模型初始化实现，再做完整口令／语音／资源适配；未编译、安装、替库或刷机。见[本批接口与来源记录](esp32_mn2_interface_20261113.md)。


ESP32 MN2官方更新复核：固定当前官方2.6.2提交bb552f1a559396a17681299cb3e199a0f1a4520a，两份MN2库与两个模型API头仍与本机2.4.7同blob，CMake继续选同一路径，未发现本次初始化问题的新实现；不据版本号升级全项目或安装新esp-dl。仅追加七款研究后续，1314份保护文件、114当前缓存通过／1历史过期／73未构建与188实机not_run保持。见[固定提交和检查范围](esp32_mn2_interface_20261113.md)。


无PSRAM Opus产品取舍已确认：牛牛接受有损压缩，保留完整功能并逐板记录音质／延迟／功能差异。新增原生服务端分帧转换源码及独立固件编解码／分段缓存候选，C3／C6各有未构建组件工程；22变体只追加决策和源码进展。正式main／profile／配置／分区未改，lookahead和五项PCM缓存替换等整机接入仍待完成；1314份既有固件及构建文件不变，114当前缓存通过／1历史过期／73未构建与188实机not_run保持。未安装、编解码运行、构建测试、重载网关、刷机或写Git。见[本轮源码与执行边界](native_opus_source_20261113.md)。
