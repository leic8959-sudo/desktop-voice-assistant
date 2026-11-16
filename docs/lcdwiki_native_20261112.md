# LCDWiki ES3C35P 的 ST77922 接入与整板构建

本批推进已有 `lcdwiki-es3c35p` 源码入口。此前ST77922 2.0.2组件缺失，新目标未编译；本次新增固定320×480／RGB565缓冲／QSPI面板流程，保留本板原63条命令、CMD1/2/3分页、方向与触摸逻辑，再构建真实目标。CAM1.83独立回归只检查现有SPI／CMake选择，不向LCDWiki继承硬件结果。

## 来源与实现

固定小智提交仍为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，既有11份板级／驱动／LVGL参考本轮重新核对SHA256。ST77922 general驱动来自 `espressif/esp-iot-solution@91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f`；本轮通过只读gh api重新读取源文件与许可，按Git blob核验，general源码与旧归档逐字一致。完整命令、blob、SHA256见 `evidence/lcdwiki-native-20261112/verified-reference.json`。没有安装或下载组件包、修改SDK。

新增 `main/lcdwiki_panel.cpp/.h`，保留原Espressif版权／Apache-2.0全文；原板级63条表仍保留小智MIT通知。`lcdwiki_display.cpp`只替换面板创建，不改触摸读数、取消语义、背光、QSPI IO、LVGL注册和软件旋转；CMake及本板manifest移除被当前固定路径替代的外部依赖，registry只更新实现说明。导出脚本补充两份既有来源许可，未执行Docker或实际导出。

| 项目 | 保留的行为 |
| --- | --- |
| 总线 | QSPI2 mode0／80MHz，CS10／CLK12／D0..3=11／13／14／9；32位命令／8位参数 |
| 复位 | ESP LCD reset为NC，发送 `0x02<<24 \| 0x01<<8` 后等待120ms，无GPIO复位 |
| 前导 | MADCTL初始08、COLMOD55，再发送原63条全部字节、长度、顺序和延时，包括零tick让出 |
| 分页 | F1／F2有参数时进入CMD2／CMD3，F0有参数时返回CMD1；仅CMD1下的MADCTL更新方向缓存 |
| 实际覆盖 | 原表CMD1最后写COLMOD01、MADCTL00；后续mirror(false,false)保持00，没有重新加回BGR位 |
| 绘图 | 原16位缓冲仍每像素2字节，02参数／32+2C像素，2A／2B末坐标减一、gap0、320×480边界 |
| 旋转 | 与原general驱动一样，swap_xy即使false也返回不支持；原port软件旋转flag继续避开该硬件调用，当前rotation0固定竖屏 |
| 刷新 | 原四像素X对齐、双20行内部DMA条带、额外软件旋转条带、RGB565字节交换和同panel IO传输完成通知保持 |

63条表按实际发送长度核对，包括零长度命令的占位字节不发送；原表摘要未改。分页审阅确认最终MADCTL00／COLMOD01，不能只看BGR构造前导就宣称实际为BGR，也不能把01擅自改成55。新面板不支持任意gap、18／24位、普通SPI、MIPI、通用默认表或动态硬件旋转。只覆盖既有g_started保护的一次板级启动；没有提供外部任意格式修改／通用重新初始化契约。

## 触摸、音频与未完成范围

触摸仍为I2C0 SDA38／SCL39、地址0x55、16位大端寄存器，RST48低10ms／高100ms，INT47输入轮询；容量0009严格1..5，0010位3、0014起7字节点、位7有效和14位坐标。原先的初始释放、多指／越界／I2C错误取消和等待释放代码原文不变，不借用FT5x06组件或按“兼容FT”注释推断控制器。当前无数据位按松手处理，其真实数据节奏和故障／释放语义仍未实机验收。

本批音频、引脚、完整唤醒、AEC、自然插话／媒体人工决定、UI、配网绑定、main和OTA源码摘要保持。继续使用单ES8311、24kHz mono、MCLK17／BCLK18／WS21／DIN16／DOUT15，PA1低有效，实际DAC解除静音确认后再启用；不是双ES7210板。

只有现有微雪1.8实物。LCDWiki的真实模组／Flash／PSRAM、ST77922修订、80MHz信号、分页／格式01／颜色／反色／部分刷新、触摸坐标／多点／首触／错误恢复、PA极性／静音／增益／时钟、AEC／完整唤醒／自然插话、媒体、绑定／卡片／提醒／同板双OTA仍未实机验收。SD、电池／充电、RGB40、MCP重配网、原厂手势和相机等旧外围缺项保持，终端构建不能证明所有外围完成。

## 实际执行与证据

目录为 `evidence/lcdwiki-native-20261112/`。LCDWiki使用新stage `I:/xy_board_builds_20261109/lcdwiki_native_20261112`，CAM1.83回归同步已有独立stage。现有SDK5.5.6-dirty、Xtensa14.2.0、17份本地override、Python联网保护，各2jobs，未安装依赖。

实际执行：

```text
python -B evidence/lcdwiki-native-20261112/lcdwiki/build_cached.py
python -B evidence/lcdwiki-native-20261112/cam183/build_cached.py
python -B evidence/lcdwiki-native-20261112/review_sources.py
python -B evidence/lcdwiki-native-20261112/cam183/collect_build.py
python -B evidence/lcdwiki-native-20261112/lcdwiki/collect_build.py
```

| 真实IDF目标（均带 `-DIDF_TARGET=esp32s3`） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| `-DASSISTANT_BOARD=lcdwiki-es3c35p build` | 2249952 | 2203819 | 首次退出0，3MiB应用槽剩895776字节 |
| `-DASSISTANT_BOARD=waveshare-esp32-s3-cam-1.83 reconfigure build` | 2250224 | 2203819 | 退出0 |

CAM回归已归档287项当前输入和26份产物；它的CH32／ST7789／双codec等入口、镜像和分区检查通过，不证明LCDWiki的新面板或触摸分支可运行。

只读直接依赖报告已实际执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/lcdwiki-native-20261112/direct-dependencies.json
```

退出0：153本地／152官方、938条直接声明，97本地／96官方满足直接条件，缺组件声明132项。直接条件不代表传递依赖、API、目标构建／启动或实机验收。

LCDWiki实际readelf／nm／size、esptool image_info、分区和SDK Git只读查询均退出0；287项当前输入与stage一致，26份产物快照归档。项目名assistant_lcdwiki、真实LCDWIKI宏、checksum／validation hash有效，双3MiB OTA及4MiB模型分区保留。ELF含原生面板、单ES8311、AEC／完整唤醒、HTTP／WebSocket，无外部ST77922、ES7210或FT5x06工厂。实际编译清单选中lcdwiki_display／lcdwiki_panel；单codec实现在audio_io.cpp，不编译CAM的waveshare_spi_audio.cpp。

归档脚本首次误用CAM模板的waveshare_spi_audio对象路径，在复制对象阶段停止；此前镜像／符号检查均退出0，固件本身也已退出0。首次脚本及失败说明保存在lcdwiki/initial-collection，随后按真实compile_commands改存audio_io.cpp.obj并完整重核。没有因此修改固件、重跑构建或删除失败事实。

两款正式构建均一次退出0，没有出现此前的GCC内部错误；原LVGL弃用等警告保留。两份ELF仍有四个SDK既有强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。真实nm、链接器-u参数及SDK来源快照保留；未改已安装SDK，不称零UND、可启动或干净发行构建。

最终归档命令：

```text
python -B evidence/lcdwiki-native-20261112/inspect_sdk_limits.py
python -B evidence/lcdwiki-native-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/lcdwiki-native-20261112/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增或修改的未跟踪源码／许可>
```

LCDWiki依赖状态改为resolved_local_overrides，旧未编译／预检状态保存在prior字段及历史证据；CAM共组研究只追加本板回归，不向其它CAM变体赋予本批当前通过。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。2 passed_cached_sources／94 stale_cached_sources／0 failed_cached_sources／92 not_run，累计96个官方变体曾缓存构建通过。94项历史成功需按当前源码重建，不能算当前通过；188项本轮实机均not_run，148项没有被显式关闭规则标出缺口也不代表完整功能已验收。

最终audit核对main仅修改LCDWiki显示及CMake、新增本板panel源／头和Apache许可；原表、触摸／旋转、音频、协议人工决定、绑定、OTA及其它main保持。普通git diff --check退出0；未跟踪源码／许可no-index记录真实差异退出码1与无空白诊断。共享root_managed_components锁事后快照另存，不称所有构建写入仅限stage。未运行独立测试套件、类型检查、服务、模型推理、声学／触摸／I2C操作；未安装依赖、Git写入、刷机、OTA、上传或发布。
