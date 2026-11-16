# 微雪 LCD 1.85／B／C 首次整板构建

后续更新：Moji2 C5复用原184条表与共享QSPI面板，已完成首次真实C5整板缓存构建；固定“你好小智”、单codec和无触摸差异保留，VoCat S3回归通过，实机未验收。见[本批说明](moji2_qspi_native_20261112.md)。

本批推进已有 `waveshare-lcd-1.85`、`waveshare-lcd-1.85b`、`waveshare-lcd-1.85c` 三个源码入口，分别对应三个官方发行变体。此前它们因ST77916 2.0.2缺失未进入整板编译。此次复用已审阅的360×360／RGB565／QSPI流程，加入NC软件复位和固定驱动默认表，保留每板ID选择／复位／音频／触摸差异；另重建VoCat核对直接GPIO复位回归。

## 来源与三套命令表

固定小智提交仍为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。本轮通过只读 `gh api` 重新取得三板config和板类共六文件，每份按Git blob及旧研究SHA256核对，来源、完整命令及摘要见 `evidence/ws185-qspi-native-20261112/board-references.json`。

默认命令表来源为 `espressif/esp-iot-solution@91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f` 的ST77916 2.0.2 SPI驱动，Git blob `d52eae7ef4f0c7a1f940f6c46cc56bd94d67c263`，SHA256 `a464c88fcfd485bd418e42cfdd099cede661936ffe81ddcb0f46b3d466cc7f67`。该公开源码前批已按Git blob核验，本轮重核归档摘要。仅转换其默认216条数据为 `st77916_default_commands.h`，保留Espressif版权和Apache-2.0许可；未安装、复制整套组件或修改SDK。

| 实际4字节ID | 原版／C版 | B版 |
| --- | --- | --- |
| `00:7f:7f:7f` | 固定2.0.2默认216条 | 原B版181条 |
| `00:02:7f:7f` | 原184条V2 | 相同184条V2 |
| 读取／清理失败或其它ID | 停止，不默认选表 | 停止，不默认选表 |

原表 `st77916_board_init.h` 字节保持。按声明长度比较实际发送参数后，B版181条与固定源完全一致，184条V2与三块固定板类均一致；216条默认表与驱动源码逐项一致。三表都结束于user bank，没有user bank下的MADCTL／COLMOD覆盖；216条与太极派STD的216条并不相同，未按条数代用。

核对脚本首次把B版三个零长度命令的占位 `{0x00}` 当成实际发送数据，错误报告本地长度有误；进一步查看后确认本地正确保存空数据／length0，上游也指定length0，占位不会发送。拟修改操作在写入前断言停止，原表摘要未变；该用户侧错误判断已明确更正。第二次核对也修正了数组名假设：原版／C版叫 `vendor_specific_init_new`。最终 `review_sources.py` 使用实际数组名和声明发送长度；失败说明独立保留，没有因此重跑固件或覆盖旧证据。

## 总线、复位与板级差异

共享面板现在支持NC复位：无GPIO时不配置／移位／写入／释放GPIO，而发送封装后的软件0x01并等待120ms；直接复位继续按有效电平10ms、释放后120ms。原RGB565／gap0、MADCTL00／COLMOD55前导、F0 bank跟踪、表内所有延时、`02`参数和`32/2c`像素封装、2A／2B含end-1窗口保持。

| 项目 | 1.85原版 | 1.85B | 1.85C |
| --- | --- | --- | --- |
| LCD板级复位 | TCA9554 P0／P1高低高，各300ms；之后软件01／120ms | 原GPIO3低20ms／高后150ms；之后panel低10ms／高后120ms | 同原版扩展器复位与软件复位 |
| 音频 | 16kHz右槽数字输入，24kHz BOTH双32位输出 | ES7210／ES8311、24kHz、PA9 | 保留VERSION_2_0硬件路径，双codec24kHz、PA15；C1.0仍未移植 |
| I2C／触摸 | 主SDA11／SCL10；触摸独立I2C1 SDA1／SCL3、IRQ4 | 共享11／10、LCDRST3／触摸RST1、IRQ4，无扩展器 | 共享11／10，扩展器复位触摸，无重复总线 |
| 其它既有范围 | PWR6新按住5秒释放及保持GPIO7 | BOOT0，原codec／静音流程 | BOOT0，原codec／静音流程 |

三款继续CS21／CLK40／D0..3=46／45／42／41、mode0、32命令位／8参数位。ID在3MHz读取，检查读取和删除probe IO成功后才创建80MHz显示IO。GPIO5背光PWM、20行DMA双缓冲、偶数区域对齐和SPI颜色传输完成通知保持。新默认表适配数组在静态存储中保留，与panel借用的IO同寿命。

新核心没有扩展到任意gap、18位、普通SPI、MIPI或其它默认表。Moji2 C5仍保留外部ST77916依赖；VoCat和太极派原命令／板级配置不被三款微雪的ID探测替代。三款导出许可条件增加既有ST77916 Apache-2.0全文，同时保留原小智MIT目录，未实际运行Docker导出。

## 终端功能与实机边界

数字音频、双codec后端、引脚头、原B／V2命令表、太极派和VoCat板级源码、完整音频流水线、控制协议及人工决策、UI、main电源逻辑、配网绑定和OTA摘要均保持本批开始时原文。没有通过移除完整唤醒、AEC、自然插话、媒体或双OTA来满足构建。

现有CST816S1.1.2单点缓存／事件限制没有由面板接入修复，多指、初始按住、错误恢复、坐标和圆屏裁切仍需单独验收。当前只有微雪1.8实物；本批三款的实际ID／dummy字节、216／181／184表、80MHz可靠性、复位波形、颜色、Flash／PSRAM、I2C／触摸、PWR、音频时钟／时隙／PA／增益／静音、AEC／完整唤醒／自然插话、媒体、绑定和同板双OTA均未实机验收。C1.0、TF、传感器、RTC、LED和电池等旧范围仍未完成，不能据终端构建称所有外围已适配。

## 实际执行与证据

证据目录：`evidence/ws185-qspi-native-20261112/`。`original/`、`b/`、`c/`是各自新独立stage；`vocat/`同步已有VoCat stage。环境为现有SDK5.5.6-dirty、Xtensa14.2.0、17份现有本地override、Python联网保护，各2jobs，没有安装或下载SDK／组件。

实际执行：

```text
python -B evidence/ws185-qspi-native-20261112/original/build_cached.py
python -B evidence/ws185-qspi-native-20261112/b/build_cached.py
python -B evidence/ws185-qspi-native-20261112/c/build_cached.py
python -B evidence/ws185-qspi-native-20261112/vocat/build_cached.py
python -B evidence/ws185-qspi-native-20261112/vocat/collect_build.py
python -B evidence/ws185-qspi-native-20261112/review_sources.py
python -B evidence/ws185-qspi-native-20261112/original/collect_build.py
python -B evidence/ws185-qspi-native-20261112/b/collect_build.py
python -B evidence/ws185-qspi-native-20261112/c/collect_build.py
```

| 真实IDF目标（均带 `-DIDF_TARGET=esp32s3`） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| `-DASSISTANT_BOARD=waveshare-lcd-1.85 build` | 2530208 | 2203819 | 首次退出0，3MiB应用槽剩615520字节 |
| `-DASSISTANT_BOARD=waveshare-lcd-1.85b build` | 2555680 | 2203819 | 首次退出0，剩590048字节 |
| `-DASSISTANT_BOARD=waveshare-lcd-1.85c build` | 2559120 | 2203819 | 首次退出0，剩586608字节 |
| `-DASSISTANT_BOARD=espressif-esp-vocat reconfigure build` | 2558080 | 2203819 | 退出0 |

VoCat归档283项当前输入／27份产物，实际readelf／nm／size、esptool镜像、分区和SDK Git只读查询均退出0；它验证自己的GPIO复位、双codec／RTC等编译分支，不替微雪三款证明选表、NC复位或不同音频分支。

只读直接依赖报告：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/ws185-qspi-native-20261112/direct-dependencies.json
```

退出0：153本地／152官方、945条直接声明，91本地／90官方满足直接条件，缺组件声明139项。该报告不证明传递依赖、API、整板构建或硬件通过。

三款各归档283项当前输入、26份产物快照，输入与各自stage一致；实际readelf／nm／size、esptool镜像、分区和SDK Git只读查询均退出0。项目名分别assistant_lcd185／assistant_lcd185b／assistant_lcd185c，真实编译宏和音频源已单独核对；checksum／validation hash有效，双3MiB OTA与4MiB模型分区保留。ELF含共享panel、CST816、AEC／完整唤醒、HTTP／WebSocket；原版含数字音频及TCA9554，B/C含ES8311和ES7210，C另保留TCA9554；无外部ST77916工厂。

四款正式构建均一次退出0，未出现此前的GCC内部错误；原LVGL弃用等警告保留。每份ELF仍有SDK既有四个强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。真实nm、链接器-u参数及SDK来源快照保留；未改已安装SDK或宣称零UND、可启动、干净发行构建。构建没有运行触摸／音频／ID或复位代码，不能证明物理行为。

最终归档命令：

```text
python -B evidence/ws185-qspi-native-20261112/inspect_sdk_limits.py
python -B evidence/ws185-qspi-native-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ws185-qspi-native-20261112/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增或修改的未跟踪源码>
```

三款依赖改为resolved_local_overrides，旧预检和研究状态保存在历史与prior字段；VoCat研究只追加回归记录。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。4 passed_cached_sources／86 stale_cached_sources／0 failed_cached_sources／98 not_run，累计90个官方变体曾缓存构建通过。86项历史成功需按当前源码重建，不能算当前通过；188项本轮实机均not_run，148项未被显式关闭规则标出缺口也不等于功能已完整验收。

最终audit核对main仅修改共享QSPI显示／CMake、公共panel源／头，新增216条默认表；原B/V2表、音频、电源、引脚、控制人工决定、配网绑定、OTA及其它main摘要保持。普通git diff --check退出0；未跟踪源码no-index检查记录真实差异退出码及无空白诊断。共享root_managed_components锁事后快照保留，不能称所有构建写入仅限stage。

未运行独立测试套件、类型检查、服务或声学／触摸操作；未安装依赖、Git写入、刷机、OTA、上传或发布。
