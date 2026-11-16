# SenseCAP Watcher 的 SPD2010 接入与整板构建

后续更新：LCD1.46已完成首次缓存整板构建，复用重命名后的SPD2010屏幕并增加有界触摸握手／报告读取；SenseCAP和VoCat回归通过。HDP续包组装、实机和声学仍未完成。见[本批说明](spd146_native_20261113.md)。

本批推进已有 `sensecap-watcher` 基础小燕子终端入口。此前因SPD2010 2.0.0~1缺失未目标编译；此次补齐固定412×412／RGB565／QSPI面板流程，保留默认402条初始化、工厂信息保护、旋钮按下和ES7243E音频，构建真实目标；LCDWiki另做独立QSPI／软件旋转／CMake回归。

固定小智提交仍为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。本轮重核16项既有来源SHA256，并通过只读gh api取得 `espressif/esp-iot-solution@12f6ca1182ec48889b17ec570fadaaf267cb336e` 的SPD2010源及许可，按Git blob核验，与旧参考逐字一致。完整命令、blob与SHA256在 `evidence/sensecap-spd-native-20261113/verified-reference.json`；其它来源在source-references.json。没有安装或复制整套组件、修改SDK。

## 面板与必要修正

从固定驱动默认数组提取402条命令到 `spd2010_default_commands.h`，最大参数4字节，按声明长度保留有效字节、顺序和全部延时；零长度占位不发送。新增 `sensecap_panel.cpp/.h`，保留Espressif版权及Apache-2.0全文；这份默认表同属Apache-2.0，没有套用其它板的MIT表。

| 项目 | 保留或修正的行为 |
| --- | --- |
| QSPI | 原SPI3 mode3／40MHz、CS45／CLK7／D0..3=9/1/14/13、32位命令／8位参数，DC与RESET为NC |
| 复位／前导 | 原软件01等20ms；FF参数20/10/00选择用户页、MADCTL00、COLMOD55，之后原402条默认表 |
| 分页／方向 | FF且参数长度>2时第三字节0为用户页；用户页MADCTL更新缓存；mirror使用位1／0，不套DCS位6／7；原表结束于用户页且无MADCTL／COLMOD覆盖 |
| 绘图 | 原02参数／32+2C像素封装，2A／2B含end-1窗口，16位缓冲、gap0，412×412范围检查 |
| LVGL修正 | 原SPD2010连swap_xy(false)也返回不支持；新增软件旋转flag让rotation0跳过端口自动硬件旋转，保持原生方向，增加一份20行条带旋转缓冲 |
| 就绪修正 | 原代码一旦创建display，即便后续PWM失败，重复调用也会返回该半初始化句柄；新增ready仅在PWM成功后置位，失败重复调用继续返回空，不自动重试 |
| 刷新／背光 | 原4像素X取整、双20行DMA、RGB565字节交换和同IO传输完成通知保留；GPIO8仍先暗、5kHz／10位PWM，不关闭系统电源 |

没有使用ST7789默认初始化、加入RAMCTRL或替换厂表。新panel只覆盖固定RGB565412平方、gap0、NC软件复位和既有一次启动，不提供18／24位、SPI／MIPI、任意gap／外部格式或通用重新初始化。软件旋转flag的额外缓冲和真实刷新性能仍待资源／实机验收；编译不证明颜色、gamma、复位波形或镜像方向正确。

## 工厂区与原功能边界

原专用分区CSV、`sensecap_partition_layout_valid()`、main在NVS／配置／板级写入前的布局门槛，以及刷机工具对保留扇区重叠的检查均未修改。

| 区域 | 地址／范围 |
| --- | --- |
| 原厂只读raw区 | 0x9000／0x32000，末端0x3b000，readonly；不作为default NVS挂载 |
| 小燕子NVS | 0x3b000／0x4000 |
| OTA数据 | 0x3f000／0x2000 |
| 双应用 | app0=0x50000，app1=0x350000，各0x300000 |
| 模型 | 0xc00000／0x400000 |

32MB声明下仍只使用前16MB布局，后16MB未分配。新固件构建不能代替原厂EUI备份／恢复证明，本轮未读取设备或执行备份／刷机，也不复制原厂凭据到新NVS。最终会解码本次产物中的分区二进制，分别检查只读工厂区、新NVS和双应用位置。

原TCA9555@0x21只拥有P9 LCD、P10系统、P12功放、P3旋钮按下，掩码RMW及读回保留；不照搬全端口启动，也不触及未拥有的AI／SD／Grove／ADC控制。ES7243E物理0x14／API0x28、RIGHT采集、ES8311 DAC、24kHz、共用时钟保留；不套ES7210 TDM或K10探测。PA只在实际DAC解除静音成功后开启，旋钮首次释放／错误取消／恢复重新释放逻辑不改。

完整唤醒、AEC、自然插话／媒体人工决定、绑定／NVS／提醒和双OTA源码保持。触摸I2C1、旋钮转动、HX6538／SSCMA AI相机、SPI2、LED40、RTC、SD、Grove、电量／充电政策及原厂sleep／自动关机未实现。当前仅旋钮按下提供基础操作，BSP_CAPS_TOUCH=0，不能称完整SenseCAP原厂功能已适配。

只有现有微雪1.8实物。SenseCAP实际32MB Flash／8MB Octal PSRAM、工厂EUI／新NVS／启动布局门槛、TCA电气行为、402条表／40MHz／颜色／对齐／缓冲峰值、ES7243E右声道／时钟／增益／PA噪声／AEC／唤醒／自然插话、旋钮、媒体／绑定／提醒／同板OTA均未实机验收。

## 实际执行与证据

证据在 `evidence/sensecap-spd-native-20261113/`。SenseCAP新stage为 `I:/xy_board_builds_20261109/sensecap_spd_native_20261113`，LCDWiki回归同步其已有stage。现有SDK5.5.6-dirty／Xtensa14.2.0／17份本地override／Python联网保护，各2jobs；未安装依赖。

```text
python -B evidence/sensecap-spd-native-20261113/sensecap/build_cached.py
python -B evidence/sensecap-spd-native-20261113/lcdwiki/build_cached.py
python -B evidence/sensecap-spd-native-20261113/lcdwiki/collect_build.py
python -B evidence/sensecap-spd-native-20261113/sensecap/collect_build.py
```

| 目标（IDF_TARGET=esp32s3） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| sensecap-watcher build | 2544976 | 2203819 | 首次退出0，3MiB应用槽剩600752字节 |
| lcdwiki-es3c35p reconfigure build | 2249952 | 2203819 | 退出0 |

LCDWiki回归归档297项当前输入／26份产物，不向SenseCAP证明SPD2010、旋钮、ES7243E或专用分区运行。

只读直接依赖报告已执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/sensecap-spd-native-20261113/direct-dependencies.json
```

退出0：153本地／152官方，929条直接声明，104本地／103官方满足直接条件，缺组件声明123项。该报告不证明传递依赖、API、目标构建／启动或实机验收。微雪1.46的SPD2010／触摸依赖仍独立，未继承本板默认表／结果。

SenseCAP归档297项当前输入／27份产物，stage与当前工作区摘要一致。实际readelf／nm／size、esptool image_info、分区解码和SDK Git只读查询均退出0；项目assistant_sensecap、真实SENSECAP宏、32MB镜像头、checksum／validation hash有效。实际二进制分区包含nvsfactory readonly200K@9000、新NVS16K@3B000、app0/app1@50000/350000各3MiB、model4MiB@C00000，与保护布局一致。ELF含本板电源／PA／旋钮输入、原生SPD面板、ES7243E与ES8311、AEC／完整唤醒、HTTP／WebSocket，无外部SPD2010工厂或ES7210。符号和分区存在不证明真实工厂EUI、布局门槛运行和声学已通过。

两目标正式构建均一次退出0，未出现此前GCC内部错误，原LVGL弃用等警告保留。两份ELF仍列出SDK强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`，链接器-u来源另记。前批对其它S3/C5镜像的静态审阅说明名称本身不足以判定缺启动代码，但本批镜像没有自动继承那个完整链接输入／反汇编结论，更无实机启动证明；未改SDK，不称零UND或干净发行环境。

最终归档命令：

```text
python -B evidence/sensecap-spd-native-20261113/inspect_sdk_limits.py
python -B evidence/sensecap-spd-native-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/sensecap-spd-native-20261113/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增或修改的未跟踪源码／许可>
```

SenseCAP依赖改为resolved_local_overrides，旧未编译／预检状态保存在prior字段及历史；LCDWiki研究只追加回归。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。2 passed_cached_sources／101 stale_cached_sources／0 failed_cached_sources／85 not_run，累计103个官方变体曾缓存构建通过。101项历史成功需按当前源码重建，不能算当前通过；188项本轮实机均not_run，148项未被显式关闭规则标出缺口也不等于完整功能已验收。

最终audit核对main只修改SenseCAP显示／CMake，新增本板panel源／头、402条默认表及Apache许可；工厂区CSV／启动门槛、板级／旋钮、音频、人工决定、绑定／OTA及其它main保持。普通git diff --check退出0，未跟踪源码／许可no-index检查记录实际差异退出码1和无空白诊断。共享root_managed_components锁事后快照另存，不称所有构建写入仅限stage。未运行独立测试套件、类型检查、服务、模型推理、GPIO／触摸／声学或故障注入；未安装、Git写入、备份、刷机、OTA、上传或发布。
