# T-Circle 播放时钟修正与十三款 S3 当前源码构建

本批检查剩余历史构建项时，发现两款 T-Circle 的 MAX98357A 仍配置为不支持的 24kHz 物理播放。修改 `board_profile.h` 和 `digital_audio.cpp`，将其接入已有 16kHz 物理播放、16 位双声道复制 mono、网络24→16kHz FIR 和相同实际播放 PCM 的软件 AEC 参考。随后为十三个旧隔离目录已丢失的板型重新准备缓存环境并逐板构建。

固定上游为小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行清单为 **154 目录／188 变体**，README 的 138／171 是旧数字。仍为 152 个官方源码入口、153 个本地 profile（含历史入口），36 项无源码入口、4 项已知必需功能缺口保留。Cardputer ADV 的本地唤醒／AFE／AEC 等缺口仍单独登记，不用重建把它计成完整适配。

## 播放时钟缺陷与最小修改

厂家固定 T-Circle README 明确功放型号为 MAX98357A。公开固定来源通过 `gh api` 读取并按 Git blob 核对，记录在 `manufacturer-reference.json`；直接 raw URL 初次连接被重置，保留失败日志。芯片规格书复用此前已取得的厂家固定 PDF，核对 SHA-256 后存入本批参考目录。规格书说明 LRCLK 支持 8／16／32／44.1／48／88.2／96kHz，并明确不支持 11.025／12／22.05／24kHz。

此前板型文档和当前源码仍为 24kHz 输出，不能用历史编译成功证明这个配置满足器件要求。本批仅将 `ASSISTANT_BOARD_TCIRCLE` 加入以下既有选择：

- `board_profile::speaker_sample_rate=16000` 的 MAX98357 分支。
- `digital_audio` 的 16 位立体声输出格式和有界 mono→左右双声道复制路径。

麦克风、引脚、PA45、软件音量／PCM 静音和启停、160×160 GC9D01N、CST816D、按键、绑定和 OTA 保留。网络 PCM 仍为 24kHz；上层已有 `audio_play_pcm24` 在物理输出为 16kHz 时，将现有 FIR 的同一组样本交给扬声器和软件 AEC 参考。本批不复制第二套重采样器、不更改协议或关闭唤醒／AEC。

| T-Circle 入口 | 麦克风保持 | 物理播放修正后 |
| --- | --- | --- |
| lilygo-t-circle-s3 | I²S0、24kHz、左侧32位槽转 PCM16 | I²S1、16kHz、stereo16，mono 复制到两声道 |
| lilygo-t-circle-s3-v11 | I²S0、PDM32kHz，现有 FIR 降至16kHz | 同左；PDM 时钟不随播放改成16kHz |

这项修改依据器件规格修正配置，不代表实际音频时钟、声道、电平、PA 启停、增益、AEC／自然插话或听感已验收。V1.1 的 PDM 时钟和原版采集分别保持；MAX98357 的 LRCLK／BCLK 停止顺序仍需实机观察。既有未修改的其他板型不因此获得新的硬件结论。

## 十三款构建范围

| profile | 显示／音频选择 |
| --- | --- |
| bread-compact-wifi-lcd | 240×320 SPI，数字音频 |
| genjutech-s3-1.54tft | 240×240 SPI，单 ES8311，16k 输入／输出 |
| lc-s3-wifi-1.54tft | 240×240 SPI，单 ES8311 |
| lilygo-t-circle-s3 | 160×160 GC9D01N／CST816D，标准 I²S 麦克风，修正后的16k播放 |
| lilygo-t-circle-s3-v11 | 同尺寸，PDM32k 采集，修正后的16k播放 |
| nologo-xingzhi-abs-2.0 | 240×240 SPI，单 ES8311 |
| nologo-xingzhi-cube-1.54tft-wifi | 240×240 SPI，数字音频 |
| nologo-xingzhi-metal-1.54-wifi | 240×240 SPI，单 ES8311，专用触摸按键解析 |
| spotpear-sp-esp32-s3-1.54-muma | 240×240 SPI／CST816S，单 ES8311 |
| waveshare-lcd-0.85 | 128×128 SPI，无触摸，ES7210＋ES8311，独立8MB分区 |
| waveshare-lcd-1.54 | 240×240 SPI／CST816S，ES7210＋ES8311 |
| waveshare-lcd-1.83 | 240×284 SPI／CST816S，ES7210＋ES8311，现有一线SD |
| zhengchen-1.54tft-wifi | 240×240 SPI，数字音频 |

全部保留自定义“你好，小燕子”、AFE／软件 AEC、媒体、绑定、提醒和双 OTA。表中尺寸取自当前板级头，真实布局、颜色、触摸角点、板卡修订与外围仍需实机验收，不能据同为1.54英寸或同为S3互用镜像。

## 串行隔离构建与失败留档

这十三项的旧临时目录已不存在。新建专用目录 `I:/xy_board_builds_20261109/remaining_s3_matrix_20261113`，只使用现有 ESP-IDF 5.5.6-dirty、IDF Python、Xtensa `esp-14.2.0_20260121`、CMake 3.30.2、Ninja 1.12.1、ROM 20260528，以及17份已有组件副本和 offline_guard，不安装或下载依赖。

十三项先分别执行 `prepare_cached_firmware_build.py --check-only`，全部退出0，然后为第一个板型准备来源。由于全部为同一芯片和兼容的相同组件缓存，使用该新目录串行运行矩阵；每板选择自己的 manifest、板配置、分区、编译宏和应用名。切换前保存并移走上一板生成的 `sdkconfig`／`sdkconfig.old`，要求当前 `sdkconfig` 不存在，再由 IDF 按 common→chip→board 默认值重新生成。

Ninja 可以依据依赖复用同一芯片／相同配置的组件对象，每板都实际重新配置、编译所需差异并链接独立镜像，不复制其他板的 main 对象或镜像。每板完成真实 ELF／镜像／分区核对并归档后才切换下一板。最终审计比较前板记录时间与后板构建时间；工作目录最后只保留最后一板，前面各板以各自完整归档为准。

重新生成板配置实际触发了大量 SDK 重编。因此在核对本任务进程树后，只停止当时未完成的 Genjutech Ninja 子树，让 IDF／runner 退出并保留 `interrupted-*` 记录（IDF退出2）。随后使用已安装的 ccache 4.12.1 和本批专用目录，容量参数1G（实际配置显示1GB）、编译器内容检查，启用 `idf.py --ccache` 后重试，继续保持2 jobs。它依据编译输入缓存对象，不更改优化或源码、不从别板手工复制对象；实际统计另存。没有安装 ccache、改变全局缓存配置或停止其他服务。切换记录及可执行文件摘要见 `cache-transition.json`，有效配置另存 `ccache-effective-config.json`，不把有意中断的尝试算成最终通过。

初次准备脚本误写 Python 编码名 `utf8-sig`，在调用 IDF 前失败，已改为 `utf-8-sig`，保留错误记录。随后第一项完整编译开始后发现 T-Circle 时钟缺陷；第一项对应修改前输入，因此保留为 `superseded-build.*`，不计为最终源码通过。修改后同步真实源文件并重新配置构建，正式账本只引用最终源码结果。这些准备／输入过期问题与实际编译结果分别记录。

运行命令：

```powershell
& C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B evidence/remaining-s3-rebuild-20261113/build_sequence.py
```

每项首次实际调用 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，通常使用 `IDF_PY_BUILD_JOBS=2`；Genjutech 最终重试为1，Genjutech 和 LC 的已有配置重试仅运行 `build`。完整参数、时间、选择快照和退出码保存在各板 `build.json`／`selection.json`。

面包板LCD最终构建在启用ccache前完成；后续目标在上述命令中增加 `--ccache`，保留准确逐板命令和缓存统计，不将它们写成相同构建模式。

Genjutech 首次 ccache 构建在 ESP-DSP 的 `dspi_conv_f32_ansi.c` 上出现 GCC14.2.0 的 IRA 阶段内部段错误；保留日志后一次单任务重试仍相同。按 CMake 的原命令、原编译器／源码／选项直接编译至独立诊断对象退出0，没有把诊断对象或其他板对象移入固件。随后在该目标的正式单任务构建中设置 `CCACHE_DISABLE=1` 旁路缓存，Ninja 自己重新生成所需对象，最终结果另记。缓存关闭只改变工具执行方式，不删除DSP或改优化；目前不能由这些结果确认GCC错误根因，历史Magiclick同类问题也不因此宣称修复。失败记录分别保留 `compiler-failure-*` 和 `single-job-failure-*`，具体命令／参数在 `dsp-command.json`／`dsp-direct-diagnostic.json`。

Genjutech 后切换 LC 时，脚本曾错误设置 `CCACHE_DISABLE=0`，ccache 对该布尔环境变量拒绝字符0，IDF退出2。已保留 `environment-failure-*`，修正为启用缓存时删除该环境变量，关闭缓存时才设置1。它是本批执行脚本错误，不是固件源码、编译器内部错误或硬件失败；LC在同一已选配置上重试，未更改代码生成参数。

每板立即执行八条真实产物检查：`readelf -h`、`nm -S -C --size-sort`、`nm -u`、`size -A -d`、`esptool image_info --version 2`、分区二进制解析和两条SDK只读Git查询。T-Circle 两项另用 CMake 的真实 `digital_audio.cpp` 编译参数执行 `-E -P`，记录编译器选中的输入／输出率、stereo16 和 mono复制分支。预处理只是编译选择证据，不是运行PCM或声学测试。每项完整命令、输出、退出码和摘要均在 `commands.json`。

```powershell
# python 指上述已安装的 IDF Python。
python -B evidence/remaining-s3-rebuild-20261113/collect_batch.py
python -B evidence/remaining-s3-rebuild-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/remaining-s3-rebuild-20261113/finalize.py
git diff --check
```

## 最终构建与矩阵

十三项最终源码整板构建均退出0；普通板每项八条产物检查，T-Circle两项各九条（含真实编译参数预处理），全部退出0。每项保留330个当前输入摘要，共归档554个产物。

| profile | app字节 | app分区剩余 | model分区剩余 | 产物数 |
| --- | ---: | ---: | ---: | ---: |
| bread-compact-wifi-lcd | 2,207,840 | 937,888 | 1,990,485 | 40 |
| genjutech-s3-1.54tft | 2,247,552 | 898,176 | 1,990,485 | 41 |
| lc-s3-wifi-1.54tft | 2,246,992 | 898,736 | 1,990,485 | 41 |
| lilygo-t-circle-s3 | 2,223,328 | 922,400 | 1,990,485 | 43 |
| lilygo-t-circle-s3-v11 | 2,226,224 | 919,504 | 1,990,485 | 43 |
| nologo-xingzhi-abs-2.0 | 2,247,168 | 898,560 | 1,990,485 | 42 |
| nologo-xingzhi-cube-1.54tft-wifi | 2,208,768 | 936,960 | 1,990,485 | 42 |
| nologo-xingzhi-metal-1.54-wifi | 2,249,280 | 896,448 | 1,990,485 | 43 |
| spotpear-sp-esp32-s3-1.54-muma | 2,257,968 | 887,760 | 1,990,485 | 43 |
| waveshare-lcd-0.85 | 2,254,176 | 498,336 | 155,477 | 44 |
| waveshare-lcd-1.54 | 2,261,888 | 883,840 | 1,990,485 | 44 |
| waveshare-lcd-1.83 | 2,331,312 | 814,416 | 1,990,485 | 44 |
| zhengchen-1.54tft-wifi | 2,208,752 | 936,976 | 1,990,485 | 44 |

两款T-Circle的编译器预处理结果确认物理输出16000Hz、stereo16复制mono；原版输入24000Hz、V1.1输入32000Hz，声学验收仍未执行。Genjutech两次GCC内部错误和LC工具环境错误保留，最终正式构建及产物检查均成功，不将诊断对象计作固件验收。

累计曾缓存构建通过仍为115个官方变体。因为本批修正共享源码，前101项当前记录变为过期，最终矩阵为 **13项当前输入通过／102项历史通过但输入已过期／73项未执行构建**。不是从功能层撤销既有适配，也不能把旧输入的成功继续算作新源码通过。全部实机仍未验收。


每板核对真实镜像项目身份、芯片、Flash设置、校验摘要、分区偏移／大小／标志，以及app和model是否容纳。0.85沿独立8MB双0x2A0000应用和0x240000模型分区，其余沿既有16MB布局，均保留NVS。模型均为2,203,819字节，MN5与自定义唤醒源文件保留。归档当前输入、镜像、ELF、map、模型、配置、分区、编译命令及所选main对象。

共享源码改变后，前101项当前通过记录会过期，不能继续称为当前源码通过。它们的原始结果没有撤销或覆盖，后续需要依据新输入重建。累计曾通过缓存构建仍包含一个有已知必需功能缺口的Cardputer ADV候选，不能据累计数字宣布完整适配。

SDK仍保留四个既有强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`，原警告与dirty状态如实记录。本批不是干净发行、启动或实机验收。188项实机仍全部`not_run`，唯一现有微雪1.8的具体修订仍待确认。

最终审计首次误要求所有最终日志都出现ccache配置提示；Genjutech／LC的正式重试只运行build，不会重复输出CMake配置提示。对照归档CMakeCache确认CCACHE_ENABLE与实际工具路径后，改为所有ccache目标核对真实配置，只有reconfigure命令额外检查提示。该审计脚本修正保存在 `final-audit-repair.json`，产物和固件未改变，最终审计退出0。

未运行独立测试套件／类型检查、启动服务、安装依赖、Git写操作、刷机、OTA、上传或发布。旋钮、APA102、传感器、电池、原厂全部手势、物理时钟、声学与同板OTA回滚仍需分别处理；本批不缩减全量目标。

## 证据入口

- [最终审计](../evidence/remaining-s3-rebuild-20261113/final-audit.json)
- [本批源码差异](../evidence/remaining-s3-rebuild-20261113/source.diff)
- [芯片规格来源](../evidence/remaining-s3-rebuild-20261113/amplifier-reference.json)／[厂家板型来源](../evidence/remaining-s3-rebuild-20261113/manufacturer-reference.json)
- [逐板结果](../evidence/remaining-s3-rebuild-20261113/batch-build-results.json)／[产物汇总](../evidence/remaining-s3-rebuild-20261113/collection-summary.json)
- [未完成构建范围](../evidence/remaining-s3-rebuild-20261113/remaining-build-scope.json)
- [官方矩阵](../firmware/assistant_espidf/board_matrix_v2.5.2.json)
