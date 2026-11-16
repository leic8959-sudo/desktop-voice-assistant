# LilyGO T-Display S3 Pro MVSRLoRa 源码适配

后续更新：微雪S3LCD3.5原版与LilyGO MVSRLoRa已移除各自外部ST7796依赖，保留原17条专用／12条默认表和复位120ms，并分别完成首次整板构建。3.5B的AXS QSPI分支未构建，见[本批记录](st7796_sdk_20261112.md)。

日期：2026-11-10，Asia/Shanghai。新增官方默认Wi-Fi变体`lilygo-t-display-s3-pro-mvsrlora`的基础小燕子终端源码候选；不是LoRa无线功能或整板功能完成声明。仅现有微雪1.8可供实机验收，本板尚未刷机。

## 固定来源

- 小智v2.5.2：`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，`main/boards/lilygo/t-display-s3-pro-mvsrlora/`的7份配置、板类、音频及README文件。
- [厂家固定提交](https://github.com/Xinyuan-LilyGO/T-Display-S3-Pro-MVSRLora/tree/3457ffc0f9569858891d3692ce52f800947bd0c4)：README、引脚头、V1.0原理图，以及CST226SE例程、DriveBus触摸实现／寄存器头。原理图已渲染并查看麦克风、功放、供电部分；GPL文件只读参考电气事实，没有复制实现或整份头文件。
- [SensorLib固定CST226实现](https://github.com/lewisxhe/SensorLib/blob/43120dee04f3054e2ce4fe9715e46a82557dee2e/src/touch/TouchDrvCST226.cpp)及[TouchLib寄存器说明](https://github.com/mmMicky/TouchLib/blob/ccaedcd9155ef8a6560ae9f594ea92e32c68020f/src/REG/CSTMutualConstants.h)，交叉核对报告布局、事件及低四位。使用的是本板8位0x00寄存器路径，未混入通用TouchLib的16位0xD000总线命令。
- 21份实际来源URL、SHA256及快照在`evidence/mvsrlora-source-20261110/source-artifacts.json`；证据目录按项目已有设置忽略。局部MIT解析来源及修改说明保留在`main/third_party/mvsrlora_touch_protocol/`，整体LICENSE未改。

## 屏幕、触摸和交互

ST7796 BGR222×480，SPI3 mode0／40MHz，MOSI17／CLK18／CS39／DC9／RST47；invert=true、mirrorX=true、不交换XY，显存窗口gap49/0。复用现有SPI显示实现和LVGL，两份20行DMA缓冲、RGB565字节交换。背光48高有效，5kHz／10位PWM，先暗再由现有界面亮度逻辑恢复。新增222×480布局，卡片操作按钮高44px，BOOT0初始释放后提供说话／卡片输入。

CST226SE在0x5A，I2C0 SDA5／SCL6、100kHz。reset13按厂家序列高100ms／低10ms／高200ms。GPIO21同时出现在触摸、SY6970、RTC、IMU中断定义中，仅作为输入，不绑定独占触摸中断；读取走LVGL轮询。

本地每次从0x00读取有界7字节首触点报告，并检查第6字节0xAB、按钮标志、原始7位点数和按下事件0x06；只接受单点。读取成功后执行检查返回值的`00 AB`同步写入，读取或同步失败均取消当前按压。没有把厂家实现中的错误返回值丢弃，也没有把通用驱动截成1点后的结果误当单指。

固定应用的X公式遗漏低四位右移；厂家DriveBus表达式还存在运算符优先级歧义。本地用已交叉核对的`(high << 4) | (shared_low >> 4)`解码X，用低半字节解码Y。触摸按显示镜像使用`221-x`，不把显存gap49从触摸坐标中扣除；超出222×480直接取消，不夹到屏幕边缘。原生触摸范围与该镜像映射仍需实物标定。

初始按下、多指、按钮报告、格式异常、越界及读取／同步失败均不会生成新点击；错误后必须读到带有效标记的零触点报告才重新接收按压。仅回显同步token不算物理释放。没有启动原厂“任意触摸切聊天”的第二条任务，没有使用会合成错误释放的默认值。

## 双麦克风、播放和功放

厂家扩展板原理图显示两只MP34DT05-A共享CLK1／DIN2，LR电阻分别拉高和拉低，供电都由GPIO3控制的P沟道MOSFET切换，低有效。这两个通道是两只麦克风，不能把第二只当作播放参考。基础终端保持两麦供电，从PDM默认LEFT相位取单声道，对应图中MIC2的LR接地R19；MIC1的R10接VDD处于另一相位。图上物理Left／Right标注与I2S槽位名称不等价，实际相位和灵敏度仍需测量。未实现双麦阵列、声源定位或硬件回声参考。

采集复用现有数字音频后端，I2S0 PDM32kHz、64倍时钟2.048MHz，再走既有32→16kHz FIR。不套用T-CameraPlus的20倍软件放大。

播放为MAX98357A，I2S1 BCLK4／WS15／DIN11，16kHz／16位立体声，使用现有固定小缓冲把mono复制到左右。沿用上批已核对的MAX98357限制，修正固定应用“配置24kHz、构造实际11025Hz”的无效组合；网络24kHz经既有FIR降为16kHz，同一实际播放PCM进入软件AEC参考。

GPIO41同时连接升压EN和MAX的SD_MODE电阻网络，高有效。板级启动先锁存低，再配置输入输出并等1ms读取实际pad电平；运行期写入通过100ms互斥锁。复用dummy codec但PA pin设NC，避免另一路GPIO写入；板级包装在静音／关闭／禁用时先拉低41，解除静音成功后才拉高，失败会尝试再次关闭并返回错误。实际PCM静音仍发送零数据。没有以codec_dev1.5.11丢弃的mute回调返回值伪报已应用，公共音频入口另行调用检查数字功放包装结果。

GPIO输入读回只能验证pad状态，不能证明5V升压电压、MAX静音电平或没有爆音。新板级包装、PDM相位、输出音量和声学均未实测。

## 供电和范围

RT9080 GPIO42按原理图保持高；先让主供电稳定20ms，再将MIC3拉低并等待10ms。SD CS14和LR1121 CS7在SPI初始化前保持高，避免未选中的共享总线器件响应；没有初始化SD或LoRa射频。电机45、LoRa reset10／IRQ40／BUSY46、RTC和IMU均未接管。

16MB Flash／Octal PSRAM采用发行默认和厂家README的16MB／OPI配置；实物容量未验收。复用根16MB双3MB OTA槽和4MB模型分区，保留现有NVS范围。

未实现：LoRa无线通信、RTC、IMU、振动、SD存储、SY6970电池／ILIM／ADC／看门狗／BATFET策略、睡眠／关机、原厂工具。没有新增发布清单不存在的NO_BATTERY变体，也没有复制固定代码的整字节充电策略写入。充电芯片默认行为、供电持续性仍待实物验收。

## 实际预检与公共回归

新目标实际命令：

```text
python -B tools/prepare_cached_firmware_build.py --profile lilygo-t-display-s3-pro-mvsrlora --destination I:\xy_board_builds_20261109\mvsrlora_preflight_20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1，缺少`espressif/esp_lcd_st7796=1.4.0`，未创建stage、未编译或产生新目标镜像。记录为`native_dependency_preflight_20261110_mvsrlora.json`。没有用ST7789代替屏控，没有安装依赖。

旧T-CameraPlus V1.2（PDM32采集／16k立体声播放）及敏思K08（I2S16采集／32位槽播放），同步全部main文件、注册表和defaults并刷新变更时间戳后，分别实际执行：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lilygo-t-cameraplus-s3-v1.2 reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=minsi-k08-dual reconfigure build
```


| 旧入口 | 最终缓存build | app字节 | 应用槽余量 | 模型字节 |
| --- | --- | --- | --- | --- |
| lilygo-t-cameraplus-s3-v1.2 | exit0 | 2,230,032 | 915,696 | 2,203,819 |
| minsi-k08-dual | exit0 | 2,207,184 | 938,544 | 2,203,819 |

两旧入口初次和最终同步后增量构建均退出0；两份镜像checksum／validation hash、ELF符号／段大小、二进制分区解码均退出0。每份186个main文件集合和193份输入摘要与最终工作区匹配，独立核对编译宏／选中源／应用身份／QUAD或OCT PSRAM／16MB容量／双OTA槽及模型分区。新mvsrlora_board和专用mute包装不在旧目标选中源／符号中。SDK仍为既有5.5.6-dirty，保留HEAD880a203ae603d08a94d06aa01e0d6761958e5e9e及原NimBLE／idf_as_lib改动，没有安装或升级。这两个旧目标只验证公共路径，没有选择`mvsrlora_board.cpp`，也没有编译MVSRLoRa专用的CST、显示、GPIO41或mute包装分支。新文件复制进stage及其摘要匹配不能替代目标编译。

镜像检查为`python -B -m esptool --chip esp32s3 image_info --version 2 <app.bin>`；ELF执行`xtensa-esp32s3-elf-nm.exe -S --size-sort -C <app.elf>`和`xtensa-esp32s3-elf-size.exe -A -d <app.elf>`；分区使用SDK `gen_esp32part.py <partition-table.bin>`解码。每条命令实际参数／日志／退出码及产物摘要记录在`cached_build_attempts_20261110_mvsrlora_<旧profile>.json`，没有伪造新目标通过记录。

未执行测试套件／独立类型检查，没有修改SDK、Git写操作、上传、发布、刷机或OTA。仍需驱动依赖、新目标真实构建，以及屏幕／触摸／供电／音频／网络绑定／提醒／OTA实机验收。
