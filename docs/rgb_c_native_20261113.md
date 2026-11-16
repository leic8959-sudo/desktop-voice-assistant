# 微雪 4.3C／7C RGB、CH32 与 GT911 接入

本批推进已有 `waveshare-esp32-s3-touch-lcd-4.3c` 和 `waveshare-esp32-s3-touch-lcd-7c`。此前缺CH32V003和GT911组件，两款未编译；本轮把CAM已审阅的CH32输出协议提成共享实现，按各板原复位与选址流程调用，触摸复用现有GT911轮询器，再分别执行完整目标构建。CAM1.83独立回归检查共享CH32提取。

固定小智提交仍为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，本轮重新核对4.3C与7C的29项既有公开／已安装接口来源SHA256。CH32公开2.0.0快照为Waveshare `9daffb7168b3c093a330da700988a22092294eef`，此前经Git blob核验；GT911 1.2.1协议参考为Espressif `4df6ea55593668302db2127b6b41760d5d1b5ea1`，本轮复核其归档内容及前批核验摘要。来源与核对范围见 `evidence/rgb-c-native-20261113/source-references.json` 和protocol-contract-review.json。未安装或复制整套组件。

## 共享协议与各板动作

新增 `ch32_output.cpp/.h`，集中已有0x24／400kHz、方向0x02、输出0x03、PWM0x05、1000ms单次IO。初始化依旧写directionFF／output00，**整个端口会重置，未拥有的供电／电池等位不保留**。输出只在缓存值变化时发送，ACK后才提交缓存；没有物理读回。任何写错锁存失败，后续不再使用不确定缓存或自动整端口重初始化，要求重启。失败写可能已经到达模组，不能宣称此后功放一定关闭；模组独立复位／热插拔也未检测。

板级函数保留启动一次、运行期100ms PA／PWM共用互斥。CAM改用同一封装，原P0／P1高低高各10ms、P4功放、P6电源LED、PWM255与寄存器写序列保持；其原CH32 Apache-2.0许可沿用，导出条件扩展到RGB C两板，未执行Docker／实际导出。

RGB C各自继续P3功放关闭、P2背光使能且PWM255暗、7C额外请求P6高；P1先低200ms，GPIO4作为INT拉低200ms，再P1高200ms，最后INT改输入。这与GT911在复位释放时选择0x5d的既有流程一致。本次未添加0x14回退，也不由GT911再次操作复位脚。7C的P6物理作用与电池接管未独立确认，不能推断充电策略。

触摸复用未修改的 `Gt911Polling`，借用原0x5d／400kHz／16位命令／100ms IO。Init读取产品及配置但不据此宣布实体身份；Read检查ready、完整0..5点报告、重复track ID及不支持标志，payload与清报告ACK都成功才发布。健康not-ready沿用已确认快照，初次／故障后没有有效帧时不伪造释放。消费者继续只接受范围内单指，初始／恢复等待零点释放，多指／越界／读错取消原动作；坐标仍800×480原生无镜像。

长时间not-ready仍可能保留过期按压，本批没有伪造超时松手。控制器静止按压／释放节奏、ACK错误、复位／选址波形、报告边界、轨迹ID、实际坐标与故障恢复均需实机验收。源码复用不等于触摸运行已通过。

## 分别保留的硬件路径

| 项目 | 4.3C | 7C |
| --- | --- | --- |
| Flash声明 | 16MB | 32MB；现有分区仍在16MB范围内，高地址未开发 |
| I2C0 | SDA8／SCL9 | SDA47／SCL48 |
| RGB D8／D9 | 48／47 | 9／8 |
| H／V后沿 | 各4 | 各8 |
| 输出codec | ES8311，mono | ES8389 DAC，stereo；原有固定缓冲复制mono到左右声道 |
| 输入 | ES7210，24kHz TDM4取MIC1 slot0 | 同左，保留37.5dB增益要求 |

两板均保留800×480 RGB565、PCLK16MHz负沿、HS46／VS3／DE5／PCLK7、各自RGB引脚、双PSRAM framebuffer、10行bounce／64burst、LVGL direct防撕裂和原生字节序；没有SPI显示IO。GPIO0仍是RGB DATA6，不配置为BOOT按键；GPIO43／44为音频DIN／BCLK，不恢复UART0日志，USB控制台保持。

音频仍MCLK6／BCLK44／WS16／DIN43／DOUT15、24kHz、软件播放参考AEC，7C的双声道扩展、codec格式与启停策略源码不改。完整唤醒、自然插话／媒体人工决定、UI、配网绑定、NVS、提醒和双OTA、原GT911轮询器、RGB时序及其它main按本批开始时摘要保护。没有通过删除语音能力满足构建。

当前只有微雪1.8实物。两款实际PCB／Flash／PSRAM、RGB信号／色序／帧同步／预算、CH32整端口影响／固件／P6作用、背光和PA极性、触摸、7C ES8389双声道、24kHz时钟／TDM槽／增益／静音、AEC／唤醒／自然插话、媒体、绑定／提醒／同板OTA均未验收。BQ27220、电量／充电USB政策、省电、SD、相机、原厂MCP和完整手势仍未实现，不能据终端编译宣称整板外围完成。

## 实际执行与证据

证据目录 `evidence/rgb-c-native-20261113/`，两新目标分别使用新stage，CAM1.83同步已有stage。现有SDK5.5.6-dirty／Xtensa14.2.0／17份本地override／Python联网保护，各2jobs；未安装依赖。

```text
python -B evidence/rgb-c-native-20261113/ws43c/build_cached.py
python -B evidence/rgb-c-native-20261113/ws7c/build_cached.py
python -B evidence/rgb-c-native-20261113/cam183/build_cached.py
python -B evidence/rgb-c-native-20261113/review_sources.py
python -B evidence/rgb-c-native-20261113/cam183/collect_build.py
python -B evidence/rgb-c-native-20261113/ws43c/collect_build.py
python -B evidence/rgb-c-native-20261113/ws7c/collect_build.py
```

| 目标（IDF_TARGET=esp32s3） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| waveshare-esp32-s3-touch-lcd-4.3c build | 2236464 | 2203819 | 首次退出0，3MiB应用槽剩909264字节 |
| waveshare-esp32-s3-touch-lcd-7c build | 2239312 | 2203819 | 首次退出0，剩906416字节 |
| waveshare-esp32-s3-cam-1.83 reconfigure build | 2250432 | 2203819 | 退出0 |

CAM共享实现回归归档293项当前输入与27份产物，结果不向RGB时序／GT911或7C的ES8389分支继承。

只读直接依赖报告已执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/rgb-c-native-20261113/direct-dependencies.json
```

退出0：153本地／152官方，930条声明，103本地／102官方满足直接条件，缺组件声明124项；直接条件不证明传递解析、API、目标构建／启动或实机通过。

三款各293项当前输入与stage一致；RGB两款各28份产物、CAM27份产物归档。实际readelf／nm／size、esptool image_info、分区解码和SDK Git只读查询均退出0。项目名assistant_ws43c／assistant_ws7c、真实RGB_C及7C宏、checksum／validation hash有效；镜像头实际为4.3C16MB／7C32MB，分区仍沿用双3MiB应用与4MiB模型。ELF含RGB面板、共享CH32、GT911 Init／Read、ES7210、AEC／完整唤醒与HTTP／WebSocket，4.3C含ES8311，7C含ES8389及原mono到stereo写入入口，无外部CH32／GT911工厂。配置和符号不证明实际硬件容量、双声道输出或声学已通过。

三目标均一次构建退出0，没有此前GCC内部错误；原LVGL弃用等警告保留。三份ELF仍列出SDK的四个强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。它们的链接器-u来源继续记录；前批对另两份S3/C5镜像的静态审阅已证明这些名称本身不足以判定缺启动代码，但本批新镜像未单独执行同样完整的链接输入／反汇编审阅，更没有实机启动结果。未改SDK、不称零UND、干净SDK或物理可用。

最终归档命令：

```text
python -B evidence/rgb-c-native-20261113/inspect_sdk_limits.py
python -B evidence/rgb-c-native-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/rgb-c-native-20261113/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增或修改的未跟踪源码>
```

两RGB入口依赖改为resolved_local_overrides，旧预检／缺依赖状态保存于prior字段及历史；CAM共组研究只追加1.83回归，其它CAM不继承本批结果。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。3 passed_cached_sources／99 stale_cached_sources／0 failed_cached_sources／86 not_run，累计102个官方变体曾缓存构建通过。99项历史成功需按当前源码重建，不能算当前通过；188项本轮实机均not_run，148项未被显式关闭规则标出缺口也不等于完整功能已验收。

最终audit核对main只改RGB板级／显示、CAM板级与CMake，新增共享CH32两文件；GT911轮询器、RGB时序／缓冲注册、引脚／音频、人工决定／绑定／OTA及其它main摘要保持。普通git diff --check退出0；未跟踪源码no-index检查记录真实差异退出码1及无空白诊断。共享root_managed_components锁事后快照另存，不称所有构建写入仅限stage。未运行独立测试套件、类型检查、服务、模型推理、GPIO／I2C／触摸／声学或故障注入；未安装、Git写入、刷机、OTA、上传或发布。
