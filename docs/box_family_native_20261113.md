# 旧 BOX／BOX-Lite 本地接入（2026-11-13）

本批推进已有 `esp32-s3-box` 和 `esp32-s3-box-lite` 源码入口，独立应用身份保持 `assistant_box`／`assistant_boxlite`。两板按固定小智v2.5.2显示参数和各自codec／引脚接入共享实现；另将BOX3与AMOLED标准双槽RX的打开参数明确为两通道，分别重建BOX3、2.16。构建和实机状态分开，结果见文末。

## 来源与选择

Espressif旧BSP固定 `342492186688ddb9469387528912d5312ab075d4`，BOX3.1.0~2、BOX-Lite2.1.0~2共27份文本；小智v2.5.2固定 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，两板8份源码。35份原文都核对Git blob和SHA256。当前codec1.5.11的I²S数据接口、ES7243E、ES8156另存原文／摘要；没有安装或把旧codec1.1.0库混入本次工程。

旧BSP使用ST7789工厂，Lite还设置swapXY=true；固定小智两板则使用相同的ILI9341自定义表、swapXY=false。本次选择任务固定小智板类的实际显示调用，明确保留这项来源差异，不把两套API当实体屏控互换证明。其11条参数行与项目既有BOX3表逐项相同，复用同一份表；原尾部0x80／0xff长度标记沿前批修正为无参数0x11／0x29和不发送结束标记。

两板板类虽然初始传RGB，表中MADCTL最终写08。外部ILI9341驱动会更新其缓存；本地使用BGR初始缓存08配合SDK DCS操作，使后续mirror产生与表一致的位值，避免只发08而SDK仍按00缓存覆盖。这个源码取舍不是颜色、方向或屏幕修订验收。

## 分别保留的差异

| 项目 | 旧BOX | BOX-Lite | BOX3对照 |
| --- | --- | --- | --- |
| I²S WS／背光 | GPIO47／45 | GPIO47／45 | GPIO45／47 |
| LCD RST48 | 低有效，10ms低／高后10ms | 同左 | 高有效，10ms高／低后10ms |
| 面板方向 | 不换轴、mirrorXY | 不换轴、mirrorY、反色 | 原有TT探测分支与mirrorXY |
| 背光 | 高有效PWM | 低有效PWM，初始高／duty1023保持暗屏 | 高有效PWM |
| 声音 | ES7210输入／ES8311输出，原生24kHz | ES7243E输入／ES8156输出，原生16kHz | 原ES7210／ES8311，24kHz |
| 触摸 | TT21100地址0x24 | 无触摸，不创建指针／I²C触摸句柄 | GT911双地址→TT21100探测顺序保留 |
| SD | 固定BSP声明无SD | 同左 | 原底座四位SD路径保留 |
| 交互 | GPIO0按住说话，卡片触摸 | GPIO0原按住说话；卡片短按换项／长按松手确认 | 原行为 |

共同显示为320×240 RGB565，SPI3 mode0／40MHz，MOSI6／CLK7／CS5／DC4、RST48。LVGL core1／优先级2，双20行内部DMA共25,600字节，单次SPI上限12,800字节；镜像在panel与LVGL配置中一致声明，避免注册时再被默认方向覆盖。两款旧板没有SD挂载符号，也没有为凑齐能力调用不存在的SD接口。

共同I²C1 SDA8／SCL18，I²S0 MCLK2／BCLK17／DOUT15／DIN16，PA46高有效。PA先低、配置INPUT_OUTPUT并读回，codec PA=NC，实际DAC解除静音确认后才允许外部PA开启。旧BOX选择ES7210 MIC1/MIC2标准双槽、第0槽；Lite选择ES7243E标准双槽第0槽，增益核对其真实 `set_mic_gain` 回调，不误用ES7210通道增益接口。

Lite采用固定小智原生16kHz输入／输出，MCLK倍率256；现有网络PCM24k转换和软件参考路径复用。早期文档的24kHz技术候选因此被取代，原文保留供追溯；不是改变网络协议，也没有关闭AFE／AEC、custom wake“你好，小燕子”、自然插话调度或媒体。16kHz时钟、ADC数据、DAC声音和声学仍需本板测量。

## 标准双槽打开参数修正

共享后端为BOX系列及两款1.75／2.16创建标准stereo16 RX，但此前部分选择器在打开麦克风时传入channel4。最初检查时我把它说成“当前接口会拒绝四通道”，这不准确：读完整 `audio_codec_data_i2s.c` 后确认，它会按channel4折算成更宽的STD槽和数据位宽。

本批让这些已确认采用STD2的分支明确使用channel2，与其初始化约定一致；TDM4板型仍保留原选择器。现有SenseCAP原本就是2，CoreS3也仍走其原STD2默认值。本次没有运行音频或复现启动故障，不能把源码参数约定修正描述为实机故障已经修复。BOX3、2.16分别完成实际重建和取证，其余1.75旧镜像继续按输入变化标为stale。

## 改动与验证范围

复用既有box3_board及checked codec，未新增main文件，修改8份已有main：box头／实现、board_profile、CMake、main启动错误处理、audio_io、shared codec和第三方来源通知。两板manifest移除旧BSP、将直接codec改为现有1.5.11；Lite配置显式开启ES7243E／ES8156。registry只改两个BSP选择及说明，保留其他profile、应用身份、16MB容量和分区。产物导出通知条件扩展，未执行导出／Docker。

控制协议人工决定、唤醒后端、绑定／凭据、NVS、OTA校验／双分区、存储、原卡片／按键UI、TT21100和GT911解析器保持本批前原文。TT21100的report-ID、tip／FIFO、空报告及硬件身份限制继续有效，没有把它冒充已实测协议。

两个直接依赖预检均退出0，无预检目录。新stage分别为 `I:/xy_board_builds_20261109/box_family_box_native_20261113` 与 `.../box_family_lite_native_20261113`，BOX3／2.16各复用自己的stage同步当前输入。使用已有SDK5.5.6-dirty、Xtensa14.2.0、17份local override、offline_guard，每stage2个编译任务，未安装SDK或依赖。

```text
python -B evidence/box-family-native-20261113/box/build_cached.py
python -B evidence/box-family-native-20261113/lite/build_cached.py
python -B evidence/box-family-native-20261113/box3/build_cached.py
python -B evidence/box-family-native-20261113/216/build_cached.py
python -B evidence/box-family-native-20261113/review_sources.py
python -B evidence/box-family-native-20261113/box/collect_build.py
python -B evidence/box-family-native-20261113/lite/collect_build.py
python -B evidence/box-family-native-20261113/box3/collect_build.py
python -B evidence/box-family-native-20261113/216/collect_build.py
python -B evidence/box-family-native-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/box-family-native-20261113/finalize.py
```

实际完整目标为每个profile自己的 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<板型> build`；两项旧目标另有reconfigure。命令数组、cwd、退出码、日志和SHA256均在本批记录中。直接依赖报告是条件结果，不能替代构建或干净发行锁。

只有现有微雪1.8实物。旧BOX／Lite实际PCB、Flash／PSRAM、ILI9341路径与旧BSP ST7789路径的硬件适用性、低有效复位、Lite背光极性／颜色／方向、TT触摸／按键卡片、原生16／24kHz时钟、MIC／PA／静音／增益、唤醒／AEC／自然插话、媒体、绑定提醒、HTTPS OTA／回滚和内存／CPU峰值均未实机验收。辅助ADC键、MUTE／home应用回调、IMU、省电、断电与原厂MCP未移植。

未执行测试套件、类型检查、模型推理、长期服务、安装、Git写操作、刷机、OTA、上传或发布，完整188变体目标继续进行。

## 最终结果

两款旧板第一次完整构建均退出0，BOX3、2.16的实际profile分别 `reconfigure build` 退出0。四项各执行readelf／nm／size、esptool镜像校验、分区解析和SDK只读查询共八条命令，全部退出0。

| 目标 | 应用字节／3MiB余量 | 模型字节 | 当前输入／产物 |
| --- | ---: | ---: | ---: |
| 旧BOX首次 | 2,544,592／601,136 | 2,203,819 | 318／27 |
| BOX-Lite首次 | 2,536,800／608,928 | 2,203,819 | 318／26 |
| BOX3回归 | 2,617,328／528,400 | 2,203,819 | 318／28 |
| 2.16回归 | 2,610,992／534,736 | 2,203,819 | 318／28 |

BOX ELF含TT21100、本地PA、ES7210／ES8311及完整协议／唤醒／AEC路径，没有GT911或SD挂载；Lite含ES7243E／ES8156，确认没有ES7210／ES8311、TT21100／GT911或SD挂载。实际编译命令分别选中BOX／BOXLITE宏，Lite启用真实codec配置；不是把BOX3镜像改名给旧板。运行中的原生采样时钟、颜色与声音仍未测量。

四份镜像身份、16MB配置、Octal PSRAM、mn5q8默认模型、NVS、双3MiB OTA和4MiB model分区二进制核对；应用和模型均在实际分区内。20,496字节软件播放参考仍在。各stage与318项当前输入摘要匹配，109份产物摘要全部核对。

四份ELF如实保留SDK既有强制UND名称 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`；SDK5.5.6-dirty和local override仍不是干净发行或启动验收。LVGL与既有BSP配置等警告保留在日志。

直接依赖报告退出0：924条声明、113本地／112官方满足直接条件；这不等于全部板型解析／构建通过。最终来源、保护文件、registry范围、当前输入／产物核对及矩阵离线同步均退出0。`git diff --check`退出0，8份改动main整文件 `--no-index --check`返回1且无空白诊断。

总范围仍为154目录／188变体，152个官方源码入口、153本地profile、36项无源码、4项明确必需功能缺口。累计 **112个官方变体曾缓存构建通过**；当前 **4 passed_cached_sources／108 stale_cached_sources／76 not_run**。188项本轮实机全部not_run，尚未完成全量适配目标。
