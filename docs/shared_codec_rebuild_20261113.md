# 原版 1.8 接入后的九板共享路径重建

本批覆盖 CO5300 1.8 v2、AMOLED 2.06／2.16／1.75／1.75C、CoreS3、BOX／BOX-Lite／BOX3，验证原版 1.8 SH8601 接入后的当前固件源码能否继续完成整板构建。只更新构建证据和矩阵，不新增板型入口、不修改固件引擎、不把构建通过当成实机验收。

小智 v2.5.2 固定提交为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。实际发行清单为 154 目录／188 变体，README 的 138／171 为旧统计。源码入口仍为 152 个官方变体、153 个本地 profile（含历史入口），36 个尚无源码入口，4 项已知必需功能缺口保留。

## 本批验证边界

1.8 v2 使用缓存 BSP 2.0.3 的 CO5300／CST816S 或 FT5x06 探测路径，仍保留旧的单 ES8311 音频初始化和专属内部 DMA 缓冲。原版 1.8 则是独立的 SH8601／FT5x06 配置。本批核对 v2 没有误链接新 SH8601 或 ES7210 驱动，不根据商品名推断现有实物修订。

| 板型 | 显示及触摸路径 | 音频路径 | SD 源码选择 |
| --- | --- | --- | --- |
| 1.8 v2 | BSP CO5300，CST816S／FT5x06；gap 由探测结果决定 | 旧单 ES8311 | 有 |
| 2.06 | 本地 SH8601／FT5x06 | 共享 ES7210＋ES8311 | 有 |
| 2.16 | 本地 CO5300／CST9217；保持 A0 方向设置 | 共享 ES7210＋ES8311 | 有 |
| 1.75 | 本地 CO5300／CST9217 | 共享 ES7210＋ES8311 | 有 |
| 1.75C | 本地 CO5300／CST9217 | 共享 ES7210＋ES8311 | 无 |
| CoreS3 | 自有 ILI9341 命令，SDK DCS 操作／FT5x06 | ES7210＋AW88298 | 有 |
| BOX | 自有 ILI9341 命令，SDK DCS 操作／TT21100 | ES7210＋ES8311 | 无 |
| BOX-Lite | 自有 ILI9341 命令，SDK DCS 操作；无触摸 | ES7243E＋ES8156 | 无 |
| BOX3 | 自有 ILI9341 命令，SDK DCS 操作／GT911 或 TT21100 | ES7210＋ES8311 | 有 |

SDK DCS 操作使用 `esp_lcd_new_panel_st7789` 的窗口／绘图接口，板级初始化命令保持各自面板的实现；符号名不能当作实际面板型号。CoreS3 同时检查 SDK 的 SPI 前后回调符号，不能以此替代 GPIO35 DC／SD-MISO 复用的实机验收。详细板级取证见既有各板记录。

## 实际命令与环境

使用已安装 ESP-IDF 5.5.6-dirty、现有 IDF Python、Xtensa 14.2.0、CMake 3.30.2、Ninja 1.12.1 和 ROM 20260528；未安装、下载或升级依赖。保留 SDK 和工作区既有改动。最多两个独立板目录同时构建，每个 `IDF_PY_BUILD_JOBS=2`。

v2 的历史临时目录已不存在，因此在 `I:/xy_board_builds_20261109/codec_rebuild_waveshare_amoled_1_8_v2_20261113` 新建隔离目录。只复用现有 17 份组件来源，检查既有 QSPI 回调和亮度错误传播补丁；不修改项目中的 managed_components。其余八个板复用其已有隔离构建目录，同步当前 main、注册信息、根配置、板配置及分区后重新配置构建。每项均有离线网络 guard。

运行命令：

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/shared-codec-rebuild-20261113/build_batch.py
```

脚本为每板执行真实 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。准确命令、工作目录、开始结束时间、同步文件摘要、准备命令和退出码保存在各板 `build.json`。`batch-build-results.json` 是进程结果汇总。

构建完成后用 `collect_batch.py` 的 `collect` 逐板核对实际产物，最后运行脚本汇总，已记录的结果保留。核对命令为 `readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、`gen_esp32part.py` 解析分区二进制，以及 SDK `git rev-parse HEAD`／`git status --short`。准确命令、输出、退出码及摘要保存在各板 `commands.json`。记录完整、源文件摘要仍匹配后才同步矩阵：

```powershell
python -B evidence/shared-codec-rebuild-20261113/collect_batch.py
python -B evidence/shared-codec-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/shared-codec-rebuild-20261113/finalize.py
git diff --check
```

以上 `python` 指现有 IDF Python。`finalize.py` 核对旧源码、注册、manifest、配置、工具和历史构建账本未被改动，核对全部本次输入／归档产物摘要，并保留全部旧研究状态与阻塞项。未运行独立测试套件或类型检查。

## 当前结果

九项实际整板构建和每项八条产物核对命令均退出 0。每项 330 个当前输入摘要，共归档 371 个产物；模型均为 2,203,819 字节，模型分区剩余 1,990,485 字节。

| profile | app 字节 | app 分区剩余 | 归档产物数 | 编译警告数 |
| --- | ---: | ---: | ---: | ---: |
| waveshare-amoled-1.8-v2 | 2,617,792 | 527,936 | 40 | 23 |
| waveshare-amoled-2.06 | 2,614,784 | 530,944 | 42 | 23 |
| waveshare-amoled-2.16 | 2,611,008 | 534,720 | 42 | 33 |
| waveshare-amoled-1.75 | 2,611,232 | 534,496 | 42 | 33 |
| waveshare-amoled-1.75c | 2,541,856 | 603,872 | 42 | 33 |
| m5stack-core-s3 | 2,604,496 | 541,232 | 40 | 25 |
| esp32-s3-box | 2,544,592 | 601,136 | 41 | 25 |
| esp32-s3-box-lite | 2,536,800 | 608,928 | 40 | 25 |
| esp32-s3-box-3 | 2,617,328 | 528,400 | 42 | 25 |

累计曾通过缓存构建的官方变体仍为 115 项。本批把九项历史通过记录更新为当前输入通过；加上上一批的原版 1.8 与历史 1.32，目前为 **11 项当前通过、104 项源码已变化、73 项未执行构建**。104 项过期记录不能当成当前源码已通过。


最终镜像必须同时保留自定义“你好，小燕子”、AFE／AEC、HTTP／WebSocket、媒体、按板 SD 和双 OTA 路径。符号检查只证明被选中并链接；不能证明识别效果、自然插话、网络、绑定、屏幕、触摸、SD、OTA 回滚或资源峰值运行通过。

本批逐项检查镜像中的项目名、ESP32-S3 和 Flash 配置、校验摘要，以及实际分区二进制：NVS 0x9000／0x4000、双 app 各 3MiB、模型 4MiB。保留旧分区布局和 NVS，不因板标称容量更大就自动扩展。ELF 的四个既有 SDK 强制 UND 逐项记录为 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`；不宣称零 UND、启动通过或干净发行。既有编译警告保留在日志和摘要中。

CoreS3 保留板级 Quad PSRAM，其他八项为 Octal。产物核对脚本第一次误用统一 Octal 条件，在 CoreS3 处断言失败；对照当前板配置、实际 sdkconfig 和历史取证后修正为逐板条件，随后重新核对，不修改固件或重编造通过结果。该检查脚本修正记录在 `verification-repairs.json`。

188 个变体实机验收仍全部 `not_run`。唯一现有微雪 1.8 的具体修订仍待确认。本批未启动服务、未做 Git 写操作、安装、刷机、OTA、上传或发布。剩余 C3／C6 内存路径、新 SDK／依赖、蜂窝会话、4 项已知功能缺口和全部实机验收继续保留。

## 交付入口

- [最终审计](../evidence/shared-codec-rebuild-20261113/final-audit.json)
- [构建进程结果](../evidence/shared-codec-rebuild-20261113/batch-build-results.json)
- [本批板型与隔离目录](../evidence/shared-codec-rebuild-20261113/plan.json)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
- [原版 1.8 SH8601 接入记录](amoled18_sh8601_native_20261113.md)
