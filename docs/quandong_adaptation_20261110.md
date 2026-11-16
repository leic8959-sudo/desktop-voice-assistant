# Quandong S3 Dev：独立ILI9341源码入口

后续更新：本页保留初次源码候选和缺依赖的历史记录；当前已改为SDK DCS操作及保留原序列的板级回调，并首次完成Quandong真实整板构建。见[本批记录](quandong_sdk_panel_20261112.md)。

日期：2026-11-10，Asia/Shanghai。官方变体 `quandong-s3-dev`，独立应用名 `assistant_quandong`；仅基础Wi-Fi语音源码候选，目标缺依赖尚未编译、无固件镜像、未实机验收。

## 固定事实与实现

来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 `main/boards/quandong-s3-dev/` 下config.h、config.json、实际 `quandong_s3_dev_board.cc` 和HARDWARE_DIFF.md。以后者描述和宏定义为线索，但以实际构造调用确定硬件：虽然config有PCA9557和ES7210宏，板类只实例化ES8311，没有扩展器或ES7210初始化，本地也不创建这些设备。

| 项目 | 源码接入 |
| --- | --- |
| 屏幕 | ILI9341，320×240横屏，反色、swapXY、mirrorY，gap0／0 |
| 总线 | SPI2 mode0／40MHz，MOSI11／CLK12／DC46／CS10，MISO和RST均NC |
| 背光 | GPIO45高有效，复用PWM |
| 音频 | 单ES8311，I2C0 SDA16／SCL15，I2S0 MCLK4／BCLK5／WS7／DIN6／DOUT8，输入输出24kHz mono，useMCLK=true |
| 功放 | GPIO1低有效；先锁存高再输出，静音／关闭先关PA，实际DAC解除静音成功后再拉低使能 |
| 按键 | BOOT0低有效，首次释放后接受，接现有按住说话／卡片选择确认逻辑 |
| 布局 | 320×240横屏与16px中文字体；无触摸／无LED，不注册pointer |
| 容量 | 固定发布默认16MB／Octal，未确认实际模组容量 |

音频复用现有重采样、PCM24播放、软件AEC与唤醒路径；不套用StickS3专有60%音量缩放。与Freenove的相同音频事实复用PA门控路径，但保留不同SPI端口、频率、MISO、屏幕方向、初始化和触摸能力。

## 初始化表中的越界读取修正

上游表有22项，其中三项分别为 `{0, {0}, 0x11, 0}`、相同一项及 `{0, {0}, 0x29, 0}`。在真实 `ili9341_lcd_init_cmd_t` 中第三字段是数据字节数，不是命令或延时标志；这些项提供1字节，却让驱动读取17／17／41字节。不能按原样移植，也不推测其作者本意为SLPOUT或DISPON。

本地保留其余19项的全部数据与顺序，移除三条错误NOP记录，并增加静态长度检查。数据头 `main/quandong_panel_commands.h` 使用固定15字节最大负载，转成ILI9341组件的命令结构。MIT通知保留于嵌套 `main/third_party/xiaozhi_quandong/`，未修改项目整体许可或复制板类应用逻辑。

实际采用ILI9341 **2.1.0**，公开固定源码来自esp-bsp提交 `93460b932beba7022a8a4ec4186e0d6b7533d05f`。其init先发送零参数SLPOUT并等待100ms，再执行vendor表；末尾亮屏由共用 `esp_lcd_panel_disp_on_off(..., true)` 完成。没有补造未知延时／额外命令。HARDWARE_DIFF.md声称1.2.0，但同一固定提交根manifest实际要求 `^2.1.0`，本地固定2.1.0。

板类构造虽然填RGB，vendor的MADCTL数据为0x08；2.1.0驱动会同步覆盖内部MADCTL缓存，因此实际BGR位保留。再应用swapXY和mirrorY，名义最终MADCTL为0xA8；面板与LVGL配置保持同一方向，不把构造RGB误称最终RGB。最终色序、gamma、方向、供电／复位和睡眠退出仍须实际屏幕验证，静态修正不是硬件通过证据。

原22项与本地19项的数据／顺序对照记录在忽略目录 `evidence/quandong-source-20261110/panel-table-comparison.json`；固定公开源码及SHA-256记录在既有 `board_research_v2.5.2.json` 的同一finding，没有重复追加变体。

## 依赖与验收边界

真实预检命令：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile quandong-s3-dev --destination I:\xy_board_builds_20261109\quandong-s3-dev-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1：`Required component is not installed: espressif/esp_lcd_ili9341; nothing was staged`，目的目录不存在。没有启动本板编译；记录 `dependency_resolution=blocked_missing_local_sources` 和 `build=not_run`，见 `native_dependency_preflight_20261110_quandong.json`。公开组件文件只读下载，没有安装；传递依赖cmake_utilities0.*解析也未完成。

上游BOOT点击切换聊天和启动期重配网手势没有完整移植，本地使用既有通用按键语义；没有添加原厂未实例化的扩展器、第二codec、触摸或LED。板卡修订／实际Flash和PSRAM、24kHz时钟与槽位、PA极性、增益／静音／重开、屏幕gamma与方向、BOOT、AEC／唤醒、配网／绑定／提醒及同板OTA均未实机验收。当前仅有微雪1.8实物，屏控修订未确认。

本轮未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。手机热点显示六位绑定码的人工决策实现未修改。

## 公共回归与当前矩阵

本轮在既有EYE隔离stage同步最终源码并刷新变化文件时间，实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=chd-esp32s3-eye reconfigure build` 退出0。日志包含main、audio_io、waveshare_spi_display实际编译；本机IDF5.5.6-dirty、工具／Python与17个既有组件override保持原样，Python offline_guard拒绝外网。旧EYE入口不选择Quandong宏／新表，这不是新板或干净发行构建通过证据。

构建进程结束后，实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/assistant_chdeye.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/assistant_chdeye.elf`，均退出0；项目assistant_chdeye／16MB／checksum／validation hash匹配，仍链接ES8311与ST7789显示路径。应用2,244,624字节，3MB分区剩901,104字节；模型2,203,819字节，在4MB分区内。124项输入摘要／117个main文件集合、编译宏、生成配置与合并defaults和工作区匹配，产物与来源归档见 `cached_build_attempts_20261110_quandong_chd-esp32s3-eye.json` 和忽略目录 `evidence/quandong-builds-20261110/`。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前73官方源码变体／74本地profile含1历史入口／115待源码；累计44个变体曾缓存编译通过。当前1 passed／43 stale／6历史依赖预检失败／138 not_run，其中23项not_run已有源码；全部188实机not_run。全量目标仍进行中。

收尾实际执行 `git diff --check` 退出0；本批新增文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1仅表示新增差异）。124输入／16项归档构建产物／7项固定源证据／预检日志SHA-256与当前文件匹配，对照编辑前快照审阅本批diff。旧EYE回归进程已结束，未更改配对人工决策函数或无关网关／运行时／自然插话代码。
