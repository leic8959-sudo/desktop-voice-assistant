# ESP-VoCat 首次整板构建与共享 QSPI 路径

后续更新：微雪1.85／B／C已分别完成首次整板缓存构建，保留216默认／181B版／184V2选表及复位差异；VoCat直接复位回归通过。实机仍未验收。见[本批说明](ws185_qspi_native_20261112.md)。

本批推进已有 `espressif-esp-vocat` 终端源码入口。此前VoCat因缺ST77916 2.0.2组件未进入目标编译；此次复用上一批已审阅的360×360／RGB565／QSPI流程，保留VoCat运行期PCB识别及复位差异，再执行独立整板构建。太极派STD／PDM分别重建，检查共享流程提取后的回归。

## 来源与改动

VoCat固定小智提交仍为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`；厂家BSP1.1.0固定提交为 `6f82fd66a099cfd388cf9376ca1c5d6dcc98381e`。12份板级来源与上批3份驱动来源本轮重新核对SHA256，路径、URL、摘要保存于 `evidence/vocat-qspi-native-20261112/source-references.json`。

公共QSPI协议来源仍为 `espressif/esp-iot-solution@91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f` 的ST77916 2.0.2 SPI实现，Git blob `d52eae7ef4f0c7a1f940f6c46cc56bd94d67c263`，SHA256 `a464c88fcfd485bd418e42cfdd099cede661936ffe81ddcb0f46b3d466cc7f67`。源文件先前已通过Git blob核验，本轮复核归档内容摘要；未安装或下载驱动组件。

新增 `st77916_qspi_panel.cpp/.h` 保存一份共用面板流程；`taiji_panel.cpp`保留自己的STD／PDM原表选择、约束和GPIO47设置，转换静态命令描述后调用公共创建函数。VoCat在原共享显示分支中传入既有184条命令和已识别的复位配置。没有另写一套VoCat绘图代码，也没有改两板原命令表。

| 项目 | 保留的协议与板级差异 |
| --- | --- |
| 参数／像素 | `0x02<<24 \| command<<8` 与 `0x32<<24 \| 0x2c<<8`，原2A／2B含end-1，RGB565每像素2字节 |
| 初始化 | 原MADCTL00／COLMOD55前导、F0 user-bank跟踪、逐条表内延时包括零tick让出，不追加默认SLPOUT或RAMCTRL |
| 数据表 | VoCat184条、太极派STD216条／PDM184条；每条字节、有效长度、顺序、延时保持原文 |
| 复位 | GPIO配置后有效电平10ms、释放后120ms；VoCat1.0为GPIO3低有效，1.2为GPIO47高有效；太极派仍GPIO47低有效 |
| 方向／时序 | VoCat无镜像／swap，CS14／CLK18／D0..3=46／13／11／12，40MHz；太极派双镜像、CS10及原总线脚，40MHz |
| 生命周期 | 借用静态表和panel IO，面板释放已知reset GPIO；LVGL继续登记原IO，保持颜色传输完成通知和DMA双缓冲 |
| API范围 | 固定360RGB565／gap0／直接GPIO复位，保留mirror／swap／invert／on-off；原驱动无sleep／brightness回调，背光由原板级负责 |

公共创建函数只接受已审阅范围：RGB、16位、有效输出复位脚、最多256条命令／每条最多14字节、结束回到user bank。它不提供通用SPI、MIPI、18位、任意gap、NC软件复位或默认命令表。此次两类板型的现有调用均在该范围内；其它微雪1.85、Moji2等ST77916入口仍保留自己的外部组件和状态，没有继承本批结果。

## PCB、供电和终端功能

`vocat_board.cpp/.h`、`vocat_panel_commands.h`、太极派原表、tft引脚头、数字音频、双codec后端、完整音频流水线、控制协议及人工决策、UI、配网绑定和OTA均保持本批开始时的源码摘要。

VoCat仍先完成冷启动codec成对应答／GPIO48选择，再确定DIN、PA和复位脚。V1.0为DIN15／PA4／LCDRST3低有效，V1.2为DIN3／PA15／LCDRST47高有效；暖启动依赖既有RTC magic／反码缓存，无有效身份时保持可能的电源而停止，不猜测修订或驱动未知PA。GPIO9、背光44先暗、PA读回及100ms互斥行为未改。公共面板创建前仍要求 `vocat_board_ready()`，未准备好时不会配置某个默认复位脚。

VoCat保留24kHz ES8311＋ES7210、TDM4取MIC1／slot0、软件AEC、完整唤醒、自然插话、媒体、绑定和双OTA。CST816仍使用原0x02原始count／event／坐标路径、初始释放和多指／越界／读错取消；没有把太极派坐标镜像或其单点组件缓存语义套给VoCat。

本轮只有微雪1.8实物。VoCat两种PCB在故障codec、冷／暖复位、GPIO48瞬态、RTC保持、首次刷入、PA回读、屏幕复位极性和40MHz波形下的行为仍未实机验收；实际Flash／PSRAM、颜色／格式／方向、触摸、24kHz时钟／TDM槽、增益／静音、AEC／唤醒／自然插话、媒体／绑定／OTA也未验收。原LED、BMI270、电容触摸、充电电量、UVC、SD、sleep及原厂MCP限制保留，终端构建不代表全部外围已完成。

## 实际执行与证据

证据目录为 `evidence/vocat-qspi-native-20261112/`。VoCat使用新独立stage `I:/xy_board_builds_20261109/vocat_qspi_native_20261112`；两个太极派回归同步已有各自stage的当前main／registry／defaults。SDK5.5.6-dirty、Xtensa14.2.0、17份现有本地override、Python联网保护，各2jobs；不安装SDK或依赖。

实际执行：

```text
python -B evidence/vocat-qspi-native-20261112/vocat/build_cached.py
python -B evidence/vocat-qspi-native-20261112/std/build_cached.py
python -B evidence/vocat-qspi-native-20261112/pdm/build_cached.py
python -B evidence/vocat-qspi-native-20261112/std/collect_build.py
python -B evidence/vocat-qspi-native-20261112/pdm/collect_build.py
python -B evidence/vocat-qspi-native-20261112/vocat/collect_build.py
```

| 真实IDF目标（均带 `-DIDF_TARGET=esp32s3`） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| `-DASSISTANT_BOARD=espressif-esp-vocat build` | 2557968 | 2203819 | 首次退出0，3MiB应用槽剩587760字节 |
| `-DASSISTANT_BOARD=taiji-pi-s3 reconfigure build` | 2236096 | 2203819 | 退出0 |
| `-DASSISTANT_BOARD=taiji-pi-s3-pdm reconfigure build` | 2238144 | 2203819 | 退出0 |

两款太极派各282项当前输入、27份产物归档，项目身份、镜像checksum／validation hash、双OTA／模型分区及panel／完整语音入口检查通过；实际audio object仍只有PDM含PDM采集API。它们验证各自回归，不替VoCat验收PCB／双codec／触摸。

只读直接依赖报告已执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/vocat-qspi-native-20261112/direct-dependencies.json
```

退出0：153本地／152官方、948条直接声明，88本地／87官方满足直接条件，缺组件声明142项；仅VoCat移除了被当前本板路径替代的依赖。直接条件不代表传递依赖、API、完整构建、启动或实机验收。

VoCat实际readelf／nm／size、esptool image_info、分区解码及SDK Git只读查询均退出0，282项当前输入与stage一致、27份产物快照。项目名assistant_vocat、真实VOCAT宏、checksum／validation hash有效；双3MiB OTA与4MiB模型分区保留。ELF含新公共面板入口、VoCat板级初始化／ready／复位脚／有效电平／PA控制、ES8311／ES7210、CST816、AEC／完整唤醒、HTTP和WebSocket；无外部ST77916工厂。两种PCB共用一个发行变体，符号和源码存在只证明两分支编译，不证明修订识别或复位波形已运行通过。

三款均一次退出0，没有出现此前的GCC内部错误；原LVGL弃用等警告继续保留。三份ELF仍有SDK既有四个强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。真实nm结果、链接器-u参数及SDK来源快照单独归档，未修改已安装SDK、不称零UND／可启动／干净发行环境。

最终归档命令：

```text
python -B evidence/vocat-qspi-native-20261112/inspect_sdk_limits.py
python -B evidence/vocat-qspi-native-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/vocat-qspi-native-20261112/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增或修改的未跟踪源码>
```

VoCat依赖状态改为resolved_local_overrides，旧预检／未编译状态保存在prior研究字段及历史证据；太极派既有研究只追加本批回归记录。152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。当前3 passed_cached_sources／84 stale_cached_sources／0 failed_cached_sources／101 not_run，累计87个官方变体曾缓存构建通过。84项历史成功需要按当前源码重建，不等于当前通过；188项本轮实机均not_run，148项未被显式关闭规则标出缺口也不等于功能已完整验收。

最终audit核对main仅修改共享QSPI显示／CMake／太极派面板适配入口，新增公共面板两文件；其它main、原表和PCB／RTC／供电／音频／控制人工决定等摘要不变。普通git diff --check退出0；未跟踪源码的no-index检查真实退出码1代表有内容差异，无空白错误，仅可能有CRLF提示。共享root_managed_components锁事后快照保留，不能称所有构建写入仅限stage。

导出脚本仅增加VoCat附带既有小智MIT和ST77916 Apache-2.0许可目录，未运行Docker／实际导出。未运行独立测试套件、类型检查、服务或触摸／声学操作，未安装依赖、Git写入、刷机、OTA、上传或发布。
