# 无PSRAM原生Opus源码第一批

牛牛已明确接受Opus有损压缩，要求保留完整功能并逐板标明差异；决策已在`native_opus.py`以Asia/Shanghai时间的人工决策注释记录。本批落实服务端原生音频边界和固件候选组件，尚未完成任何新板型的整机接入／构建或实机验收。现有构建计数不增加。

## 源码改动

- `gateway.py`：认证后的`/device/v1/`握手接受明确声明`xy_opus_v1`的20ms Opus；仍创建原有`NativeDevice`和`VoiceSession(raw_pcm=True)`。绑定、唤醒复核、自然插话、工具和媒体业务沿原生入口处理。
- `native_opus.py`：只为选中该帧协议的连接提供双向转换。上行还原16k PCM后才交给原生会话；下行24k PCM在物理发送前编码。TTS／媒体正常结束排空编码器，取消丢弃旧输出，播放编号匹配后才关闭对应编码流。
- `audio_codec.py`：复用现有PyAV／OpusBridge，新增可选帧长／码率、保留编码包PTS的方法和输入排空；旧调用默认仍为60ms、24kbps及原有VBR设置。新增原生路径使用20ms、40kbps CBR下行。
- `firmware/assistant_espidf/opus_candidate/`：采集编码、播放解码、网络字节序包头和分段前缀缓存。该目录未加入正式main／profile，依赖清单只约束候选组件。
- `tools/prepare_opus_component_build.py`：在仓库外准备独立目标的候选源码工程，不安装或构建。

没有修改`control_protocol.cpp`、`audio_io.cpp`、正式板型manifest、默认配置或分区。现有自然插话和媒体唤醒的人工决策未改变。服务端只改本地源码，未重载运行中的网关、触发OTA或发布。

## 线协议与计数

上行握手需`format=opus`、`sample_rate=16000`、`channels=1`、`frame_duration=20`、`framing=xy_opus_v1`。普通无包头的小智v1 Opus不能混入此原生路径。服务端回应24kHz／20ms／40kbps及相同framing；内部云端、媒体位置和`audio_bytes`继续以有效PCM样本计数。

每个二进制消息包含24字节大端头及至多1275字节Opus载荷：

| 偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0 | 4字节 | `XYO1` |
| 4 | uint32 | stream，连接内严格递增，重连才能重新编号 |
| 8 | uint32 | sequence，首包0，连续递增 |
| 12 | uint32 | 本段此前有效PCM样本数 |
| 16 | uint16 | 解码帧样本数：上行320，下行480 |
| 18 | uint16 | 帧首裁剪样本数 |
| 20 | uint16 | 本帧有效样本数 |
| 22 | uint16 | RESET=1、END=2或CANCEL=4 |

首音频包带RESET；普通音频包flags=0。END是无载荷的正常结束，先排空重采样和有效样本，再发送wake_clip_end／listen stop等边界。CANCEL同样无载荷，丢弃该段尚未交付的解码状态，须在abort之前送达；不能当作正常唤醒片段结束。编号／样本位置／载荷时长／声道不符则终止，不把压缩字节数当作PCM字节数，也不靠丢包继续假定连续。

服务端解码按帧保存最多4条裁剪元数据，处理重采样的跨包延迟；每段无效预热／填充样本有上限。编码端按真实包PTS裁剪首端延迟和末端补零，正常结束要求有效样本总数等于源PCM样本总数。上述是源码约束，未运行验证PyAV19.0.2或ESP库的实际输出行为。

## 前缀缓存与取消

不能只保存任意连续Opus包并假定从中间开始解码等同于原始连续流。候选让采集编码器按独立段重置，每段保留首包至END；缓存淘汰完整段。唤醒时关闭当前段、冻结代次，重放覆盖所需尾部的完整段。起点前必须解码的预热包仍传送，valid=0，之后按实际有效样本重写位置，保留完整解码起点。

例如`SegmentCache<5,52,128,64000>`给四秒有效前缀留出完整起始段及活动段空间；52个槽容纳一秒50帧、延迟尾帧及END。候选上行配置16kHz／20ms／24kbps CBR，理论编码载荷每帧60字节；128字节缓存载荷是上限，超出会失败，未以理论码率冒充实测包大小。单纯包存储字段为`5*52*(24+128+2)=40040`字节，另有结构对齐、段元数据、编解码器、算法、栈、TLS和显示等开销。尚未替换现有五项533376字节的PCM载荷，所以不能称无PSRAM内存问题已解决。

捕获／发送／播放仍须采用原有取消和失败回执；冻结缓存不能被写入，读操作必须匹配代次，解码器只由播放任务使用。取消不重放未确认的业务请求，不关闭AFE／AEC、唤醒或自然插话来凑RAM。

## 逐板决策边界

本轮将Opus接受决策追加到20个C3／C6变体和两款已有INTERNAL_RAM候选（AtomS3 Echo Base、Cardputer ADV）的研究记录。逐板继续保留现有功能缺口、未构建和未验收状态。C3／C6仍采用已接受的“你好小智”固定唤醒词；其它芯片的口令不由此次压缩决策改变。C5现有PSRAM入口继续当前PCM配置。

音质是有损压缩，与PCM不等价；额外分帧、编码、重采样、预热及缓存等待延迟尚未测量。每块板的ASR首字／唤醒／媒体音质、自然插话、AFE／AEC和CPU／RAM峰值均须实测后逐板补录，不能填统一的“无差异”。Guition和纸屏V1的4MB模型／双OTA容量问题也没有因此解除。

## 依赖与实际执行

候选组件固定`espressif/esp_audio_codec=2.5.0`。只读取固定来源的API头文件并核Git blob，未下载编解码二进制、安装依赖、运行构建／测试／类型检查或启动服务。本轮只执行源码准备、文件摘要／差异检查和矩阵记录同步。

独立工程准备命令（不安装）：

```powershell
python -B tools/prepare_opus_component_build.py --target esp32c3 --destination I:/xy_board_builds_20261109/native-opus-source-20261113-c3
python -B tools/prepare_opus_component_build.py --target esp32c6 --destination I:/xy_board_builds_20261109/native-opus-source-20261113-c6
```

工程是空app_main加不执行的API链接保留根，只用于后续真实组件编译／链接；不是板型固件，也不证明算法、音频、分区或OTA。在现有ESP-IDF5.5.6环境中，下一条命令可能联网下载组件，必须由牛牛执行：

```powershell
python "$env:IDF_PATH/tools/idf.py" -C I:/xy_board_builds_20261109/native-opus-source-20261113-c3 -DIDF_TARGET=esp32c3 reconfigure build
```

C6工程对应替换路径及`IDF_TARGET=esp32c6`。上述安装／构建命令尚未执行。安装边界来自牛牛的AGENTS规则“由牛牛执行安装”。

## 尚未完成

关键条件是实际ESP编码器lookahead。2.5.0公开封装没有对应查询API；候选Initialize要求调用方明确给出已核实的16kHz样本延迟，没有默认值，不能猜0或照搬PC值。本轮API源码并不能解决这个运行参数。

之后还需完成控制协议／接收分片／压缩发送播放队列／五项PCM缓存的真正替换、无PSRAM AFE／AEC和任务栈分配、逐板音频显示接线及完整分区，再进行目标编译与实机验收。当前仍152官方源码入口、36项无入口、4项已知功能缺口，114当前缓存构建通过／1历史过期／73未构建，188项实机not_run。本次决策解除“是否接受Opus”的阻碍，没有解除这些工程或硬件条件。


## 源码记录结果

实际执行两条准备命令，各复制9份候选文件，复制后摘要匹配。record_source.py及离线矩阵同步退出0：22项仅追加Opus人工决策／源码进展，1314份既有固件／配置／构建文件摘要不变；源码与两个隔离工程的当前摘要记录在evidence/native-opus-source-20261113/source-record.json。git diff --check退出0；12份新增源码／候选配置／文档逐项git diff --no-index --check无空白错误，新增文件差异返回1属正常结果。没有执行编码、解码、算法、声学、构建、测试或类型检查。
