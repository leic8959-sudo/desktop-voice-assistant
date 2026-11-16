# Spotpear 1.28 BOX／1.54 MUMA 基础适配

后续更新：本页为历史范围记录；Spotpear1.28BOX、MovecallMoji/璀璨、Electron现已用SDK DCS操作保留原GC9A01默认42命令，各自成功完成整板构建。专用GC9107分支未替换。当前依赖963声明、74本地/73官方满足直接条件，最新来源和限制见[本批记录](gc9a01_default_sdk_20261112.md)。

日期：2026-11-09，Asia/Shanghai。目标仍是固定小智v2.5.2的154个发布目录／188个变体。此批新增两个源码候选后，30个本地profile对应29个官方变体，另有一个历史微雪兼容入口；159个官方变体尚无本地实现。全部188项的实机验收仍未执行，牛牛目前只有现有微雪1.8，屏控修订尚未确认。

## 两款硬件分别处理

固定来源为`78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，目录分别为`main/boards/spotpear/sp-esp32-s3-1.28-box`和`main/boards/spotpear/sp-esp32-s3-1.54-muma`。详细URL随同调查登记在`board_research_v2.5.2.json`。README商品身份为N16R8，按16MB Flash／Octal PSRAM建立独立配置，尚未测量实物。

| profile | 应用身份 | 显示 |
| --- | --- | --- |
| spotpear-sp-esp32-s3-1.28-box | assistant_spbox128 | 240×240 GC9A01圆屏，BGR，40MHz mode0，反色，X镜像，gap0 |
| spotpear-sp-esp32-s3-1.54-muma | assistant_spmuma154 | 240×240 ST7789方屏，RGB，60MHz mode0，反色，无镜像，gap0 |

共同SPI3引脚为CLK4／MOSI2／CS5／DC47／RST38，背光GPIO42反向PWM，启动占空比1023对应关闭。MUMA实际板类使用60MHz，而config宏为40MHz，本地按实际调用实现。原板类的扩展器句柄始终为null，`EnableLcdCs()`无实际操作，因此使用原SPI IO的GPIO5片选，没有把数值5猜成扩展器位。

BOX保留0x62／63／C3／C4调参，MUMA保留0xBB=0x38，每次写入检查返回值。BOX的MADCTL0x48通过BGR＋驱动mirror接口表达，让驱动内部缓存和LVGL登记后的方向一致。GC9A01使用组件2.0.4，其固定实现位于`espressif/esp-bsp@618b05297dae1aa3fab986d5ca7b89b328e1919a/components/lcd/esp_lcd_gc9a01`；已读取公开API，未将驱动下载安装到缓存。板级调参的MIT来源通知保留在`main/third_party/xiaozhi_spotpear`，未改项目整体许可证。

音频均为单ES8311双向24kHz：I2C0 SDA15／SCL14，MCLK16／BCLK9／WS45／输入10／输出8／PA46。复用现有单codec、24→16kHz采集和24kHz播放参考路径。GPIO3按原实际电源初始化设置RTC输出高；未实现自动关机／深睡、充电检测和电池遥测。内置LED、SD视频播放等也不属于此批已实现功能。

## 可选触摸与屏幕大小

CST816D位于独立I2C1 SDA11／SCL7、RST6／IRQ12，地址0x15。先复位再探测；只有明确NACK（ESP_ERR_NOT_FOUND）才释放自己创建的总线并以无触摸模式继续。总线配置、超时等错误仍作为初始化失败，不伪装成无触摸版本。

有触摸时复用CST816S1.1.2组件读取0x02起的坐标帧；固定原CST816D类使用相同字节布局。读取失败／越界取消当前LVGL按压。无触摸时不分配pointer，BOOT GPIO0仍用于按住说话、卡片短按选择／长按松手确认；长卡片文字使用自动滚动。有触摸的版本也保留BOOT入口，卡片操作继续经过原ID／revision及待确认状态检查。它们仍需实机检查坐标映射、同时操作、断连恢复及录音取消，不把协议静态相同当作硬件验收。

BOX有独立圆屏布局，文字和按钮向内收，短标题／配对提示和提醒“稍后”按钮避开圆边。MUMA使用已有240px方屏布局。屏幕尺寸是编译时板级配置；每块板有独立驱动、引脚和镜像身份，当前没有一份二进制自动识别所有板卡。

## 实际命令和证据

两款分别执行以下缓存构建入口，使用已安装SDK5.5.6-dirty、既有17个组件本地override、Python外网守卫，设置`IDF_PY_BUILD_JOBS=2`：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board <对应profile>
```

BOX预检实际退出1：缺少`espressif/esp_lcd_gc9a01 2.0.4`，未进入编译，也未创建隔离目录。失败日志及摘要已登记在`cached_build_attempts_20261109_spotpear_spotpear-sp-esp32-s3-1.28-box.json`。该驱动依赖需由牛牛后续准备，不能用MUMA成功替代BOX构建。

MUMA完整缓存构建及最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=spotpear-sp-esp32-s3-1.54-muma reconfigure build`均退出0。微雪LCD1.83／AMOLED1.8-v2分别在其既有隔离目录执行相同形式的独立reconfigure/build，均退出0；LCD1.83在末尾圆屏按钮文案修改后又构建一次，保留初次日志。没有将构建日志中的烧录建议作为命令执行。

三个应用分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用身份／16MB设置正确，checksum及validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| spotpear-sp-esp32-s3-1.54-muma | 2,542,224 | 603,504（约19%） |
| waveshare-lcd-1.83 | 2,615,584 | 530,144 |
| waveshare-amoled-1.8-v2 | 2,614,832 | 530,896 |

每款59个源码／配置摘要及完整main文件集合与当前工作区一致；记录为`cached_build_attempts_20261109_spotpear_<profile>.json`，日志、镜像、配置和源码副本在忽略目录`evidence/spotpear-builds-20261109/`。SDK仍保留原NimBLE／idf_as_lib修改并自报5.5.6-dirty，LVGL已有弃用警告仍在；本地override包含未选用的历史BSP，不属于干净发行依赖锁。

矩阵当前有3个变体通过本批源码缓存构建，另10个历史通过变为stale；累计13个官方变体曾各自编译通过。其余15个源码入口未构建，BOX1项预检失败；159项尚无源码入口。全部188项实机未验收，不能把出现于摘要中的未选宏分支称为已经编译。

实际执行`git diff --check`及针对新文件的`git diff --no-index --check -- NUL <文件>`进行差异审阅，无空白错误，存在Git的LF转CRLF提示。执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`刷新清单；这不是固件测试。

没有运行测试套件／类型检查，没有安装依赖、刷机、OTA、启动服务、Git写入、上传或发布。并发网关／运行时及自然插话改动保留。
