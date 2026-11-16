# DFRobot K10 源码适配与厂家音频核对

后续更新：本页保留此前状态快照；Freenove2.8与K10现已移除外部ILI9341依赖并分别完成首次整板构建。当前依赖967条声明、70本地/69官方满足直接条件。最新来源、失败修正和验收边界见[本批记录](ili9341_default_sdk_20261112.md)。

日期：2026-11-10，Asia/Shanghai。新增官方变体`dfrobot-df-k10`的基础小燕子终端源码候选。目标依赖预检缺ILI9341，K10尚未编译或实机验收；仅现有微雪1.8实物可用。没有宣称相机、传感器或原厂全部手势完成。

## 固定来源和误差纠正

- 小智v2.5.2固定提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，`main/boards/dfrobot/df-k10/`的配置、板类、K10音频实现及README。
- [DFRobot Arduino框架固定提交](https://github.com/DFRobot/framework-arduinounihiker/tree/9fabfd6d6324dbb304ad47947675baf7cf1ab29e)：K10引脚头、`unihiker_k10`源码／头、板级`initBoard.h`、静态`libmodules.a`等。
- [厂家PlatformIO板型](https://github.com/DFRobot/platform-unihiker/blob/508e31875ad92205bf946e28570fb78fc01ceb2e/boards/unihiker_k10.json)，以及[厂家MicroPython构建说明](https://github.com/DFRobot/K10_Micropython/blob/394674d7d14c178455276fea6fdf99e5cb8e450c/README.md)。
- 21份来源URL、SHA256及快照在`evidence/k10-source-20261110/source-artifacts.json`；证据目录按项目已有设置忽略。没有运行上游脚本或厂商二进制，没有安装、链接或烧录厂商框架。

固定小智配置把输入命名为ES7210，但实际创建的是ES7243E ADC，输出也没有创建ES8311 codec。不能套入普通ES7210＋ES8311双codec路径。固定实现的TX MCLK字段被注释，结构体零初始化会留下GPIO0，而GPIO0又是BCLK；本地显式使用厂家MCLK3。

厂家Arduino `UNIHIKER_K10::initI2S`采用标准双声道16位／16kHz，MCLK3、BCLK0、WS38、DIN39、DOUT45，与小智24kHz／四槽TDM混合路径不同。本地采用厂家标准格式，复用既有数字音频后端，并单独创建ES7243E输入codec。

### 地址和扩展IO的静态证据

厂家公开框架里板级初始化只有头文件，实现在静态库。使用既有Xtensa `nm -A -C`、`ar t`、`ar x ... initBoard.c.obj`和`objdump -dr -C`仅作只读符号／反汇编检查；没有执行、链接或安装库。原始输出和审计在`manufacturer-static-audit.json`及`manufacturer/modules-*.log`。

`init_board`先给ADC初始化传入十进制21（7位0x15），失败后尝试十进制17（0x11）；`es7243e_write_reg`在发I2C地址字节前把这个参数左移1位。因此这些是7位地址。当前codec_dev1.5.11的控制接口相反，接收8位表示并右移1位；本地对0x15／0x11分别传0x2A／0x22。固定小智字面量0x23在该SDK中实际访问0x11，不能把它误说成7位0x23，也不能因此断言一定无法访问所有K10修订。

本地只探测厂家给出的0x15和0x11，恰好一个ACK才选择；两者都应答、都无应答、超时或其他错误均停止。地址应答仍不等于芯片身份或实物修订已验证。

厂家`initBoard.h`的扩展IO枚举明确为P0=LCD_BLK、P1=Camera_rst、P2=KeyB、P12=KeyA、P15=Amp_Gain；其Arduino引脚头和Button初始化与之相符。固定小智A/B标签与厂家相反。本地按厂家P12／P2识别实体键。P15是功放增益预设，不作为独立PA使能；厂家begin把它设低，本地保持这个预设。

## 板级IO与交互

I2C1 SDA47／SCL48，TCA9555在0x20。新增`k10_board.h/.cpp`，读实际输出／方向寄存器，使用既有掩码RMW及读回检查，只接管P0背光、P15增益和P12／P2输入方向／极性。背光先低，增益P15保持低；没有使用会复位整端口的扩展器构造函数，也没有复制固定板类对P0／P1一起拉低的相机复位动作。其他扩展IO位和P1保持原状态。

A/B两键均接入现有说话／卡片操作，初始必须释放，双键组合、读取错误取消当前输入，恢复后重新等待释放。GPIO0为I2S BCLK，`listen_button=NC`并关闭屏幕说话入口选择，专用键读取函数处理扩展器，不配置GPIO0按键。原厂短按切聊天／长按调音量手势未完整移植，不把“按住说话”冒充长按音量。

## 显示

ILI9341 BGR240×320竖屏，SPI3 mode0／40MHz，MOSI21／CLK12／CS14／DC13／RST NC；invert=false、mirrorY=true、不交换XY、gap0。复用现有SPI驱动、两份20行DMA缓冲、RGB565字节交换和240×320布局。没有LCD触摸硬件入口，不创建触摸指针。

背光使用TCA9555 P0的0／100二值控制，现有界面把非零逻辑亮度映射为100；没有新增PWM或宣称连续调光。空闲熄背光／恢复仍走现有逻辑。P0极性、可见窗口、颜色和实体按键均未实物验收。

## 音频

复用`digital_audio.cpp`，仅K10分支增加ES7243E输入codec，数字扬声器仍走dummy codec及已有软件音量／零PCM静音。没有复制厂商二进制、ADC整套寄存器表或增加第二个驱动依赖，采用已缓存codec_dev1.5.11的ES7243E实现。

- 共享I2S0，标准双声道16位，输入输出均16kHz；MCLK3=256fs／4.096MHz、BCLK0=32fs／512kHz、WS38、DIN39、DOUT45。初始化显式指定MCLK，输入启动前确保TX已启用提供共享时钟；关闭顺序先RX后TX。
- 从输入左声道抽取单声道PCM，输出用固定小缓冲复制mono到左右；没有把第二通道当作播放回声参考，也未宣称波束成形或双麦阵列。
- 网络24kHz播放复用既有24→16kHz FIR，实际发送的PCM同时进入软件AEC参考；输入16kHz直通既有采集链路。现有一秒总超时和部分读写累计逻辑继续生效。
- 保留固定小智37.5dB输入增益。除调用ES7243E实际gain回调，还从控制接口读回0x20／0x21两寄存器的0x1E；避开已安装驱动`get_reg`中的指针问题。codec_dev会丢弃某些回调返回值，因此在共同音频入口检查实际增益结果，不以成功回执推断寄存器已写入。
- P15低是增益配置，没有独立硬件PA断电能力声明。静音发送零PCM；没有把数字成功当作实测无底噪、无爆音或电气静音。

ADC初始化序列、MCLK／SCLK格式、输入左声道、37.5dB削顶和扬声器实际芯片／音量／声学均待实物验证。16MB Flash／Octal PSRAM采用厂家板型和固件构建默认，实际容量未测量。

## 实际预检与公共回归

目标实际预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile dfrobot-df-k10 --destination I:\xy_board_builds_20261109\k10_preflight_20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1，缺`espressif/esp_lcd_ili9341=2.1.0`，未创建stage、未编译或产生新目标镜像。记录在`native_dependency_preflight_20261110_k10.json`。

旧T-CameraPlus V1.2（PDM32采集）及敏思K08（I2S16采集），同步所有main文件／注册表／defaults并刷新变更时间戳后实际执行：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lilygo-t-cameraplus-s3-v1.2 reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=minsi-k08-dual reconfigure build
```


| 旧入口 | 最终缓存build | app字节 | 应用槽余量 | 模型字节 |
| --- | --- | --- | --- | --- |
| lilygo-t-cameraplus-s3-v1.2 | exit0 | 2,230,032 | 915,696 | 2,203,819 |
| minsi-k08-dual | exit0 | 2,207,184 | 938,544 | 2,203,819 |

两旧入口初次和最终同步后增量构建均退出0；两份镜像checksum／validation hash、ELF符号／段大小、二进制分区解码均退出0。每份188个main文件集合和195份输入摘要与最终工作区匹配，独立核对编译宏／选中源／应用身份／QUAD或OCT PSRAM／16MB容量／双OTA槽及模型分区。新k10_board和ES7243E实例／增益包装不在旧目标选中源／符号中。SDK仍为既有5.5.6-dirty，保留HEAD880a203ae603d08a94d06aa01e0d6761958e5e9e及原NimBLE／idf_as_lib改动，没有安装或升级。旧目标只验证公共头、音频和显示分支的兼容性，没有选择`k10_board.cpp`或K10 ES7243E／标准双工／PGA读回分支；复制新文件进stage并核摘要不等于新目标编译通过。

镜像检查为`python -B -m esptool --chip esp32s3 image_info --version 2 <app.bin>`，ELF检查为`xtensa-esp32s3-elf-nm.exe -S --size-sort -C <app.elf>`和`xtensa-esp32s3-elf-size.exe -A -d <app.elf>`，分区用SDK `gen_esp32part.py <partition-table.bin>`解码。每条命令参数、退出码、原日志、源文件及产物SHA256记录在`cached_build_attempts_20261110_k10_<旧profile>.json`。缓存构建不代表干净依赖发行验收。

未实现：GC2145相机／P1相机复位、RGB46、传感器、SD、原厂全部手势和MCP工具、睡眠。未执行测试套件或独立类型检查，没有安装依赖、修改SDK、Git写操作、上传、发布、刷机或OTA。仍需ILI9341依赖、新目标构建，以及引脚／ADC／显示／按键／音频／网络绑定／提醒／OTA实机验收。
