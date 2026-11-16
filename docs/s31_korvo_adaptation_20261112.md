# ESP32-S31-Korvo-1源码候选（2026-11-12）

新增 `espressif-esp32-s31-korvo-1` 独立源码入口，覆盖RGB屏、GT1151、ES8389及S31 ADC按键读取。固定上游将机型标为preview；**目标尚未编译或实机验收**。S31四款已有源码候选，不代表四款均能刷写或完整运行。

## 固定来源和板级差异

采用小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的Korvo V1.1目录及实际 `Es8389AudioCodec`，并读取固定IDF6.1提交 `f0114250e205e33a945e2f3e7ec40085c5a029a2` 的RGB／ADC公共接口。manifest范围 `>=6.1.0,<6.2.0`，应用身份 `assistant_s31korvo`。原板README称16MB Flash／16MB八线PSRAM，候选沿OCT200MHz／CPU320MHz；实物修订和容量待确认。

| 模块 | 候选配置 |
| --- | --- |
| I²C0 | SDA0／SCL1，codec与触摸共用 |
| I2S0 | MCLK2／BCLK3／WS4／DIN6／DOUT5 |
| 功放 | GPIO7高有效，INPUT_OUTPUT保留读回，启动先关闭 |
| 用户键 | BOOT61＋GPIO42／ADC1通道0，0dB衰减 |
| RGB屏 | ST7262E43，800×480，16bit RGB565，16MHz PCLK负边沿 |
| RGB同步 | HS44／VS45／DE43／PCLK40／DISP38 |
| RGB数据 | D0..15=8..19、33..36 |
| 触摸 | GT1151，共享I²C0／400kHz，reset／IRQ NC |

两份RGB565帧共1,536,000字节PSRAM，保持原板8行bounce，两份内部缓冲共25,600字节。H和V时序均同步4／后肩8／前肩8，沿原板双帧及项目已有RGB direct LVGL／bounce／avoid_tearing接法。实际内存峰值、欠载、闪烁、颜色及并发网络负载仍需目标测量。

背光硬件常供电，没有PWM亮度脚。候选用DISP38切换面板画面使能，非零设置均为同一开状态，日志区分请求百分比与实际scan_enable；**这不等于可调亮度或背光真正断电**。熄屏／启动错误时关闭面板使能并切PA，不关闭供电、联网或媒体后台。

GT1151精确1.0.1参考包已归档，未安装。复用此前LCD EV SUB2核对过的公开报告结构：状态＋单点8字节＋键／校验，共11字节，检查触点数和校验并传播清报告失败；多点、读错和越界取消并等待松手。初始化和800×480触摸方向仍未实机验收。

## ES8389声音路径

使用现有ES8389 BOTH后端，共用codec对象、分别创建IN和OUT设备；单声道16kHz输入、双声道16kHz输出。播放边界复用已有有界PCM复制，将每个单声道样本同时写左右声道；保留单份软件AEC参考，不把复制立体声解释为独立左右声道媒体源。

codec从BCLK恢复时钟，use_mclk=false，ESP为时钟master、codec为slave；MCLK2仍按原板类驱动。codec默认地址常量为0x20（codec API形式），不把README中的地址文字直接当作已测七位地址。PA设NC，由GPIO7路径在真正数字静音回调成功后开启；关闭时先切PA，保留TX时钟和原板共享codec生命周期。输入增益40dB，使用实际mic回调返回；24kHz媒体经既有采样转换进入16kHz物理输出。

保留mn7“你好，小燕子”、PCM软件AEC、Wi-Fi绑定、WebSocket媒体／真实回执和同板OTA。单麦采集不宣称双麦波束形成，也不宣称硬件播放参考已验证。时钟恢复、输入选择、输出复制、静音／重开、采样与声学效果均待硬件验收。

## ADC按键与范围边界

按固定来源原字节保存Apache许可的 `esp32_s31_adc_calibration.c/.h`，包含17位权重、7种配置×2种极性×256次采样和五轮权重修正；临时样本堆约14,336字节，成功／失败路径均释放。该实现调用S31内部校准寄存器函数，必须在目标SDK链接／执行后才能证明可用，不能用宿主计算代替。

运行时四次采样平均后做软件校准。板级电压中心380／820／1340／1870mV，idle2000mV；不对反向原始值套用常规线性公式。

| 输入 | 候选动作 |
| --- | --- |
| 160≤电压＜600mV | 音量加 |
| 600＜电压＜1080mV | 音量减 |
| 1080＜电压＜1605mV（MODE） | 按住说话／卡片操作 |
| 电压＞1935mV且≤2000mV | ADC键松开 |
| BOOT61单独按下 | 按住说话／卡片操作 |
| SET窗口、边界、读错、其他电压 | 取消并等松手；SET重配网未接入 |

连续三次稳定读取后才改变有效键状态，短暂抖动保持此前有效状态；启动必须先确认所有键松开。BOOT和ADC键重叠、跨键或错误会取消。**任意两个ADC键同时按下可能产生某个单键的正常电压，软件不能据此保证识别全部组合**，需按实际电阻网络验收。没有让SET键静默重置凭据、切网络或中断后台任务。

## 发现并纠正的控制台冲突

精确SDK的gpio_ll明确指出GPIO33／34切为IOMUX时会关闭USB Serial/JTAG。Korvo的RGB数据线和此前Maza S31纸屏的MOSI／CLK占用这两个脚，继承项目USB控制台会发生冲突。

本批为两款候选显式选择UART控制台，关闭USB主／副控制台及USJ_ENABLE。Maza仅改这四项板级defaults并追加研究纠正记录，原profile对象及其他引脚不变；此前Maza批次的快照和构建记录保留为历史证据，不重写成新执行。UART接线、下载口和控制台仍待实机验收。

## 实际执行与未完成项

Korvo实际只读预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp32-s31-korvo-1 --destination I:/xy_board_builds_20261109/s31korvo_review_espressif-esp32-s31-korvo-1_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

退出1：SDK5.5.6不满足 `>=6.1.0,<6.2.0`。未创建stage、解析完整依赖、编译内部ADC函数或生成目标镜像。GT1151／SDK和传递依赖仍待准备，参考源码包未安装。

两项已授权旧S3缓存回归实际终态均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app2,263,824字节、RLCD2,228,000字节，各3MB；model均2,203,819字节／4MB。每项240个main文件、248输入摘要与最终工作区匹配，23产物归档。实际image_info、nm、size和SDK分区解码均退出0，核对身份／16MB／NVS／双OTA／model及codec、FT或TE；使用既有IDF5.5.6-dirty、17个override、offline_guard、每stage并发度2。**未编译Korvo或使用SDK6.1，也不证明Maza UART纠正后已能在S31启动。**

148个旧profile对象保持原值，148项旧音频本地宏条件文本比较相同；此脚本不展开include／SDK，不是目标预处理或编译。控制协议人工决策、OTA、配网、唤醒、display_ui、纸屏和Mosaico板级／显示八保护文件摘要未变。

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0。`close_batch.py`核对21份来源、原始校准代码、RGB缓冲配置、按键阈值／稳定状态、Maza四项纠正、旧profile／研究／保护摘要、预检、最终输入和产物；`git diff --check`退出0。完整证据位于 `evidence/s31-korvo-source-20261112/`。

当前148官方源码入口＋1历史，共149本地profile；40官方变体待源码。S31四款均为源码候选，全部待目标构建和实机验收。累计64官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（78有源码），188项本轮实机全not_run；唯一现有微雪1.8不能代替Korvo。

OV3660相机、WS2812灯37、SD、USB host、电池及原厂手势未实现，SET重配网和背光PWM亦未交付。未运行测试套件、安装SDK／依赖、启动Docker、写Git、刷机、OTA、上传或发布；全量目标继续进行。
