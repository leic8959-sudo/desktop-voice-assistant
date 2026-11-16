# M5Stack Stopwatch基础语音源码与缓存目标构建

日期：2026-11-10，Asia/Shanghai。新增 `m5stack-stopwatch` → `assistant_stopwatch`：16MB Flash、8MB Octal PSRAM、466×466圆形CO5300、CST820B、单ES8311和M5IOE1外围供电。真实目标缓存构建、镜像、二进制分区和链接检查通过；没有Stopwatch实物，源码／编译不等于实机运行验收。

## 固定来源与改动范围

使用小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的Stopwatch config、config.json、板类和README；对照[厂家StopWatch页面](https://docs.m5stack.com/en/core/StopWatch)。M5IOE1精确1.0.9提交 `846eec7d05e25c09013be2acdb8804487f48a62e` 的寄存器／引脚定义与实现，M5PM1精确1.0.7提交 `be9a5456c007c333e7ac963f33bfde1ffa5d82ee` 用于识别应保持原状的充电／升压设置；没有安装这两个库或复制整套实现。

共19项原文／URL／SHA-256在research和忽略目录 `evidence/stopwatch-source-20261110/`。新 `stopwatch_board.h/.cpp` 用既有I2C位更新封装实现最小GPIO接管；`stopwatch_panel_commands.h`保留原MIT命令数据及局部许可说明。显示扩展已有 `waveshare_143c_display.cpp`，按板型区分方向、引脚、gap和触摸复位，不另写第二套CO5300刷新。其他改动限CMake、板配置／能力、圆屏布局、启动失败与单codec音频选择，不改规则文件或项目整体LICENSE。

固定README称SYS I2C/I2C1，但实际板类创建I2C0；本地采用实际I2C0，SDA47／SCL48。厂家页面确认显示、触摸、ES8311、M5IOE1与PM1共享这组SYS线，不根据文字标签创建第二条冲突总线。

## M5IOE1编号与最小供电

M5IOE1 GPIO名称从1开始，枚举从0开始；下表按精确1.0.9接口核对，避免将电机和功放混淆。

| 功能 | 厂家引脚名 | 本地寄存器位 |
| --- | --- | --- |
| 音频L3B使能 | IO3 | 低字节bit2 |
| CST820B复位 | IO4 | 低字节bit3 |
| LCD复位 | IO5 | 低字节bit4 |
| L3B／LCD电源 | IO8 | 低字节bit7 |
| AW8737A功放使能 | IO10 | 高字节bit1 |
| 电机 | IO9 | 高字节bit0，未接管 |

只访问扩展器0x4f，100kHz、单次100ms超时；读UID／REV，按固定库4F／50接受的REV字符W校验，其他版本在写入前停止。UID仅报告、不编造固定型号或实际硬件修订。MODE为0x03／04（1是output），OUT为0x05／06，IN为0x07／08，DRV为0x13／14（0是push-pull）。

先预装音频／LCD电源高、两个reset和PA低，再只设置所拥有引脚的pull／drive／direction，逐次通过真实寄存器读回核对；PA另外读input寄存器核对电平。扩展器IO8／IO10也有PWM复用，分别只清PWM2高寄存器0x1e及PWM4高寄存器0x22的enable bit7；电机IO9的PWM1、其他输出／上下拉／中断／PWM配置保持原值。运行期PA更新共用100ms互斥。

LCD外置复位低10ms／高120ms；触摸复位从引脚低起累计等待200ms后释放高，再等200ms。CO5300构造reset GPIO为NC，随后组件软件复位，保持原板类的“外置复位＋软件复位”生命周期；CST构造reset也为NC，防止把本板MIC DIN16当成1.43C触摸复位脚再次切换。

没有访问PM1的0x6e地址，也没有移植 `setChargeEnable`、`setBoostEnable`、PM1 G3低电平425mA充电设置、PMIC掉电策略、电量公式或电机控制。CPU已能运行并不证明PM1各基线轨在所有USB／电池场景都合适；实际供电接管、复位／电源电平、IOE固件和AW8737A上电／增益／静音行为必须验机。寄存器读回只是代码路径，不声称本轮已在硬件上读到W或某个电平。

## 466圆屏、触摸和两键

CO5300 2.2.0，QSPI2 mode0／40MHz，CS39／CLK40／D0=41／D1=42／D2=46／D3=45，队列10，RGB565、无mirror／swap，运行期gap(6,0)。两个466×20行内部DMA缓冲共37,280字节，2像素边界rounder、LVGL port异步完成与一次字节交换，466圆屏使用独立布局，保留圆边安全区域。

固定11条初始化命令的顺序、payload、零长度条目和延时均保留，仅0x51亮度FF改为00，避免UI／触摸就绪前全亮。源中FE、3A和35的数据长度为0，CO5300构造前序仍设置RGB565；没有凭注释偷偷改为1。表的初始列窗口是0–465，实际绘制gap为6，这个差异保留待验机，不能由分辨率推断应改0或6。亮度使用组件公共API，LCD_TE38未同步，不宣称无撕裂。

Stopwatch与1.43C的共享源明确区分：前者gap6／原生方向／外置IOE reset、CST INT13；后者仍gap8／双mirror／GPIO13显示复位／GPIO16触摸复位／INT17。旧1.43C分支有真实回归，不以共用文件名替代其板型事实。

固定板类只声明CST820B而没有触摸初始化，本地按厂家硬件补齐：0x15／I2C0 400kHz、INT13输入轮询，复用CST816S family1.1.2初始化／ID读取，rst为NC以复用已完成的外置复位。family接口不做ID白名单；本地从0x02读取原始5字节count／event／12位坐标，只接受单个down／contact、0–465范围，初始先等释放。多指、保留event、越界或读错均取消按压并等释放。Stopwatch原生XY不作镜像；这是未物理标定的源码候选，方向、复位、ID及错误恢复仍待实机。

GPIO2黄色键承担说话／卡片，GPIO1蓝色键映射项目已有VolumeUp（单次10%、长按重复5%），初始释放、双键或来源切换均取消。本地没有照搬上游0→10→…→100→0循环音量，采用已有按键服务，操作差异明确保留；暂未增加硬件音量降低键。触摸可操作卡片，根页录音仍使用实体说话键，未接完整手势、原厂秒表程序或电机振动。

## 单ES8311与验收边界

单ES8311、I2C0 SDA47／SCL48、mono24kHz，I2S0 MCLK18／BCLK17／WS15／DIN16／DOUT21。固定config包含MCLK18，但板类codec构造明确use_mclk=false，本地保留它的BCLK派生codec选择及现有ESP I2S MCLK信号；是否需要／实际时钟关系由实机确认，未因宏名改成另一方案。

复用单codec真实MIC gain、DAC mute回调及软件AEC。GPIO PA为NC，由M5IOE1 IO10独立门控；静音先关IO10，只有实际DAC解除静音成功才开启，错误不继续使能。MIC增益沿用30dB；未接ES7210、硬件参考AEC或beamforming。AW8737A的实际功率／增益、PA极性、codec供电、24kHz音频／唤醒／AEC／重开仍待硬件验收。

未实现PM1电量／充电状态、RTC RX8130CE、BMI270、Grove 5V、振动电机、TE同步和完整触摸手势。电池状态仍unknown，不按450mAh规格或电压线性公式推算。联网、手机配网／绑定码、提醒／DND与同板OTA走现有基础终端源码，全部需实机验证；当前只有微雪1.8实物，屏控修订未确认。

## 实际构建与镜像证据

实际执行：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board m5stack-stopwatch
```

新目标首次完整构建退出0，stage为 `I:\xy_board_builds_20261109\xy_cached_c550803fb7ff`，复制17个既有组件、Python offline_guard阻止外部socket／DNS，没有安装或下载组件。旧1.43C共享显示回归同步改动／刷新文件时间后构建退出0。

保存初次日志后，修正登记文案和共享触摸reset说明注释，同步最终源，两板分别实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，均退出0，实际重编共享CO5300显示。原始完整编译和最终增量日志均保留，不把注释更新后的原始stage当成最终源码证据。

两组进一步执行：

```powershell
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -S --size-sort -C build/<app>.elf
xtensa-esp32s3-elf-size.exe -A -d build/<app>.elf
python -B <SDK>/components/partition_table/gen_esp32part.py build/partition_table/partition-table.bin
```

镜像／符号／section／分区解码均退出0。Stopwatch项目名assistant_stopwatch、16MB、checksum／validation hash正确；选中stopwatch_board／共享显示、CO5300、CST family、单ES8311与AEC，不含ES7210或PM1库符号。旧1.43C仍为8MB／CO5300／双codec；它通过不等于新M5IOE1硬件行为通过。

| 目标 | 应用字节 | 单OTA槽大小 | OTA槽剩余 | 模型字节／分区大小 |
| --- | ---: | ---: | ---: | ---: |
| Stopwatch | 2,542,640 | 3,145,728 | 603,088 | 2,203,819／4,194,304 |
| 旧1.43C | 2,544,976 | 2,752,512 | 207,536 | 2,203,819／2,359,296 |

二进制分区表核对：新板app0=0x20000／3MB、app1=0x320000／3MB、model=0xc00000／4MB、最大终点16MB；旧板两OTA槽各0x2a0000、模型0x5c0000／0x240000、最大终点8MB；两板NVS均0x9000／0x4000。没有物理写入或NVS读回。

两组各171个main文件集合、178项输入摘要、编译宏、生成配置、合并defaults和真实分区来源匹配工作区；各22项初次／最终日志、镜像／ELF／map、分区及配置归档，记录 `cached_build_attempts_20261110_stopwatch_<profile>.json`，证据目录 `evidence/stopwatch-source-20261110/builds/`。这是SDK5.5.6-dirty＋17个local override缓存构建，不是干净发行依赖构建。

当前99官方源码／100本地入口含1历史／89待源码，累计55官方变体曾缓存构建通过；当前2 passed（Stopwatch／1.43C）、53 stale、6历史failed、127 not_run（其中38有源码），全部188实机not_run，全量目标继续。

未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、Git写操作、上传、发布、刷机或OTA。协议、无屏绑定UI、原底座／StickS3供电／双codec封装、网关及运行时保护摘要保留，人工决策逻辑未改。

收尾实际执行 `git diff --check` 退出0；22项新增／变更文件逐项 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1表示新增差异。19项来源、两组各171个main文件／178项输入／22项归档产物摘要匹配；11条命令仅声明的51亮度修改，M5IOE1一基编号与低9c／高02掩码核对。原99个profile与其他research保留，7项保护文件摘要相同；矩阵摘要／目标passed_cached_sources／99官方源码／全部188实机not_run匹配。两个最终构建进程均已结束，未执行设备写入。
