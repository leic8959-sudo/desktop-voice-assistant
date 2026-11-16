# 微雪AMOLED1.43C源码与8MB缓存目标构建

日期：2026-11-10，Asia/Shanghai。新增 `waveshare-esp32-s3-touch-amoled-1.43c` → `assistant_ws143c`：466×466圆屏、CO5300、CST820、ES8311＋ES7210。已完成真实缓存目标构建／镜像／分区／容量核对，仍没有1.43C实物或干净发行依赖构建，不能称为实机适配完成。

## 同型号来源与驱动选择

固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的config／板类／README，固定厂家提交 `waveshareteam/ESP32-S3-Touch-AMOLED-1.43C@8cea11ddae6167dd591d1f5465fd7b615080360f` 的同型号显示／触摸／音频示例，以及[厂家1.43C产品页](https://www.waveshare.net/shop/ESP32-S3-Touch-AMOLED-1.43C.htm)共同核对。产品页明确CO5300、CST820、8MB Flash和8MB PSRAM；小智和厂家示例却调用SH8601命名驱动。英文C型wiki尚无有效技术内容，未套用非C型FT3168页面。

本地采用已经安装的CO5300 2.2.0公共API；CST820复用已安装CST816S family1.1.2的复位／初始化接口，原始count／event由本地按厂家协议读取。没有安装驱动或把C型当成1.32／1.75硬件。24项来源的URL／原文／SHA-256归档于忽略目录 `evidence/ws143c-source-20261110/`，更新原有1.43C research finding，保留其余finding，没有重复追加。

新增 `waveshare_143c_display.cpp`、`waveshare_143c_commands.h`、8MB profile／manifest／defaults／partitions和 `third_party/xiaozhi_ws143c/` 许可说明。公共代码仅增加1.43C的CMake、GPIO46启动、main失败处理、板能力及双codec分支。12条命令数据保留MIT版权／许可及修改说明，未复制整份厂家板类或UI类，不改规则文件和项目整体LICENSE。

## 显示、偏移和亮度

QSPI2 mode0／40MHz、队列8，CS15／CLK14／D0=9／D1=10／D2=11／D3=12，LCD复位13，RGB565。466圆形UI，两个466×20行内部DMA缓冲，共37,280字节，LVGL port负责异步完成与一次字节交换；2像素边界rounder与466偶数尺寸一致。采用mirrorX／mirrorY使MADCTL保持C0，避免LVGL默认轴设置覆盖源码在构造后补写的180度方向。

保留全部12条厂商命令的顺序、延时和其他payload，只把0x51初始亮度FF改为00，显示／触摸／UI未就绪前保持暗。运行期使用CO5300公共亮度API，0–100映射由组件完成，不把PWM引脚当AMOLED背光。显示失败尝试关闭PA并停止本次启动，保留配置。

两套固定示例的运行期gap都是(8,0)，而初始化CASET为6–471、RASET为0–465。本地保留实际运行期gap8，未凭计算改成6；每次绘制按驱动实际gap写窗口。列起点差异、居中／边缘、颜色、C0方向和触摸对应必须实物验收。LCD_TE8未接入同步，不能宣称无撕裂；保持后台运行，不加入新睡眠／关机策略。

## 触摸与按键

厂家CST820为I2C0 SDA47／SCL48、400kHz、地址0x15，GPIO16复位／17中断。启动先16高200ms，再由family驱动低200ms／高200ms；驱动读A7并记录ID但不做ID白名单。本地没有由编译结果宣称实际读到某个芯片。17配置输入后显式禁用IRQ，使用轮询，不注册中断回调。

Family坐标接口会把多指数量压成1且不暴露event，因此只复用其reset／init，通过100ms I2C IO读取0x02开始的5字节原始count、event与12位X／Y。只接受单个down／contact，up或无点正常释放；多指、保留event、错误或坐标超出0–465均取消LVGL按压并等待释放。初始先等释放，不用失败包触发后续点击。镜像坐标为 `x=465-raw_x`、`y=465-raw_y`，修正厂家 `466-raw` 的端点问题，方向仍未物理标定。

BOOT0使用既有先释放后的说话／卡片输入，触摸根页按住说话与卡片操作也保留。小智固定板类只声明touch变量而未初始化；本地触摸路径由同型号厂家示例补齐，没有根据产品名推断触摸可用。

## 音频与硬件边界

ES8311输出＋ES7210输入，I2C0共用47／48，24kHz，MCLK38／BCLK39／WS40／DIN42／DOUT41。复用既有TDM4／MIC1 slot0输入和单声道软件AEC，不将BoxAudioCodec的I2C1默认端口套入此板，也不宣称启用原厂两路硬件参考AEC。24kHz来自固定小智板配置；厂家音频演示使用16kHz，两者不等于已测得本板24kHz声学表现。

PA46先锁存低再设输出，实际DAC解除静音回调成功后才使能，静音先关PA。增益沿用30dB和公共实际回调检查。厂家头文件另有CODEC_LDO_EN_PIN18，但所核对的音频init／main和小智板类均未使用，未凭一个未使用宏驱动18；它的实际物理作用仍需确认。

未接电池ADC4、LED5、充电状态7、充电调参、SD、stock MCP、硬件AEC或TE同步。电池状态继续未知。实际供电／PA极性、触摸复位与范围、24kHz时钟／槽位／增益／静音重开／AEC唤醒、联网／手机绑定／提醒及同板OTA均待实机；现有实物仍只有微雪1.8，屏控修订未确认。

## 8MB分区与真实构建

复用已有8MB分区布局，保留NVS0x9000／0x4000和OTA数据0xd000／0x2000：app0从0x20000、app1从0x2c0000，两槽各0x2a0000（2,752,512字节）；model从0x5c0000、大小0x240000（2,359,296字节），全部分区最大终点恰为0x800000。没有把16MB默认分区直接用于8MB设备。

首次真实执行：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board waveshare-esp32-s3-touch-amoled-1.43c
```

准备阶段通过，复制17个既有组件到隔离目录 `I:\xy_board_builds_20261109\xy_cached_a9d2797d00c7`；Python offline_guard阻止外部socket／DNS，没有下载或安装。首次编译在CST816S初始化宏的C++字段顺序处失败，脚本退出1、内部idf退出2。已归档完整失败日志与当时display源文件；修正仅在本地调用处改为显式初始化dev_addr／控制字节／命令位宽／超时等字段，不修改已安装组件。

随后同步源并刷新改动文件时间，实际执行：

```powershell
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-touch-amoled-1.43c reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=espressif-esp32s3-korvo2-v3 reconfigure build
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -C build/<app>.elf
python -B <SDK>/components/partition_table/gen_esp32part.py build/partition_table/partition-table.bin
```

新目标与旧Korvo2最终构建、两镜像／符号检查及新目标二进制分区表解码均退出0。新日志实际重编1.43C display；新链接符号含CO5300亮度API、CST family初始化、ES8311／ES7210和TDM4，项目名 `assistant_ws143c`、8MB、checksum／validation hash匹配。Korvo2初次日志重编公共main／audio／power／双codec／旧显示；修正后同步了它未选中的新display文件并再次成功构建，最终源集合一致，不声称第二次又重编全部公共源。

| 目标 | 应用字节 | 单个OTA槽余量 | 模型字节 | 模型分区余量 |
| --- | ---: | ---: | ---: | ---: |
| 新1.43C／8MB | 2,544,880 | 207,632 | 2,203,819 | 155,477 |
| 旧Korvo2／16MB | 2,260,112 | 885,616 | 2,203,819 | 1,990,485 |

各161个main文件集合／168项输入摘要、所选宏和生成配置、实际分区来源与工作区匹配。1.43C归档20项产物，含解码分区表、初始失败日志和失败源；Korvo2归档18项，含初次／最终日志。记录为 `cached_build_attempts_20261110_ws143c_<profile>.json`，产物／源码快照在 `evidence/ws143c-builds-20261110/`。这是IDF5.5.6-dirty／17组件local override缓存构建，不是干净发行依赖构建；大小通过也不代表设备升级或NVS读回通过。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：96官方源码变体／97本地入口含1历史入口／92待源码；累计53个官方变体曾缓存编译通过。当前2 passed（新1.43C／Korvo2）、51 stale、6历史failed、129 not_run（其中37有源码），全部188实机not_run，全量目标继续。

未运行测试套件／独立类型检查，未安装依赖、启动宿主长期服务、Git写操作、上传、发布、刷机或OTA。headless_ui、3.5／3.49／4B封装及网关／运行时保护摘要保留。

收尾实际执行 `git diff --check` 退出0；21项新增／变更文件逐一 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1代表新增差异。24项来源、两组各168项输入及新目标20／旧目标18项归档产物摘要匹配；12条命令逐项一致，仅声明的51亮度FF改00。原96个profile和其他research内容保留，更新既有1.43C finding未重复追加；矩阵摘要／目标passed_cached_sources／全部188实机not_run一致，6项保护文件摘要相同，两个最终构建进程均已结束。
