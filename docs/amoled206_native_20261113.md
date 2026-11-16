# 微雪 AMOLED 2.06 首次整板接入（2026-11-13）

本批将此前SH8601候选接入 `waveshare-amoled-2.06`，应用身份保持 `assistant_amoled206`。范围包括实际QSPI、电源、触摸、双codec、SD和既有语音／网络／双OTA；2.16独立重建以核对共享层。1.8 SH8601和1.32仍是不同板型，不能继承本结果。

固定来源沿[SH8601候选记录](sh8601_protocol_candidate_20261113.md)：47份厂商原文逐份核对Git blob和SHA256；2.06 BSP2.0.0及SH8601驱动固定 `9daffb7168b3c093a330da700988a22092294eef`。适配源／头与此前已构建候选内容一致，仅来源通知路径改变；11条2.06命令也与候选逐项一致。唯一表内差异是最后0x51初始FF改00，其余字节、长度、顺序和延时保留。没有下载／安装SH8601组件、SDK或BSP。

## 本板接入

| 模块 | 源码配置 |
| --- | --- |
| 显示 | 410×502 RGB565/RGB，X偏移22；SPI2 mode0／40MHz，CS12／CLK11／D0..3=4/5/6/7，32位QSPI命令及02参数／32像素封装 |
| 复位 | LCD GPIO8低有效，低10ms／高后150ms；触摸GPIO9低有效，由现有FT5x06工厂初始化；IRQ38配置为输入，无应用中断callback |
| 刷新 | LVGL core1、优先级2，两份20行内部DMA条带共32,800字节，最大单次SPI16,400字节；原两像素X/Y刷新边界，无换轴／镜像 |
| 触摸 | FT5x06 1.1.1、0x38，有限100ms每次I/O；初始化后读0x02起7字节原始count／event／坐标，多点／非法事件／越界／错误取消按压并等释放，初始先等释放 |
| 电源 | I²C1 SDA15／SCL14；沿既有AXP2101 ALDO1／2设3.3V、按位使能及读回，不改变充电、其它电源轨或按键配置 |
| 音频 | ES7210 MIC1/MIC2标准双槽并选第0槽、ES8311输出，24kHz；MCLK16／BCLK41／WS45／DIN42／DOUT40，I²S0；PA46由checked实际DAC静音结果后开启 |
| 存储／输入 | SDMMC单比特CLK2／CMD1／D0=3，禁止失败格式化；BOOT0及现有410×502 UI／卡片路径 |
| 语音／协议 | 自定义“你好，小燕子”、AFE／软件AEC、播放参考、自然插话调度、媒体、Wi-Fi配网绑定、HTTP／WebSocket及HTTPS双OTA保留 |
| 容量 | 32MB Flash／Octal PSRAM配置；既有NVS、双3MiB应用和4MiB模型仍在前16MB，不移动凭据区或自动利用剩余空间 |

SH8601适配复用SDK DCS绘图／窗口方法，通过借用IO桥接成实际32位QSPI；SDK ST7789初始化被替换，不发送它的RAMCTRL，也不使用CO5300工厂。Y镜像和XY交换为true仍明确不支持，false作为幂等初始化允许LVGL注册，不能据此宣称已实现换轴。所有像素／窗口边界及颜色传输错误沿候选检查，亮度0..100直接传入实际0x51命令。

适配保留panel的user_data，借用静态命令和raw IO；删除前必须停止／join调用者并排空传输。本板初始化失败暗屏／关闭PA并停止，保留回调可能引用的部分对象、要求重启，没有实现运行时销毁重建。SDK总线获取／排队／回收仍可能无限等待；I²C时限和几何上限不是整个外设生命周期有限时长证明。

共享AMOLED层从单一size拆为width／height，2.06走410×502；既有1.75仍466×466，2.16仍480×480。2.06使用FT路径，既有CST9217解析器和CO5300命令表没有变化；2.16原有MV／MY方向继续单独配置。文件仍保留 `amoled216_*` 历史名称，但所选硬件身份由profile／宏／镜像确定。

## 改动及实际命令

新增6份main文件：SH8601适配源／头、2.06命令表、2.06面板接线实现，以及Apache许可／通知。修改8份已有main：共享AMOLED板头／实现、原面板的宽度引用、board_profile、CMake、audio_io、checked codec和main失败处理。manifest改用现有FT5x06 1.1.1，registry仅本板BSP选择／说明改变；导出工具只增加第三方通知目录，未执行Docker或导出。

控制协议人工决定、唤醒后端、绑定／凭据、NVS、OTA校验／分区、存储、board_power、原CO5300命令及CST9217解析器按本批前摘要保留。音频是现有STD2／24kHz路径，没有改成按键替代唤醒，也没有取消AEC或自然插话。

直接依赖预检实际退出0且无预检目录。新stage为 `I:/xy_board_builds_20261109/amoled206_native_20261113`；2.16在自身stage同步当前源码。现有SDK5.5.6-dirty、Xtensa14.2.0、17个本地override、offline_guard和各2个编译任务。

```text
python -B evidence/amoled206-native-20261113/206/build_cached.py
python -B evidence/amoled206-native-20261113/216/build_cached.py
python -B evidence/amoled206-native-20261113/review_sources.py
python -B evidence/amoled206-native-20261113/206/collect_build.py
python -B evidence/amoled206-native-20261113/216/collect_build.py
python -B evidence/amoled206-native-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/amoled206-native-20261113/finalize.py
```

2.06真实目标命令为 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-amoled-2.06 build`；2.16另执行自己的profile `reconfigure build`。每板分别核对ELF、镜像校验／身份、分区二进制、编译选择器和当前源码／stage摘要；结果见文末和本批final-audit。

## 尚未验收

只有现有微雪1.8实物。2.06实际PCB、32MB Flash和PSRAM、SH8601／FT修订、复位、40MHz可靠性、颜色／X偏移22／方向／触点坐标和取消、PA／实际电源轨、24kHz时钟／MIC增益与槽、AFE／AEC／唤醒／自然插话、媒体、配网绑定／提醒、SD、HTTPS OTA／回滚及DMA／堆／栈峰值均未实机验收。寄存器读回不代表实体电压，静态符号不代表函数已运行。

未接入电池／充电遥测、其它传感器、睡眠、关机或原厂MCP。未运行测试套件、类型检查、模型推理、长期服务；未安装、写Git、刷机、OTA、上传或发布。全量188变体目标仍未完成。

## 实际结果

2.06首次完整构建一次退出0，2.16的独立profile重建退出0；每板八项readelf／nm／size、esptool镜像校验、分区解析和SDK只读查询均退出0。

| 目标 | 应用字节／3MiB余量 | 模型字节 | 当前输入／产物 |
| --- | ---: | ---: | ---: |
| 2.06首次 | 2,614,784／530,944 | 2,203,819 | 324／28 |
| 2.16回归 | 2,610,992／534,736 | 2,203,819 | 324／28 |

2.06 ELF含本地SH8601适配、实际SDK DCS工厂、FT5x06、ES7210／ES8311、PA、SD、custom wake／AEC、控制协议、HTTP／WebSocket和播放入口；不含CO5300或CST9217工厂／轮询路径。实际编译命令选中AMOLED_206，镜像身份assistant_amoled206、Flash头32MB；2.16继续assistant_amoled216和16MB／CO5300／CST9217。初始GPIO复位、QSPI字节流、屏幕与触点位置没有在硬件执行。

两板实际配置保留Octal PSRAM、mn5q8默认模型、OTA回滚及20,496字节软件播放参考；分区二进制保留原NVS、双3MiB应用和4MiB模型，应用／模型均在实际槽内。2.06虽配置32MB，分区仍限前16MB。stage与每份324项当前工作区输入摘要一致，56份产物摘要已核对。

两份ELF均保留SDK既有四个强制UND名称：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。SDK5.5.6-dirty与local override构建不是干净发行／启动验收；LVGL及既有BSP配置等警告继续留在日志，未扩大本轮配置清理。

直接依赖报告实际退出0：924条声明、114本地／113官方入口满足直接条件；这不等于所有板型传递依赖或构建完成。最终来源／保护文件、registry范围、当前输入与产物审计以及矩阵离线同步均退出0。`git diff --check`退出0，改动／新增main的整文件 `--no-index --check`返回1且无空白错误，原始LICENSE保持原文。

全量仍为154目录／188变体、152个官方源码入口、153本地profile、36项无源码、4项明确必需功能缺口。累计 **113个官方变体曾缓存构建通过**；当前 **2 passed_cached_sources／111 stale_cached_sources／75 not_run**。188项本轮实机全not_run，1.8 SH8601和1.32整板仍未构建，不能从本板继承通过状态。
