# 十六款便携、小屏和 OLED 终端当前源码重建

本批重建 StickS3、Stopwatch、Magiclick 两款、Spotpear 1.28 Box、Movecall 两款、Jiuchuan、Kevin SP V4、乐动／掌控、MixGo Nova、AI-VOX3、云聊、Kevin Box2 和 HU-087。逐板核对显示、音频、内存及分区，不修改固件引擎、注册、manifest、配置、分区或人工决策。

小智 v2.5.2 固定提交为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围 **154 目录／188 变体**；README 的 138／171 是旧数字。仍有 152 个官方源码入口、153 个本地 profile（含历史入口），36 项无入口、4 项已知必需功能缺口保留。本批补齐当前源码构建，不新增源码入口或首次构建通过变体。

## 逐板差异

| profile | 当前界面／显示路径 | 内存与音频差异 |
| --- | --- | --- |
| m5stack-stick-s3 | 135×240 ST7789P3 配置，专用窗口 | 8MB／Octal，单 ES8311，M5PM1 功放／电源控制 |
| m5stack-stopwatch | 466×466 CO5300／CST820B 家族初始化 | 16MB／Octal，单 ES8311，M5IOE1 复位／电源／按键 |
| magiclick-2p4 | 128×128 NV3023 专用默认命令 | 16MB／Octal，单 ES8311，2.4 引脚与背光极性 |
| magiclick-2p5 | 128×128，ADC 选择 GC9107／ST7735 | 16MB／Octal，单 ES8311，未知修订停止，2.5 独立引脚 |
| spotpear-sp-esp32-s3-1.28-box | 240×240 GC9A01 默认命令＋SDK DCS | 16MB／Octal，单 ES8311 |
| movecall-cuican-esp32s3 | 240×240 GC9A01 默认命令＋SDK DCS | 8MB／Octal，单 ES8311，保留既有 size 优化配置 |
| movecall-moji-esp32s3 | 240×240 GC9A01 默认命令＋SDK DCS | 16MB／Octal，单 ES8311，独立板级窗口／按键 |
| jiuchuan-s3 | 240×296 专用 GC9309NA 有效命令路径 | 16MB／Octal，单 ES8311，专用布局和像素格式 |
| kevin-sp-v4-dev | 240×280 SPI 屏 | 16MB／Octal，单 ES8311，16k 输入／输出 |
| labplus-ledong-v2 | 320×172，原 ST7789 兼容初始化 | 16MB／Quad，ES8388，独立按键映射 |
| labplus-mpython-v3 | 320×172，原 ST7789 兼容初始化 | 16MB／Quad，ES8388，保留 V3 配置 |
| mixgo-nova | 128×160 SPI 屏 | 8MB／Quad，ES8374，保留输入选择寄存器检查 |
| nulllab-ai-vox-v3 | 外挂 240×240 ST7789 配置 | 16MB／Octal，单 ES8311，128fs 时钟配置 |
| yunliao-s3 | 320×240 SPI 屏 | 16MB／Octal，ES8388 |
| kevin-box-2 | SSD1306 128×64 OLED 配置 | 16MB／Octal，独立 codec I²C，ES7210＋ES8311 |
| hu-087 | SSD1306 128×64 OLED 配置 | 16MB／Octal，数字音频 |

尺寸来自当前板级头，实际编译选择与产物另行核对；不代表实机可见布局、圆角裁切、文字、颜色、方向、触摸或按键已验收。Jiuchuan 原文件名、注释和有效创建函数命名并不一致，本批保留现有专用实现，不按文件名替换屏控。AI-VOX3 本入口要求实际装有所配置的外挂屏，无屏或 OLED 实物不继承这份配置。

三项 Quad PSRAM 单独检查，其他十三项为 Octal。StickS3、Cuican、MixGo 均保留 8MB 分区：双 app 各 0x2A0000，model 0x240000。其余入口沿既有 16MB 分区。NVS 均保留 0x9000／0x4000；当前镜像适配分区不等于实物容量已确认。

十六项保留自定义“你好，小燕子”、AFE／软件 AEC、媒体、绑定、提醒和双 OTA。不同 codec、采样率和物理功放控制沿各板已有实现；不把单 codec 构建推广成双 codec、把数字音频当作模拟 codec 或把寄存器读回当作声音已正确播放。

## Magiclick 2.5 的历史编译器问题

沿用此前恢复成功的原隔离目录，本批为该目标保留 `IDF_PY_BUILD_JOBS=1`；其余目标为 2，每次最多两个板同时构建。没有修改代码生成优化、DSP 源码或编译器，没有复制其他板的对象文件。

此前 GCC 内部错误的根因仍未确定。此前同参数重放及最终构建成功，不能证明编译器已修复，也不能把错误确定归因于并发、内存或硬件。该历史问题仍按 [原诊断记录](magiclick_gcc_recovery_20261112.md) 保留，本批实际结果见下表。

2.5 的修订选择函数可能被内联，核对实际 ADC 读取／配置／释放符号、未知修订停止诊断、GC9107 初始化回调和当前板型宏；不为制造独立符号添加 noinline 或改优化。2.4 的 NV3023 命令及 2.5 的 ADC／面板选择分别保留，未合并为一个默认屏幕。

## 实际执行与产物核对

使用现有 ESP-IDF 5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake 3.30.2、Ninja 1.12.1、ROM 20260528，以及各隔离目录已有组件 override 和 offline_guard。不下载或安装 SDK／依赖。同步当前 main、根构建元数据、芯片／板配置及分区后重新配置构建。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/portable-oled-rebuild-20261113/build_batch.py
```

每项实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，准确解释器路径、工作目录、时间、同步文件、jobs、退出码及日志摘要保存在各板 `build.json`。

完成后分次调用 `collect_batch.py` 的 `collect`，最后汇总。每板执行八条真实产物核对命令：`readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、分区二进制解析及 SDK 两条只读 Git 查询，命令和输出均保存在 `commands.json`。后续执行：

```powershell
# python 指上述已安装的 IDF Python。
python -B evidence/portable-oled-rebuild-20261113/collect_batch.py
python -B evidence/portable-oled-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/portable-oled-rebuild-20261113/finalize.py
git diff --check
```

`symbol-expectations.json` 保留每板 codec、显示、功放／电源和修订识别的要求，实际编译宏及文件另行核对。OLED 两项分别检查双 codec 与数字音频；ES8388、ES8374 和 ES8311 的链接选择也分别核对，不能用共享组件在目录中存在代替目标选择证据。

## 最终产物与矩阵

十六项整板构建及每项八条真实产物核对命令均退出 0；每项保留 330 个当前输入摘要，共归档 633 个产物。Magiclick2.5 本次单任务 reconfigure build 退出 0，历史 GCC 根因未明的结论不变。

| profile | app 字节 | app 分区剩余 | 模型分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| m5stack-stick-s3 | 2,249,824 | 502,688 | 155,477 | 40 |
| m5stack-stopwatch | 2,544,592 | 601,136 | 1,990,485 | 40 |
| magiclick-2p4 | 2,249,776 | 895,952 | 1,990,485 | 39 |
| magiclick-2p5 | 2,256,864 | 888,864 | 1,990,485 | 39 |
| spotpear-sp-esp32-s3-1.28-box | 2,259,072 | 886,656 | 1,990,485 | 39 |
| movecall-cuican-esp32s3 | 2,088,176 | 664,336 | 155,477 | 39 |
| movecall-moji-esp32s3 | 2,247,984 | 897,744 | 1,990,485 | 39 |
| jiuchuan-s3 | 2,250,000 | 895,728 | 1,990,485 | 40 |
| kevin-sp-v4-dev | 2,246,304 | 899,424 | 1,990,485 | 39 |
| labplus-ledong-v2 | 2,244,176 | 901,552 | 1,990,485 | 40 |
| labplus-mpython-v3 | 2,243,904 | 901,824 | 1,990,485 | 40 |
| mixgo-nova | 2,243,120 | 509,392 | 155,477 | 40 |
| nulllab-ai-vox-v3 | 2,248,416 | 897,312 | 1,990,485 | 39 |
| yunliao-s3 | 2,247,696 | 898,032 | 1,990,485 | 40 |
| kevin-box-2 | 2,190,960 | 954,768 | 1,990,485 | 40 |
| hu-087 | 2,157,456 | 988,272 | 1,990,485 | 40 |

累计曾缓存构建通过的官方变体仍为 115 个。本批更新十六项当前输入通过，矩阵为 **80 项当前通过／35 项历史通过但源码已过期／73 项未执行构建**。这不等于新增十六个源码入口、十六个首次通过或任何实机完成。


模型均为 2,203,819 字节，保留 MN5 和自定义唤醒源码，排除固定词、手动唤醒或无屏 UI 选择。核对真实镜像项目名、ESP32-S3、Flash 设置、校验摘要及分区；分区名称、偏移、长度和标志按每板 CSV 比较，app／model 均须实际容纳。

每项归档输入摘要、镜像、ELF、map、模型、分区、配置、编译命令及实际选中的 main 对象。最终审计要求旧 main、注册、manifest、配置、工具和历史账本不变，只追加当前构建及研究后续证据。过期历史通过记录不能当作当前源码通过。

SDK 四个既有强制 UND 继续记录为 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。警告与 dirty 状态保留，本批不是干净发行、启动、声学或物理验收。

188 项实机仍全部 `not_run`，唯一现有微雪 1.8 的具体修订仍待确认。本批没有运行独立测试套件／类型检查、启动服务、安装依赖、Git 写操作、刷机、OTA、上传或发布。各板电源、实际容量、显示修订、ADC、触摸／按键、麦克风、时钟、AEC／自然插话、联网及同板 OTA 回滚仍需实机验收；厂商全部外围与手势不据此计为完成。

## 证据入口

- [最终审计](../evidence/portable-oled-rebuild-20261113/final-audit.json)
- [构建进程结果](../evidence/portable-oled-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/portable-oled-rebuild-20261113/collection-summary.json)
- [板型／内存配置／jobs](../evidence/portable-oled-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/portable-oled-rebuild-20261113/symbol-expectations.json)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
