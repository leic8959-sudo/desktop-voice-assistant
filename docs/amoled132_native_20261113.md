# 微雪 1.32 历史 SH8601 版接入（2026-11-13）

本批推进已有 `waveshare-amoled-1.32` 入口，独立应用身份保持 `assistant_amoled132`。它对应历史SH8601版，不能把后来同名CO5300 BSP当作同一硬件。接入单ES8311、CST820和原8MB布局；2.06独立重建检查共享SH8601／codec改动。最终构建结果见文末，实机状态分开记录。

## 固定来源与选择

厂商BSP固定 `22733e088a533378680a03e398279b2f7ab09a73`，SH8601适配／初始化表沿[此前协议候选](sh8601_protocol_candidate_20261113.md)，其47份来源重新核对Git blob与SHA256。1.32的11条表逐项复用，唯一差异为0x51初始FF改00；原466×466、X偏移6、时序和命令顺序保留。

历史manifest依赖 `kodediy/esp_lcd_touch_cst820 ^1.0.0`。本轮读取并固定该仓库 `340906c82fdee8f218c39bc8f59084f356e515db` 的六份参考，manifest为1.0.1，属于该范围，但没有安装、伪造为原版1.0.0或复制完整组件。源码／头／许可均按Git blob及SHA256归档。codec直接依赖改为现有1.5.11，沿项目既有checked接口，不把本机1.5.11当成历史1.3.1的原始构建。

## 板级路径

| 模块 | 本板配置 |
| --- | --- |
| 显示 | SH8601、466×466圆屏，RGB565、X偏移6、无换轴／镜像；QSPI2 mode0／40MHz，CS10／CLK11／D0..3=12/13/14/15、LCD RST8 |
| 面板协议 | 已审阅的02参数／32颜色32位桥接；硬件低10ms／高后150ms复位；独立11命令表，仅启动亮度FF→00 |
| 显示任务 | LVGL core1／优先级2，双20行内部DMA共37,280字节，单次最大18,640字节；既有466圆形安全布局和两像素刷新边界 |
| 触摸 | CST820地址0x15，RST7低200ms／高200ms，IRQ6只配置输入并轮询；I²C每次100ms，读0x02起5字节原始count／12位坐标 |
| 音频 | 一份ES8311 BOTH／checked IN_OUT设备，24kHz mono；I²C1 SDA47／SCL48，I²S0 MCLK38／BCLK39／WS41／DOUT42／DIN40 |
| PA | GPIO46先低、INPUT_OUTPUT读回；codec PA=NC，实际DAC解除静音确认后再启用外部PA；没有额外ES7210 |
| 电源／输入 | 原GPIO18电池保持、GPIO17电源键及忽略初始按住的任务不变；BOOT0说话与卡片操作沿既有入口 |
| 存储 | 厂商声明无SD，本地不编译SD挂载入口，不借用2.06的CLK2／CMD1／D0=3 |
| 容量／语音 | 8MB Flash、Octal PSRAM配置；自定义“你好，小燕子”、AFE／AEC、自然插话调度、媒体、配网绑定、HTTP／WebSocket和双OTA保留 |

ES8311输入与输出是同一设备句柄，释放／关闭路径只拥有一次，格式、增益与实际数字静音继续核对现有底层callback。没有为单codec启用C5固定唤醒或SR前端，S3仍走原自定义模型／前端；也没有因8MB而降成纯按键。

板上没有AXP2101路径，本轮不加入PMIC写入。GPIO18／17既有供电／电源键实现原文保留，没有新加自动睡眠或后台断电。实际锁存、PWR按键极性、PA及电源波形仍需实物验收。

## CST820 的边界

固定参考从0x02读取5字节，得到count及单组12位坐标；它把count截断后为多个点重复填充同一组坐标。本地保留完整原始count，只有count0视为释放、count1视为单点，多点／I²C错误／越界取消当前按压并等合法释放。初始先等释放，避免启动时已按住的触点落成点击。

当前CST820参考没有明确解释高位event语义，本地不套用CST816S或FT5x06的event位判断。没有导入全局可变坐标offset，映射按固定BSP的无换轴／无镜像原始坐标处理。400kHz来自本板BSP对默认100kHz宏的实际速度覆盖，可靠性仍需测量。

初始化读取0xA7并记录ID，与参考相同；没有可用的芯片白名单，读取成功不能当身份认证。GPIO低／高各200ms沿参考，接口初始化错误停止启动；轮询故障只记录首次及恢复，不反复刷同类错误日志。实际控制器修订、多点报文、坐标、错误取消和按键手感均未执行硬件验收。

## 分区与改动范围

8MB分区沿用既有原文：NVS 0x9000／0x4000，OTA数据0xD000，app0=0x20000、app1=0x2C0000，各0x2A0000（2,752,512字节）；storage0x560000／0x60000，model0x5C0000／0x240000（2,359,296字节），最高结束0x800000。模型与双应用是否有余量，以本次实际镜像和分区二进制检查为准，不能从CSV合法或其它16MB板构建推断。

新增4份main文件：1.32面板实现、1.32初始化表、CST820 Apache许可／通知。修改8份已有main：共享AMOLED板头／实现、board_profile、CMake、main启动错误处理、audio_io、共享codec和SH8601通知。manifest移除未安装BSP，改用已有codec1.5.11；registry只改本板BSP、QSPI补丁选择及说明，应用身份／容量／分区不变。导出通知条件扩展，未执行Docker或导出。

控制协议人工决定、唤醒后端、绑定／NVS／凭据、OTA校验／分区、存储、原电源代码、SH8601适配、CO5300表和CST9217解析器均按本批前摘要保持原值。历史厂商Git提交仍作为硬件／表的来源证据；没有因移除外部BSP依赖而改用后期CO5300。

## 实际命令和未完成项

本板直接依赖预检退出0，未创建预检目录。新stage `I:/xy_board_builds_20261109/amoled132_native_20261113`，2.06复用自身隔离stage同步当前源码。使用现有SDK5.5.6-dirty、Xtensa14.2.0、17份本地override及offline_guard，各2个编译任务，没有安装SDK／组件。

```text
python -B evidence/amoled132-native-20261113/132/build_cached.py
python -B evidence/amoled132-native-20261113/206/build_cached.py
python -B evidence/amoled132-native-20261113/review_sources.py
python -B evidence/amoled132-native-20261113/132/collect_build.py
python -B evidence/amoled132-native-20261113/206/collect_build.py
python -B evidence/amoled132-native-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/amoled132-native-20261113/finalize.py
```

1.32真实目标为 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-amoled-1.32 build`；2.06执行自身profile的 `reconfigure build`。产物检查分别核对ELF／镜像身份、校验、分区二进制、实际选中对象、工作区／stage及产物摘要。没有运行测试套件、模型推理、类型检查或实际外设／网络请求。

只有现有微雪1.8实物。1.32的PCB／历史SH8601修订、8MB Flash／PSRAM、屏幕gap／颜色／复位／40MHz、CST820实际身份和触摸、GPIO18／17／PA46、24kHz时钟／增益／声音、唤醒／AEC／自然插话、媒体、配网绑定／提醒、HTTPS OTA／回滚，以及内部DMA／CPU／堆／栈峰值均未实机验收。未接入电池／充电遥测、其它传感器、原厂MCP。未写Git、刷机、OTA、上传或发布，完整188变体目标继续进行。

## 实际结果

1.32首次真实8MB目标构建一次退出0，2.06独立profile的共享回归退出0。两板各八项readelf／nm／size、esptool镜像检查、分区二进制解析和SDK只读查询均退出0。

| 目标 | 应用字节／实际槽余量 | 模型字节／模型区余量 | 当前输入／产物 |
| --- | ---: | ---: | ---: |
| 1.32首次8MB | 2,535,808／216,704 | 2,203,819／155,477 | 328／28 |
| 2.06回归 | 2,614,784／530,944 | 2,203,819／1,990,485 | 328／28 |

1.32镜像头与实际sdkconfig均为8MB Flash、Octal PSRAM，应用身份为assistant_amoled132。分区二进制确认两个0x2A0000应用槽、0x240000模型区、原NVS和OTA数据位置，总结束0x800000；两个镜像均未超实际槽。这个余量只适用于本次源码／模型，不能保证以后增加功能仍装得下。

1.32 ELF包含本地SH8601适配、真实ES8311、PA和原协议／wake／AEC；没有ES7210、SD挂载、外部CST820工厂、FT5x06或CST9217路径。触摸直接读取的本地代码已编译，不能因外部工厂不存在就推断触摸被删除。2.06仍包含双codec／FT5x06／SD，应用及模型尺寸与上一批一致。两份软件播放参考均保持20,496字节。

两份stage与各328项当前输入摘要匹配，56份产物摘要核对。SDK四个既有强制UND名称 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores` 原样记录；SDK5.5.6-dirty和local override不等于干净发行或已启动，既有LVGL／BSP配置警告保留在日志。

直接依赖报告实际退出0：923条声明、115本地／114官方入口满足直接条件；不是所有传递依赖、构建或硬件完成。CST820参考清单的API链接格式曾漏写 `/repos/`，收尾已按实际gh读取的固定blob地址修正并核对来源格式、blob与内容摘要；未因此修改固件或重新解释资料。最终来源／保护文件、registry范围、当前输入／产物核对及矩阵离线同步均退出0。`git diff --check`退出0，改动／新增main整文件 `--no-index --check`返回1且无空白诊断，第三方原始LICENSE保持原文。

全量仍为154目录／188变体、152个官方源码入口、153本地profile、36项无源码、4项明确必需功能缺口。累计 **114个官方变体曾缓存构建通过**；当前 **2 passed_cached_sources／112 stale_cached_sources／74 not_run**。188项本轮实机全部not_run，尚未完成全量适配目标。
