# ATK BOX0基础语音终端源码候选

2026-11-10，Asia/Shanghai。固定范围为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个发布目录／188个变体。本批新增 `alientek-atk-dnesp32s3-box0`，应用身份 `assistant_atkbox0`。它与上一批ATK开发板的ES8388／XL9555不同，单独配置ES8311、电源和按键，尚非整板所有功能完成。

## 来源及证据等级

主来源是[固定BOX0目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/alientek/atk-dnesp32s3-box0)的config、实际板类和power_manager，以及同提交 [Button定义](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/common/button.h)／button.cc。实际构造的 `middle_button_(GPIO4,true)` 中第二参数就是active_high，故中键高有效；左右键false为低有效，不能把三键统一上拉低有效。

实际Es8311构造传MCLK=NC、use_mclk=false、PA=NC、收放音16kHz。config宏同时写MCLK13和LED13，没有被该构造用作音频MCLK；本地沿实际构造，不驱动13为MCLK或LED。

功放21只有配置宏，没有在固定上游实际构造中使用。另读取社区仓库 [yohoky/ATK-BOX0-Stock-Firmware固定提交](https://github.com/yohoky/ATK-BOX0-Stock-Firmware/tree/1a359acf46be6b67e13e426f92751c69057aa436)：cfg_pins.h称21为NS4150使能，beep.cpp启动和结束置低、播放置高，提供高有效候选依据。**这是社区源码线索，不是厂家原理图或实机证明**。该社区实现还使用MCLK13，与固定小智构造不同；本地仅据前者补充PA21候选，时钟仍沿固定小智的无MCLK／BCLK衍生路径。

公开源码和逐文件URL／SHA256位于忽略目录 `evidence/box0-public-20261110/`，保留社区提交／树及按键定义。首次社区API读取遇到EOF，随后成功固定提交；失败读取不作为硬件证据。所有公开读取有30秒超时，未执行上游或社区代码，未下载社区固件或导入其应用／音频／电池实现，不新增第三方驱动源码许可文件，不改整体LICENSE。

## 本地实现

| 项目 | 源码候选配置 |
| --- | --- |
| 身份／存储 | `assistant_atkbox0`，16MB Flash／Octal PSRAM沿固定根默认，config.json未覆盖；不是实测容量 |
| 显示 | ST7789、240×240、RGB565／big endian，SPI2 mode0／80MHz，CLK39／MOSI40／DC38／CS41／RST=NC，背光42高有效PWM；invert=true、无swap／mirror、gap0 |
| 布局 | 复用240×240、16px中文与固定卡片按钮；无触摸，卡片提示改为“中键短按选择，长按松手确认”，真实圆角裁切仍待实机 |
| Codec | 单ES8311，16kHz输入／输出，I2C0 SDA11／SCL12，I2S0 BCLK5／WS10／DIN6／DOUT9，MCLK=NC、use_mclk=false；PA21高有效为社区依据候选 |
| 供电 | GPIO2系统、14 codec、47充电控制先设输出锁存高电平，再开启输出方向，保持稳定；PA21初始低。GPIO48充电状态只设输入上拉，不伪造电量／供电状态 |
| 中键 | GPIO4高有效输入下拉，先完全释放再按住说话；有卡片时使用现有选择／确认流程，不把长按中键变成未确认的电源关断 |
| 左右键 | 左3音量减、右0音量加，低有效上拉；先释放、多键／跨键取消沿既有状态机，首次10%／保持2秒后5%重复，不直接套原长按0／100手势 |

`board_profile::listen_active_high` 使共用input任务按所选板型设置上／下拉，其他板保持false。没有让共用任务在初始化时将中键4覆盖成上拉。左右键和GPIO48由BOX0板级启动初始化，不创建虚构触摸或扩展口键。

音频复用既有单codec的模拟增益、软件音量／静音、16kHz语音管道、唤醒及软件AEC；PA通过现有ES8311 GPIO生命周期使能／关闭。没有重写数字麦克风通道或按每段提示音切换codec电源，避免供电复位。待机30dB／播放12dB及codec电压增益沿既有候选值，不能称为BOX0声学校准。真实BCLK衍生时钟、无MCLK收音、PA21有效电平及静音／关闭重开均待设备确认。

## 充电、电池与行为限制

充电控制GPIO47高仅沿固定上游启动电平；不是完整充电／电池策略通过验收。上游采ADC1_CH0时会暂时拉低47并使用一组原始ADC阈值，同时有低压和省电关机。当前没有移植这些未经本机硬件标定的数值或阻塞定时器回调，不输出电池百分比、不自动切电；后续仍需建立采样、充电状态、校准、低压保护与OTA／活跃语音期间的电源边界。

系统和codec供电保持开启；原中键长按息屏／关机、自动省电／深睡／唤醒、启动配网手势、LED13尚未实现，0／3原长按静音／最大音量由项目统一重复音量逻辑代替。板级基础语音候选不等于电池设备可以交付。只有现有微雪1.8实物，屏控修订尚未确认，全部188项实机验收仍为not_run。

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
  -Board alientek-atk-dnesp32s3-box0
```

使用已有SDK5.5.6-dirty／匹配工具／Python及17份本地组件override，Python外网守卫拒绝外网socket／DNS；SDK保留原NimBLE／idf_as_lib修改，override包含其他BSP，不是干净发行依赖锁。没有安装组件。ATK开发板、Kevin SP V4、立创分别在原隔离目录运行临时 `xy_box0_regression_20261110.ps1 -Stage <目录> -Board <profile>`，内容摘要变化时同步并刷新目标时间，再执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，覆盖ES8388／16kHz单codec／PCA9557双codec和共用按键初始化路径。

BOX0完整缓存构建和最终同目录 `reconfigure build` 均退出0；最终增量前刷新隔离目录main／audio／display／power编译文件的UTC时间，日志确认main／audio实际重编。三个既有入口回归均退出0，日志同样确认共用input和audio文件重编。BOX0仅选择现有SPI显示和单codec路径，没有编译XL9555、数字音频或双codec板源；其他入口也不算作BOX0分支已编译。既有LVGL9.6弃用接口警告保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| alientek-atk-dnesp32s3-box0 | 2,245,760 | 3,145,728 | 899,968（约29%） |
| alientek-atk-dnesp32s3 | 2,239,520 | 3,145,728 | 906,208 |
| kevin-sp-v4-dev | 2,244,288 | 3,145,728 | 901,440 |
| lckfb-lichuang-dev | 2,263,424 | 3,145,728 | 882,304 |

四款分别执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，项目名／16MB配置／checksum／validation hash有效。模型均2,203,819字节、分区4,194,304字节，应用和模型均未超容量。一次性 `xy_record_box0_20261110.py` 退出0，核对每份87个源码／配置摘要、完整80文件main集合、合并defaults、实际编译源与生成配置，并要求最终main／audio重编日志；原始日志、ELF／镜像、配置和源码副本在忽略目录 `evidence/box0-builds-20261110/`，记录为 `cached_build_attempts_20261110_box0_<profile>.json`。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前52个本地profile对应51个官方变体（另1历史兼容入口），137项尚无源码；累计30个官方变体曾分别通过缓存编译。按当前源码摘要，4项通过、26项历史通过证据过期、6项依赖预检失败、152项未构建（其中15项已有源码）。累计通过不等于当前源码通过，全部188项实机状态仍not_run。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/main.cpp firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp`，退出0；新增／未跟踪源码、配置、文档使用 `git diff --no-index --check -- NUL <文件>` 空白审阅，返回1表示新增差异，未输出空白错误，保留LF／CRLF提示。未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、刷机／OTA、连接设备进行物理验收、Git写入、上传或发布；保留并发网关／运行时及自然插话改动。
