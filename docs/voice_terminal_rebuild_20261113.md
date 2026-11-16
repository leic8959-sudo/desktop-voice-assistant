# 十六款语音终端当前源码重建

本批为 ATK 五款、CHD 两款、Freenove、全动、立创开发板、BigSmart、Korvo2、K10、AtomS3R Echo Base／Pyramid 和 VoCat 补齐当前源码的整板构建证据。逐板保留独立应用身份和编译宏，不把不同 codec、输入或硬件修订视为同一套接线。

固定上游为小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围 **154 目录／188 变体**；138／171 是 README 的旧统计。仍有 152 个官方源码入口、153 个本地 profile（含历史入口），36 项无入口、4 项已知必需功能缺口继续保留。本批不修改固件引擎、注册、manifest、配置、分区或人工决策。

## 本批逐板选择

| profile | 当前界面尺寸／显示 | 保留的音频或输入差异 |
| --- | --- | --- |
| alientek-atk-dnesp32s3 | 320×240 ST7789 SPI | ES8388；XL9555 的背光／功放控制 |
| alientek-atk-dnesp32s3-box | 320×240 ST7789 I80 | 运行时读取硬件选型；ES8311 或数字双工，两条分支分别保留 |
| alientek-atk-dnesp32s3-box0 | 240×240 ST7789 SPI | 单 ES8311，16k、无 MCLK；中键高有效，左右键低有效 |
| alientek-atk-dnesp32s3-box2-wifi | 240×320 ST7789 SPI | ES8389，板级功放及 ADC 按键；Wi-Fi 入口不当作蜂窝入口 |
| alientek-atk-dnesp32s3-box3 | 320×240 ST7789 SPI | ES7210＋ES8311，独立功放／背光和按键 |
| chd-esp32s3-box | 320×240 SPI | ES7210＋ES8311；现有 GT911／FT5x06 选择 |
| chd-esp32s3-box-lcd35 | 480×320，独立 ST7796 命令 | ES7210＋ES8311；保留本板方向及触摸路径 |
| freenove-esp32s3-display-2.8-lcd | 320×240，ILI9341 命令＋SDK DCS 操作 | 单 ES8311，FT5x06 及按键路径 |
| quandong-s3-dev | 320×240，专用初始化命令＋SDK DCS 操作 | 单 ES8311，保留本板电源和引脚 |
| lckfb-lichuang-dev | 320×240 SPI | ES7210＋ES8311，PCA9557、FT5x06 |
| rymcu-bigsmart | 320×240 SPI | ES7210＋ES8311，PCA9557、GT911 |
| espressif-esp32s3-korvo2-v3 | 280×240 SPI | ES7210＋ES8311，ADC 按键；不是 RNDIS 变体 |
| dfrobot-df-k10 | 240×320，ILI9341 命令＋SDK DCS 操作 | ES7243E 输入＋数字输出，16k；厂家 A／B 键映射 |
| m5stack-atoms3r-echo-base | 128×128，GC9107／ST7735 ID 选择 | 单 ES8311，Atomic Echo Base 扩展口功放 |
| m5stack-atoms3r-echo-pyramid | 128×128，独立方向／窗口 | ES7210＋ES8311，外部 Si5351 时钟、Pyramid 功放 |
| espressif-esp-vocat | 360×360 ST77916 QSPI | ES7210＋ES8311，板级复位与电源选择、CST816S |

尺寸和显示选择取自当前板级头及 CMake，并核对真实编译源文件与链接符号；不等于实际屏幕方向、偏移、圆角裁切、触摸角点或可见界面已通过。部分面板复用 `esp_lcd_new_panel_st7789` 的 DCS 操作，其实际初始化命令与面板身份分别保留，不能仅凭该符号判断屏控型号。

ATK BOX 的两条音频分支是在同一固件内等待实际硬件选型后才初始化，构建不能说明本机实物会选哪条，也不能把 I²C 错误当作选择数字音频的依据。BOX2 本批仅验证 Wi-Fi 变体，不因其源码含模组使能控制函数就计入 4G 适配。

K10 保留厂家标准采样与 ES7243E 路径，不替换为 ES7210＋ES8311。Pyramid 保留独立底座发现、外部音频时钟和功放状态检查；Echo Base 与 Pyramid 分别核对音频和面板选择，不按同为 128 方屏合并。真实时钟、电压、选型、增益、静音和采样均需实机验收。

十六项保留自定义“你好，小燕子”、AFE／软件 AEC、媒体、网络绑定、提醒和双 OTA。没有以纯按键语音、单应用分区或关闭 AEC 来算通过；本批也没有新增相机、传感器、SD、厂商全部手势或外围功能。

## 构建与核对命令

复用各板原隔离目录，同步当前 main、根构建元数据、芯片／板配置及分区，再重新配置构建。使用已安装 ESP-IDF 5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake 3.30.2、Ninja 1.12.1、ROM 20260528，以及已有组件 override 和 offline_guard。最多两个目标并行，每项 `IDF_PY_BUILD_JOBS=2`，不安装或下载依赖。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/voice-terminal-rebuild-20261113/build_batch.py
```

每项实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。完整解释器路径、工作目录、开始／结束时间、同步文件、日志摘要与退出码保存在各板 `build.json`。

完成后分次调用 `collect_batch.py` 的 `collect`，最后汇总。每板执行八条实际产物核对命令：`readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、分区二进制解析和两条 SDK 只读 Git 查询，命令与输出保存在 `commands.json`。后续执行：

```powershell
# python 指上述已安装的 IDF Python。
python -B evidence/voice-terminal-rebuild-20261113/collect_batch.py
python -B evidence/voice-terminal-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/voice-terminal-rebuild-20261113/finalize.py
git diff --check
```

`symbol-expectations.json` 记录各板不同的 codec、板级控制、I80／SPI／QSPI、触摸或面板初始化符号，并对照当前源码复核已有取证。实际编译命令也核对自定义唤醒和显示源码被选择，排除固定词、手动唤醒和无屏 UI 入口。符号或对象存在只能证明选中并链接，不能证明物理功能运行。

## 最终产物与矩阵

十六项整板构建及每项八条真实产物核对命令均退出 0；每项保留 330 个当前输入摘要，共归档 658 个产物。

| profile | app 字节 | app 分区剩余 | 模型分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| alientek-atk-dnesp32s3 | 2,241,664 | 904,064 | 1,990,485 | 41 |
| alientek-atk-dnesp32s3-box | 2,227,648 | 918,080 | 1,990,485 | 42 |
| alientek-atk-dnesp32s3-box0 | 2,247,776 | 897,952 | 1,990,485 | 39 |
| alientek-atk-dnesp32s3-box2-wifi | 2,236,352 | 909,376 | 1,990,485 | 42 |
| alientek-atk-dnesp32s3-box3 | 2,252,512 | 893,216 | 1,990,485 | 41 |
| chd-esp32s3-box | 2,266,816 | 878,912 | 1,990,485 | 41 |
| chd-esp32s3-box-lcd35 | 2,267,696 | 878,032 | 1,990,485 | 41 |
| freenove-esp32s3-display-2.8-lcd | 2,258,560 | 887,168 | 1,990,485 | 40 |
| quandong-s3-dev | 2,249,168 | 896,560 | 1,990,485 | 39 |
| lckfb-lichuang-dev | 2,265,648 | 880,080 | 1,990,485 | 41 |
| rymcu-bigsmart | 2,260,528 | 885,200 | 1,990,485 | 42 |
| espressif-esp32s3-korvo2-v3 | 2,262,176 | 883,552 | 1,990,485 | 41 |
| dfrobot-df-k10 | 2,227,024 | 918,704 | 1,990,485 | 41 |
| m5stack-atoms3r-echo-base | 2,246,496 | 506,016 | 155,477 | 42 |
| m5stack-atoms3r-echo-pyramid | 2,256,976 | 495,536 | 155,477 | 43 |
| espressif-esp-vocat | 2,558,080 | 587,648 | 1,990,485 | 42 |

累计曾通过缓存构建的官方变体仍为 115 个。本批补齐十六项当前输入通过，矩阵为 **64 项当前通过／51 项历史通过但源码已过期／73 项未执行构建**。不是新增十六个源码入口或首次构建通过变体，也不代表实机完成。


每份模型为 2,203,819 字节，MN5 和自定义唤醒源码保留。实际镜像逐项核对项目身份、ESP32-S3、Flash 设置和校验摘要。分区二进制与各板 CSV 的名称、偏移、大小和标志比较，保留 NVS 0x9000／0x4000、双 OTA 和模型分区。

两款 AtomS3R 底座入口沿独立 8MB 分区：app 各 0x2A0000，model 0x240000；其余本批入口沿既有 16MB 分区。检查当前 app／model 容量，不从芯片、同尺寸屏幕或别板构建推断实物容量。SDK 四个既有强制 UND 仍记录为 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`；警告和 dirty 状态保留，不宣称干净发行、启动或零 UND。

最终审计核对旧 main、注册、manifest、配置、工具与旧构建账本未变，仅追加研究后续记录和当前构建账本。归档镜像、ELF、map、模型、分区、配置、编译命令及每个实际选中的 main 对象，不把相同产物数量当作全部板型的通过条件。

188 项实机仍全部 `not_run`，唯一现有微雪 1.8 的具体修订仍待确认。本批未运行独立测试套件／类型检查、启动服务、安装依赖、Git 写操作、刷机、OTA、上传或发布。源码入口、当前构建通过、完整外围适配和实机验收分别记录，剩余 36 项无源码入口及 4 项已知必需功能缺口不因本批构建而消失。

## 证据入口

- [最终审计](../evidence/voice-terminal-rebuild-20261113/final-audit.json)
- [构建进程结果](../evidence/voice-terminal-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/voice-terminal-rebuild-20261113/collection-summary.json)
- [板型及隔离目录](../evidence/voice-terminal-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/voice-terminal-rebuild-20261113/symbol-expectations.json)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
