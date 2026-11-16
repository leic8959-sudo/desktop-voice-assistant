# ESP-VoCat 双 PCB 修订源码适配

后续更新：VoCat已完成首次完整缓存构建，复用已审阅QSPI流程并保留两版PCB复位差异；太极派STD/PDM共享路径回归通过，实机仍未验收。见[本批说明](vocat_qspi_native_20261112.md)。

日期：2026-11-10，Asia/Shanghai。新增官方变体`espressif-esp-vocat`的基础小燕子终端源码候选。新目标因缺少ST77916驱动，尚未编译；仅现有微雪1.8可供实机验收，VoCat两种修订均未刷机。

## 固定来源

- 小智v2.5.2：`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，`main/boards/espressif/esp-vocat/`下的板类、配置、README及触摸头文件。
- [乐鑫BSP固定提交](https://github.com/espressif/esp-bsp/tree/6f82fd66a099cfd388cf9376ca1c5d6dcc98381e/bsp/esp_vocat)：BSP1.1.0的板级头、音频、显示、供电、I2C、184条显示命令和组件声明。
- 12份来源的URL、SHA256和快照：`evidence/vocat-source-20261110/source-artifacts.json`。证据目录按项目已有设置忽略。此前调查记录仍保留，当前实施状态以本文件、注册表、研究JSON和构建矩阵为准。

固定应用与厂家BSP的184条ST77916命令逐条完全一致，包含数据、长度、顺序及延时，末尾为21／11／零参数NOP等待120ms；它们与本地现有四套ST77916表均不相同。本地新增`vocat_panel_commands.h`，保留MIT原条款和修改说明于`main/third_party/xiaozhi_vocat/`，没有修改整体LICENSE。

## 修订识别与供电

| 项目 | PCB V1.0 | PCB V1.2 |
| --- | --- | --- |
| ES7210 DIN | GPIO15 | GPIO3 |
| 功放使能 | GPIO4 | GPIO15 |
| LCD reset | GPIO3，低有效 | GPIO47，高有效 |
| GPIO48 | 固定来源视为未使用 | codec供电使能，高有效 |

GPIO15在两版的方向相反，因此不能提前把两组功放脚都配置成输出。新增`vocat_board.h/.cpp`负责以下流程：

1. GPIO44先锁存低并配置输出，保持背光暗。
2. 仅在`ESP_RST_POWERON`冷启动时将GPIO48拉低等待100ms，分别探测ES8311的0x18及ES7210的0x40。两者都ACK才选择V1.0；两者都NACK才把48拉高，每200ms探测一次、最多10次，直到两者都ACK才选择V1.2；每次设备探测超时100ms。
3. 低供电阶段的混合应答、超时或其他总线错误均停止，不默认选择V1.0。相比固定板类仅探测ES8311，本地增加ES7210配对应答，依据是厂家BSP将MIC_EN和SPEAKER_EN都定义为48；用于拒绝单codec异常，不能当作已验证的硬件身份识别。
4. 选定后才设置对应DIN和功放脚。功放先锁存低，再设为输入输出模式，等1ms读实际pad电平；运行期通过100ms互斥锁控制，并在实际DAC解除静音成功后才开启PA。
5. GPIO9按固定应用及厂家逻辑保持低，同时也会使能SD供电；不初始化或写SD，不修改0x55充电／电量策略。没有猜测其他供电脚。
6. 完成GPIO设置后，将修订保存在本固件专用的16字节RTC_NOINIT缓存，含magic及反码、revision及反码；不读写NVS中的板型、绑定或凭据。
7. 非上电复位只接受有效RTC缓存。V1.2先预装48高电平再配置输出，代码不再发出主动拉低48的请求，实际复位波形仍待测量；先关闭已知PA，再等待两颗codec应答。无有效缓存时保持48高，不驱动未知PA候选，停止并提示真正断电后重新上电。这包括首次从其他固件刷入后、仍以软件复位启动的情况。

双codec的供电应答仍不能排除两颗芯片同时延迟或故障导致的异常。RTC保持、复位原因、供电瞬态、首次刷入后的冷启动要求和PA读回均未用V1.0／V1.2实物验证；这是源码候选，不是硬件识别验收通过。

## 显示、触摸和音频

- 复用`waveshare_qspi_display.cpp`，没有复制第二套显示实现。360×360圆屏，QSPI2 mode0／40MHz，CS14／CLK18／D0=46／D1=13／D2=11／D3=12，DC NC，32位命令／8位参数；运行期选择reset脚和有效电平。两像素对齐、两份20行DMA缓冲，RGB565及SPI字节交换，无镜像／XY交换。没有TCA扩展器，也不执行微雪CS21的屏幕ID探测。
- 采用固定应用40MHz及其包含21命令的序列。厂家新BSP默认80MHz并额外调用invert(true)，不将该默认配置直接替换进来。GPIO44使用既有5kHz／10位PWM，供电先暗，正常亮度、空闲熄背光和恢复走现有界面逻辑；效果仍需实物确认。
- CST816S在0x15／100kHz，reset NC，INT10设输入后禁用IRQ，采用轮询。复用组件构造及IO，读取0x02起始6字节的原始点数／事件／坐标，避免组件把多点数截成1。初始需释放，多指、越界、读取失败会取消当前按压，恢复后重新等待释放；保留原生坐标，不声明标定或唤醒后首次触摸效果已通过。
- BOOT0初始释放后支持说话／卡片动作，同时提供触摸卡片操作。复用现有360圆屏布局；未移植原厂“触摸释放切聊天”、顶部／侧面电容触摸、滑条音量等全部手势。
- 复用双codec音频后端：ES8311＋ES7210，I2C0 SDA2／SCL1，MCLK42／BCLK40／WS39／DOUT41／DIN按修订，24kHz，四槽TDM输入取MIC1／slot0，软件AEC；ES7210 codec API默认0x80对应实际7位地址0x40，不套用LCD EV的0x82。实际增益、DAC静音和后端打开结果沿用已有检查，codec PA pin为NC，由板级函数控制已识别的PA。未使用MIC3硬件回声参考或波束成形。

厂家BSP音频默认48kHz／标准双槽，与固定应用24kHz BoxAudioCodec路径不同，本地保留固定应用的采样率和既有单麦软件AEC合同。16MB Flash／Octal PSRAM采用发行默认，不代表已确认每块模组实际容量。

未实现：LED43、BMI270、GPIO6/7电容触摸、硬件电量／温度／充电、UVC相机、SD挂载、睡眠、原厂MCP工具。基础终端入口不能代表整板功能完成。

## 实际预检与回归边界

目标依赖预检命令：

```text
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp-vocat --destination I:\xy_board_builds_20261109\vocat_preflight_20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1，缺少`espressif/esp_lcd_st77916=2.0.2`，未创建stage、未编译、未生成新目标镜像。记录：`native_dependency_preflight_20261110_vocat.json`。没有安装依赖、修改SDK或执行测试套件／独立类型检查。

旧SparkBot（16kHz单ES8311）和微雪1.43C（24kHz双codec）在隔离缓存目录同步全部main文件／注册表／defaults、刷新变化文件时间戳后，实际执行：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=espressif-esp-sparkbot reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-touch-amoled-1.43c reconfigure build
```


| 旧入口 | 最终缓存build | app字节 | 应用槽余量 | 模型字节 |
| --- | --- | --- | --- | --- |
| espressif-esp-sparkbot | exit0 | 2,237,888 | 907,840 | 2,203,819 |
| waveshare-esp32-s3-touch-amoled-1.43c | exit0 | 2,544,976 | 207,536 | 2,203,819 |

两旧目标初次及最终同步后增量构建退出0；两份镜像checksum／validation hash、ELF符号／段大小、二进制分区解码均退出0。每份182个main文件集合与189份输入摘要匹配最终工作区，独立核对编译宏、选中源文件、板型身份、16／8MB容量、双OTA槽和模型分区。新VoCat源不在选中编译清单或ELF符号内。SDK仍为既有5.5.6-dirty，保留NimBLE／idf_as_lib原改动；没有安装依赖或升级SDK。这些旧目标只验证公共头文件、音频、界面及编译配置；它们不选择`vocat_board.cpp`或`waveshare_qspi_display.cpp`，因此没有验证VoCat新供电／RTC／QSPI／触摸分支的编译。把新文件复制到stage并核对摘要，也不能作为选中编译的证明。

镜像检查命令为`python -B -m esptool --chip esp32s3 image_info --version 2 <app.bin>`，ELF符号／段为`xtensa-esp32s3-elf-nm.exe -S --size-sort -C <app.elf>`及`xtensa-esp32s3-elf-size.exe -A -d <app.elf>`，分区用SDK的`gen_esp32part.py <partition-table.bin>`解码。每条实际参数／日志／退出码及产物摘要记录在`cached_build_attempts_20261110_vocat_<旧profile>.json`，不创建伪造的VoCat通过记录。

仍需目标驱动依赖、VoCat真实构建、两版实物的识别／供电／复位／颜色／触摸／音频时钟及声学、配网绑定、提醒、OTA验收。未执行Git写操作、上传、发布、刷机或OTA。
