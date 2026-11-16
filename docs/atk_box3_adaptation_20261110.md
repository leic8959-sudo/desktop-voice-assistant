# ATK BOX3：AW9523B协议纠正与基础语音源码

2026-11-10，Asia/Shanghai。固定小智v2.5.2基线 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，范围仍为154个发布目录／188个变体。新增 `alientek-atk-dnesp32s3-box3`，应用身份 `assistant_atkbox3`；基础屏幕、音频和按键源码不等于所有板卡功能已完成或可交付电池设备。

## 固定来源与协议冲突

主来源为[小智BOX3固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/alientek/atk-dnesp32s3-box3)的config、板类、power_manager和同提交BoxAudioCodec。该配置明确 `AW9523B_ADDR=0x59`，但实际初始化调用 `esp_io_expander_new_i2c_tca95xx_16bit`。不能把函数执行成功视作正确配置了AW9523B。

本轮固定厂家 [openedv BOX3 Wiki提交](https://github.com/openedv/openedv-wiki-boards-dnesp32s3b3/tree/c6f9797b64aef2666547206b4b274017d6d28a3d) `c6f9797b64aef2666547206b4b274017d6d28a3d`。其 [IIC扩展章节](https://github.com/openedv/openedv-wiki-boards-dnesp32s3b3/blob/c6f9797b64aef2666547206b4b274017d6d28a3d/example-idf/iic_exio.md) 明确：

| 项目 | AW9523B实际定义 | 与TCA9555的差异 |
| --- | --- | --- |
| 输入／输出 | 输入0x00／01，输出0x02／03 | 这四项相近，不能据此推断其他寄存器相同 |
| 方向 | **0x04／05**，1为输入 | TCA方向为0x06／07 |
| 中断 | **0x06／07** | 把TCA方向写入这些地址会修改中断掩码 |
| 身份 | 地址0x59，ID寄存器0x10应为0x23 | 初始化必须先核ID，不按I2C错误尝试其他协议 |
| GPIO／LED模式 | 0x12／13的位设1为GPIO | 方向设置前需保证相关脚不处于恒流LED模式 |
| P0输出类型 | CTL0x11 bit4设1为推挽 | 属整组P0配置，不能宣称只影响一个脚 |

厂家 [配套原理图局部](https://raw.githubusercontent.com/openedv/openedv-wiki-boards-dnesp32s3b3/c6f9797b64aef2666547206b4b274017d6d28a3d/example-idf/img/v.png) 也标明K1接P0_0、上拉后按下接地；K2接P0_1、下拉后按下接VBAT，分别低有效／高有效。LCD章节的初始化以 `LCD_BL=0` 完成点亮，固定小智同样置低。厂家初始化与小智都令PA_CTRL高、系统／电池／模拟音频供电高，支持当前候选电平，仍不证明实机切换和声学正确。

公开源码、厂家文档、原理图图片、API提交／树和逐文件URL／SHA256索引位于忽略目录 `evidence/atkbox3-public-20261110/`。本轮仅读公开资料，HTTP调用有30秒超时；没有读取设备、账号或私有凭据，没有导入厂家驱动函数或上游应用源码。AW9523B本地实现复用项目既有寄存器读改写封装，不新增第三方驱动源码或整体LICENSE变更；硬件事实引用不是完整发行来源审计。

## 本地板级实现

| 项目 | 源码候选配置 |
| --- | --- |
| 存储 | 16MB Flash／Octal PSRAM沿固定默认，config.json未覆盖；未实测容量 |
| 显示 | 320×240 ST7789，SPI2 mode0／60MHz；CLK15／MOSI16／MISO17／DC48／CS47／RST=NC，RGB565／big endian、invert=true、swapXY=true／mirrorX=true、gap0 |
| 界面 | 320×240横屏布局、16px中文，无触摸；中键卡片选择／确认，左减右加音量，显示提示使用“中键” |
| 控制 | I2C0 SDA3／SCL2，AW9523B 0x59，100kHz；先读ID0x23，100ms超时、互斥及写后回读 |
| 音频 | ES8311输出＋ES7210输入24kHz；I2S0 MCLK21／BCLK38／WS39／DIN41／DOUT40，ADC默认8bit地址0x80，PA由AW bit5控制 |
| 采集 | 四路ES7210 ADC／RX4×16bit TDM，TX初始32bit双槽匹配64时钟／帧；当前只取slot0／MIC1单声道供现有软件AEC |

`atk_box3_board.cpp` 在任何供电写入之前核对ID；读取失败或ID不符直接返回错误，不以TCA协议兜底。先将K1／K2及充电控制2／状态3设输入，避免旧方向在切GPIO／推挽模式时短暂驱动充电控制；随后建立PA5低、背光8高（关闭）、供电11／12／13高的输出锁存，再将所拥有位切成GPIO及输出方向。P0所涉及mask0x2F，P1 mask0x39。相机2.8V位14、共用相机／触摸reset15、LED9／10、EXT6／7及ADC选择4的输出、方向与GPIO／LED模式位保持原值，不为了基础语音启动相机。

P0 push-pull通过CTL bit4启用，这个设置影响整组P0已有输出，不能用“保留其他位”声称完全不影响其他外设的电气类型；这是厂家要求的板级模式，仍需结合实际外接设备验收。本地没有软件复位AW9523B，也不全端口写0xFF／0xFC覆盖未接管状态，不写0x06／07冒充方向配置。

背光通过AW bit8低有效开关，初始关闭；偏好0关闭、非零映射100，并分别记录请求／实际值。底层拒绝0／100外的值，不在GPIO_NC上初始化PWM；没有声称中间亮度或夜间渐暗可用。SPI接口仍复用现有ST7789／LVGL显示层，60MHz信号、MISO／数据顺序、方向和无物理复位的启动均待实机验收。

## 输入、功放和音频边界

K1 bit0低有效为音量减，K2 bit1高有效为按住说话／卡片输入，BOOT GPIO0低有效为音量加；BOOT输入初始化由板级完成，不把无GPIO的扩展中键创建为虚构touch listen。轮询每次20ms锁等待／I2C超时，与PA／背光更新共用互斥；读取失败返回Invalid，现有输入任务取消持有操作，恢复后完整释放才能重新按键；日志按错误状态变化记录。多键／跨键取消和首次10%／保持2秒后5%重复音量沿项目现有逻辑，原长按0／100、点击聊天及关机手势未搬入。

复用既有双codec的显式采集／输出超时和底层格式／启停结果检查。输入增益只改物理MIC1，待机30dB／播放12dB沿现有软件AEC候选，不能称为本板校准。来源的硬件参考输入未送入现有AFE，仍使用软件播放参考；四槽配置和MIC1选择不等于硬件AEC／波束成形验收完成。

PA5高有效、初始关闭；静音／中止／close先尝试关闭PA，即使失败仍尝试DAC静音；取消静音要求实际DAC callback成功后才开PA，回读失败再尝试关闭并保留错误。实际mic gain和DAC mute回调返回值被检查，避免codec_dev1.5.11吞掉这些回调失败；其他未接管的codec错误传播仍待核验。I2C故障时硬件状态可能未知，重试不代表已经静音。

电池ADC1、充电策略／低压保护、自动睡眠／唤醒／关机、IRQ42、LED、相机与reset／2.8V电源、外接GPIO管理和原手势均未完成。只有现有微雪1.8实物，屏控修订尚未确认，全部188项实机状态仍not_run。厂家文档解决了寄存器前提冲突，不替代供电、触摸／按键、声学与同板OTA验收。

## 实际构建

新板实际调用：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board alientek-atk-dnesp32s3-box3
```

使用已有SDK5.5.6-dirty／匹配工具／Python及17份本地组件override，Python外网守卫拒绝外网socket／DNS，未安装依赖。SDK保留原NimBLE／idf_as_lib修改，override含其他BSP，不是干净发行依赖锁。BOX2 Wi-Fi、ATK开发板、立创在既有隔离目录运行临时 `xy_box3_regression_20261110.ps1 -Stage <目录> -Board <profile>`，按内容摘要同步变化并刷新目标时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，覆盖ES8389／ES8388／双codec与现有扩展口路径。

BOX3完整缓存构建和最终同目录 `reconfigure build` 均退出0；最终增量前刷新main／audio／SPI audio／SPI display／AW board／display UI编译文件UTC时间，日志确认main、audio、AW board重编，纳入充电控制先设输入的最后调整。三个旧入口回归均退出0，共用main／audio重编日志已核对；旧入口未选择编译的AW board文件仅同步完整归档快照，不把它算成旧入口编译了AW协议。既有LVGL9.6弃用接口警告保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| alientek-atk-dnesp32s3-box3 | 2,250,384 | 3,145,728 | 895,344（约28%） |
| alientek-atk-dnesp32s3-box2-wifi | 2,234,192 | 3,145,728 | 911,536 |
| alientek-atk-dnesp32s3 | 2,239,520 | 3,145,728 | 906,208 |
| lckfb-lichuang-dev | 2,263,424 | 3,145,728 | 882,304 |

四款分别实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，项目身份、16MB配置、checksum及validation hash有效。模型均2,203,819字节、分区4,194,304字节，应用和模型均未超容量。新板另实际执行匹配工具链 `xtensa-esp32s3-elf-nm.exe -C build/assistant_atkbox3.elf`，退出0，最终ELF保留 `atk_box3_board_init`、`atk_box3_set_amplifier`、`es8311_codec_new`、`es7210_codec_new`；只是代码已链接，不能证明真实ID读取、寄存器写入或收放音成功。

一次性 `xy_record_box3_20261110.py` 退出0，核对每份98个源码／配置摘要和完整91文件main集合、合并defaults、实际选择源、生成配置、容量与新板ELF符号，并要求最终main／audio重编日志。记录为 `cached_build_attempts_20261110_box3_<profile>.json`，日志、ELF／镜像、配置和源码副本在忽略目录 `evidence/box3-builds-20261110/`。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前55个本地profile对应54个官方变体（另1历史兼容入口），134项尚无源码；累计33个官方变体曾分别缓存编译通过。按当前源码摘要，4项通过、29项历史通过证据过期、6项依赖预检失败、149项未构建（其中15项已有源码）。累计通过不能当作当前源码通过，全部188项实机仍为not_run。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`，退出0；新增／未跟踪源码、配置、文档执行 `git diff --no-index --check -- NUL <文件>`，返回1表示新增差异，未输出空白错误，保留LF／CRLF提示。未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、刷机／OTA、连接设备验收、Git写入、上传或发布；保留网关／运行时及自然插话并发改动。
