# C5 固定唤醒、无屏与 OLED 九板当前源码重建

本批重建四款 C5 和五款 S3，补齐当前源码的整板构建证据，保留逐板唤醒、音频、显示及绑定差异。不新增源码入口，不改变固件引擎、配置、分区、人工决策或设备凭据。

固定范围仍为小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 **154 个发行目录／188 个变体**；README 的 138／171 是旧数字。当前 152 个官方变体有源码入口、153 个本地 profile（含一个历史入口），36 个尚无入口、4 项已知必需功能缺口仍保留。

## 逐板差异

| profile | 芯片／唤醒词 | 显示或绑定入口 | 音频路径 |
| --- | --- | --- | --- |
| waveshare-esp32-c5-touch-lcd-1.69 | C5／你好小智 | 240×280 ST7789、CST816S | ES8311 BOTH mono24k，TCA9554 P0 控制 PA |
| movecall-moji2-esp32c5 | C5／你好小智 | 360×360 ST77916 QSPI；没有声明触摸 | ES8311 BOTH mono24k，PA5；保留 USB 控制台避开音频引脚 |
| espressif-esp-sensairshuttle | C5／你好小智 | 284×240，板级初始化命令＋SDK DCS；触摸按键 | ADC16k＋PDM24k，PA1，独立 ADC owner |
| espressif-esp-spot-c5 | C5／你好小智 | 无屏，手机连接热点页面 | 单 ES8311 BOTH mono16k，从 BCLK 获得 codec 时钟，PA23 |
| doit-s3-aibox | S3／你好，小燕子 | 无屏，手机连接热点页面 | PDM16k 输入／数字 I²S24k 输出；GPIO48 不当成已证实 PA |
| m5stack-atom-echos3r | S3／你好，小燕子 | 无屏，手机连接热点页面 | 单 ES8311 24k，从 BCLK 获得 codec 时钟，PA18 |
| m5stack-atoms3r-cam-m12-echo-base | S3／你好，小燕子 | 无屏，手机连接热点页面；无应用键，重启开查看窗口 | 单 ES8311 24k，PI4IOE 的 P0 控制 PA |
| bread-compact-wifi | S3／你好，小燕子 | SSD1306 128×32 OLED | 数字 I²S 麦克风／扬声器 |
| bread-compact-wifi-128x64 | S3／你好，小燕子 | SSD1306 128×64 OLED | 数字 I²S 麦克风／扬声器 |

C5 四项均保留已经接受的固定 `wn9s_nihaoxiaozhi` 模型、精确模型名／词元数据检查和固定词提示，不以手动按键代替唤醒。S3 五项仍选择原自定义唤醒实现。C5 用 Quad PSRAM，S3 五项用 Octal；不同芯片的 AFE／AEC 路径分别编译，不把 S3 库当作 C5 构建证据。

四项无屏板选择 `headless_ui.cpp`／`headless_portal.cpp`，不创建 LVGL 屏幕。源码复核确认：未绑定时，主启动流程先等待 `pair_device` 成功，再进入唤醒／音频和控制协议；六位码由 headless UI 保存并通过现有热点页面读取。热点请求检查 AP 本地地址、短期会话和查看窗口，凭据领取仍走原设备 nonce 流程。这些是源码顺序和链接证据，**没有用真实手机验证 AP 隔离、网页显示、绑定或提醒送达**。开放热点的访问边界与原实现一致，不宣称独立账号认证。

## 实际执行与中途修正

使用已经安装的 ESP-IDF 5.5.6-dirty、现有 IDF Python，C5 用 RISC-V、S3 用 Xtensa 工具链，均为 `esp-14.2.0_20260121`；CMake 3.30.2、Ninja 1.12.1、ROM 20260528。复用各板原隔离目录和既有组件 override，使用 offline_guard，不下载或安装。最多两个板目录同时构建，每项 `IDF_PY_BUILD_JOBS=2`。

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/c5-headless-rebuild-20261113/build_batch.py
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/c5-headless-rebuild-20261113/retry_batch.py
```

初批进程退出 1：四款 C5 和 128×32 OLED 构建退出 0；其余四项的旧隔离目录没有独立 `sdkconfig_board.defaults`，脚本在调用 IDF 前读取该文件失败。旧目录来自合并配置的历史暂存布局，不能假设它们已有新的分层配置文件。

修正范围仅为本批构建脚本：目标文件不存在时从当前板配置复制，再重新配置构建。保留 `exception.log` 和 `initial-exception.log`，不覆盖初批结果，不把准备失败记成固件编译失败或构建通过。重试只针对这四项，现有固件配置及源码未修改。准确结果见初批与重试 JSON。

每项实际 IDF 命令为 `idf.py -DIDF_TARGET=<esp32c5或esp32s3> -DASSISTANT_BOARD=<profile> reconfigure build`，完整解释器路径、工作目录、开始结束时间、同步文件摘要与退出码保存在各板 `build.json`。

构建完成后分次调用 `collect_batch.py` 中的 `collect`，最后汇总。产物核对使用对应架构的 `readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`，以及 `esptool image_info --version 2`、实际分区二进制解析、SDK 只读 Git 查询；每项八条命令及真实输出均保留。随后执行：

```powershell
# python 指上述已安装的 IDF Python。
python -B evidence/c5-headless-rebuild-20261113/collect_batch.py
python -B evidence/c5-headless-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/c5-headless-rebuild-20261113/finalize.py
git diff --check
```

## 最终产物结果

最终九项整板构建及每项八条产物核对命令均退出 0；每项保存 330 个当前输入摘要，共归档 365 个产物。

| profile | app 字节 | app 分区剩余 | 模型字节 | 模型分区剩余 |
| --- | ---: | ---: | ---: | ---: |
| waveshare-esp32-c5-touch-lcd-1.69 | 2,226,224 | 919,504 | 125,951 | 4,068,353 |
| movecall-moji2-esp32c5 | 2,495,040 | 650,688 | 125,951 | 4,068,353 |
| espressif-esp-sensairshuttle | 2,492,672 | 653,056 | 125,951 | 4,068,353 |
| espressif-esp-spot-c5 | 1,552,336 | 1,593,392 | 125,951 | 922,625 |
| doit-s3-aibox | 1,583,216 | 1,562,512 | 2,203,819 | 1,990,485 |
| m5stack-atom-echos3r | 1,618,144 | 1,134,368 | 2,203,819 | 155,477 |
| m5stack-atoms3r-cam-m12-echo-base | 1,619,808 | 1,132,704 | 2,203,819 | 155,477 |
| bread-compact-wifi | 2,158,816 | 986,912 | 2,203,819 | 1,990,485 |
| bread-compact-wifi-128x64 | 2,158,624 | 987,104 | 2,203,819 | 1,990,485 |

累计曾缓存构建通过的官方变体仍为 115 个。本批更新九项当前输入通过；全部矩阵变为 **20 项当前通过／95 项已过期／73 项未执行构建**。本批没有新增首次通过的官方变体，不把历史通过写成当前通过。


C5 四份模型均为 125,951 字节，SHA-256 均为 `fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b`，保留 `wn9s_nihaoxiaozhi`。S3 五份模型均为 2,203,819 字节，选用自定义唤醒源码。编译命令核对 fixed／custom wake 及 headless／display 的实际选择，ELF 核对 AEC、网络、媒体及各板音频／显示接口，不把无屏固件误算成已链接 OLED。

实际镜像核对芯片、项目身份、Flash 配置和校验摘要；分区二进制逐项与当前板 CSV 对比，保留 NVS 0x9000／0x4000、双 OTA、模型及存储布局。Spot 的 8MB 为双 3MiB app＋1MiB model；两款 8MB Atom 为双 0x2A0000 app＋0x240000 model。其余项沿 16MB 既有分区。当前 app／model 必须落入各自分区，不能从其他芯片或板型的产物推断容量。

ELF 仍保留 SDK 强制 UND：C5 为 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`；S3 另有 `start_app_other_cores`。警告数量和日志如实保留。本批不是干净发行、启动、声学或手机验收；符号存在不能证明唤醒／AEC／自然插话、网络、绑定、媒体、提醒、SD 或 OTA 的真实运行。

本批不运行独立测试套件／类型检查，不启动服务，不执行 Git 写操作、刷机、OTA、上传或发布。188 项实机仍全部 `not_run`，只有一块具体修订待确认的微雪 1.8。C3／C6 的内部内存路径、其他新 SDK／缺失依赖、蜂窝会话及四项已知功能缺口仍需继续推进，不用删减功能来计入完成。

## 证据入口

- [最终审计](../evidence/c5-headless-rebuild-20261113/final-audit.json)
- [初批结果（五项构建通过、四项准备失败）](../evidence/c5-headless-rebuild-20261113/batch-build-results.json)
- [四项重试结果](../evidence/c5-headless-rebuild-20261113/retry-build-results.json)
- [本批板型／目录／唤醒差异](../evidence/c5-headless-rebuild-20261113/plan.json)
- [构建与产物汇总](../evidence/c5-headless-rebuild-20261113/collection-summary.json)
- [官方适配矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
