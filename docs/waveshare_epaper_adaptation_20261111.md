# 微雪1.54电子纸分版本适配（2026-11-11起）

范围：小智v2.5.2固定提交49b4380b3faa0ceadd28a3e78aaf0f311c46c57e中的两个发行变体。当前仅V2登记源码候选；V1仍inventory_only，正在等待用户决定是否接受按键说话／USB升级的受限方案。两者均未实机验收，不能因V2成功推断V1。

## 来源和版本差异

厂家新文档：https://docs.waveshare.net/ESP32-S3-ePaper-1.54 。旧wiki目前页面正文为空。官网明确2025-11-01开始换V2，以V2贴纸或PCB左上角丝印区分，且例程不通用。

- V1：ESP32-S3FH4R2／4MB Flash／2MB QUAD PSRAM。没有套用V2分区、模型或内存预算，未新增profile。
- V2：ESP32-S3-PICO-1-N8R8／8MB Flash／8MB Octal PSRAM；厂家V2例程确认Octal，但通用sdkconfig.defaults仍残留4MB，不能直接复制。
- 厂家代码固定aaf395c1bbd86f7c5b7a2f9f0401241ff5c44956。小智板目录8文件与厂家15文件、官网两页均已归档并记录SHA256；厂商Git blob SHA也核对成功。
- V1／V2与小智两份159字节全刷／局刷波形逐字节一致，不据此推断所有硬件版本通用。屏控芯片准确料号仍未独立确认，不能擅自写作某型号。

## V2实现

新增`waveshare-esp32-s3-epaper-1.54-v2` → `assistant_wspaper2`；只选择`ASSISTANT_BOARD_WS_EPAPER=1`。复用Maza电子纸后台worker，保留Maza单独引脚／波形／296×128映射。

- 200×200 RGB565 LVGL整帧80000字节位于PSRAM；转换为5000字节1bit，MSB first，索引y×25+x/8。三个有界单色快照共15000字节PSRAM，一份5000字节内部DMA传输；不创建点云图层。
- SPI3 mode0／40MHz，MOSI13／CLK12／CS11／DC10／RST9／BUSY8高忙。厂家例程SPI2是可选host软件资源，所接物理引脚相同；本地沿固定小智SPI3，无SD共享实现。
- 159字节波形：153字节写32，尾部依次写3F、03、04三字节、2C。数据entry01、X0..24、Y199..0、cursor199。全刷24+26建立base、C7激活，局刷24／CF激活。局刷reset后重申gate和entry避免依赖残留默认寄存器。
- 后台合并最新帧，至少1秒间隔，20次局刷后全刷。LVGL锁内仅做有界转换与复制，不执行SPI或BUSY等待。必须观察BUSY置忙再回空闲才完成generation；超时停止后续传输并保留DMA／transaction存储，避免悬空内存和虚假shown回执。
- 184px正文，分页替代滚动动画；方屏卡片按钮44px。Maza旧280px正文、51px卡片／85px待机分页数值保持一致。BOOT0首次松开后启用说话和卡片短按换项／长按确认；不接触摸控制器。
- ES8311单codec，I2C0 SDA47／SCL48；MCLK14／BCLK15／WS38／DIN16／DOUT45，PA46高有效，24kHz双工与既有软件AEC。没有初始化ES7210或数字dummy codec。
- 供电输出先装载latch，再启用GPIO并读回：电池锁存17高、EPD6低、audio42低、PA46初始低。屏幕偏好不切断后台供电。PWR18保留，不复制无限等待按键松开、关机任务或深睡眠。
- 反射屏无背光，亮度／熄屏偏好保留最后图像；不宣称真实黑屏。ADC电量／充电状态未知，不复制原代码恒false或百分比估算。RTC／SHTC3／SD／NFC／触摸／LED3闪烁未实现。

V2分区：NVS9000/4000，app0 20000/2A0000、app1 2C0000/2A0000、model5C0000/240000，最大结束800000。绑定NVS位置保留。

## 构建和检查

2026-11-12收尾：新V2首次完整构建及最终源码reconfigure build退出0；旧Maza初次／最终公共回归均退出0。

| 入口 | 应用字节 | 最小app分区 | 剩余字节 | 模型字节／分区 |
| --- | ---: | ---: | ---: | ---: |
| V2 `assistant_wspaper2` | 2,196,400 | 2,752,512 | 556,112 | 2,203,819／2,359,296 |
| Maza `assistant_maza29` | 2,195,248 | 3,145,728 | 950,480 | 2,203,819／4,194,304 |

新V2stage为`I:\xy_board_builds_20261109\xy_cached_c00524edf944`。两份esptool image_info、nm、size和gen_esp32part命令均退出0；应用身份／8MB或16MBFlash／镜像checksum和hash有效，实际app与model均在分区范围。V2宏确实选中paper_display/audio_io/board_power/display_ui/main，ELF含V2两份波形及ES8311、软件AEC；Maza不含V2波形，均无dummy／ES7210／上游CustomLcdDisplay对象。

每份stage与最终工作区189个main文件集合、196份输入摘要完全匹配；每份22归档产物，44份hash匹配。最终小修之后再次同步并构建，没有把旧镜像算作新源码通过。新增10字节局刷参数采用持久存储，超时不留下栈payload。LVGL弃用API等既有告警仍存在，未扩大为全项目清理。

记录：`cached_build_attempts_20261111_epaper_*.json`和`evidence/epaper-source-20261111/`。

执行入口：`tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board waveshare-esp32-s3-epaper-1.54-v2`。仅17个已有本地组件override，offline_guard阻止外网，无安装。

既有Maza回归通过`xy_epaper_regression_20261111.ps1 -Stage I:\xy_board_builds_20261109\xy_cached_c6346a24a1d1 -Board maza-ai-esp32-s3`同步最终源码后`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=maza-ai-esp32-s3 reconfigure build`；结果以实际日志为准。

## 验收边界

仅现有微雪1.8实物可用，本批V2的屏幕方向／部分刷新残影／BUSY边沿／供电／PA爆音／MIC增益／AEC／配网绑定／提醒／OTA都未实机验收。编译和镜像检查不能替代实机。没有测试套件、安装、Git写操作、刷机、OTA上传或发布。
