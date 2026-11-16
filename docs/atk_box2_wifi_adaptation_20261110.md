# ATK BOX2 Wi-Fi：ES8389与I80源码候选

2026-11-10，Asia/Shanghai。固定范围仍为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个发布目录／188个变体。新增 `alientek-atk-dnesp32s3-box2-wifi`，应用身份 `assistant_atkbox2`；本批只接Wi-Fi基础语音终端，4G版是独立发布变体，不继承此入口。

## 来源与板级范围

来源为[固定BOX2 Wi-Fi目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/alientek/atk-dnesp32s3-box2-wifi)的config／实际板类／power_manager，以及同提交 `main/audio/codecs/es8389_audio_codec.cc`／.h。公开副本、逐文件URL／SHA256与20条面板命令提取记录位于忽略目录 `evidence/atkbox2-public-20261110/`。ES8389使用本机已有 `esp_codec_dev` 1.5.11驱动，没有下载组件或用ES8388代替。

| 项目 | 源码候选 |
| --- | --- |
| 存储 | 16MB Flash／Octal PSRAM沿固定根默认，config.json未覆盖；未实测容量 |
| 显示总线 | 240×320 ST7789，I80 8bit／20MHz，RD10保持高、DC12／WR11／CS14；D0–7为13／9／8／7／6／5／4／3；队列7、DC idle高、64字节burst、最大240×20×2字节DMA |
| 面板／布局 | RGB565、RST=NC、invert=true、gap0、无轴交换／镜像，背光21高有效PWM；复用240×320布局与16px中文，无触摸 |
| Codec | ES8389、24kHz单声道输入／输出，共享I2S0，MCLK38／BCLK40／WS42／DIN39／DOUT41，外部MCLK、ESP master／codec slave |
| 控制总线 | I2C0 SDA48／SCL47；codec默认8bit地址0x20由codec接口换算为7bit0x10，TCA9555扩展口使用7bit0x20，两者不是同地址 |
| 输入 | 扩展口左5低有效／中7高有效，右GPIO0低有效；中键按住说话和现有卡片选择确认，左右音量沿项目统一步长／重复逻辑 |

I80创建逻辑由上批ATK BOX提取为 `i80_panel_io.cpp`，两款按各自明确引脚／时钟／DC idle／队列传参；旧BOX仍为10MHz／320×240，不把BOX2的20MHz或纵屏坐标套过去。失败时释放刚创建的bus并保留清理错误。现有LVGL适配层处理panel IO完成回调、RGB565字节交换与双缓冲，不另建一套UI。

`atk_box2_panel_commands.h` 保留固定板类标准init／invert之后的20条额外命令，最大数据14字节，逐条核长度并传播传输错误；随后仍由驱动应用no-swap／no-mirror。原始MADCTL0x60写入和之后驱动方向调用按来源顺序保留，不据一条原始寄存器值推断最终方向。来源根MIT版权／许可与改编说明位于 `main/third_party/xiaozhi_atk_box2/`；没有导入整份上游板类／电源／codec应用代码或更改整体LICENSE。实际面板芯片、颜色、DMA对齐、20MHz信号与240×320目标均未实机验收。

## 扩展口、电源与三键

`atk_box2_board.cpp` 对TCA9555使用100ms I2C超时、互斥锁及写后回读，只拥有需要的位。启动先建立USB_SEL8／SYS_POW10／EN_3V3A13高、SPK_EN9／VBUS_EN11低的输出锁存，再配置这几位为输出，减少使能方向时的短暂错误电平。保留SBU3／4及4G控制12的现有电平和方向，不在Wi-Fi终端初始化时自动拉高蜂窝电源。

CHG_CTRL14在来源中平时为输入，仅ADC采样／关机时转成驱动低，本地保持输入；CHRG15也保持输入，没有伪造充电、供电方式或电量。当前只维持基本系统／模拟音频供电，完整充电／ADC校准／低压／关机策略尚未完成。

按键轮询每次读扩展输入寄存器，20ms锁等待／I2C超时，和功放写共用互斥。I2C错误返回Invalid，现有输入任务取消持有的录音／卡片操作，恢复后要求完整释放；按键状态错误按变化记录，避免每20ms重复刷日志。多键或跨键沿既有取消流程。Q6额外键及IRQ2未接入，不借用这些脚作为直接录音GPIO；中键来自扩展口，主输入任务不创建GPIO_NC输入或虚拟触摸按钮。左减／右加沿初次10%／保持2秒后5%重复，未照搬原长按0／100手势。

## ES8389、功放与TX时钟

单物理ES8389由输入／输出两个handle共享，两者 `disable_when_closed=false`，避免关一个方向把另一个方向的codec停掉。初始I2S标准双声道16bit，设备按来源以单声道输入／输出打开；不使用ES7210的四槽或通道mask。待机输入增益40dB沿固定构造，播放12dB仍为项目既有软件AEC候选策略，未做此板声学校准；右侧DAC参考不接入现有AFE，当前仍使用软件播放参考。

固定ES8389应用源码明确保留TX接口：其注释指出ESP32-S3双工RX依赖TX时钟，IDF6在关闭／重开TX后可能无声。当前本地使用IDF5.5.6，仍沿这个保留时钟原则：逻辑idle关闭RX、关闭外部PA并静音DAC，保持TX handle／数据接口打开，后续重开RX和取消静音。没有宣称此措施已解决所有本机时钟竞态；实际空闲、暂停、中断、RX重开与声学需要硬件验收。保持TX会保留时钟／资源，不等于低功耗关机。

功放9高有效，启动关闭；静音／中止／idle先尝试关闭PA，再尝试DAC静音，即使扩展口失败仍尝试静音codec；只有DAC实际回调成功后才启用PA，写入后回读失败时再尝试关闭并保留失败结果。直接核实际DAC mute／mic gain回调，避开codec_dev1.5.11丢弃这些返回值的问题；未接管的其他codec底层错误仍需独立核验，I2C失败时硬件状态可能未知，不能把重试写成已静音。

## 未完成验收与外部状态

只有现有微雪1.8实物，屏控修订尚未确认；本板电源／USB／PA／三键、I80屏、ES8389时钟／单声道麦克风路由／40dB增益／静音重开／软件AEC／唤醒、真实16MB容量和同板OTA均未验收。充电与电池ADC1校准、低压保护、自动睡眠／唤醒／关机、Q键／IRQ和原长按手势仍未完成，不把源码候选称为完整电池设备交付。全部188项实机状态保持not_run。

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
  -Board alientek-atk-dnesp32s3-box2-wifi
```

使用已有SDK5.5.6-dirty／匹配工具／Python及17份本地override，Python外网守卫拒绝外网socket／DNS，未安装组件。SDK保留原NimBLE／idf_as_lib修改，override含其他BSP，不是干净发行依赖锁。ATK BOX、ATK开发板、立创分别在原隔离目录运行临时 `xy_box2_regression_20261110.ps1 -Stage <目录> -Board <profile>`，按内容摘要同步变化并刷新目标时间，再运行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，覆盖共用I80、ES8388、PCA9557双codec与原混合音频选择。

BOX2完整缓存构建和最终同目录 `reconfigure build` 均退出0，日志确认ES8389驱动、板级控制和I80源码编译。最终前刷新隔离目录main／audio／SPI audio／display／board／I80编译文件UTC时间，确保产物包含当前代码；三个旧入口的最终回归也均退出0。ATK BOX额外保存中途收尾日志，最终日志明确main／audio／I80重新编译。既有LVGL9.6弃用接口警告保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| alientek-atk-dnesp32s3-box2-wifi | 2,234,192 | 3,145,728 | 911,536（约29%） |
| alientek-atk-dnesp32s3-box | 2,225,488 | 3,145,728 | 920,240 |
| alientek-atk-dnesp32s3 | 2,239,520 | 3,145,728 | 906,208 |
| lckfb-lichuang-dev | 2,263,424 | 3,145,728 | 882,304 |

四款分别执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用身份、16MB配置、checksum及validation hash有效。模型均2,203,819字节，分区4,194,304字节，应用与模型均未超容量。新板实际执行匹配工具链 `xtensa-esp32s3-elf-nm.exe -C build/assistant_atkbox2.elf`，退出0，最终ELF保留 `atk_box2_board_init`、`create_i80_panel_io`、`es8389_codec_new`、`esp_lcd_new_i80_bus`；这仅证明被选择代码链接，不证明物理时钟、供电或声学成功。

一次性 `xy_record_box2_20261110.py` 退出0，四份各核对96个源码／配置摘要和完整89文件main集合、合并defaults、实际编译源、生成配置、模型／应用容量及新板ELF符号；要求最终main／audio重编日志。记录为 `cached_build_attempts_20261110_box2_<profile>.json`，日志、ELF／镜像、配置和源码副本位于忽略目录 `evidence/box2-builds-20261110/`。只有BOX2选择其专用ES8389／板级分支，其他旧入口不算作BOX2已编译证据。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前54个本地profile对应53个官方变体（另1历史兼容入口），135项尚无源码；累计32个官方变体曾分别缓存编译通过。按当前源码摘要，4项通过、28项历史通过证据过期、6项依赖预检失败、150项未构建（其中15项已有源码）。累计通过不能当作当前源码通过，全部188项实机状态仍not_run。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`，退出0；对新增／未跟踪文件执行 `git diff --no-index --check -- NUL <文件>`，发现并清除抽取I80后旧文件末尾多余空行，最终未输出空白错误，返回1表示新增内容差异，保留LF／CRLF提示。未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、刷机／OTA、连接硬件验收、Git写入、上传或发布；保留网关／运行时和自然插话的并发改动。
