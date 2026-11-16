# 微雪 1.75／1.75C 整板接入（2026-11-13）

本批推进最初选定的微雪1.75，并按独立修订接入1.75C。两板复用现有本地CO5300、LVGL port、CST9217轮询及双codec实现，分别构建；2.16另做共享驱动回归。源码入口数量不增加，编译与实机状态分别记录，最终命令结果见文末。

## 来源与逐板差异

小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，其1.75目录同时提供非C／C的配置和板类。四份上游原文、两套厂商BSP共24份文件全部保存并核对Git blob及SHA256：

- 非C BSP3.0.1：`bfeb6e6d5737178cdde78b630c8118074da0a657`。
- C BSP3.0.0：`010c5fb39ea7f0e996efd1f8e162cc70e569e57f`。

两个固定提交中的CST9217六份文件与2.16前批参考的Git blob逐项一致。这个证据支持复用同一固定协议实现，不证明商品名相同的所有触控修订可用。本批不覆盖SH8601面板或未经核对的CST9220协议。

| 项目 | 1.75 非C | 1.75C | 2.16回归 |
| --- | --- | --- | --- |
| 应用身份 | assistant_amoled175 | assistant_amoled175c | assistant_amoled216 |
| 画布 | 466×466圆形布局 | 466×466圆形布局 | 480×480矩形布局 |
| 配置Flash | 16MB | 32MB | 16MB |
| 音频MCLK | GPIO42 | GPIO16 | GPIO42 |
| LCD复位／触摸复位 | GPIO39／40 | GPIO1／2 | GPIO39／40 |
| SD缓存 | 单比特CLK2／CMD1／D0=3 | 厂商声明无SD，不编译挂载入口 | 单比特CLK2／CMD1／D0=3 |
| 初始化表 | 14命令 | 与非C同14命令 | 保留自身15命令及MADCTL A0 |
| 列偏移 | 6 | 6 | 0 |
| 触摸方向 | 不换轴、镜像X与Y | 不换轴、镜像X与Y | 先镜像Y，再交换XY |

Flash／PSRAM仍是固定来源及工程配置，未读取本板实物。C型32MB配置不表示本次使用全部空间；既有分区仍位于前16MB。两款独立应用名和产物目录继续保留，不能互刷。

## 共享实现与保留行为

没有另写一套CO5300／触摸／codec驱动。共享文件保留先前的 `amoled216_*` 名称，由编译选择器确定实际变体；该历史文件名不代表当前固件身份。

1.75与C型都使用SPI2 mode0／40MHz，CS12／CLK38／D0..3=4/5/6/7。14条表的字节、长度、顺序和延时与两套BSP及固定小智表逐项一致，唯一差异是0x51初始亮度FF改00。列窗口6..471、行窗口0..465，`set_gap(6,0)`保留；0x2B和0x11之后各600ms。没有继承2.16的MADCTL A0，也没有为适配尺寸修改其它厂商寄存器。

LVGL core1／优先级2，两份20行内部DMA条带共37,280字节；单次SPI上限18,640字节，两像素X/Y刷新边界沿固定原板。复用现有466圆屏安全布局、字体和卡片，不把屏幕外的圆角区域当可用按钮。亮度与显示初始化错误传播，失败保持暗屏／PA关闭并要求重启，保留回调可能引用的部分资源。

触摸复用已有 `Cst9217Polling`，本批解析器源码未改。0x5A、D000十字节／AB标记、原始count／status检查、有限100ms每次I/O、初始及错误后等待释放均保留；多点／非法状态／越界取消按压。466变体映射 `x=465-raw_x，y=465-raw_y`；2.16继续 `x=479-raw_y，y=raw_x`。信息读取后的额外硬件复位、身份只记录不认证、没有CST9220白名单等前批限制仍有效。

固定小智1.75／C共同类明确启用ALDO1麦克风电源。本地扩展既有AXP2101 ALDO1设压／使能和读回路径，保留其它电源轨和充电配置；不复制上游整组关闭电源轨、充电电流／电压或300秒关机。PA46先低、解除hold、配置INPUT_OUTPUT并读回；codec PA=NC，由实际DAC解除静音确认后开启外部PA。

音频沿现有I²C1 SDA15／SCL14、I²S0 24kHz双codec路径：ES7210 MIC1/MIC2标准双槽并选择第0槽，ES8311输出，BCLK9／WS45／DOUT8／DIN10，MCLK按表选择。软件播放参考、16kHz前端／AEC、自定义“你好，小燕子”、自然插话调度、媒体和原网络协议保留。I²C／I²S控制器编号采用本地已核对共享实现，物理时钟／声学效果仍未实测。

非C SD挂载明确禁止失败格式化；C型编译时关闭SD能力且无挂载符号。固定小智非C类还将TCA9554 P4设为输入，但该输入未用于本项目GPIO0语音或卡片操作，本批未创建这一无消费者外设。电量、充电遥测、睡眠、原厂MCP和其它专用外设没有因本次接入而实现。

## 改动范围

共修改10份已有main文件，未新增main文件：共享board／panel三文件、board_profile、CMake、board_power、main显示失败分支、audio_io及checked codec选择、第三方来源通知。两个manifest改用已有CO5300 2.2.0和LVGL port2.9.0，registry仅两板BSP选择与实现说明变化；产物导出的Apache通知条件扩展到两款1.75。没有执行导出或Docker。

控制协议人工决定、唤醒后端、绑定／NVS、OTA内容／身份／分区保护、存储及现有CST9217解析器均按本批前摘要保持原值。其他151个profile对象未改；现有2.16表和方向仍独立，不能从它的旧构建推断两款新板通过。

## 实际命令与边界

两个预检分别执行 `prepare_cached_firmware_build.py --profile <各板> --destination <新预检目录> --idf-path <现有5.5.6> --check-only`，均退出0，没有创建预检目录。之后各自建立全新stage；2.16复用原stage同步当前源码：

- `I:/xy_board_builds_20261109/amoled175_family_native_20261113`。
- `I:/xy_board_builds_20261109/amoled175c_family_native_20261113`。
- `I:/xy_board_builds_20261109/amoled216_board_native_20261113`。

使用现有SDK5.5.6-dirty、Xtensa14.2.0、17份本地组件override、offline_guard，各2个编译任务，未安装SDK／依赖。

```text
python -B evidence/amoled175-family-20261113/175/build_cached.py
python -B evidence/amoled175-family-20261113/175c/build_cached.py
python -B evidence/amoled175-family-20261113/216/build_cached.py
python -B evidence/amoled175-family-20261113/review_sources.py
python -B evidence/amoled175-family-20261113/175/collect_build.py
python -B evidence/amoled175-family-20261113/175c/collect_build.py
python -B evidence/amoled175-family-20261113/216/collect_build.py
python -B evidence/amoled175-family-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/amoled175-family-20261113/finalize.py
```

实际板级命令为 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<各自profile> build`，2.16另有reconfigure。读取ELF、关键对象、镜像身份／校验、分区二进制、配置与输入摘要，结果见文末和本批 `final-audit.json`。

来源核对脚本第一次因LICENSE仅行尾不同而返回1；随后核对正文完全相同，保留各自原始文件摘要，并按正文比较通过。补充符号检查脚本生成时另有一次Python条件表达式拼写错误，未修改文件；修正后生成成功。这两项均未改变固件源码或构建结果。

只有现有微雪1.8实物。新板PCB／屏控／触控器身份、16／32MB和PSRAM、复位、40MHz显示／色彩／圆形边缘、触摸镜像／释放／错误、ALDO1／PA46／音频时钟和增益、自定义唤醒／AEC／自然插话、媒体、绑定／提醒、SD、HTTPS OTA／回滚及资源峰值仍需实机验收。没有运行测试套件、模型推理、类型检查或长期服务；未写Git、刷机、OTA、上传或发布，全部188变体目标继续进行。

## 最终结果

三份实际目标命令均退出0，readelf／nm／size、esptool镜像校验、分区解析和SDK只读查询各八项均退出0：

| 板型 | 应用字节／3MiB余量 | 模型字节 | 当前输入／产物 |
| --- | ---: | ---: | ---: |
| 1.75 | 2,611,280／534,448 | 2,203,819 | 307／28 |
| 1.75C | 2,541,840／603,888 | 2,203,819 | 307／28 |
| 2.16回归 | 2,610,992／534,736 | 2,203,819 | 307／28 |

两个新ELF的 `kCommands` 实际符号大小均为224字节，即14条16字节结构；2.16为240字节／15条。1.75C的ELF没有SD挂载符号，非C与2.16有；镜像头及sdkconfig分别为16／32／16MB。关键符号包含共享CO5300、CST9217轮询、ES7210／ES8311、custom wake／AEC、HTTP／WS和播放入口，不含外部adapter或CST9217／CST9220工厂。每份stage与归档的307项当前输入摘要匹配，84份产物摘要全部核对。

实际sdkconfig保留Octal PSRAM、mn5q8默认模型、回滚选项；分区二进制保留NVS、双3MiB app和4MiB model，总范围不超前16MB。32MB C型后16MB没有新增用途，未移动凭据区。20,496字节软件播放参考仍存在。

三份ELF均如实记录既有SDK四个强制UND名称：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`，未改SDK、不称零UND或已启动。LVGL及既有BSP配置等警告保留在构建日志，构建通过不消除这些发行与运行限制。

直接依赖报告命令实际退出0：927条声明，**109本地／108官方入口满足直接条件**；新两板通过现有local override实际解析／编译，仍不等于干净安装或规范发行依赖锁。最终来源／范围审计与矩阵离线同步退出0，`git diff --check`退出0；10份改动main文件另作整文件`--no-index --check`，返回1且无空白诊断。

范围仍为154目录／188变体、152个官方源码入口、153本地profile、36无源码入口、4个明确必需功能缺口。累计 **108个官方变体曾缓存构建通过**；当前账本 **3 passed_cached_sources／105 stale_cached_sources／80 not_run**，188项本轮实机均not_run。目标尚未完成，不能把源码入口、历史构建或本次三板通过等同全部适配。
