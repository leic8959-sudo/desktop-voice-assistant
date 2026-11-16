# Guition JC4827W543：4MB单应用源码候选

日期：2026-11-10，Asia/Shanghai。官方变体 `guition-jc4827w543` 与 `guition-jc4827w543-ext-amp`；独立应用名 `assistant_guition`、`assistant_guition_ext`。两款新增源码入口都因本机缺GT911组件而未编译、无新板镜像、未实机验收。

## 固定硬件与容量边界

来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 `main/boards/guition/jc4827w543/`：config.h、config.json、实际板类、NV3041A驱动及README。其声明厂商型号ESP32-4827A043／原理图v0.2、N4R8／4MB Flash＋8MB OPI PSRAM；本轮据此实现固定配置候选，未确认任何实物模组或同名商品修订。

4MB无法容纳现有双应用OTA分区和约2.2MB模型。固定上游也明确无OTA、无本地唤醒；本地没有把容量虚填为8MB或16MB。Python／PowerShell／CMake板型读取器增加4MB容量，保持原有其他容量和边界检查。

两板各自使用 `nvs 0x9000/0x4000`、`phy_init 0xf000/0x1000` 和 `factory app 0x20000/0x3e0000`，末地址恰为0x400000；没有第二app、otadata、model。启动在NVS写入前检查factory与NVS地址／尺寸，拒绝含OTA数据或model的错误布局。应用分区的实际可用量与最终镜像大小仍需新板真实构建确认。

## 无模型与无OTA实现

CMake仅为Guition选择 `manual_wake_word.cpp` 和 `single_app_update.cpp`；其他板仍选择既有wake_word／ota_update实现。前者返回不支持本地唤醒，feed始终false，不加载模型；后者在下载、创建任务、擦写和启动分区切换前拒绝OTA，并提示通过数据线更新。板级defaults关闭rollback，选择中英文MultiNet NONE；没有model分区，ESP-SR不会为该配置生成模型分区烧录项。

audio_io只在Guition分支跳过 `esp_srmodel_init`，用空模型列表创建现有AFE，显式保持无WakeNet／NS模型；已安装ESP-SR2.4.7公开头说明 `vad_model_name=nullptr` 使用WebRTC VAD。保留既有软件AEC、重采样、手动录放音、PSRAM队列和任务栈；这不是AtomS3那种无PSRAM硬件的解决方案。空模型AFE的实际分配／运行尚未验证，不能由头文件或旧板回归推断成功。

控制协议只增加新板条件分支：hello不宣称two_stage_barge_in、wake_clip、wake_input_ready等本地唤醒能力，触发类型manual；manual_input_ready保持现有流程，软件AEC／自然插话能力仍依据实际初始化结果。原有自然插话实现及其他板默认字段保留。新板的完整握手、手动按住／松开、服务端语义与OTA拒绝行为仍待编译和实机验收。

## 两种音频接线

| 部分 | 固定配置 |
| --- | --- |
| 麦克风 | 板上无麦克风，需外接INMP441；I2S0 BCLK6／WS7／DIN15，LEFT，16kHz／32位槽转PCM16 |
| 普通版扬声器 | 板载NS4168，I2S1 BCLK42／WS2／DOUT41，24kHz／LEFT／32位槽 |
| 外接功放版 | 外接I2S功放，BCLK9／WS14／DOUT16；板载NS4168的42／2／41先锁存低再配置输出，避免闲置线浮空 |
| 静音 | 两种均无可信的独立PA使能脚，复用实际PCM静音／软件音量，不伪造dummy codec成功即物理静音 |

两种输出宏与应用身份分开登记。固定来源关于MAX98357A的GAIN／SD悬空描述不是对用户实际接线的确认；音频时钟、LEFT槽、增益、静音、板载功放噪声和外接版停放线电平均需测量。

## NV3041A显示与GT911

480×272横屏，NV3041A QSPI2 mode0／40MHz，CS45／CLK47／D0-21／D1-48／D2-40／D3-39，LCD复位NC；GPIO38属于触摸复位，不能用作LCD复位。背光GPIO1高有效PWM，初始暗，只有显示／触摸完整初始化后才交给UI设置亮度。使用独立480×272布局、16px中文字体和现有“按住说话”按钮／卡片交互。

本地 `nv3041a_panel.c/.h` 改编固定小智板内驱动，保留92项初始化数据／顺序／延时、基准MADCTL0xc0、IPS反色与只支持0／180度的轴限制。寄存器命令封装为 `0x02<<24 | cmd<<8`，每次绘制先发送CASET／RASET／RAMWR，再用 `0x32<<24 | 0x3c<<8` 传四线像素。增加固定480×272绘制范围／null校验、零gap／RGB／无复位／无vendor约束、GPIO结果检查及MADCTL写成功后才提交缓存；没有替换为ST7789或只复制屏幕尺寸。

上游驱动称表来自Arduino_GFX。另读其固定提交 `a8615fbc7dcd05c769c3031173e2f60dbf410469` 后发现C1／C2／C6值分别与小智表不同，因此本地只宣称精确保留固定小智表，没有继续声称与该Arduino版本逐字节一致。MIT版权与Arduino_GFX的BSD／Adafruit2012通知保留于嵌套 `main/third_party/xiaozhi_guition/`，不改变项目整体许可。对照记录位于忽略目录 `evidence/guition-source-20261110/panel-table-comparison.json`。

触摸GT911采用I2C1 SDA8／SCL4、RST38／INT3，400kHz、100ms传输超时。固定公开GT9111.2.1源码确认：必须传 `driver_data.dev_addr=0x5d`，让驱动在释放复位时主动拉低INT选择地址，不能先probe未复位的地址就判断触摸不存在。本入口固定0x5d；没有尝试0x14或假装触摸缺失仍能正常交互。触摸初始化失败停止本次启动，不建立空pointer或panic重启循环。

坐标按固定480×272无轴变换进入LVGL；初始必须先释放，多指、越界、读取或坐标获取失败触发INDEV_RESET并等待释放；错误时仍清空驱动缓存，防止旧按下变成误确认。没有声称完成坐标标定或任意多键检测。BOOT0与屏幕TE共线且外壳可能不可达，本入口保留未映射，不将它伪装为可用的按住说话入口；使用可见触摸按钮。本地语义是按住说话，未移植上游“点任意区域切换聊天”。

电池分压／电量与校准、充电状态、SW1充电器按键、BOOT0／TE复用、物理RST、热插拔、0x14地址回退等仍未实现或验收。屏控表、QSPI数据传输、真实容量／PSRAM、触摸坐标／错误取消、软件AEC／WebRTC VAD、16／24kHz音频、USB控制台、联网／绑定／提醒和电缆更新仍待硬件验证。现有实物只有微雪1.8，屏控修订尚未确认。

## 真实依赖预检

两板分别实际执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile <profile> --destination I:\xy_board_builds_20261109\<profile>-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

都退出1：`Required component is not installed: espressif/esp_lcd_touch_gt911; nothing was staged`，目的目录均不存在。依赖声明固定1.2.1，实际公开API／地址选择源码来自esp-bsp提交 `4df6ea55593668302db2127b6b41760d5d1b5ea1`，仅供阅读和归档，没有安装组件。

记录为 `dependency_resolution=blocked_missing_local_sources`、`build=not_run`，见 `native_dependency_preflight_20261110_guition.json`。新NV3041A／GT911／4MB分区／空模型AFE／无OTA及无唤醒文件均尚未被目标构建选择和编译；旧板回归通过也不能证明这些新路径可编译或启动。

## 公共路径回归与矩阵

现有 `bread-compact-wifi-s3cam`（数字音频／竖屏）和 `espressif-esp32s3-korvo2-v3`（双codec／ADC键）分别实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，初次均退出0，日志包含main／audio_io／display_ui等公共代码编译。追加Guition启动分区检查并收紧本地驱动来源注释后，同步最终输入、刷新变化文件时间，再分别执行同一命令，均退出0，最终main实际重编；初次日志保留。

旧板仍实际选择wake_word.cpp／ota_update.cpp和16MB／双应用／模型配置，没有选择guition_display、nv3041a_panel、manual_wake_word或single_app_update。它们的回归只验证旧板公共路径，没有验证新4MB目标、GT911或空模型运行。最终各135项输入摘要／128个main文件集合、编译宏、合并defaults、生成配置和工作区匹配。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| bread-compact-wifi-s3cam | 2,213,776 | 931,952 |
| espressif-esp32s3-korvo2-v3 | 2,260,096 | 885,632 |

两镜像在构建结束后实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0，独立项目名／16MB／checksum／validation hash匹配；仍分别链接digital_audio／dummy codec和Korvo2双codec／ADC实现，以及既有wake／OTA函数。模型均2,203,819字节，在4MB模型分区内；这个模型分区大小是旧16MB板的证据，不能当作Guition4MB总容量的可用空间。

记录为 `cached_build_attempts_20261110_guition_<profile>.json`，首次／最终日志、产物／ELF、配置／编译命令／依赖锁、源快照和SHA-256归档在忽略目录 `evidence/guition-builds-20261110/`。使用本机IDF5.5.6-dirty／工具／Python和17个既有组件override，Python offline_guard拒绝外网；保留SDK原有修改，不是干净发行依赖构建。

实际只读执行 `. tools/board_profiles.ps1; Get-AssistantBoardProfiles -ProjectPath firmware/assistant_espidf`，两款新入口均读为4MB及各自应用名；Python读取器由真实预检运行，已通过容量校验后才在GT911缺失处停止。CMake4MB和新分区尚无目标编译证据，不能以读取器成功替代构建。

实际 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：78官方源码变体／79本地profile含1历史入口／110待源码；累计47变体曾缓存通过。当前2 passed／45 stale／6历史依赖预检失败／135 not_run，其中25项not_run已有源码；全部188实机not_run。全量目标仍进行中。

本轮未运行测试套件／独立类型检查、安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。control_protocol只在已有文件上增加Guition特性声明及初始化日志分支，保留其他并发改动；无屏六位绑定码人工决策函数、网关与运行时代码未修改。

收尾实际 `git diff --check` 退出0，本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。两份各135输入／17项构建归档产物、两板共用13项固定公开／已安装API源证据、两个预检日志摘要匹配；逐项读取两份CSV确认不重叠、4KB对齐、末地址4MB且仅factory应用／无model或otadata，这仍不是4MB目标构建证据。矩阵研究／registry摘要与两新板not_run状态匹配，对照编辑前快照审阅本批diff；两项回归进程均已结束。


2026-11-10后续时钟更正：T-CameraPlus批次读取MAX98357A/B规格第19页，明确24kHz及11025Hz均不支持。敏思K08的已声明MAX98357和Guition外接MAX98357A入口改为16kHz物理播放，复用既有网络24→16kHz FIR及相同实际PCM软件AEC参考；Guition原板NS4168入口保留原24kHz。敏思新缓存回归及证据见[本批记录](tcamera_adaptation_20261110.md)；Guition外接入口仍缺GT911未编译，均未新增实机验收。本文前段24kHz输出为历史状态，当前以profile源码为准。
