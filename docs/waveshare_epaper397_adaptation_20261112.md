# 微雪ESP32-S3-ePaper-3.97适配（2026-11-12）

范围：小智v2.5.2固定49b4380b3faa0ceadd28a3e78aaf0f311c46c57e的`waveshare-esp32-s3-epaper-3.97`基础语音终端。新入口`assistant_wspaper397`仅选择`ASSISTANT_BOARD_WS_EPAPER397=1`，未实机验收。

## 厂家证据与差异纠正

本批19来源归档在`evidence/epaper397-source-20261112/source-artifacts.json`：小智板目录6文件、厂家3.97参考代码5文件／固定9b12d40731a80213b927ee8a421cae4082952819、官网商品／文档／资料页、厂家原理图与屏幕规格书和TG28规格书，以及两份仅供寄存器合同交叉核对的GxEPD2比较源。

- 厂家商品／文档和原理图U7确认N16R8：16MB Flash、8MB Octal PSRAM；屏幕手册PDF第7页明确SSD1677。逻辑画布沿厂家例程为800×480，实物方向尚未验收。
- 原理图第1页UP1标注TG28，与上游AXP2101 API所用寄存器兼容。ALDO2接Audio_VCC（MIC与ES8311模拟电源），ALDO3接EPD_VCC_AXP，经Q3/Q2控制屏幕供电。ALDO1的AudioCTR_VCC只经未贴R74连接功放CTRL，实际贴装R23=0R由GPIO39控制NS4150B，功放VDD来自VCC3V3。
- TG28规格书第41–42页确认90的bit1/bit2分别使能ALDO2/3，93/94低5位按0.5V＋0.1V×值设压，28对应3.3V，上3位只读。只修改这两路低5位和90掩码06并读回；ALDO1和其他DCDC/LDO、充电61–64、电源按键22/27均不写。
- 厂家原始代码的`EPD_Init`把Y递减窗口设为479..0，却把cursor写为0；小智局刷helper还将传入的像素X再乘8并存在参数顺序问题。GxEPD2相同SSD1677/800×480器件合同也确认entry01为X增/Y减、X为16位像素地址、Y从窗口最后一行起写。本地明确X0..799、Y479..0、cursor479，不复制旧helper。
- 厂家完整基准例程同时写24/26，再F7全刷；局刷FF后本地显式更新26作为下一次旧图，避免靠未确认的自动拷贝或仅写24建立基准。比较源仅用于解释寻址和显示更新寄存器，未导入／链接GPL实现、类、函数或资源。

PDF已用pypdf提取相关页，并用Poppler渲染核对原理图第1页、屏幕第7页、TG28第41/42页；对应PNG与原始PDF保留作证据，未改原始PDF。

## 实现

复用现有纸屏worker，不引入第二套LVGL或刷新服务。Maza、微雪1.54 V2、NOTE4保留各自面板分支与极性。

- SPI3 mode0／20MHz（厂家参考20MHz；固定小智40MHz未沿用），MOSI12、CLK11、CS10、DC9、RST46、BUSY3高忙。48,000字节内部DMA快照作为一次SPI事务发送，bus.max_transfer_sz明确覆盖；无需栈payload／每块临时分配。48KB连续DMA余量仍需实机量测。
- RGB565整帧768,000字节PSRAM，三份48,000字节单色快照共144,000字节PSRAM，按y×100+x/8、MSB first转换。`text_only`不分配点云画布；相同数据不重复刷屏。
- 全刷reset 50/2/50ms、SWRESET与10ms等待，18=80内部温度、0C=AE-C7-C3-C0-80、01=DF-01-02、11=01、X/Y/window/cursor独立设置。写24新图与26基准，21=40-00后F7/20激活。
- 局刷进入时保留已建立RAM，设置border80、entry01和窗口，21=00-00后FF/20激活；完成真实BUSY忙→闲后重新寻址并写26作为下一基准。不会在切入局刷时硬件复位并破坏旧图。
- 后台合并帧，至少1秒刷新间隔，20次局刷后全刷。SPI排队／完成超时各1秒、BUSY10秒、忙断言1秒。超时停止后续SPI并保留持久transaction、DMA和长参数，generation不提前完成。LVGL锁内只做有界转换／复制，不等待物理刷新。
- I2C0 SDA41／SCL42，TG28兼容地址34；既有PMIC helper先读当前使能和电压，仅对93/94低5位、90的06置位做保留其他位的写入并读回。不复制上游80/90/91整组关闭、DC1重设、充电电流或300秒自动关机。
- PA39先置低、解除旧hold、启用pad并检查，随后交给ES8311驱动；无PA hold。PWR1保留输入，BOOT0初次松开后接受说话／卡片选择确认。
- 单ES8311 24kHz双工，MCLK13/BCLK14/WS47/DIN21/DOUT48、PA39；模拟和功放3.3V增益参数来自图纸。沿用16kHz采集处理／网络24kHz和实际PCM软件AEC，无ES7210或dummy codec。
- 复用项目31px字体，现有16px中文字库补齐缺字，20px标题，768px分页正文，56px按钮。纸屏布局改从已登记尺寸计算；旧Maza的280px正文／51/85分页、微雪V2的184px正文／99/157分页、NOTE4的384px正文／199/257分页保持原值。反射屏亮度／熄屏偏好保留最后图像，后台继续运行。
- 新板显示启动失败保持供电并关闭实际PA39后停止启动；刷新任务后续失败独立停更。没有给后台添加深睡眠或电源切断。

根16MB分区保留NVS9000/4000、双app20000/300000与320000/300000、model C00000/400000。绑定凭据、音频授权与既有初始唤醒逻辑不改。

未初始化RTC、SHTC3、IMU、SD、拨轮业务、充电LED／电量ADC；电量未知，不宣称上游整机应用和电池管理完成。实际R74/R23贴装依硬件修订复核。

## 实际构建与核对

目标首次完整构建及最终字库回退修改后的reconfigure build均退出0。旧微雪V2最终同步后重构建退出0，Maza与NOTE4最终源码回归退出0。没有将首次旧源码镜像当作最终源码通过。

| 入口 | app字节 | app分区字节 | 剩余字节 | model字节／容量 |
| --- | ---: | ---: | ---: | ---: |
| 3.97 `assistant_wspaper397` | 2,738,592 | 3,145,728 | 407,136 | 2,203,819／4,194,304 |
| V2 `assistant_wspaper2` | 2,196,416 | 2,752,512 | 556,096 | 2,203,819／2,359,296 |
| Maza `assistant_maza29` | 2,195,264 | 3,145,728 | 950,464 | 2,203,819／4,194,304 |
| NOTE4 `assistant_zectrix` | 2,196,928 | 3,145,728 | 948,800 | 2,203,819／4,194,304 |

新板首次镜像未含最终CJK回退，较小；以表中最终2,738,592字节为准。四份esptool image_info、nm、size、gen_esp32part均退出0，应用身份／Flash容量／checksum和验证hash有效，所有app/model在对应分区内。3.97确实选中新宏和纸屏／单ES8311／PMIC／大字库分支，ELF含kPaperUiFont和Montserrat20、实际静态单色快照总计144,000字节以上external BSS，动态整帧与DMA分配仍需实机检查。明确无Maza／微雪1.54波形、NOTE4双位行、ES7210或dummy codec。

每个stage与工作区191个main文件集合／198份输入摘要一致，各21份归档产物共84份SHA256匹配；5份厂商Git blob也匹配。记录见`cached_build_attempts_20261112_epaper397_*.json`和`evidence/epaper397-source-20261112/`。LVGL弃用API等既有告警保留。

目标实际命令：`tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board waveshare-esp32-s3-epaper-3.97`，stage `I:\xy_board_builds_20261109\xy_cached_2dffb6dc07f3`。

使用本机17组件override、既有SDK5.5.6-dirty、offline_guard与2个构建作业；没有安装依赖。旧入口用`xy_epaper397_regression_20261112.ps1 -Stage <各stage> -Board <profile>`同步后实际执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，新板最终字库回退修改也要同步后再构建。镜像、ELF、分区和最终输入摘要另记，初次旧源码通过不算最后源码通过。

## 尚未验收

只有现有微雪1.8实物，本板的800×480实际方向、cursor479修正、F7/FF和旧图基准、BUSY时序、48KB连续DMA／PSRAM运行余量、31px文字实际可读性、TG28电压及R74贴装、MIC增益／PA噪声／AEC、配网绑定／提醒／OTA均待实机。构建／镜像与图纸不能代替实机。

没有运行测试套件、Git写操作、安装、上传发布、刷机或OTA。
