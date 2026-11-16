# NT26 基础终端与蜂窝网络适配（2026-11-12）

本批为 `bread-compact-nt26` 新增独立源码候选 `assistant_nt26`。实际接入的是UART Ethernet蜂窝网络，复用ESP-IDF的HTTP／WebSocket／OTA网络栈。**目标缺少驱动与兼容SDK，尚未目标编译或实机验收。**两项旧板回归不能证明NT26分支可运行。

## 固定来源与板级实现

小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行清单154目录／188变体，README的138／171已过时。源码、组件登记页、官方参考源码包和仓库tree共76份资料归档于 `evidence/nt26-source-20261112/source-artifacts.json`，均记录SHA256；包中源文件另按固定tree的Git blob SHA1核对，组件注册元数据按原包保留。

- UART1、3Mbps、TX12、RX11、MRDY9、SRDY10；固定原板没有额外模块复位／供电GPIO定义，本批不猜测或驱动其他电源脚。
- 发布变体指定SSD1306 128×32，I2C0 SDA41／SCL42、XY镜像；复用现有OLED布局、绑定码和非触屏操作卡。
- 数字麦克风I2S0 WS4／BCLK5／DIN6，左声道32位槽采集16kHz；扬声器I2S1 BCLK15／WS16／DOUT7，沿固定上游选择24kHz。实际功放型号与24kHz能力仍须核对，不能把任意外接功放视为兼容。
- BOOT0、触摸按键47、音量40／39，复用已有松手后启用、组合键取消、按住说话和卡片选择／确认路径。这里的触摸按键不是屏幕触摸设备。
- 保留现有PSRAM、完整唤醒、PCM软件AEC和双OTA分区实现；本批没有通过取消功能绕过容量问题。LED48、灯控18和原厂低功耗管理未接入。

## SDK与驱动要求

组件精确锁定 `78/uart-eth-modem 0.7.0`，组件登记页／官方源码包对应 `78/uart-eth-modem` 提交 `bb1fb812be01f2f1d90a6bd021ff6d3f90d6e849`。其manifest要求：

```text
idf >=6.0.1
78/uart-uhci ^0.4.0
espressif/iot_eth ^1.1.0
```

因此NT26单板manifest明确 `idf >=6.0.1,<6.2.0`，main目标使用C++23，源码对过旧SDK报错。本地已有可用回归环境是5.5.6-dirty，不能用于这个目标。官方参考源码包只保存在evidence，未安装、复制到managed_components、作为override或参与链接。完整传递依赖和SDK需要由牛牛准备，尚未验证其与现有完整音频引擎的SDK6兼容性。

## 网络与生命周期

`nt26_network`在安装／复用GPIO ISR服务后创建持久 `UartEthModem`，先注册回调再异步Start。沿固定PSRAM配置，把驱动有界TX池（默认队列32＋2槽）及重组区放PSRAM，UART DMA仍由驱动单独管理，内存余量须目标构建和实机测量。

地址就绪依据驱动Connected回调与原子IsInitialized状态；固定0.7.0中Connected来自IP事件，或网络恢复时同时满足保留IP和数据链路就绪。断线、SIM错误、注册丢失和模块复位等会清除就绪标志。应用不借用GetNetif返回的裸指针，以免驱动重置恢复时销毁／重建netif造成跨任务悬空访问。

不从回调中发AT命令、Stop或析构。持久对象在部分失败后继续保留，由重启重试；main沿用最多60秒地址等待。驱动自己的事件组分配失败会abort，`new(std::nothrow)`只处理对象存储失败，不能据此宣称驱动所有资源失败都已改成可恢复错误。驱动IP回调没有过滤netif来源；当前profile只创建一个ETH接口，热点使用Wi-Fi AP，未宣称多个Ethernet接口共存。

热点配网页仅要求服务地址、保留既有SSID／密码；配置完成后通过NT26联网，在OLED显示绑定码，绑定仍先于音频初始化。没有实现自定义APN、SIM PIN或射频实验页面，使用驱动默认PDP流程，运营商与模块固件条件须实测。HTTP绑定、媒体下载、WebSocket与OTA沿现有网络栈；`control_protocol.cpp`、OTA和无屏配对实现保持本批前摘要，未改人工决策。

## 实际执行记录

NT26真实预检：

```powershell
& 'C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe' -B tools/prepare_cached_firmware_build.py --profile bread-compact-nt26 --destination I:/xy_board_builds_20261109/nt26_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6
```

退出1：`Required component is not installed: 78/uart-eth-modem; nothing was staged`。没有目标构建目录、目标编译、链接或镜像。SDK版本阻塞由精确驱动manifest直接证明；此次预检在缺组件时提前退出，并没有进入SDK6编译。日志和摘要见 `native_dependency_preflight_20261112_nt26.json`。

已授权缓存回归的实际命令均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=bread-compact-wifi reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

| 回归目标 | 镜像身份 | app字节／3MB分区 | 结果 |
| --- | --- | --- | --- |
| 默认OLED32 Wi-Fi | assistant_oled32 | 2,157,056 | 构建、链接、镜像校验通过 |
| 微雪RLCD4.2 | assistant_wsrlcd42 | 2,228,000 | 构建、链接、镜像校验通过 |

环境：本机既有IDF5.5.6-dirty、17个组件override、offline_guard和构建并发度2；未下载构建依赖。两镜像model均2,203,819字节／4MB；各205个main文件集合与最终工作区一致，212项输入摘要一致，21份产物归档。`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0，检查独立应用名、16MB、双OTA／NVS／model布局与镜像校验。OLED的SSD1306／数字音频和RLCD的TE／帧缓冲仍链接；选中源码和ELF排除NT26／RNDIS网络实现。**NT26驱动、SDK6、UHCI、ISR、PSRAM网络池、回调和仅服务地址POST分支没有被这些旧板构建覆盖。**

正式记录为 `cached_build_attempts_20261112_nt26_*.json`。脚本、来源、构建日志／镜像／ELF／分区、最终diff和审计见 `evidence/nt26-source-20261112/`。`git diff --check`退出0，9个保护文件保持SHA256。未运行测试套件、安装依赖或SDK、写Git、刷机、OTA、上传或发布。

## ML307后续范围

本批还核对8个尚无本地入口的蜂窝变体：ATK BOX2 4G、面包板ML307两种OLED、星智0.85 TFT／0.96 OLED／1.54 TFT、正辰1.54 TFT／CAM ML307。各自配置与类实现已进入研究矩阵，仍是inventory_only，不按Wi-Fi别名计为适配。

精确 `78/esp-ml307 3.7.5` 对应提交 `506477b5af639f5dce9386e423e939091fe7fa11`，提供NetworkInterface的CreateHttp／CreateTcp／CreateSsl／CreateWebSocket等AT连接接口。它与当前ESP-IDF HTTP／WebSocket API不同；后续必须打通绑定、TLS、二进制音频、控制消息、媒体读取和OTA，不能只在启动时等待模块注册成功。固定板型的电源、屏幕、编解码器和串口也必须逐一核对，不能按名称复用NT26驱动。

当前117个官方变体有源码入口，另1个历史profile，共118个本地入口；71个官方变体待源码。累计64项曾缓存构建通过；当前输入2 passed、62 stale、6历史failed、118 not_run，其中47项已有源码。全部188项本轮实机not_run；只有现有微雪1.8实物，不能代替NT26／ML307验收。全量适配目标继续保留，未把本批源码候选当作整板完成。


后续SDK预检更新：准备工具已在创建目录前核对单板IDF版本约束。另一次真实NT26预检以新目录运行，先报SDK5.5.6不满足>=6.0.1,<6.2.0，退出1且无stage；原有缺组件日志保留。普通OLED32实际暂存成功说明SDK匹配目标可以继续走组件阶段；没有新增NT26构建。见[SDK预检与ML307传输核对](cellular_transport_readiness_20261112.md)。
