# 二十一款特殊终端当前源码重建

本批重建仍有完整隔离目录、且未登记已知必需语音功能缺口的二十一个官方入口，覆盖机器人基础语音终端、摄像头板、无屏、小屏、OLED 和电子纸。最初估计二十项，完整枚举实际为二十一项；第一次准备断言在写入基线或构建前停止，重新枚举后全部纳入，记录在 `preparation-note.json`。

固定上游为小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际 **154 个发行目录／188 个变体**；138／171 是旧 README 统计。本批不新增源码入口，仍有 152 个官方源码入口、153 个本地 profile（含历史入口），36 项无入口和 4 项已知必需功能缺口保留。

## 逐板配置与边界

| profile | 本批核对的基础终端路径 |
| --- | --- |
| bread-compact-wifi-s3cam | 240×320 SPI 显示，数字音频，保留摄像头板接线配置 |
| chd-esp32s3-eye | 240×240 SPI 显示，单 ES8311，16k 输入／输出 |
| dfrobot-df-s3-ai-cam | 无屏热点网页绑定，PDM 麦克风／数字输出，独立查看键 |
| du-chatx | 128×160 SPI 显示，数字音频 |
| electron-bot | 圆形 GC9A01 命令＋SDK DCS 操作，数字音频；不含机器人运动验收 |
| espressif-esp-sparkbot | 240×240 SPI、单 ES8311 16k，共用背光／PA46 维持稳定，数字静音 |
| lceda-course-examples-eda-robot-pro | 128×64 OLED，数字音频；不含机械动作验收 |
| lceda-course-examples-eda-super-bear | 无屏热点网页绑定，数字音频，实体键只开查看窗口 |
| lceda-course-examples-eda-tv-pro | SPI 显示，数字音频，保留独立板型宏 |
| lilygo-t-cameraplus-s3 | SPI 显示／CST816S，Quad PSRAM，标准 I²S 右槽采集 |
| lilygo-t-cameraplus-s3-v1.2 | 独立显示／触摸引脚，Quad PSRAM，PDM 采集 |
| lilygo-t-display-s3-pro-mvsrlora | 222×480 ST7796 专用命令／CST226SE，PDM 采集、数字输出及功放控制 |
| maza-ai-esp32-s3 | 296×128 电子纸，单 ES8311，刷新 generation 路径 |
| minsi-k08-dual | 240×240 SPI，独立 I²S 麦克风／扬声器，软件 AEC |
| nologo-xingzhi-cube-0.85tft-wifi | 128×128 NV3023 专用命令，数字音频 |
| nologo-xingzhi-cube-0.96oled-wifi | 128×64 OLED，数字音频 |
| otto-robot | 运行时区分 CAM／非 CAM 接线，显示与音频引脚随之选择，16k／24k 输出分支 |
| waveshare-esp32-s3-touch-lcd-3.5 | 480×320，ST7796 独立命令＋SDK DCS 操作，FT5x06，单 ES8311 |
| wk-esp32s3-dev | 128×32 OLED，数字音频 |
| xorigin-aipi-lite | 128×128 SPI，单 ES8311，RTC GPIO10 保持供电，独立工厂区保护 |
| zhengchen-cam | 320×240 SPI，ES7210＋ES8311，PCA9557 控制路径 |

尺寸和选择来自当前板级头、CMake 和具体实现，另核对编译源文件与 ELF；不能代替可见布局、真实屏控、方向、颜色、触摸、声学或实物接线验收。部分专用面板使用 SDK ST7789 对象的 DCS 操作，不能按符号名推断实际面板型号。

两款 T-Camera 使用 Quad PSRAM，本批其余入口使用 Octal；全部沿各板现有 16MB 声明和分区。摄像头板的基础语音入口不代表摄像头已经工作，MVSRLoRa 的默认 Wi-Fi 终端不代表 LoRa 无线功能已实现，机器人名称也不代表机械运动或工具服务已验收。

Otto 的同一镜像保留两套硬件接线的运行时探测与输出采样率选择；GPIO 判别逻辑或分支存在不能证明实机探测可靠。SparkBot 保留共用 GPIO46 的既有策略：不将背光 PWM 同时施加到功放，不声称有独立硬件 PA 切断。T-Camera 两修订保持独立 PDM／标准采集及显示引脚，不互相继承硬件结果。

无屏两项选择 `headless_ui.cpp`／`headless_portal.cpp`，通过设备热点网页读取六位码，绑定完成后才初始化音频。查看窗口、AP 接口检查、短期会话和提醒呈现闭环沿现有实现；本批未真实手机验收，不把链接该代码称作网络隔离或网页已通过。

二十一项均保留自定义“你好，小燕子”、AFE／软件 AEC、媒体、绑定、提醒和双 OTA。Cardputer ADV 的本地唤醒／AFE／AEC 等已知缺口仍单独保留，本批没有通过重建它来把功能缺口计为完成。其他缺少旧隔离目录的板型后续另建环境处理。

## 实际构建与核对

使用现有 ESP-IDF 5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake 3.30.2、Ninja 1.12.1、ROM 20260528，以及各隔离目录已有的组件 override／offline_guard。最多两个板同时构建，每项 `IDF_PY_BUILD_JOBS=2`，不下载或安装依赖。同步当前 main、根构建元数据、芯片／板配置和分区后重新配置构建。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/special-terminal-rebuild-20261113/build_batch.py
```

每项实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。准确解释器路径、工作目录、时间、同步文件、退出码和日志摘要保存在各板 `build.json`。

分次调用 `collect_batch.py` 中 `collect` 核对已经完成的实际产物，最后汇总。每项八条真实命令为 `readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、分区二进制解析和 SDK 两条只读 Git 查询；命令与输出均保存在各板 `commands.json`。然后执行：

```powershell
# python 指上述已安装的 IDF Python。
python -B evidence/special-terminal-rebuild-20261113/collect_batch.py
python -B evidence/special-terminal-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/special-terminal-rebuild-20261113/finalize.py
git diff --check
```

逐板符号期望在 `symbol-expectations.json`，并核对当前宏和选中的源文件：有屏／无屏、OLED／专用 SPI、标准／PDM 音频，以及单 codec／双 codec／数字音频分别检查。对小函数的内联不要求人为改变优化或增加独立符号。存在相应代码仅证明编译与链接，不证明运行。

MVSRLoRa 的首次收集因复用历史期望中的旧回调名而停止。当前共享显示实现已经使用 `initialize_st7796_default_panel`／`reset_st7796_default_panel`，实际 ELF 包含这两个符号，stage 源文件与当前工作区一致。核对后只更正检查名称并重新收集，未改固件或重建；初始期望和说明保存在 `initial-symbol-expectations.json`／`verification-repairs.json`。历史命名不能替代当前源码核对。

AiPi Lite 首次收集因本批期望误填 PCA9557 而停止。对照当前电源分支确认实际是 RTC GPIO10，工厂区保留在 0x9000／0x4000，工作 NVS 在 0x10000／0x4000。更正为核对实际 RTC 电源／分区接口并排除 PCA9557，完整比较其专用分区偏移和 readonly 标志；不修改固件或工厂凭据。错误期望和修正原因另行保留，未把检查脚本错误记作固件编译失败。

## 最终产物与矩阵

二十一项整板构建和每项八条真实产物核对命令均退出 0；每项保留 330 个当前输入摘要，共归档 839 个产物。

| profile | app 字节 | app 分区剩余 | 模型分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| bread-compact-wifi-s3cam | 2,215,744 | 929,984 | 1,990,485 | 40 |
| chd-esp32s3-eye | 2,246,576 | 899,152 | 1,990,485 | 39 |
| dfrobot-df-s3-ai-cam | 1,581,920 | 1,563,808 | 1,990,485 | 40 |
| du-chatx | 2,209,104 | 936,624 | 1,990,485 | 40 |
| electron-bot | 2,209,424 | 936,304 | 1,990,485 | 40 |
| espressif-esp-sparkbot | 2,239,840 | 905,888 | 1,990,485 | 39 |
| lceda-course-examples-eda-robot-pro | 2,157,456 | 988,272 | 1,990,485 | 40 |
| lceda-course-examples-eda-super-bear | 1,578,912 | 1,566,816 | 1,990,485 | 40 |
| lceda-course-examples-eda-tv-pro | 2,208,144 | 937,584 | 1,990,485 | 40 |
| lilygo-t-cameraplus-s3 | 2,228,016 | 917,712 | 1,990,485 | 40 |
| lilygo-t-cameraplus-s3-v1.2 | 2,231,920 | 913,808 | 1,990,485 | 40 |
| lilygo-t-display-s3-pro-mvsrlora | 2,233,088 | 912,640 | 1,990,485 | 41 |
| maza-ai-esp32-s3 | 2,197,344 | 948,384 | 1,990,485 | 39 |
| minsi-k08-dual | 2,209,184 | 936,544 | 1,990,485 | 40 |
| nologo-xingzhi-cube-0.85tft-wifi | 2,210,672 | 935,056 | 1,990,485 | 40 |
| nologo-xingzhi-cube-0.96oled-wifi | 2,159,696 | 986,032 | 1,990,485 | 40 |
| otto-robot | 2,209,232 | 936,496 | 1,990,485 | 41 |
| waveshare-esp32-s3-touch-lcd-3.5 | 2,546,768 | 598,960 | 1,990,485 | 40 |
| wk-esp32s3-dev | 2,158,768 | 986,960 | 1,990,485 | 40 |
| xorigin-aipi-lite | 2,248,816 | 896,912 | 1,990,485 | 39 |
| zhengchen-cam | 2,258,272 | 887,456 | 1,990,485 | 41 |

累计曾缓存构建通过的官方变体仍为 115 个。本批更新二十一项当前输入通过，矩阵为 **101 项当前通过／14 项历史通过但源码已过期／73 项未执行构建**。没有新增源码入口或首次构建通过变体，也未完成任何实机验收。


模型均为 2,203,819 字节，保留 MN5 及自定义唤醒源文件，排除固定词和手动唤醒替代实现。实际镜像核对项目身份、ESP32-S3、Flash 声明及校验摘要；分区二进制按各板 CSV 比较名称、偏移、长度和标志，app／model 均须实际容纳。保留原 NVS 和双 OTA，不因同为 S3 而复制别板镜像。

每项归档 330 个当前输入摘要、镜像、ELF、map、模型、分区、配置、编译命令和全部实际选择的 main 对象。最终审计核对旧 main、注册、manifest、配置、工具和历史构建账本不变，仅追加研究与当前构建证据。历史记录、旧阻塞和已知功能缺口不删除。

SDK 四个既有强制 UND 仍为 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`，dirty 状态与编译警告继续记录，不宣称零 UND、干净发行、启动或实机通过。

188 项实机仍全部 `not_run`，唯一现有微雪 1.8 的具体修订仍待确认。本批未运行独立测试套件／类型检查、启动服务、安装依赖、Git 写操作、刷机、OTA、上传或发布。摄像头、运动、LoRa、传感器、RTC、电池、省电、原厂全部手势及实际唤醒／AEC／网络／同板 OTA 回滚不据此计为完成。

## 证据入口

- [最终审计](../evidence/special-terminal-rebuild-20261113/final-audit.json)
- [构建进程结果](../evidence/special-terminal-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/special-terminal-rebuild-20261113/collection-summary.json)
- [本批板型／隔离目录](../evidence/special-terminal-rebuild-20261113/plan.json)
- [逐板符号期望](../evidence/special-terminal-rebuild-20261113/symbol-expectations.json)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
