# 微雪 S3 CAM 四屏的 CH32 与屏幕接入

后续更新：4.3C／7C已分别完成首次整板缓存构建，复用CH32输出与原GT911轮询，保留独立复位、时序和codec差异；CAM共享CH32回归通过，实机未验收。见[本批说明](rgb_c_native_20261113.md)。

本批推进已有 `waveshare-esp32-s3-cam-1.83`、`-2`、`-2.8`、`-3.5` 四个终端源码入口。此前均因CH32V003组件缺失未进入目标编译，3.5还缺ST7796。此次按核对过的公开寄存器协议实现本板所用的复位、PWM和功放调用，3.5复用已审阅的ST7796默认SPI16位流程；四款分别构建，CHD3.5另做共享面板回归。

## 来源与协议范围

固定小智v2.5.2提交仍为 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，原理图固定于Waveshare `ESP32-S3-CAM-OVxxxx@72af9302030bb6f018d5c21b1225005481e09fda`。此前归档的11份来源本轮重新核对SHA256；原理图及原视觉审阅记录保留，未改图或重写引脚含义。

CH32协议来自 `waveshareteam/Waveshare-ESP32-components@9daffb7168b3c093a330da700988a22092294eef` 的公开2.0.0实现。本轮通过只读gh api重新取得C文件及公开头，Git blob核验并与旧归档逐字比较一致；实际命令／blob／SHA256在 `evidence/ws-cam-native-20261112/verified-ch32-reference.json`。该快照的旧repository_info提交曾404，未验证或安装canonical registry压缩包；本次原生实现依据固定公开协议，不声称使用了canonical组件二进制。

`waveshare_cam_board.cpp`替换原组件API调用，直接借用既有I2C1总线创建0x24／400kHz设备，只提供这块板所需的寄存器写入，单次IO仍1000ms；没有添加通用expander、输入／ADC／充电读回或其它模块固件接口。原Espressif版权和Apache-2.0通知保留，全文放在 `main/third_party/ch32v003/LICENSE`。未安装、复制整套组件或修改SDK。

| 寄存器／引脚 | 保留的协议与行为 |
| --- | --- |
| 0x02方向 | 启动写0xff，1表示output；这是原组件构造函数行为，所有8位都会受到影响 |
| 0x03输出 | 启动写0x00；按软件缓存只在值变化时写入，I2C接受后才提交缓存，不代表硬件读回 |
| 0x05 PWM | 启动255保持暗，运行期 `(100-percent)*255/100`，0%为255、100%为0 |
| P0／P1 | 触摸／LCD复位，高10ms→低10ms→高10ms |
| P4 | 启动保持低，只在实际DAC解除静音成功后由原音频路径请求启用；静音／关闭请求先关P4 |
| P6 | 电源LED高；不是相机电源 |
| P2／P3／P5／P7 | 原理图分别为SD_CS／CAM_PWDN／BAT_EN／CHG_DET；继承整端口重置，没有保存其原电平或完成电池接管 |

静态审阅得到正常启动的寄存器写序列为 `02ff,0300,05ff,0303,0300,0303,0343`。原set_dir因方向缓存已经ff而不写；原PA-off因输出缓存已经0而不写。这是对源码调用链的推导，没有执行I2C、协议模拟或故障注入，不能当实物时序结果。

启动只允许一次，完成后才接受运行期调用；功放和PWM继续共用100ms互斥锁。新增写错锁存：失败写可能已经到达模组，后续不能继续使用不确定缓存假报成功，也不会自动再做整端口初始化；要求重启。它不保证失败后功放已经物理关闭，也不能检测CH32独立复位、热插拔或未知固件行为。日志和函数返回保留具体错误，调用者仍走原音频／显示错误处理。

## 四种屏幕与 ST7796

| profile后缀 | 项目身份 | 屏幕／布局 |
| --- | --- | --- |
| 1.83 | assistant_wscam183 | ST7789、RGB、240×284、无镜像 |
| 2 | assistant_wscam2 | ST7789、RGB、240×320、无镜像 |
| 2.8 | assistant_wscam28 | ST7789、RGB、240×320、无镜像 |
| 3.5 | assistant_wscam35 | ST7796、BGR、320×480、mirrorX |

原SPI2 mode0／40MHz、MOSI1／CLK5／DC3／CS6／MISO NC，反色、无swap、gap0及各自16px布局保持。背光继续通过CH32 PWM，不配置ESP LEDC或虚构GPIO背光。

3.5仍由CH32 P1先做板级复位，之后ESP面板reset为NC，复用软件01／120ms。其ST7796 1.4.0默认12条命令此前已与1.3.5逐项核对；本轮重核来源摘要，沿用SLPOUT100ms、MADCTL08、COLMOD05、全部12条及零tick让出。SDK工厂只提供DCS绘图／方向操作，reset／init明确替换，不执行默认ST7789初始化／RAMCTRL。16位缓冲、窗口end-1、BGR缓存、mirrorX和反色与旧调用保持，原无sleep／brightness回调范围不扩大。P4的外部ST7796依赖和其它屏幕分支没有迁移。

## 完整终端与外围边界

双codec音频、引脚头、ST7796默认数据、控制协议及人工决策、完整音频流水线、UI、配网绑定、main和OTA源码摘要保持本批开始时原文。四款仍为ES8311＋ES7210、I2C1 SDA8／SCL7、MCLK10／BCLK11／WS12／DIN13／DOUT14、24kHz、TDM4取MIC1／slot0和软件播放参考AEC，保留完整唤醒、自然插话、媒体及双OTA。BOOT0首次释放后用于说话／卡片，没有改成只按键才能工作的降级方案。

现有实现没有相机初始化／拍摄／识物、SD缓存、电池／充电／关机、触摸驱动、原厂点击／factory跳转／双击AEC切换等功能。虽然未初始化这些外围，整端口重置仍会改变其控制位；不能把“未实现”说成“未触及硬件”或“板子没有”。这些范围仍保留在全量待办。

当前只有微雪1.8实物。四款实际Flash／PSRAM／屏控修订、CH32地址／固件／方向极性、启动整端口影响、1000ms错误／100ms锁、物理PA与PWM、复位／色序／40MHz／窗口、24kHz时钟／TDM槽／增益／静音、AEC／唤醒／自然插话／媒体、BOOT、绑定／提醒和同板OTA均未实机验收。CHD3.5 GT911+80全高坐标限制也没有被本批回归解决。

## 实际执行与证据

目录为 `evidence/ws-cam-native-20261112/`。四款分别使用cam183／cam2／cam28／cam35新独立stage，CHD3.5回归同步其原stage的当前main／registry／defaults。环境是既有SDK5.5.6-dirty、Xtensa14.2.0、17份本地override、Python联网保护，各2jobs；未安装或下载组件。

实际命令：

```text
python -B evidence/ws-cam-native-20261112/cam183/build_cached.py
python -B evidence/ws-cam-native-20261112/cam2/build_cached.py
python -B evidence/ws-cam-native-20261112/cam28/build_cached.py
python -B evidence/ws-cam-native-20261112/cam35/build_cached.py
python -B evidence/ws-cam-native-20261112/chd35/build_cached.py
python -B evidence/ws-cam-native-20261112/review_sources.py
python -B evidence/ws-cam-native-20261112/chd35/collect_build.py
python -B evidence/ws-cam-native-20261112/cam183/collect_build.py
python -B evidence/ws-cam-native-20261112/cam2/collect_build.py
python -B evidence/ws-cam-native-20261112/cam28/collect_build.py
python -B evidence/ws-cam-native-20261112/cam35/collect_build.py
```

| 真实IDF目标（均带 `-DIDF_TARGET=esp32s3`） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| `-DASSISTANT_BOARD=waveshare-esp32-s3-cam-1.83 build` | 2250224 | 2203819 | 首次退出0，3MiB应用槽剩895504字节 |
| `-DASSISTANT_BOARD=waveshare-esp32-s3-cam-2 build` | 2250224 | 2203819 | 首次退出0，剩895504字节 |
| `-DASSISTANT_BOARD=waveshare-esp32-s3-cam-2.8 build` | 2250224 | 2203819 | 首次退出0，剩895504字节 |
| `-DASSISTANT_BOARD=waveshare-esp32-s3-cam-3.5 build` | 2251824 | 2203819 | 首次退出0，剩893904字节 |
| `-DASSISTANT_BOARD=chd-esp32s3-box-lcd35 reconfigure build` | 2267696 | 2203819 | 退出0 |

CHD回归已核对284项当前输入与24份产物，独立项目名、GT／FT、双codec、AEC／完整唤醒和双OTA不能替CAM证明CH32或其它屏幕尺寸。

只读直接依赖报告已执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/ws-cam-native-20261112/direct-dependencies.json
```

退出0：153本地／152官方、939条直接声明，96本地／95官方满足直接条件，缺组件声明133项。仅四款CAM移除本板替代后的CH32依赖、3.5再移除ST7796依赖；其它CH32消费者保留原依赖。直接条件满足不等于传递解析、构建、启动或硬件验收。

四款各归档284项当前输入、26份产物快照，stage与工作区摘要一致。实际readelf／nm／size、esptool image_info、分区解码和SDK Git只读查询均退出0；每款独立项目名和真实板型宏已分别核对，checksum／validation hash有效，双3MiB OTA及4MiB模型分区保留。ELF含CAM板级启动／PA／PWM、双codec、AEC／完整唤醒、HTTP／WebSocket和SDK面板入口；3.5另保留ST7796专用reset/init，无原外部CH32／ST7796工厂。前三款尺寸相同字节数不等于镜像相同，项目身份与配置各自独立。

五款正式构建均一次退出0，未出现此前的GCC内部错误，原LVGL弃用等警告保留。五份ELF仍有四个SDK既有强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。真实nm、链接器-u参数和SDK来源快照保留，未修改已安装SDK、不称零UND／可启动／干净发行构建。

最终归档命令：

```text
python -B evidence/ws-cam-native-20261112/inspect_sdk_limits.py
python -B evidence/ws-cam-native-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/ws-cam-native-20261112/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增许可或修改的未跟踪源码>
```

四CAM依赖改为resolved_local_overrides，旧预检／缺依赖状态保存在prior研究字段及历史；CHD共组研究只追加回归证据，GT+80限制保持。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。5 passed_cached_sources／90 stale_cached_sources／0 failed_cached_sources／93 not_run，累计95个官方变体曾缓存构建通过。90项历史成功需按当前源码重建，不能算当前通过；188项本轮实机均not_run，148项未被显式关闭规则标出缺口也不等于完整功能已验收。

等待CAM构建期间另完成LCDWiki固定源预审，保存为 `next-lcdwiki-source-preparation.json`：ST77922的CMD1/2/3分页、320×480、COLMOD01／MADCTL00覆盖和swap_xy拒绝等与ST77916不同，需要保留本板合同。仅归档来源差异，没有改LCDWiki固件／入口或赋予构建通过。

最终audit核对main仅修改CAM板级、SPI面板选择和CMake，新增CH32许可；原音频／引脚／控制人工决定／绑定／OTA及其它main保持。普通git diff --check退出0，未跟踪源码／许可no-index检查记录真实差异退出码及无空白诊断。共享root_managed_components锁事后快照另存，不能称所有构建写入仅限stage。

许可导出条件增加四款CH32 Apache-2.0及3.5既有ST7796许可，未实际执行Docker导出。未执行独立测试套件、类型检查、服务或声学／触摸／I2C操作，未安装依赖、Git写入、刷机、OTA、上传或发布。
