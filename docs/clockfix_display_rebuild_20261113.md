# T-Circle时钟修正后二十八款显示终端重建

本批为二十八款既有S3入口补齐当前输入的整板构建证据，覆盖微雪小屏／CAM修订、RGB大屏、LCD EV、LCDWiki、电子纸／反射屏、SenseCAP、Audio Board和太极派。它们分别使用自己的隔离build目录。本批不再修改固件main、注册、manifest、配置、分区或人工决策，只追加构建、研究后续和矩阵记录。

固定上游为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围 **154目录／188变体**；旧README的138／171不作实际总数。源码入口仍为152个官方变体、153个本地profile（含历史入口），36项无入口、4项已知必需功能缺口保留。

上批T-Circle修正只改变该板宏下的物理播放率和发送格式。共享文件摘要变化使旧记录过期，本批用实际目标重新配置、编译、链接和归档补齐新输入证据，不以“宏未选中”推测替代构建结果。

## 显示、音频和修订选择

| profile | 当前界面与保留的主要差异 |
| --- | --- |
| waveshare-esp32-s3-cam-1.83 | 240×284 SPI，无触摸；CH32输出、ES7210＋ES8311 |
| waveshare-esp32-s3-cam-2 | 240×320 SPI，无触摸；双codec及独立板型宏 |
| waveshare-esp32-s3-cam-2.8 | 240×320 SPI，无触摸；本板引脚与背光选择 |
| waveshare-esp32-s3-cam-3.5 | 320×480，ST7796默认命令＋SDK DCS操作；双codec |
| waveshare-lcd-1.46 | 412×412 SPD2010 QSPI＋本地SPD触摸；数字音频 |
| waveshare-lcd-1.85 | 360×360 ST77916，ID选择命令表、扩展器复位、独立触摸总线；数字音频 |
| waveshare-lcd-1.85b | 360×360 ST77916，GPIO复位、CST816S；ES7210＋ES8311／PA9 |
| waveshare-lcd-1.85c | 360×360 ST77916，扩展器复位、CST816S；当前V2.0双codec／PA15 |
| waveshare-esp32-s3-touch-amoled-1.43c | 466×466 CO5300／CST820家族初始化和原始触点；双codec，8MB |
| waveshare-esp32-s3-touch-lcd-3.49 | 172×640 AXS QSPI、整帧分条；独立触摸总线、双codec |
| waveshare-esp32-s3-touch-lcd-3.49-v2 | 相同尺寸，独立v2背光／引脚配置 |
| waveshare-esp32-s3-touch-lcd-3.5b | 480×320逻辑界面／AXS物理帧；单ES8311 |
| waveshare-esp32-s3-touch-lcd-4b | 480×480 RGB／ST7701板级命令；GT911、双codec |
| waveshare-esp32-s3-touch-lcd-4.3c | 800×480 RGB；GT911、ES7210＋ES8311、CH32控制 |
| waveshare-esp32-s3-touch-lcd-7c | 800×480 RGB，独立时序／引脚；ES7210＋ES8389，mono复制stereo，32MB声明 |
| kevin-yuying-313lcd | 376×960 RGB／GC9503V命令；无触摸、单ES8311、16k音频 |
| espressif-esp-s3-lcd-ev-board-1p4 | SUB2 GC9503CV 480×480 RGB／FT5x06，1.4接线、双codec16k |
| espressif-esp-s3-lcd-ev-board-1p5 | SUB2 480×480 RGB／FT5x06，1.5独立接线、双codec16k |
| espressif-esp-s3-lcd-ev-board-2 | SUB3 ST7262E43 800×480直接RGB／GT1151，双codec24k |
| lcdwiki-es3c35p | 320×480 ST77922 QSPI／TDDI原始触摸，单ES8311 |
| waveshare-esp32-s3-epaper-1.54-v2 | 200×200电子纸，无触摸，单ES8311、8MB |
| waveshare-esp32-s3-epaper-3.97 | 800×480电子纸，专用刷新／电源引脚，单ES8311 |
| waveshare-esp32-s3-rlcd-4.2 | 400×300 ST7305反射液晶，TE扫描完成路径，双codec |
| zectrix-s3-epaper-4.2 | 400×300电子纸，独立刷新，单ES8311、16k |
| sensecap-watcher | 412×412 SPD2010 QSPI，旋钮按下输入，ES7243E＋ES8311，独立工厂区 |
| waveshare-audio-board | 默认JD9853 320×172 SPI，BOOT输入，TCA9555、双codec |
| taiji-pi-s3 | 360×360 ST77916 QSPI／CST816S，标准I²S麦克风 |
| taiji-pi-s3-pdm | 相同显示，独立PDM麦克风及引脚配置 |

这些是当前源码和实际编译选择，并不替代真实屏控、PCB修订、颜色、圆屏裁切、文字／按钮布局或触摸标定。部分面板借SDK ST7789对象的DCS绘图操作配合自己的命令表，符号名不能当作实际面板型号。1.85C旧C1.0修订不继承当前V2.0配置结果。

RGB帧缓冲／bounce、SPI／QSPI条带和纸屏后台刷新分别沿原实现。RLCD是反射式液晶，不是电子纸；复用帧任务不等于复用纸屏波形。SenseCAP当前仅实现旋钮按下输入，未把未接入的触摸或旋转记成完成。Audio Board验证默认JD9853，不代表其可选屏幕、摄像头或触摸都已移植。

二十八项均保留自定义“你好，小燕子”、AFE／软件AEC、媒体、绑定、提醒和双OTA。没有以纯按键语音、关闭AEC或单应用分区取代要求；相机、LoRa、传感器、电池、SD、原厂全部手势及其他外围仍按各板既有记录，不据终端构建计为完成。

## Flash及NVS检查

1.43C和1.54 V2保留各自8MB分区，双app各0x2A0000、model0x240000。SenseCAP与7C声明32MB Flash，既有分区仍在16MB范围内，不自动扩容。其余沿原16MB配置；Flash／PSRAM实际容量仍需对应实物确认。

SenseCAP保留0x9000／0x32000的readonly工厂分区，工作NVS为0x3B000／0x4000，app从0x50000／0x350000开始。其他本批入口保留原NVS0x9000／0x4000。核对真实分区二进制和每板CSV的名称、偏移、长度、标志，而不是对全部板套一个NVS地址或应用分区大小。

## 实际构建和核对

使用已安装ESP-IDF5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake3.30.2、Ninja1.12.1、ROM20260528，以及每板stage原有组件override和offline_guard。只同步当前main、根元数据、芯片／板配置及分区后重新配置构建，不安装、下载或升级SDK／依赖。

最多两个独立stage同时构建，每项 `IDF_PY_BUILD_JOBS=2`。本批不新增ccache模式或改优化选项，也不复制别板对象／镜像。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/clockfix-display-rebuild-20261113/build_batch.py
```

每板实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，准确解释器路径、cwd、时间、同步文件和退出码保存在各板 `build.json`。

完成后分次调用 `collect_batch.py` 的 `collect`，最后汇总。每板八条真实产物命令为 `readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、实际分区二进制解析、SDK HEAD／status只读查询。命令、原输出、退出码和摘要保存于各板 `commands.json`。

```powershell
# python指上述已安装的IDF Python。
python -B evidence/clockfix-display-rebuild-20261113/collect_batch.py
python -B evidence/clockfix-display-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/clockfix-display-rebuild-20261113/finalize.py
git diff --check
```

`symbol-expectations.json` 保留逐板正反选择：例如7C必须选ES8389及mono扩展、SUB2／SUB3区分三线初始化与触摸、太极派区分PDM、纸屏保留generation提交／呈现接口。实际编译宏、所选main文件、模型和ELF一起检查，不从依赖目录存在或历史成功推断本目标已编译。

## 最终结果

二十八项整板构建和每项八条实际产物核对命令均退出0；每项保留330个当前输入摘要，共归档1182个产物。

| profile | app字节 | app分区剩余 | model分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| waveshare-esp32-s3-cam-1.83 | 2,250,432 | 895,296 | 1,990,485 | 43 |
| waveshare-esp32-s3-cam-2 | 2,250,432 | 895,296 | 1,990,485 | 43 |
| waveshare-esp32-s3-cam-2.8 | 2,250,432 | 895,296 | 1,990,485 | 43 |
| waveshare-esp32-s3-cam-3.5 | 2,252,032 | 893,696 | 1,990,485 | 43 |
| waveshare-lcd-1.46 | 2,521,440 | 624,288 | 1,990,485 | 43 |
| waveshare-lcd-1.85 | 2,530,208 | 615,520 | 1,990,485 | 42 |
| waveshare-lcd-1.85b | 2,555,680 | 590,048 | 1,990,485 | 42 |
| waveshare-lcd-1.85c | 2,559,120 | 586,608 | 1,990,485 | 42 |
| waveshare-esp32-s3-touch-amoled-1.43c | 2,546,992 | 205,520 | 155,477 | 41 |
| waveshare-esp32-s3-touch-lcd-3.49 | 2,259,616 | 886,112 | 1,990,485 | 43 |
| waveshare-esp32-s3-touch-lcd-3.49-v2 | 2,260,160 | 885,568 | 1,990,485 | 43 |
| waveshare-esp32-s3-touch-lcd-3.5b | 2,540,608 | 605,120 | 1,990,485 | 42 |
| waveshare-esp32-s3-touch-lcd-4b | 2,541,168 | 604,560 | 1,990,485 | 43 |
| waveshare-esp32-s3-touch-lcd-4.3c | 2,236,464 | 909,264 | 1,990,485 | 44 |
| waveshare-esp32-s3-touch-lcd-7c | 2,239,312 | 906,416 | 1,990,485 | 44 |
| kevin-yuying-313lcd | 2,245,200 | 900,528 | 1,990,485 | 40 |
| espressif-esp-s3-lcd-ev-board-1p4 | 2,543,296 | 602,432 | 1,990,485 | 42 |
| espressif-esp-s3-lcd-ev-board-1p5 | 2,543,296 | 602,432 | 1,990,485 | 42 |
| espressif-esp-s3-lcd-ev-board-2 | 2,530,464 | 615,264 | 1,990,485 | 42 |
| lcdwiki-es3c35p | 2,249,952 | 895,776 | 1,990,485 | 41 |
| waveshare-esp32-s3-epaper-1.54-v2 | 2,198,448 | 554,064 | 155,477 | 40 |
| waveshare-esp32-s3-epaper-3.97 | 2,740,672 | 405,056 | 1,990,485 | 40 |
| waveshare-esp32-s3-rlcd-4.2 | 2,229,824 | 915,904 | 1,990,485 | 41 |
| zectrix-s3-epaper-4.2 | 2,198,832 | 946,896 | 1,990,485 | 40 |
| sensecap-watcher | 2,544,976 | 600,752 | 1,990,485 | 44 |
| waveshare-audio-board | 2,258,208 | 887,520 | 1,990,485 | 43 |
| taiji-pi-s3 | 2,236,208 | 909,520 | 1,990,485 | 43 |
| taiji-pi-s3-pdm | 2,238,272 | 907,456 | 1,990,485 | 43 |

累计曾缓存构建通过仍为115个官方变体。本批为二十八项补齐新输入构建，之前二十八项仍当前，矩阵变为 **56项当前通过／59项历史通过但输入已过期／73项未执行构建**。没有新增源码入口、首次通过变体或任何实机验收。


模型均为2,203,819字节，使用MN5及自定义唤醒源码，排除固定词、手动唤醒和无屏UI选择。逐板核对项目身份、Xtensa／ESP32-S3、Flash设置、checksum／validation hash和实际分区，app／model必须确实容纳。

每板归档330个当前输入摘要、ELF、镜像、map、模型、分区、配置、编译命令及每个实际选中的main对象。最终审计核对旧main、注册、manifest、配置、工具和历史构建账本不变；研究只追加本次记录，之前28项当前通过仍保持，旧缺口不删除。

## 尚未构建项的本机依赖快照

本批另实际运行 `firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/clockfix-display-rebuild-20261113/direct-dependencies.json`，退出0。读取当前153个本地profile、17份已有缓存的真实声明和SDK版本；注册文件摘要与报告一致。当前923条直接声明中，116个本地入口／115个官方变体满足直接条件。它不执行安装、远程可用性查询或传递依赖解析，不能代替整板构建。

73个从未构建的官方变体中，37项有源码、36项无入口。对这37项的新报告按问题分组，保存为 `unbuilt-dependency-groups.json`。同一板可同时缺SDK和多个组件，以下数目不可相加当板型总数。

| 当前阻塞组 | 实际本机情况 |
| --- | --- |
| NT26 | 需SDK>=6.0.1,<6.2及78/uart-eth-modem0.7.0 |
| CoreP4X | 需SDK>=6.0.2,<6.1及自身组件清单 |
| 三款S31及Mosaico | 三款需SDK>=6.1,<6.2；Mosaico>=6.2,<6.3，另有固定提交门槛与组件版本约束 |
| P4／P4X共27项 | 缺Hosted2.12.11、WiFi Remote1.6.4，以及逐板屏控／触摸／BSP；不能共用未经确认的协处理器固件 |
| RGB Matrix | 缺esphome/esp-hub75 0.3.6 |
| Korvo2 RNDIS | 缺espressif/iot_usbh_rndis0.3.1，不能以已通过Wi-Fi入口替代 |
| AtomS3 Echo Base | 缺GC9A01 2.0.4，且本地唤醒／AFE／AEC等功能缺口未解决 |
| 两款Guition | 缺GT911 1.2.1，且本地唤醒、自然插话及双OTA等必需功能缺口仍保留，安装组件也不能单独解决 |

本表只陈述当前本地SDK／缓存和manifest所需条件，不承诺尚未联网核实的版本能下载，也不将历史SDK开发快照称为当前稳定发行。需要新增SDK／依赖时仍由牛牛准备，保留当前5.5.6环境；本批不执行安装或升级。

旧依赖文档中的C5 LCD1.69需要SDK6.1、历史BOX／面板缺组件等内容是当时快照。当前四款C5已在5.5.6独立目标构建，多个屏控已转为各自核对过的本地实现，不能按旧表重复升级或安装。已在旧文档顶部补充本次更正，原文留存。

SDK四个既有强制UND继续记录：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。原警告、dirty状态和缓存依赖边界保留，不称零UND、干净发行、启动或硬件通过。之前Genjutech／Magiclick的GCC内部错误根因仍未确认，本批其他目标成功不改变该结论。

188项实机仍全部`not_run`，唯一现有微雪1.8的具体修订仍待确认。本批未运行独立测试套件／类型检查、启动服务、安装依赖、Git写操作、刷机、OTA、上传或发布。物理时序、实际刷新／呈现、触摸、音频时钟、PA、唤醒／AEC／自然插话、联网绑定和同板OTA回滚仍需对应实机验收。

## 证据入口

- [最终审计](../evidence/clockfix-display-rebuild-20261113/final-audit.json)
- [逐板构建结果](../evidence/clockfix-display-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/clockfix-display-rebuild-20261113/collection-summary.json)
- [板型与独立stage](../evidence/clockfix-display-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/clockfix-display-rebuild-20261113/symbol-expectations.json)
- [当前本机直接依赖](../evidence/clockfix-display-rebuild-20261113/direct-dependencies.json)／[未构建37项的依赖分组](../evidence/clockfix-display-rebuild-20261113/unbuilt-dependency-groups.json)
- [T-Circle时钟修正记录](remaining_s3_rebuild_20261113.md)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
