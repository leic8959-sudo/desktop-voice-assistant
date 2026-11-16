# 十二款微雪 S3 屏幕板当前源码重建

本批补齐 CAM 1.83／2／2.8／3.5、LCD 1.46／1.85／1.85B／1.85C、AMOLED 1.43C、LCD 3.49／3.49-v2／3.5B 的当前源码整板构建证据。只更新证据、研究后续记录和适配矩阵，固件引擎、注册、manifest、配置、分区及人工决策均不修改。

固定上游为小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行范围 **154 目录／188 变体**，README 的 138／171 为旧统计。仍有 152 个官方源码入口、153 个本地 profile（含历史入口），36 项无入口、4 项已知必需功能缺口保留。

## 源码选择与逐板差异

| profile | 当前界面分辨率 | 显示／触摸选择 | 音频选择 |
| --- | --- | --- | --- |
| waveshare-esp32-s3-cam-1.83 | 240×284 | 板级 SPI DCS，无触摸 | ES7210＋ES8311，CH32 输出控制 |
| waveshare-esp32-s3-cam-2 | 240×320 | 板级 SPI DCS，无触摸 | ES7210＋ES8311，CH32 输出控制 |
| waveshare-esp32-s3-cam-2.8 | 240×320 | 板级 SPI DCS，无触摸 | ES7210＋ES8311，CH32 输出控制 |
| waveshare-esp32-s3-cam-3.5 | 320×480 | 独立 ST7796 命令＋SDK DCS 操作，无触摸 | ES7210＋ES8311，CH32 输出控制 |
| waveshare-lcd-1.46 | 412×412 | SPD2010 QSPI＋本地 SPD2010 触摸 | 数字麦克风16k／扬声器24k |
| waveshare-lcd-1.85 | 360×360 | ST77916 QSPI＋ID 选择命令表／独立触摸总线 | 16k 右槽数字输入／24k 双槽输出 |
| waveshare-lcd-1.85b | 360×360 | ST77916 QSPI，GPIO 复位，CST816S | ES7210＋ES8311，PA9 |
| waveshare-lcd-1.85c | 360×360 | ST77916 QSPI，扩展芯片复位，CST816S | ES7210＋ES8311，PA15，现有 V2.0 路径 |
| waveshare-esp32-s3-touch-amoled-1.43c | 466×466 | CO5300，CST820 原始报文／CST816S 家族初始化 | ES7210＋ES8311，8MB 分区 |
| waveshare-esp32-s3-touch-lcd-3.49 | 172×640 | AXS QSPI，分条发送整帧，独立触摸总线 | ES7210＋ES8311，扩展芯片电源／PA |
| waveshare-esp32-s3-touch-lcd-3.49-v2 | 172×640 | 独立 v2 背光／引脚配置，AXS QSPI | ES7210＋ES8311，扩展芯片电源／PA |
| waveshare-esp32-s3-touch-lcd-3.5b | 480×320 | AXS QSPI，物理帧与逻辑界面方向分别处理 | 单 ES8311，独立板级 PA 路径 |

这些分辨率取自当前 `board_profile.h`／`tft_board.h`，每板编译宏与实际选中源文件另行核对。它们说明源码如何配置界面，不能替代可见布局、圆形裁切、触摸角点和实际屏控修订验收。部分板使用 SDK `esp_lcd_new_panel_st7789` 的 DCS 窗口／绘图操作配合自己的命令表，该符号名不能当作实际面板型号。

十二项均保留“你好，小燕子”、自定义唤醒源码、AFE／软件 AEC、网络、媒体、绑定、提醒和双 OTA；均没有选用无屏 UI 或手动语音替代唤醒。本批未新增摄像头、SD、传感器、充电／电池或厂商 MCP 功能，不能把 CAM 板的语音终端构建称为摄像头已适配。1.85C 的旧 C1.0 修订仍未移植，不与当前 V2.0 路径混用。

## 实际执行

十二项复用各自现有隔离构建目录，同步当前 main、根构建元数据、芯片／板配置及分区，再重新配置构建。仅使用已安装 ESP-IDF 5.5.6-dirty、现有 IDF Python、Xtensa `esp-14.2.0_20260121`、CMake 3.30.2、Ninja 1.12.1、ROM 20260528，以及原目录中的本地组件 override 和 offline_guard。最多两个板同时构建，每项 `IDF_PY_BUILD_JOBS=2`。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/waveshare-panel-rebuild-20261113/build_batch.py
```

逐板实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。每项的解释器绝对路径、工作目录、时间、同步文件和日志摘要均在 `build.json`，进程结果在 `batch-build-results.json`。

完成后分次调用 `collect_batch.py` 的 `collect` 核对实际产物，最后运行脚本汇总。每项使用 `readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、分区二进制解析及两条 SDK 只读 Git 查询，真实命令／结果保存在各板 `commands.json`。之后执行：

```powershell
# python 指上述已安装的 IDF Python。
python -B evidence/waveshare-panel-rebuild-20261113/collect_batch.py
python -B evidence/waveshare-panel-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/waveshare-panel-rebuild-20261113/finalize.py
git diff --check
```

符号期望沿各板已有取证并对照当前驱动／CMake 选择复核，记录在 `symbol-expectations.json`。首个 CAM 1.83 核对曾因错误禁止 `esp_wn_handle_from_name` 而停止：通用 ESP-SR／AFE 也能链接此接口，它不能证明应用选择了固定唤醒。已保留原期望和修正记录，删除该无依据禁用项；仍核对 `wake_word.cpp`、排除 `fixed_wake_word.cpp`／`manual_wake_word.cpp`，核对模型和 MN5 配置。固件本身构建已通过，不为修正检查脚本重编或改动唤醒实现。

## 最终镜像与矩阵

十二项整板构建及每项八条实际产物核对命令全部退出 0；每项保留 330 个当前输入摘要，共归档 498 个产物。

| profile | app 字节 | app 分区剩余 | 模型分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| waveshare-esp32-s3-cam-1.83 | 2,250,432 | 895,296 | 1,990,485 | 42 |
| waveshare-esp32-s3-cam-2 | 2,250,432 | 895,296 | 1,990,485 | 42 |
| waveshare-esp32-s3-cam-2.8 | 2,250,432 | 895,296 | 1,990,485 | 42 |
| waveshare-esp32-s3-cam-3.5 | 2,252,032 | 893,696 | 1,990,485 | 42 |
| waveshare-lcd-1.46 | 2,521,440 | 624,288 | 1,990,485 | 42 |
| waveshare-lcd-1.85 | 2,530,208 | 615,520 | 1,990,485 | 41 |
| waveshare-lcd-1.85b | 2,555,680 | 590,048 | 1,990,485 | 41 |
| waveshare-lcd-1.85c | 2,559,120 | 586,608 | 1,990,485 | 41 |
| waveshare-esp32-s3-touch-amoled-1.43c | 2,546,992 | 205,520 | 155,477 | 40 |
| waveshare-esp32-s3-touch-lcd-3.49 | 2,259,616 | 886,112 | 1,990,485 | 42 |
| waveshare-esp32-s3-touch-lcd-3.49-v2 | 2,260,160 | 885,568 | 1,990,485 | 42 |
| waveshare-esp32-s3-touch-lcd-3.5b | 2,540,608 | 605,120 | 1,990,485 | 41 |

累计曾通过缓存构建的官方变体仍为 115 项。本批补齐十二项当前输入通过，全部矩阵为 **32 项当前通过／83 项历史通过但源码已过期／73 项未执行构建**。不是新增十二个源码入口或十二个首次通过变体。


每份模型为 2,203,819 字节，选用 S3 MN5 配置；实际镜像核对项目身份、ESP32-S3、Flash 大小和校验摘要。实际分区二进制逐项对照所选板的 CSV，保持 NVS 0x9000／0x4000、双 OTA、模型及存储布局，并检查 app／model 确实容纳。1.43C 使用独立 8MB 分区，其余沿 16MB 配置，不从屏幕尺寸推断 Flash 容量。

所有 ELF 均核对自定义唤醒、AEC、网络、媒体和本板驱动，不误链接别板的单／双 codec 或显示接口。SDK 仍有四个既有强制 UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。警告数量和原日志保留，不宣称零 UND、干净发行、启动或运行通过。

最终审计同时核对全部旧 main、注册、manifest、配置、工具及历史构建账本摘要不变，研究记录只追加本次后续证据，不删除旧阻塞项。当前通过只对应本次输入集合；过期账本不能当成当前源码构建通过。

188 项实机仍全部 `not_run`，唯一现有微雪 1.8 的具体硬件修订仍待确认。本批未运行独立测试套件／类型检查、启动服务、安装或下载依赖、Git 写操作、刷机、OTA、上传或发布。屏控 ID、命令时序、触摸、界面可见性、音频时钟、PA、AEC／自然插话、资源峰值及同板 OTA 回滚均未实测。

## 证据入口

- [最终审计](../evidence/waveshare-panel-rebuild-20261113/final-audit.json)
- [逐板构建进程结果](../evidence/waveshare-panel-rebuild-20261113/batch-build-results.json)
- [产物汇总](../evidence/waveshare-panel-rebuild-20261113/collection-summary.json)
- [板型及隔离目录](../evidence/waveshare-panel-rebuild-20261113/plan.json)
- [符号期望](../evidence/waveshare-panel-rebuild-20261113/symbol-expectations.json)／[检查脚本修正](../evidence/waveshare-panel-rebuild-20261113/verification-repairs.json)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
