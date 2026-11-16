# 正点原子ATK-DNESP32S3开发板源码适配

2026-11-10，Asia/Shanghai。固定范围仍为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个发布目录／188个变体。新增profile `alientek-atk-dnesp32s3`，应用身份 `assistant_atkdev`；本批不包含同厂BOX／BOX0／BOX2／BOX3的实现，不把品牌相同当硬件兼容。

## 固定来源与错误前提纠正

- [小智固定开发板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/alientek/atk-dnesp32s3)：config.h／config.json／atk_dnesp32s3.cc。显示为SPI2 ST7789，音频为ES8388；XL9555位8置高、位2置低没有在该板类解释用途。
- [厂家openedv仓库](https://github.com/openedv/ATK-DNESP32S3-Board/tree/c7434a3da5b9e6feda05added5d6a686f1c95f13)：本轮通过公开API固定提交 `c7434a3da5b9e6feda05added5d6a686f1c95f13`。`12_spilcd/components/BSP/XL9555/xl9555.h` 明确bit2=`SPK_EN`、bit8=`LCD_BL`、bit10=`SLCD_RST`、bit11=`SLCD_PWR`；`30_music/main/main.c` 以 `SPK_EN=0` 注释“enable speaker”，XL9555初始化令其为1。由此确认功放低有效，不能照搬PCA9557的高有效bit1。
- 厂家LCD示例另确认bit10／11是屏幕复位／供电，不能把bit8误作屏复位。厂家示例的面板初始化、摄像头和音频代码没有导入本地驱动，仅用于核对硬件事实。

公开源码、API提交／树和逐文件来源URL／SHA256索引位于忽略目录 `evidence/atk-public-20261110/`。公开HTTP读取均有30秒超时，没有读取设备或账号数据，没有安装依赖或复制厂家算法／驱动表，因此本批不新增第三方驱动源码许可文件，也不修改项目整体LICENSE。硬件引脚证据不等于实机验收或完整发行来源审计。

## 源码范围

| 项目 | 本地候选 |
| --- | --- |
| 存储 | 16MB Flash／Octal PSRAM沿固定根默认，当前config.json未覆盖；不是实测容量 |
| 屏幕 | 320×240 ST7789，RGB565／big endian，SPI2 mode0／20MHz，CLK12／MOSI11／DC40／CS21／RST=NC，invert=true／swapXY=true／mirrorX=true，gap0 |
| 显示布局 | 复用320×240横屏布局及16px中文；无触摸，BOOT0先释放再按住说话，卡片短按选择／长按松手确认 |
| 扩展口 | I2C0 SDA41／SCL42，XL9555地址0x20，100kHz；端口0输出0x02／配置0x06，端口1输出0x03／配置0x07 |
| 输出所有权 | SPK_EN bit2低有效；LCD_BL bit8高有效开关；SLCD_RST bit10高电平释放；SLCD_PWR bit11高电平供电。保留相机、触摸复位、蜂鸣器、按键等其他位及方向 |
| 音频 | ES8388输入／输出24kHz，I2S0 MCLK3／BCLK46／WS9／DIN14／DOUT10，LEFT输入；GPIO PA=NC，实际功放走XL9555 |

`atk_xl9555.cpp` 在启动任务创建音频／显示任务前，先设置功放禁用、背光关闭、屏幕供电／复位释放电平，再配置所拥有位方向。只改所拥有位，写后读取同寄存器核对，锁等待与各次I2C调用100ms超时。不会把厂家整体0xF003配置写到其他输出，也不沿厂家从输入寄存器取值再覆盖输出的方式实现。没有额外发起硬复位脉冲，ST7789通过其既有软件reset／init；需实物核上电／软重启恢复。

本轮把上批PCA9557的“读取输出寄存器、修改指定位、写入、回读”的既有算法提取至 `i2c_register_bits.h`，PCA9557的寄存器／掩码不变。XL9555保留独立板级映射与生命周期，不把两种芯片当同一寄存器协议。相机电源与复位、蜂鸣器及键值不接入，也不改其方向／输出；实际启动状态仍需故障与实机验收。

## 背光、音频及未完成能力

背光由数字XL9555位控制，只有0／100两种实际状态：原偏好0关闭，非零偏好映射为100，日志分别记录请求值与实际值；原偏好仍作为用户配置保存，不宣称可获得百分比调光。`bsp_display_brightness_set`拒绝0／100之外的底层值，不在GPIO_NC上初始化PWM；超时熄屏与活动恢复沿现有显示状态机。夜间渐暗／中间亮度无法由当前硬件输出实现，只有开关能力。

ES8388复用云聊的现有单物理codec／双handle实现：ESP输出时钟、codec从模式，避免上游ESP和codec均置master；24kHz采集到16kHz语音管道，软件播放参考AEC；共享codec关闭单方向不禁用另一方向。沿 owning ES8388 配置将模拟输出寄存器46–49设30并回读；待机输入24dB、播放12dB是软件AEC候选，不是此板校准值。

扩展口功放控制接现有静音／暂停／播放中止／close流程。启动关闭；静音先尝试关PA，即使失败仍尝试DAC静音；取消静音要在DAC回调实际成功后才开PA。若写入后回读失败再尝试关闭，返回原失败，不把重试或I2C成功当声音真的停止。实际麦克风gain／DAC mute回调结果经过检查，规避codec_dev1.5.11丢弃该回调错误的问题。其他未接管的codec错误传播仍需独立核验。

摄像头、LED1、XL9555按键、SD卡、上游点击聊天／启动配网手势尚未实现；无触摸屏控制器声明，不虚构触摸入口或硬件回声参考。只有现有微雪1.8实物，无法验收本板颜色／时钟／声学／电源／背光／GPIO及同板OTA；全部188项实机状态仍为not_run。

## 同厂BOX调查

仅登记固定源码调查，不新增native profile，也不继承本批构建结果：

- ATK BOX：8bit I80／10MHz面板，I2C0 48／45，按XL9555硬件标志选择ES8311或NS4168数字双工；不能套SPI2／ES8388。
- BOX0：240×240 SPI2／80MHz，16kHz ES8311，实际构造MCLK=NC且use_mclk=false；配置MCLK13与LED13有冲突。M4高有效、0／3低有效；codec／系统供电和充电控制需单独处理，SPK21用途仍待核。
- BOX2 Wi-Fi：I80／20MHz、240×320，ES8389及XL9555管理系统／音频电源、充电与按键；不能替换成ES8388寄存器，不因存在4G脚默认启用蜂窝。
- BOX3：SPI2＋双codec，配置名AW9523B／地址0x59，实际调用TCA95xx驱动，需先核实芯片协议；不搬用未经核实的电源寄存器。相机／2.8V与共用复位另有生命周期。

## 实际构建

新板调用：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board alientek-atk-dnesp32s3
```

使用已有SDK5.5.6-dirty／匹配工具／Python和17份本地组件override，Python外网守卫拒绝外网socket／DNS。SDK保留原NimBLE／idf_as_lib修改，override含其他BSP，不是干净发行依赖锁。云聊、立创和微雪LCD0.85分别在原隔离目录运行临时 `xy_atk_regression_20261110.ps1 -Stage <目录> -Board <profile>`，比较源码摘要后同步变化并刷新目标时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，覆盖已有ES8388／PCA9557／双codec路径。

ATK完整构建及最终同目录 `reconfigure build` 均退出0；最终构建前刷新隔离目录中audio／SPI audio／SPI display／XL9555／display UI编译文件的UTC时间，日志确认重新编译，避免复制保留时间导致增量跳过。三个既有入口的回归均退出0，日志确认音频文件重编，立创另重编PCA9557。最终源码无后续变动，未再次全量构建。既有LVGL9.6弃用接口警告保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| alientek-atk-dnesp32s3 | 2,239,520 | 3,145,728 | 906,208（约29%） |
| yunliao-s3 | 2,245,632 | 3,145,728 | 900,096 |
| lckfb-lichuang-dev | 2,263,424 | 3,145,728 | 882,304 |
| waveshare-lcd-0.85 | 2,252,112 | 2,752,512 | 500,400 |

四款分别执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0。项目身份、0.85的8MB／其他16MB设置、checksum及validation hash有效。模型均2,203,819字节，0.85模型分区2,359,296字节，其余4,194,304字节，均未超容量；0.85按板级专用8MB表核算。

一次性 `xy_record_atk_20261110.py` 退出0，核对每份87个源码／配置摘要和完整80文件main集合、合并defaults、实际选择编译源、生成配置、应用／模型容量，并要求最终audio文件重编日志。记录为 `cached_build_attempts_20261110_atk_<profile>.json`，日志、ELF／镜像、配置和源码副本位于忽略目录 `evidence/atk-builds-20261110/`。仅ATK选中编译 `atk_xl9555.cpp`，不把归档完整main当作其他分支全部编译。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前51个本地profile对应50个官方变体（另1历史兼容入口），138项尚无源码；累计29个官方变体曾分别缓存编译通过。按当前源码摘要，4项通过、25项历史通过证据过期、6项依赖预检失败、153项未构建（其中15项已有源码）。未编译的BigSmart／太极派等仍保留真实状态。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`，退出0；对新增／未跟踪源码、配置、文档执行 `git diff --no-index --check -- NUL <文件>`，返回1表示新增差异，未输出空白错误，保留LF／CRLF提示。未运行测试套件或独立类型检查，未安装依赖、刷机／OTA、连接硬件验收、启动宿主长期服务、Git写入、上传或发布。保留网关／运行时与自然插话的并发改动。
