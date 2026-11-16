# 立创SZPI与RYMCU BigSmart的PCA9557板级适配

后续更新：BigSmart现已接入共享GT911固定容量轮询，保留上电默认0x5D/noRST/noINT与原坐标变换，首次整板构建通过；立创SZPI的FT5x06路径独立回归通过。最新边界见[本批记录](gt911_bigsmart_20261112.md)。

2026-11-10，Asia/Shanghai。固定范围仍为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个发布目录／188个发布变体。新增 `lckfb-lichuang-dev` 和 `rymcu-bigsmart` 基础语音终端源码候选。两款分别登记触摸依赖；BigSmart缺GT911，尚未编译，不继承立创结果。

## 来源与公共硬件

来源为[固定立创目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/lckfb/szpi-esp32s3)、[固定BigSmart目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/rymcu/bigsmart)，以及同提交 `main/audio/codecs/box_audio_codec.cc`／.h、`main/idf_component.yml`。公开副本和逐文件URL／SHA256位于忽略目录 `evidence/pcabox-public-20261110/`。没有复制上游应用／相机代码或导入第三方面板初始化数据；PCA9557本地代码按固定寄存器事实实现，不把8bit PCA9557替换成TCA9554或16bit XL9555。

| 项目 | 共用源码候选 |
| --- | --- |
| 显示 | 320×240 ST7789、RGB565、SPI3 mode2／80MHz，CLK41／MOSI40／DC39，CS／RST=NC；invert=true、swapXY=true、mirrorX=true／mirrorY=false、gap0 |
| 背光 | GPIO42低有效；PWM启动为熄灭状态，亮度值取反；复用320×240布局和16px中文，不使用240×240默认坐标 |
| I2C | I2C1 SDA1／SCL2；PCA9557地址0x19；ES7210使用固定板源码的8bit地址0x82，不能套默认0x80 |
| 音频 | ES8311输出＋ES7210输入24kHz，共享I2S0 MCLK38／BCLK14／WS13，DIN12／DOUT45；ES8311没有直接PA GPIO |
| 采集 | ES7210启用4个ADC；RX用4×16bit TDM，TX从初始配置匹配64时钟／帧；当前选择slot0／MIC1单声道接既有软件AEC |
| 输入 | BOOT0先释放再按住说话，现有卡片短按选择／长按松手确认；触摸卡片并存，没有虚构音量键或根页面触摸说话按钮 |

BigSmart README声明N16R8，立创沿固定根16MB／Octal PSRAM默认，两者均未实测容量。相近引脚不等于其他硬件相同，分别维持应用身份 `assistant_lichuang`／`assistant_bigsmart`。

## PCA9557及音频生命周期

`pca9557_box.cpp` 只拥有输出bit0（LCD control）和bit1（功放）。初始化读取实际寄存器，先令bit0高、bit1低，再把这两位方向配置为输出。保留摄像头bit2和其他位的原值与方向，不复制上游将bit2也设输出的0xF8整体配置。ST7789 reset之后再令bit0低，沿实际板类时序；bit0具体电气作用仍需硬件确认，不随意称作物理reset脚。

每次读改写有互斥锁、100ms锁等待及各次I2C传输100ms超时，写后回读所拥有位；不扫描总线、不用未验证的缓存覆盖其他输出。初始化在音频／显示任务前完成。功放初始保持关闭，音频打开并取消静音后才尝试置高；暂停、静音、播放中止及关闭codec均尝试关闭功放。

静音时先尝试关闭功放，即使失败仍尝试静音DAC；取消静音要在DAC请求成功后才使能PA。功放写入成功但回读失败时，再尝试关闭，保留原失败结果；物理I2C故障可能导致硬件状态未知，重试不等于已经静音。codec_dev1.5.11会丢弃mute／input-gain回调返回值，本地新板分别调用底层DAC mute和物理MIC1增益接口核实际返回，避免仅凭包装函数成功放大音频。其他既有板型保留原codec调用行为；其他未接管的codec_dev底层错误传播仍待系统性核验。

立创待机MIC1增益28dB，BigSmart沿BoxAudioCodec默认30dB；播放期间12dB仍为本项目既有软件AEC候选策略。只改物理MIC1增益，不把MIC3参考输入一起放大。立创按来源另保持物理MIC3增益0dB：其增益mask是物理通道2，**TDM参考位置却是slot1**，不是slot2。当前AFE仍使用软件播放参考，尚未消费这个硬件参考输入；完整硬件参考适配仍是未完成项，不据四槽采集或编译宣称已通过硬件AEC。

## 两种触摸

| profile | 固定依赖 | I2C事实及状态 |
| --- | --- | --- |
| lckfb-lichuang-dev | `espressif/esp_lcd_touch_ft5x06` 1.1.1 | 本机已有；地址0x38，8bit命令、400kHz |
| rymcu-bigsmart | `espressif/esp_lcd_touch_gt911` 1.2.1 | 本机没有；固定来源默认地址0x5D，16bit命令、400kHz；实际GT911分支未编译 |

两款上游触摸以原始240×320坐标做mirrorX后swapXY。本地不启用会使用 `x_max-x` 的公共镜像，而先核原始 `x<240,y<320`，转换成 `(rawY,239-rawX)`，使零起点边界留在320×240范围。没有为异常点捏造有效坐标；读取／坐标错误取消当前LVGL按压，不触发陈旧点击。没有屏上复位／IRQ GPIO，不借用其他板触摸脚；触摸初始化失败使显示启动失败，不冒充成功或替换为另一个芯片。实际方向、边缘、复位及取消行为都待实机验收。

## 实际构建

两款都实际调用以下已有缓存构建入口：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board <lckfb-lichuang-dev或rymcu-bigsmart>
```

BigSmart两次预检均退出1，缺 `espressif/esp_lcd_touch_gt911` 1.2.1，未创建stage、未编译。后一次原始错误保存在 `evidence/pcabox-builds-20261110/rymcu-bigsmart/preflight.log`，记录为 `cached_build_attempts_20261110_pcabox_rymcu-bigsmart.json`。源码候选保留，依赖由牛牛准备，本轮不安装。

立创使用既有SDK5.5.6-dirty及17份本地组件override，Python外网守卫拒绝外网socket／DNS；SDK保留原NimBLE／idf_as_lib修改，override包含其他BSP，不是干净发行依赖锁。Kevin Box-2、微雪LCD0.85、Jiuchuan分别在既有隔离目录运行临时 `xy_pcabox_regression_20261110.ps1 -Stage <目录> -Board <profile>`，同步完整main／third_party和配置，再执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。源码复核补充PA回读失败处理后，原三个入口已开始的构建先保留结果，再同步最终代码做增量构建；不重启正在运行的构建。

归档前发现，复制保留源时间可能使Ninja忽略内容已改变但时间早于目标文件的更新。因此保存 `pcabox-regression-before-freshness.log`，对四个隔离目录中本批涉及的编译文件刷新 `LastWriteTimeUtc`，再次运行最终 `reconfigure build`；临时同步脚本改为比较内容摘要，只有内容变化才复制并刷新目标时间。最终记录要求日志出现 `audio_io.cpp.obj` 重编，不能只凭源文件摘要一致或“4/4完成”断言产物包含最后改动。本轮不修改既有SDK／工作区文件时间，不扩展为全量重复构建。

立创完整构建和最终增量构建均退出0，Kevin Box-2／微雪LCD0.85／Jiuchuan的最终回归也均退出0。最终日志确认各自重新编译audio_io，立创另实际编译PCA9557、FT5x06及双codec分支；没有编译BigSmart GT911分支。既有LVGL9.6弃用API警告保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| lckfb-lichuang-dev | 2,263,168 | 3,145,728 | 882,560（约28%） |
| kevin-box-2 | 2,188,768 | 3,145,728 | 956,960 |
| jiuchuan-s3 | 2,247,840 | 3,145,728 | 897,888 |
| waveshare-lcd-0.85 | 2,252,112 | 2,752,512 | 500,400 |

四款分别实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0。项目身份、0.85的8MB／其他16MB设置、checksum及validation hash有效。模型均2,203,819字节，0.85模型分区2,359,296字节，其余4,194,304字节，未超容量。0.85使用其专用8MB分区，未按根16MB表夸大余量。

一次性 `xy_record_pcabox_20261110.py` 退出0，四份成功记录各核对84个源码／配置摘要、完整77文件main集合、合并defaults、实际选择源及生成配置；要求最终音频文件重编日志。记录为 `cached_build_attempts_20261110_pcabox_<profile>.json`，日志、ELF／镜像、配置和源码副本在忽略目录 `evidence/pcabox-builds-20261110/`。另有BigSmart预检失败记录，共五份本批记录。只核对被选中的编译分支，不把全main文件归档当每个分支都编译。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前50个本地profile对应49个官方变体（另1历史兼容入口），139项尚无源码；累计28个官方变体曾分别通过缓存编译。当前源码4项通过、24项历史通过证据过期、6项依赖预检失败、154项未构建（其中15项已有源码）。BigSmart包含在6项预检失败中，不计入28项曾通过。累计通过数不等于当前源码通过数。

实际对本批跟踪修改执行 `git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`，退出0。新增／未跟踪源码、配置、文档使用 `git diff --no-index --check -- NUL <文件>` 空白审阅，返回1表示与NUL存在差异，未输出空白错误，保留Git的LF／CRLF提示。未运行测试套件或独立类型检查。

## 未完成的验收与板卡功能

只有现有微雪1.8实物，屏控修订仍未确认。两款新板的PCA9557实际bit0作用／PA切换及故障、触摸地址／方向／边界、80MHz信号／低有效背光、I2C1／ADC0x82／TDM槽序、MIC1增益／输入输出时钟／AEC／唤醒／静音重开、同板OTA及真实容量均未验收。所有188项实机状态仍为not_run。

硬件MIC3参考输入尚未用于AFE。两板相机／LED及原点击通话或动态切换按住模式未接入；BigSmart电池ADC11／充电3／RGB LED43／自定义键10仍未完成。未导入上游触发配网的MCP工具，不把源码存在写成整板全部功能就绪。

未运行测试套件或独立类型检查，未安装依赖、刷机／OTA、启动宿主长期服务、Git写入、上传或发布。并发网关／运行时及自然插话改动保留。
