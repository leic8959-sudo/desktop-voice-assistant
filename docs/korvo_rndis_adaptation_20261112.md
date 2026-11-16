# Korvo2 V3 RNDIS 基础终端适配（2026-11-12）

牛牛要求全量纳入官方清单、分批适配。本批给 `espressif-esp32s3-korvo2-v3-rndis` 增加实际 USB RNDIS 网络源码入口。**目标缺依赖，尚未编译，更没有实机联网验收。**普通 Korvo2 和微雪反射屏的缓存回归通过只证明它们选择的公共 Wi-Fi 分支。

## 来源与硬件范围

小智固定版本 v2.5.2，提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。实际发布清单是154个目录、188个变体，README中的138／171已过时。

- 独立应用身份 `assistant_korvo_rndis`，ESP32-S3、16MB Flash、Octal PSRAM。
- 固定 Kconfig 默认 ST7789 280×240，复用现有 Korvo2 的屏幕、电源掩码读改写、ADC1CH4 校准按键、ES8311＋ES7210 TDM4 MIC1 24kHz和PCM软件AEC实现。ILI9341、触摸、相机、SD、电池、充电、休眠和原厂 MUTE／PLAY／SET 行为没有接入。
- USB OTG使用原生GPIO19／20，控制台切到UART0，关闭主／次USB控制台；目标源码增加编译期约束。外部模块的接口接线、VBUS和供电仍须实物核实。
- 精确声明 `espressif/iot_usbh_rndis: 0.3.1`。该组件登记页关联 `espressif/esp-iot-solution` 提交 `a558bfb49d2b61e4361e748d446b20f1b5f93c65`，包括 CDC、IOT_ETH 等传递依赖。参考源码仅归档于 evidence，没有安装或链接为本地依赖。

29份参考来源及SHA256见 [source-artifacts.json](../evidence/korvo-rndis-source-20261112/source-artifacts.json)，包含固定板级／共用网络源码、厂商依赖和现有SDK快照。SDK为本机5.5.6-dirty，不能称为官方干净发行环境。

## 实现行为

`device_network`提供启动、地址就绪和网络名称三个入口。默认板保留原Wi-Fi实现；RNDIS profile选择独立 `rndis_network.cpp`。启动最多等待地址60秒，失败显示错误并停止后续流程，不在构造函数永久等待，也不擦除NVS。配置页仍通过设备热点访问，RNDIS页只要求服务地址，保存时保留既有SSID和密码。

RNDIS使用持久USB设备匹配表、driver、ETH句柄和netif，建立 `XY_RNDIS` DHCP客户端。ETH事件按句柄过滤，地址事件按netif过滤；拔出后清除就绪标志，重新插入必须收到新DHCP事件。SDK5.5.6的 `IP_EVENT_ETH_LOST_IP` 同样传递 `ip_event_got_ip_t`，已按实际源码修正解析。初始化失败保留已分配的持久状态，需重启重试，避免释放回调仍在使用的对象。

厂商0.3.1驱动在install阶段就启动USB检测。为避免预先插入的模块在netif准备前上报连接，先安装USB host但保持root port关闭，运行USB事件任务，安装CDC（跳过重复host初始化），随后安装RNDIS／netif／glue、注册回调并start ETH，最后启用root port。阻塞等待只存在于专用事件任务；主启动流程保留60秒界限。此时序已静态核对SDK和厂商源码，尚未目标编译或物理验证。

USB模块必须自行提供已联网、可分配DHCP地址的RNDIS接口。本批没有AT拨号、SIM或APN配置器，不能把“支持RNDIS接口”当作“所有4G模块即插即用”。DNS、HTTP绑定、WebSocket重连和OTA复用原网络栈。`control_protocol.cpp`含已有人工决策，本批保持字节不变；其中Wi-Fi性能模式逻辑仍作用于已初始化Wi-Fi，须在新目标核实日志和运行影响。

无屏板继续采用牛牛确认的手机热点配网页读取六位绑定码方案。现有 `headless_ui`、热点会话限制、绑定码剩余时间和音频初始化前绑定顺序均保留，本批没有更改这些实现；该流程也仍需实机验收。

## 实际验证与限制

新目标真实执行过以下预检，退出1：

```powershell
& 'C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe' -B tools/prepare_cached_firmware_build.py --profile espressif-esp32s3-korvo2-v3-rndis --destination I:/xy_board_builds_20261109/korvo_rndis_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6
```

结果：`Required component is not installed: espressif/iot_usbh_rndis; nothing was staged`。没有目标stage、目标编译、链接或镜像。预检原始日志与摘要在 `native_dependency_preflight_20261112_korvo_rndis.json`。完整依赖解析未运行，由牛牛准备依赖，不自行安装。

已授权的两项旧目标缓存回归均退出0，实际命令为：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=espressif-esp32s3-korvo2-v3 reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

环境及复制输入由 `evidence/korvo-rndis-source-20261112/xy_korvo_rndis_regression_20261112.ps1` 记录，复用各自独立缓存stage和17个既有组件override，`offline_guard`禁止新依赖下载，构建并发度2。没有运行测试套件。

| 回归目标 | 镜像身份 | app字节／3MB分区 | model字节／4MB分区 | 结果 |
| --- | --- | --- | --- | --- |
| 普通Korvo2 Wi-Fi | assistant_korvo2 | 2,260,384 | 2,203,819 | 构建、链接、镜像校验通过 |
| 微雪RLCD4.2 | assistant_wsrlcd42 | 2,228,000 | 2,203,819 | 构建、链接、镜像校验通过 |

两目标各203个main文件集合一致、210项输入摘要与最终工作区匹配、21份产物归档。`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0。确认16MB镜像、独立应用名、双app／NVS／model分区和校验有效；Korvo2的ADC校准／按键／双codec，以及RLCD的TE与帧缓冲仍链接。源码选择和ELF明确排除RNDIS分支，不能据此宣称新USB事件／UART控制台／仅服务地址POST已编译。

`git diff --check`退出0。8个保护文件保持原SHA256。命令、来源、输入和产物清单以及最终diff见 `evidence/korvo-rndis-source-20261112/`；正式构建记录在 `cached_build_attempts_20261112_korvo_rndis_*.json`。

## 当前总量与后续验收

当前116个官方变体有源码入口，另1个历史profile，共117个本地入口；72个官方变体待源码。累计64个官方变体曾缓存构建通过；按当前输入，2项passed、62项stale、6项历史failed、118项not_run（其中46项已有源码）。全部188项本轮实机not_run。微雪1.54电子纸V1受限实验已撤回，继续保持inventory_only／not_run，不计入累计通过。

RNDIS后续需要完整依赖、专属目标构建及实物验收：预先插入／延迟插入／拔插、DHCP与地址丢失、DNS／默认路由、模块供电与APN、UART日志、屏幕／按键／音频／唤醒／绑定／提醒／媒体和同板OTA。现有实物只有微雪1.8，不能替代Korvo2 RNDIS验收。无Git写入、依赖安装、刷机、OTA、上传或发布。
