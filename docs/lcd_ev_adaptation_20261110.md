# 乐鑫 LCD EV Board 三变体适配记录

后续更新：LCD EV Board2已保留GT1151产品与报告检查，完成首次整板缓存构建；Board1.5的RGB／FT／16kHz回归通过。实机及声学仍未验收。见[本批说明](gt1151_lcdev2_20261112.md)。

日期：2026-11-10，Asia/Shanghai。范围是基础小燕子语音终端源码，不代表整板功能或实机验收完成。仅有现有微雪1.8实物，三款LCD EV均未刷机。

## 固定来源与硬件差异

- 小智v2.5.2：`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，`main/boards/espressif/esp32-s3-lcd-ev-board`及`esp32-s3-lcd-ev-board-2`。
- [乐鑫官方指南](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-lcd-ev-board/user_guide.html)。
- [乐鑫BSP固定源码](https://github.com/espressif/esp-bsp/blob/6f82fd66a099cfd388cf9376ca1c5d6dcc98381e/bsp/esp32_s3_lcd_ev_board/src/bsp_sub_board.c)，同时读取该提交的板级头、主BSP、组件声明及GT1151源码。来源URL、SHA256和本地快照见`evidence/lcdev-source-20261110/source-artifacts.json`，证据目录按项目既有设置忽略。

| 本地／官方变体 | 显示与触摸 | I2C0 SDA/SCL | RGB D6/D7 | 实际音频采样率 |
| --- | --- | --- | --- | --- |
| espressif-esp-s3-lcd-ev-board-1p4 | SUB2 GC9503CV 480×480＋FT5x06 | 8/18 | 47/48 | 16kHz |
| espressif-esp-s3-lcd-ev-board-1p5 | SUB2 GC9503CV 480×480＋FT5x06 | 47/48 | 8/18 | 16kHz |
| espressif-esp-s3-lcd-ev-board-2 | SUB3 ST7262E43 800×480＋GT1151 | 47/48 | 8/18 | 24kHz |

本地接线选择跟随固定发行配置。厂家新BSP按模组类型区分这两套接线，而不是自动识别“1.4／1.5”板名；此处没有实现模组检测，选择入口前仍需核对实物修订。16MB Flash／Octal PSRAM采用固定发行默认；不能从厂家当前N16R16V配置推断所有旧板实物都具有16MB PSRAM。

固定小智Board2复制了GC9503初始化，和官方SUB3屏控不符。本地采用厂家直接RGB路径及18MHz、H40/40/48、V23/32/13、PCLK负沿，不发GC9503三线命令。两个SUB2保留16MHz、H10/10/20、V10/10/10及全部时序极性0，初始化前VSYNC3拉高。

## 实现范围

新增`lcd_ev_board.h/.cpp`、`lcd_ev_display.cpp`、`lcd_ev_panel_commands.h`，接入现有profile、CMake、电源、显示布局、主入口和音频分支。

- RGB：HS46／VS3／DE17／PCLK9，数据脚`10,11,12,13,14,21,D6,D7,45,38,39,40,41,42,2,1`。两份PSRAM原生RGB565 framebuffer，10行bounce、DMA burst64，LVGL直接帧与RGB完成同步。无像素字节交换、镜像或XY交换。
- SUB2：复用既有三线IO组件，TCA9554 P1=CS、P2=SCL、P3=SDO，mode0上升沿。SWRESET等待120ms、B1=10、COLMOD=60，以及固定源43条有效命令的顺序／数据／零长度条目／延时全部保留；控制器18位参数与RGB565总线16位是两个配置层。无论初始化是否成功都尝试删除临时IO，LVGL不持有该句柄。局部Apache-2.0许可和修改说明在`main/third_party/xiaozhi_lcd_ev/`，整体LICENSE不变。
- TCA9554：0x20／100kHz。避免已安装TCA构造函数的全端口复位，通过公开`esp_io_expander_t`接口适配既有三线IO，只修改拥有的P0..P3；Board2只拥有P0。输出／方向读实际寄存器，掩码RMW并读回，其他位保留。PA先预装低再使能输出，P0输入极性单独清零并读物理输入确认。显示初始化后才启动音频，运行期PA写入互斥；实际DAC解除静音成功后才开启PA。
- 触摸：复用官方组件构造和I2C IO。FT5x06在0x38读取原始点数／事件，GT1151在0x14按公开协议读取有界11字节单指报告并核对校验和／点数／清报告写入；多指、越界、读取或清报告失败取消当前按压，释放后才重新接收。两者reset／INT均NC，保留厂家原生坐标；无硬件标定结果。GT1151路径因缺依赖尚未目标编译。
- 音频：共用ES8311＋ES7210，MCLK5／BCLK16／WS7／DIN15／DOUT6。ES7210的codec API地址0x82是8位表示，实际I2C地址0x41。TX标准I2S32位槽与RX四槽TDM，取MIC1／slot0。原共用codec后端按profile初始化16kHz或24kHz，并限制两侧时钟采样率相同；已有板型仍为24kHz。两个SUB2通过既有FIR把网络24kHz播放降为16kHz，同一实际播放样本进入软件AEC参考。未启用厂家MIC3硬件回声参考或波束成形。
- BOOT0初始释放后承担说话／卡片操作，触摸卡片按钮48px，两个分辨率分别布局。没有接入UVC／USB host相机、状态LED4、SD、硬件电量、睡眠等外围功能。

## 固定背光限制

乐鑫BSP明确不支持背光调节。小智配置中的GPIO19背光宏没有板类消费者，GPIO19在厂家资料中是USB D-；本地不驱动它。控制台采用UART0而非USB，USB host也未启用。

启动界面跳过不支持的亮度设置，屏幕保持硬件固定背光；空闲熄屏／夜间调光未实现。显式亮度偏好返回`ESP_ERR_NOT_SUPPORTED`，现有协议会如实返回`display=false`，不伪报已应用。音频和屏幕内容仍可正常执行各自逻辑，实际功能仍待硬件。

## 构建与验收

使用既有ESP-IDF5.5.6、工具和17份本地组件缓存，在独立I盘目录构建；没有安装依赖或改动SDK。缓存构建有持续授权；没有运行测试套件或独立类型检查。

目标完整构建命令（分别将`<profile>`替换为V1.4／V1.5全名）：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board <profile>
```

最终增量命令在同步所有main源码／注册表／defaults并刷新变化文件时间戳后执行：`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。旧微雪1.43C作为24kHz双codec回归；其结果不能代替新板验收。


| 入口 | 最终缓存build | app字节 | 应用槽余量 | 模型字节 |
| --- | --- | --- | --- | --- |
| espressif-esp-s3-lcd-ev-board-1p4 | exit0 | 2,541,408 | 604,320 | 2,203,819 |
| espressif-esp-s3-lcd-ev-board-1p5 | exit0 | 2,541,408 | 604,320 | 2,203,819 |
| waveshare-esp32-s3-touch-amoled-1.43c | exit0 | 2,544,976 | 207,536 | 2,203,819 |

两个新目标完整构建和最终增量、旧1.43C增量均退出0；三项镜像checksum／validation hash、ELF符号／段大小、二进制分区解码命令均退出0。每份177个main文件的集合及184份输入摘要与最终工作区一致，编译宏、应用身份、16／8MB容量、双OTA槽和模型分区均独立核对；产物及日志摘要已归档。缓存不是干净依赖发行验收。SDK仍为既有5.5.6-dirty，HEAD880a203ae603d08a94d06aa01e0d6761958e5e9e；原NimBLE和idf_as_lib修改保留。既有LVGL弃用提示等警告保留，新板文件没有编译错误。记录为`cached_build_attempts_20261110_lcdev_<profile>.json`。Board2真实预检命令：

```text
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp-s3-lcd-ev-board-2 --destination I:\xy_board_builds_20261109\lcdev2_preflight_20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1，缺少`espressif/esp_lcd_touch_gt1151=1.1.0~1`；未创建stage、未编译，记录在`native_dependency_preflight_20261110_lcdev2.json`。阅读公共头文件／源码不代表安装组件，也不代表目标通过。

三板的引脚修订、RGB时序稳定性／颜色、触摸原点、多指取消、PA电平、音频时钟、声学、配网绑定、提醒和OTA均未实机验收。没有执行Git写操作、上传、发布、刷机或OTA。
