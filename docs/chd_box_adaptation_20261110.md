# 虫洞 BOX 两种屏幕源码候选（2026-11-10）

后续更新：虫洞BOX3.5已完成首次完整缓存构建，使用核对过的ST7796默认初始化及SDK DCS操作；BOX2.8与LilyGO回归通过。GT3.5的+80坐标偏移及实机验收仍未完成。见[本批记录](chd35_st7796_sdk_20261112.md)。

后续更新：虫洞BOX共享触摸已保留FT6336及原探测顺序并接入GT911轮询；2.8版首次整板构建通过，3.5版仅共享源码编译并仍缺ST7796 1.3.5，不能继承整板结果。最新记录见[本批说明](gt911_chd_20261112.md)。

本批接入小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 `chd-esp32s3-box` 与 `chd-esp32s3-box-lcd35`。两款因本机缺GT911停在依赖预检，**尚未编译，更未实机验收**。只有现有微雪1.8可供后续实机验收；公开清单仍154目录／188变体。

## 显示与布局

| 项目 | BOX 2.8 | BOX 3.5 |
| --- | --- | --- |
| profile／应用身份 | `chd-esp32s3-box`／`assistant_chdbox` | `chd-esp32s3-box-lcd35`／`assistant_chdbox35` |
| 显示 | 320×240、RGB、ST7789 | 480×320、BGR、专用ST7796驱动1.3.5默认初始化 |
| 反色 | 关闭 | 开启 |
| 最终方向 | swapXY＋mirrorX | swapXY，不镜像 |
| 布局 | 复用320×240布局 | 新增480×320布局 |
| 字体 | 16px中文 | 16px中文 |

两款共用SPI3 mode0／40MHz，MOSI6／CLK7／DC4／CS5／RST NC，gap0／0，背光47高有效，初始PWM关闭。I2C1 SDA8／SCL18，16MB Flash／Octal PSRAM发布默认值。没有把上游构造中省略reset字段而产生的GPIO0当作面板复位；GPIO0保留BOOT输入。

上游3.5配置明确命名ST7796，实际板类却固定创建ST7789。这种矛盾不能当作屏控兼容证明。本候选按明确ST7796配置选择乐鑫专用组件1.3.5，其固定公开API及SPI实现位于 `espressif/esp-bsp@618b05297dae1aa3fab986d5ca7b89b328e1919a`，声明支持ESP32-S3和RGB565／COLMOD05，默认包含解锁与gamma等初始化。没有从ST7789成功推导ST7796可用；**实际面板身份、默认初始化、颜色／方向／复位仍待板卡证据及实机确认**。未复制该驱动源码进项目或缓存。

## 音频、功放与按键

两款均为ES8311输出＋ES7210输入，I2S0 MCLK2／BCLK17／WS45／DIN16／DOUT15，24kHz。复用既有TDM4／MIC1 slot0单声道路径，TX初始32位槽匹配四个16位RX槽；两codec在I2C1，地址使用实际BoxAudioCodec默认地址，ADC启用MIC1–4。待机MIC1增益30dB，播放期间沿用共用12dB；本地软件AEC，不消费上游物理参考slot1，也不声明波束成形或双麦同时处理。

PA46高有效，板级初始化先锁存低再设输出。codec的PA pin设NC，由既有显式静音生命周期控制GPIO46：静音先关PA，取消静音在实际codec回调成功后开PA，关闭音频也先关PA。I2S格式／enable错误、增益／静音回调错误与功放GPIO返回值按现有路径传播。没有套用其他板的PCA9557输出。

BOOT0低有效，首次释放后按住说话／无触摸操作卡沿用小燕子交互。触摸存在时可点操作卡；即使未装触摸，BOOT仍可选项与确认。上游五击清NVS、摄像头和网络切换等手势没有移植，不引入自动擦除配置。

## 可选触摸与边界

保留FT6336（FT5x06驱动1.1.1）和GT911（1.1.3）依赖与运行时探测。I2C1先探测FT0x38，仅NOT_FOUND才依次探测GT0x5D和公开API注明的备用0x14；每次100ms。三处均NOT_FOUND才按无触摸继续，其他总线错误或探测到设备后初始化失败向上返回，不当作可用触摸。两driver均以raw坐标返回，16／8位命令区分，RST／INT为NC，400kHz。

固定板类的GT横屏处理在rawX加80后做镜像／交换。本地保留该次序，使用有符号中间值、零基端点和显示范围检查，避免uint16回绕或`x_max-x`边界。FT使用原板类独立的反向mirrorX规则，不与GT混用：

| 控制器与屏幕 | raw(x,y)到屏幕(x,y) |
| --- | --- |
| FT 2.8 | (y,x) |
| FT 3.5 | (y,319-x) |
| GT 2.8 | (y,239-x)，包含先加80再按320宽镜像 |
| GT 3.5 | (y,x+80) |

越界点、坐标读取错误和I2C故障均重置LVGL按压，取消当前动作。FT raw边界也先校验。**GT3.5保留的上游+80修正目前只能映射显示Y=80..319，超出范围会取消；不能声称全高触摸已适配。** 两种屏幕／触摸的原点、端点、实际地址及3.5偏移仍需实物标定；持久化竖屏／自动旋转尚未实现。当前只是默认横屏源码候选。

## 依赖与尚未完成

实际命令 `python -B tools/prepare_cached_firmware_build.py --profile <两款profile> --destination <I盘独立路径> --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6` 均退出1：缺 `espressif/esp_lcd_touch_gt911`，没有创建stage或启动编译。安装组件目录也没有ST7796，3.5还需该依赖；预检只报告首个缺失项。具体参数、日志和SHA-256在 `native_dependency_preflight_20261110_chd.json` 及忽略目录 `evidence/chd-preflight-20261110/`。不自动安装。

摄像头、ML307蜂窝／网络切换、GPIO1额外按键、电池ADC／充电48、休眠、电源管理、硬件参考输入和NVS五击清除均未完成；基础Wi-Fi源码候选也未在目标配置编译。两profile不会继承立创／微雪回归成功。

本批同时复核全动S3：固定ILI9341自定义表仍有三条`cmd=0`、1字节缓冲却写长度17／41的记录，存在越界读取风险。只归档固定证据，未猜测替换为sleep-out／display-on，也未登记该板源码入口。厂家面板序列和实际codec参考模式仍待核实，全动继续排队。

固定CHD／全动／BoxAudioCodec及ST7796／GT911公开API源码归档于忽略目录 `evidence/chd-source-20261110/`，URL和SHA-256记录在 `board_research_v2.5.2.json`。公开读取并非依赖安装或目标板验收。

## 公共回归构建

立创 `lckfb-lichuang-dev` 与微雪 `waveshare-audio-board` 在原隔离目录同步本批main文件、注册表／defaults并刷新变化文件目的时间，分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build` 均退出0。只复用本机IDF5.5.6-dirty、匹配工具及17个既有组件副本，无安装。两份日志实际包含main／audio／display重编。此回归覆盖已有FT触摸／PCA9557／TDM4与已有专用JD9853／TDM4公共路径，**没有编译CHD宏、ST7796或GT911新分支**。

| Profile | 应用字节 | 3MB应用分区剩余字节 | 结果 |
| --- | ---: | ---: | --- |
| lckfb-lichuang-dev | 2,263,424 | 882,304 | reconfigure build退出0 |
| waveshare-audio-board | 2,256,096 | 889,632 | reconfigure build退出0 |

两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目名／16MB／checksum／validation hash均通过；`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认现有FT5x06／PCA9557／双codec与微雪JD9853相关符号。逐份108项输入摘要、101个main文件集合、合并defaults、实际sdkconfig、main／audio／display／UI编译宏、二进制内板型身份和分区容量匹配；模型2,203,819字节，在4MB模型分区内。

原始日志、镜像／ELF、配置、编译命令、依赖锁、源快照及SHA-256归档在忽略目录 `evidence/chd-builds-20261110/<profile>/`，轻量记录为 `cached_build_attempts_20261110_chd_<profile>.json`。SDK既有改动、17组件override和LVGL弃用告警仍保留，不当作干净发行构建。

实际矩阵同步 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`退出0。当前65官方变体有源码入口／123待源码，66本地profile含1历史兼容入口；累计40官方变体曾各自缓存编译通过。当前2 passed／38 stale／6历史依赖预检失败／142 not_run（其中19项已有源码，含CHD两款缺依赖），全部188实机not_run。未安装依赖、启动宿主服务、运行测试套件或独立类型检查、Git写入、上传、发布、刷机或OTA；全量适配目标保持进行中。

收尾实际执行 `git diff --check` 退出0，本批新增／未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。对照上批源快照检查本批板级、音频、显示／字体／布局及构建路由diff；两份108输入与16项归档产物摘要再核匹配。所有本批构建进程已结束。
