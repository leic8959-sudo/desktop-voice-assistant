# Magiclick 2.5 编译器错误定位与整板构建恢复

本批继续上轮真实失败的 `magiclick-2p5`，未修改固件main、板型配置、依赖、SDK或编译优化选项。先核对成功的AtomS3R EchoBase与失败的Magiclick输入，再记录原始编译命令的重放结果。不是降低语音功能、替换算法库或复制另一板对象文件来取得构建成功。

## 新取得的证据

两份真实 `compile_commands.json` 中，ESP-DSP 1.8.0 `dspi_conv_f32_ansi.c` 的命令去除stage路径差异后逐项一致；完整预处理文本去掉行号指令、归一化stage路径后也逐字一致，非仅比较函数尾部。原始命令、归一化diff和前批预处理来源分别保存。

保留原目标／优化参数，直接编译两份已生成的预处理文件，两项退出0；再**完整重放Magiclick原始编译命令**（同一源码、同一参数、同一stage对象输出路径）退出0。命令经过Windows参数解析后以数组调用，未经shell重解释，未添加优化规避开关。

随后调用原 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=magiclick-2p5 build`，并发沿上一轮单任务设置。最终结果另行归档为本批构建记录，不覆盖前批两次真实失败。

最终整板构建退出0，日志明确包含Ninja重新编译 `dspi_conv_f32_ansi.c.obj`，不是只接受手工重放产生的对象后跳过该文件。应用 `assistant_magic25` 为2256864字节、模型2203819字节；271项当前输入与stage一致、24份产物快照。实际readelf／nm／size、esptool及分区解码均退出0，项目身份、checksum／validation hash、双3MiB OTA和4MiB模型分区核对通过，应用槽剩888864字节。

这些证据说明此前GCC内部错误不能由当前源码语法、已识别的参数差异或预处理后的代码差异解释；**没有确定根因**。不能据重放成功声称工具链已修复，也不能把先前失败确定归因为内存不足、并发或硬件问题。使用的编译器仍为现有 `crosstool-NG esp-14.2.0_20260121 / GCC14.2.0`，二进制摘要与版本输出归档。

## 实际命令与边界

证据在 `evidence/magiclick-gcc-diagnosis-20261112/`。诊断编译的完整参数、cwd、退出码、日志和对象摘要在 `preprocessed-compile.json`、`original-command-replay.json`；前批失败记录与摘要在 `compiler-and-prior-evidence.json`。诊断对象仅作定位，不代替最终整板构建、链接或镜像。

```text
python -B evidence/magiclick-gcc-diagnosis-20261112/build_cached.py
python -B evidence/magiclick-gcc-diagnosis-20261112/collect_build.py
python -B evidence/magiclick-gcc-diagnosis-20261112/inspect_sdk_limits.py
python -B evidence/magiclick-gcc-diagnosis-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/magiclick-gcc-diagnosis-20261112/finalize.py
```

构建复用先前Magiclick隔离stage，保持SDK5.5.6-dirty／现有本地override／Python外网保护；单任务仅为既有构建环境选项，不更改代码生成优化。原DSP编译重放正常产生自身stage对象和依赖文件，未将预处理诊断对象或其它目标object拷入该位置；最终IDF/Ninja日志另作验收依据。

显示仍为原ADC选择的GC9107／ST7735两分支，24条专用命令及COLMOD05、原GPIO、24kHz音频、AEC／完整自定义唤醒／自然插话、媒体、配网／绑定和双OTA均保持原文。

初次产物收集脚本要求 `detect_magiclick_panel()` 独立符号，实际优化编译将它内联，因而断言停止。已改为核对真实ADC读取／配置／删除调用、未知修订停止的诊断字符串、当前板型宏及源码／配置摘要，同时检查新的GC9107回调；这些实际证据均存在。没有为了核对增加noinline、改优化或更换镜像。该收集断言问题与之前真实编译器崩溃分开保存。

## 未完成

GCC内部段错误根因与长期稳定性未解决；SDK强制UND、原LVGL等警告继续保留。本批不宣称干净发行构建、零UND、可启动或实机通过。当前只有微雪1.8实物，Magiclick屏幕两修订、ADC检测、声学与网络／OTA仍需该板验收。

本批没有新增profile、安装依赖、改SDK、执行独立测试套件或类型检查、启动服务、Git写入、刷机、OTA、上传或发布。

Magiclick当前依赖状态改为resolved_local_overrides，历史失败和 `direct_cache_available_build_failed` 保留在此前快照。官方仍152／188源码入口、153本地profile、36待源码；当前4 passed_cached_sources／72 stale_cached_sources／3 failed_cached_sources／109 not_run，累计76个官方变体曾缓存构建通过。当前Magiclick通过与之前AtomS3R／Freenove通过均对应同一份未更改的main输入集合；188项本轮实机均not_run。
