# Maza ESP32-S31 A01电子纸源码候选（2026-11-12）

新增 `maza-ai-esp32-s31` 独立源码入口，复用已有Maza A01 2.9寸电子纸的刷新任务／文本布局，按S31实际引脚接入电源、按键和单ES8311。**本机没有IDF6.1，S31目标未编译、未实机验收。**

## 来源与引脚

采用小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的Maza S31 README、config.h、config.json和实际类，以及同提交的共享EpdDisplay／EpdDisplay_2_9四文件，按Git blob和SHA256归档。上游将本机型标为preview并要求IDF≥6.1。

| 项目 | Maza S31候选 |
| --- | --- |
| 屏幕 | 默认A01 2.9寸，128×296面板RAM；296×128横屏文本UI |
| SPI | SPI3 mode0／40MHz，MOSI33／CLK34／CS17／DC35／RST14／BUSY13 |
| 电源／功放 | POWER6高，PA2高有效；启动先将PA关闭 |
| ES8311 I²C | I²C1 SDA3／SCL0 |
| I2S0 | MCLK7／BCLK11／WS9／DIN10／DOUT8，单声道16bit／24kHz |
| 按键 | BOOT1、离散触摸键12、音量加4／减61 |
| 未驱动 | 普通LED5、电池／低功耗／软件断电、SD、并口及其他FPC屏幕 |

原板实际创建I²C1，总线句柄传入音频时端口却写I²C0。候选统一使用已创建的I²C1，未重复创建总线。单ES8311复用既有checked data／codec实现，BOTH模式、一个IN_OUT设备，共享句柄失败清理只释放一次；实际set_fmt／enable、麦克风增益和DAC静音回调错误传播到公共音频状态。PA2为INPUT_OUTPUT保留GPIO读回，codec PA设NC，数字静音成功后才开PA。

按键继续先等完整松开，多键或跨键取消；BOOT和离散触摸键用于按住说话／卡片选择确认，音量键沿已有重复与步进逻辑，不照搬上游长按直接设最大音量。它没有坐标触摸控制器。保留mn7“你好，小燕子”、软件PCM AEC、Wi-Fi绑定、WebSocket媒体／真实回执及同板OTA。

应用身份 `assistant_maza31`，目标esp32s31、SDK范围 `>=6.1.0,<6.2.0`，沿已有S31芯片配置CPU320MHz／OCT PSRAM200MHz。16MB Flash是固定源码体系的默认配置和当前分区候选，未测实物容量，不把它当作型号全部修订的容量保证。NVS保留范围和根分区未改。

## 电子纸复用范围

两代板类均使用同一 `EpdDisplay_2_9`；固定源码的C221 25C全刷／局刷各30字节，与现有 `maza_panel_commands.h` 逐字节相同。候选只增加S31引脚和I²C端口分支，不改波形、命令或布局。像素(x,y)仍映射到RAM(y,295−x)，有效传输4,736字节；上游分配5000字节不作为有效面板RAM长度。

沿现有有界刷新路径：LVGL短回调转换整帧，保存一个待刷最新帧，独立worker执行SPI／BUSY等待，不持有LVGL锁；SPI入队／完成各1秒、BUSY10秒、最短刷新间隔1秒、每20次局刷转全刷。超时停止并保留可能仍被DMA使用的事务和缓冲。pending／converted／shown各4,736字节，DMA帧另4,736字节，全屏RGB565为75,776字节，外加任务／互斥资源；内存和耗时未在S31测量。

文本卡片、配对码、提醒／计时、DND未读和呈现完成回执复用原实现。LVGL flush结束不等于墨水屏呈现完成，不能提前报已读。实际方向、25C以外温度、残影、BUSY极性及断线／提示音／卡片交互仍需A01实物验证，不泛化为所有FPC或并口屏幕已适配。

146个旧profile对象保持原值；146项旧音频和5项旧纸屏本地宏条件文本比较一致。旧纸屏的 `bus.i2c_port=I2C_NUM_0` 改由其已有 `BSP_I2C_NUM` 提供相同值，比较时显式记录此归一化。该检查不展开SDK／include，**不是目标预处理或编译**。控制协议人工决策、OTA、配网、唤醒、display_ui、Maza波形、headless_ui和CoreP4显示共八保护文件不变。

## 实际执行与限制

新目标真实只读预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile maza-ai-esp32-s31 --destination I:/xy_board_builds_20261109/mazas31_review_maza-ai-esp32-s31_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

退出1：SDK5.5.6不满足 `>=6.1.0,<6.2.0`，未创建stage、解析完整依赖或生成S31镜像。没有安装SDK或组件。其他机型预检沿各批历史记录，本轮没有将它们记为重新执行。

两项已授权旧S3缓存回归实际终态均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app2,263,824字节、RLCD2,228,000字节，各3MB；model均2,203,819字节／4MB。各230个main文件、238输入摘要匹配最终工作区，23产物归档。实际image_info、nm、size和SDK分区解码均退出0，核对身份／16MB／NVS／双OTA／model及codec、FT或TE符号；使用既有IDF5.5.6-dirty、17个override、offline_guard、每stage并发度2。**这些S3构建未编译Maza S31分支，也未使用SDK6.1。**

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0。`close_batch.py`核对17份来源、波形字节、既有profile／保护摘要、151项局部条件文本、目标预检日志、最终快照和构建产物；`git diff --check`退出0。完整证据位于 `evidence/maza-s31-source-20261112/`。

当前146官方源码入口＋1历史，共147本地profile；42官方变体待源码。S31四项中2候选／2待实现。累计64官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（76有源码）；188项本轮实机全not_run，现有微雪1.8不能替代Maza S31验收。C3／C5／C6自定义唤醒词取舍仍待用户选择，本批未改其功能或登记空壳入口。

未运行测试套件、安装依赖／SDK、启动Docker、写Git、刷机、OTA、上传或发布；全量目标继续进行。


后续控制台纠正（S31 Korvo批次）：SDK确认GPIO33／34与USB Serial/JTAG共用，而本板EPD使用MOSI33／CLK34。当前板级defaults已显式选择UART并关闭USB主／副控制台和USJ_ENABLE；本页前文为该批历史记录，未补写新的S31编译或实机证据。四项精确变更与来源见[Korvo批次记录](s31_korvo_adaptation_20261112.md)。
