# AEC参考缓存内存收敛

本批将AEC软件参考的逐样本64位时间戳改为有界分段记录，保留8192个16位PCM样本及每次push原始时间起点。当前S3音频文件实际编译退出0，当前Spot C5整板缓存构建退出0；两种目标均显示参考缓存对象20496字节。原PCM＋时间戳＋两个索引为81928字节，本批合计减少**61432字节，约60KiB**。没有将这项节省算作C3/C6整板适配完成。

## 实现与边界

新增`main/audio_reference_buffer.h`，由现有`g_reference_lock`保护，不增加线程或动态内存分配。PCM仍是8192样本；最多256段，每段保存起始微秒、原始样本数和已经消费的样本数。前端读取的时间仍是`start_us + floor(consumed * 1000000 / 16000)`；部分读取和环绕淘汰只增加consumed，不重新确定起点，因此保留每次push的62／63微秒取整节奏。缓存空后清读写位置。原7ms播放提前量、250ms过期判断、静音参考、能量／延迟统计及样本容量溢出淘汰方式保留。

这是一项有资源上限的实现：新元数据边界为256个尚未消费完的写入段，独立于8192个PCM样本容量。普通320样本参考段的8192样本缓存最多约26段，但不能据此假设所有网络分片都如此；持续极小碎片可能先耗尽分段槽。本批未做分片运行验收。

push在变更任何数据前，先计算按原样本容量淘汰后能否腾出分段槽；若仍满则明确返回失败。调用者记录`reference_schedule_full`，增加参考generation、清理缓存、结束本包播放状态、释放播放锁并返回`ESP_ERR_NO_MEM`，不会写出这包PCM后继续假装AEC参考完整。实际播放错误由既有调用方处理。上限场景、音频连续性、波形、回声和自然插话仍需验收，不能由源代码公式或链接结果推断已经等价。

显式internal-RAM manual分支按1样本／1段实例化；它仍不启用本地唤醒／AFE／AEC，本批没有把它改名为完整无PSRAM方案。C3/C6完整语音引擎仍缺内部RAM分配、芯片API接入和协议缓存收敛。这次共享参考缓存节省的是另一项静态开销；五项协议缓存载荷下限533376字节仍然存在，不能直接从其中减去61432。

## 修改范围

只修改`main/audio_io.cpp`并新增`main/audio_reference_buffer.h`。其他已有main文件及native registry逐项与上批摘要核对保持原文，包括控制协议中的自然插话／媒体完整唤醒人工决定、固定词、绑定和OTA代码。没有变更板型注册、引脚、采样率或生产分区，没有增加可选配置开关。

## 实际执行

```text
python -B evidence/reference-buffer-20261112/compile_s3_audio.py
python -B evidence/reference-buffer-20261112/build_cached.py
python -B evidence/reference-buffer-20261112/collect_build.py
python -B evidence/reference-buffer-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/reference-buffer-20261112/close_research.py
```

S3编译使用已有真实生成配置及其原始编译参数，将源码路径和输出目标改为当前音频文件及新的证据对象。编译／nm退出0，无编译输出；这是当前S3音频单文件编译，不能当作S3完整重新链接或实机结果。

Spot C5使用新目录`I:/xy_board_builds_20261109/reference_buffer_spot_20261112`，先运行现有prepare工具核对SDK并暂存，再执行：

```text
idf.py -DIDF_TARGET=esp32c5 -DASSISTANT_BOARD=espressif-esp-spot-c5 build
```

实际SDK为本机既有`v5.5.6-dirty`，HEAD为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，16份本地override，Python外网保护有效。当前263项固件输入与工作区一致，main源文件另外归档；23份产物／配置／命令资料保存摘要。构建退出0，镜像assistant_spotc5为1550800字节，单词模型125951字节／SHA256与前批相同；镜像checksum／hash、RISC-V EXEC和双3MiB OTA分区核对退出0。实际nm确认新参考缓存对象0x5010字节，旧两个大数组符号不再出现，同时保留AEC／固定唤醒／播放入口。原有无屏BSP_DISPLAY_LVGL_BUF_HEIGHT Kconfig未知符号警告仍存在，未把本次构建描述为无警告或干净安装通过。

build脚本的首份记录误把prepare命令字段写成后续idf命令，实际prepare子进程正确执行。collect修正这个元数据字段并保留说明，没有改构建退出码、日志或产物。全部实际命令数组、工作目录、结果及摘要在本批证据中。没有新增或运行测试套件、独立类型检查、播放或错误注入；不把静态审计当作运行测试。

## 适配账本与未完成

Spot C5新增独立构建记录`cached_build_attempts_20261112_reference_buffer_spot.json`。当前1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run；历史构建记录不改写。官方源码入口仍152／188，153本地profile含1历史入口，36项待源码。188项本轮实机全部not_run；当前只有微雪1.8实物。

接下来仍要保留完整功能地处理唤醒前后缓存、上传队列与播放／插话缓存，完成C3/C6芯片音频端口及具体板卡构建，再验收运行堆／栈峰值、分段槽耗尽、碎片化／静音／取消／回声／自然插话。没有安装、启动长期服务、Git写操作、刷机、OTA、上传或发布。
