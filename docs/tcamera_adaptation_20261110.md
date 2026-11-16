# LilyGO T-CameraPlus S3 双修订适配与 MAX98357 时钟更正

日期：2026-11-10，Asia/Shanghai。新增官方`lilygo-t-cameraplus-s3`（V1.0／V1.1）和`lilygo-t-cameraplus-s3-v1.2`两个基础Wi-Fi终端源码入口。仅现有微雪1.8实物可供验收，本批不刷机，不宣称摄像头等整板功能完成。

## 来源和关键纠正

固定小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，板目录`main/boards/lilygo/t-cameraplus-s3`。厂家仓库为[Xinyuan-LilyGO/T-CameraPlus-S3](https://github.com/Xinyuan-LilyGO/T-CameraPlus-S3)，本轮固定到`d2d6d36194569706c4a48b54d5af7b7d81f6592c`（默认分支arduino-esp32-libs_V2.0.14）。

读取厂家README、两份引脚头、麦克风／音乐及Dashcam示例、PlatformIO配置、两版原理图，以及MP34DT05-A、MSM261S4030H0R、MAX98357A/B规格书；原理图和关键规格页另渲染查看。26份来源的URL和SHA256在`evidence/tcamera-source-20261110/source-artifacts.json`，本地证据目录按项目既有设置忽略。PDF文字提取用于定位，电路连接通过图页核对。

固定小智配置声明扬声器24kHz，实际构造却初始化为11025Hz，基类EnableOutput只改状态，没有把时钟改为24kHz。MAX98357A/B规格第19页明确：I2S支持8／16／32／44.1／48／88.2／96kHz，**不支持11.025／12／22.05／24kHz**。本地两版统一采用16kHz物理播放，复用既有网络24→16kHz FIR，并让同一实际播放PCM进入软件AEC参考，不复制这两处错误时钟。

MP34DT05-A第3／4页规定正常PDM输入时钟1.2–3.25MHz；16kHz×64只有1.024MHz。本地V1.2选择32kHz硬件采集，ESP-IDF DSR8产生64倍时钟即2.048MHz，再通过既有32→16kHz FIR进入语音链路。

## 分开选择硬件入口

| 项目 | V1.0／V1.1 | V1.2 |
| --- | --- | --- |
| 本地profile | lilygo-t-cameraplus-s3 | lilygo-t-cameraplus-s3-v1.2 |
| I2C0 SDA／SCL | 1／2 | 33／37 |
| SPI3 MOSI／CLK／CS | 35／36／34 | 34／35／36 |
| LCD reset | 33 | NC |
| touch reset | 48 | NC |
| 麦克风 | MSM261，I2S0 BCLK18／WS39／DIN40 | MP34DT05-A，PDM I2S0 CLK40／DIN38 |
| 硬件采集 | 16kHz、32位RIGHT槽、BCLK1.024MHz | 32kHz、16位PCM、PDM时钟2.048MHz |
| MAX98357A DIN | 38 | 39 |
| MIC／PA供电开关 | 没有移植独立使能脚 | GPIO18低有效，麦克风和功放共用 |

两版共同：ST7789 RGB240×240，SPI3 mode0／60MHz，DC45，背光46高有效5kHz／10位PWM；invert=true，无镜像、XY交换及窗口偏移。复用现有屏幕驱动／LVGL、两份20行DMA缓冲和SPI字节交换。16MB Flash／8MB QUAD PSRAM来自厂家及发行配置，使用明确QUAD defaults，实物容量未测。

旧版原理图R41把MSM261的L/R拉至VDD，R43为未装，选择右声道；使用正常WS与32位槽，不复制固定应用16位接收＋WS反相的简化。MSM261正常BCLK下限1MHz，16kHz×64=1.024MHz。读完整32位槽取PCM16后，保留原应用20倍软件放大并饱和裁剪，未提供模拟增益接口。

V1.2图纸的R52标NC、R54接地，MP34的LR应按接地相位选择，故采用PDM默认LEFT；图上“Right channel”文字与已装电阻及规格相位表不一致，明确列为实机核对项，不把注释当作电气验收。PDM转换为PCM16后同样保留20倍软件放大，实际灵敏度、削顶和噪声未测。

扬声器I2S1 BCLK41／WS42，16kHz、16位立体声，用现有固定小缓冲复制mono到左右，兼容MAX98357的声道选择；32倍BCLK为512kHz。传输复用一个1秒总超时与部分读写累计逻辑，失败不返回伪成功。静音将实际发送PCM清零，没有独立PA硬切断。

## 供电和触摸

V1.2原理图Q4／Q6是P沟道MOSFET，两路都由MIC_SPKR_EN控制，GPIO18低才供电。启动先锁存低再配置输出，等待10ms。运行期静音不拉高18，避免连麦克风供电一起关闭。图纸和厂家例程确认此逻辑，真实功放爆音、底噪和静音电平仍未验收。

背光46先暗。共享SPI的SD片选21在初始化前锁存高，防止插卡后未选中器件干扰LCD；不挂载或写SD。BOOT0初始释放后承担说话／卡片动作。KEY17只配置为保留输入，没有把原厂相机键改成音量键；IR切换AP1511B GPIO16不写，不发相机／机械动作。

CST816在0x15，100kHz，复用现有组件构造和IO。按修订使用reset48或NC，读取0x02起始6字节原始点数、事件和坐标，避免通用组件把多指截成单指。初始释放、多指、越界或读取失败会取消按压，错误恢复后重新等待释放。使用原生240坐标，不声明标定通过；没有创建原厂“任意触摸切聊天”的第二条任务。轮询不占用GPIO47中断，旧版本还将47命名为SY6970中断脚。

没有复制厂家的GPL实现或引脚头。厂家GPL文件／原理图只作为只读接线与器件资料，实际代码是项目既有SPI、数字音频、触摸驱动的板级配置和分支扩展，整体LICENSE未改。

## 已有 MAX98357 入口的同类修正

固定敏思K08 README声明MAX98357，Guition扩展功放配置明确推荐MAX98357A，原本也选择24kHz物理播放。因此本批同步把`ASSISTANT_BOARD_MINSI`与`ASSISTANT_BOARD_GUITION_EXT_AMP`改为16kHz，继续使用各自既有32位mono输出和24→16kHz FIR。Guition原板NS4168入口维持其原采样率；不能把一种芯片的限制推广到其他功放。

敏思执行独立缓存回归。Guition扩展入口真实依赖预检退出1，缺GT911，未创建stage／未编译；更正采样率不等于目标构建或实机通过。本批依赖预检命令及日志在`native_dependency_preflight_20261110_tcamera_guition_ext.json`。两份历史适配文档已追加更正，早期24kHz段落保留为历史状态。

## 实际构建与证据

使用既有ESP-IDF5.5.6、工具、Python及17份组件缓存，offline_guard隔离网络，不安装依赖、不修改SDK。两新目标分别实际执行：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board <两版profile>
```

同步全部main文件／注册表／defaults、刷新变动文件时间戳后的最终增量命令为`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，用于两新入口、旧T-Circle V1.1（PDM32采集）和敏思K08（MAX16k播放更正）。旧入口只验证共用路径，不替代新硬件验收。

T-Circle旧stage初次配置失败，实际缺12个既有组件目录；恢复本机既有来源的缺失目录后，第二次配置发现原BSP目录还缺CMakeLists等13个文件。只补回缺失文件，保留已有stage文件，没有下载／安装或改版本。两份失败原日志和修复清单保存在本批`tcircle-cache-repair.json`及对应日志，后续结果独立记录。

两个新目标首次都编译到本地音频分支，在`std::clamp`模板推导处失败：Xtensa ABI的`int32_t`是`long`，与整数字面量的`int`不同，已用`std::clamp<int32_t>`修正。构建包装命令退出1，实际idf.py退出2／Ninja退出1，原日志保留。检查生成配置时还发现新profile漏选`CONFIG_CODEC_DUMMY_SUPPORT`，已显式开启，并在已停止的两个stage的sdkconfig中修正后重新配置构建；没有安装依赖或伪报首次成功。记录在`initial-target-failures.json`，各构建记录保留失败与修正后的结果。


| 入口 | 最终缓存build | app字节 | 应用槽余量 | 模型字节 |
| --- | --- | --- | --- | --- |
| lilygo-t-cameraplus-s3 | exit0 | 2,226,080 | 919,648 | 2,203,819 |
| lilygo-t-cameraplus-s3-v1.2 | exit0 | 2,230,032 | 915,696 | 2,203,819 |
| lilygo-t-circle-s3-v11 | exit0 | 2,224,032 | 921,696 | 2,203,819 |
| minsi-k08-dual | exit0 | 2,207,184 | 938,544 | 2,203,819 |

两新入口首次失败修复后，最终reconfigure build均退出0；两旧入口最终回归退出0。四份镜像checksum／validation hash、ELF符号／段大小、二进制分区解码命令均退出0。每份182个main文件集合和189份输入摘要匹配最终工作区，编译宏／选中源／应用身份／QUAD与OCT PSRAM／16MB容量／双3MB OTA槽／4MB模型分区独立核对。旧I2S麦克风目标无PDM初始化符号，新PDM目标及T-Circle含PDM初始化；四目标均不链接ES8311／ES7210实例。SDK保留原5.5.6-dirty／HEAD880a203ae603d08a94d06aa01e0d6761958e5e9e和原NimBLE／idf_as_lib修改。每份结果记录于`cached_build_attempts_20261110_tcamera_<profile>.json`，包括真实命令／退出码、`esptool image_info --version 2`、ELF `nm -S --size-sort -C`／`size -A -d`、SDK `gen_esp32part.py`分区解码、应用身份／选中源／编译宏／PSRAM配置和源文件／产物SHA256。缓存构建不是干净安装的发行验收。未运行测试套件或独立类型检查。

未实现：OV2640相机及KEY17拍照、IR滤镜AP1511B、SD存储、SY6970的ILIM／ADC／看门狗／充电策略、硬件电量、睡眠或自动关机。没有复制固定应用对SY6970寄存器00=08、02=DD、07=8D的整字节写入。电源芯片默认看门狗／供电持续性仍是硬件验收项，不以构建推断。

两版实物的容量／接线、屏幕颜色与触摸原点、麦克风声道与PDM相位／时钟、软件增益及声学、MAX98357静音／爆音、网络绑定、提醒和OTA均未验收。没有执行Git写操作、上传、发布、刷机或OTA。

后续MVSRLoRa前置调查：已归档7份固定板类／配置／音频源码，另核对5份固定厂家提交／源码树／README／引脚头／原理图，222×480 ST7796／窗口gap49／CST226SE与共享SPI还需后续实现。扩展板原理图确认两只MP34共享PDM线、相位相反，不能把第二麦克风直接当硬件回声参考；同样存在MAX98357配置24kHz、实际11025Hz的矛盾。该官方变体仍inventory_only，没有profile／构建／实机声明，研究JSON记录后续阻塞。
