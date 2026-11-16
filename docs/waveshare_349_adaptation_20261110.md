# 微雪3.49原版与V2基础语音源码适配

后续更新：3.5B／3.49／3.49V2三个AXS目标已分别完成首次整板缓存构建，原版ST7796回归通过；原整帧DMA、触摸、电源差异保留，实机未验收。见[本批说明](axs_native_20261112.md)。

日期：2026-11-10，Asia/Shanghai。新增两个独立源码候选：`waveshare-esp32-s3-touch-lcd-3.49` → `assistant_ws349`、`waveshare-esp32-s3-touch-lcd-3.49-v2` → `assistant_ws349v2`。真实预检均缺AXS15231B 2.1.0，在创建stage前停止；目标尚未编译，没有这两种实物验收。

## 来源和修订差异

固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的两板config、板类、自定义显示和README，核对AXS15231B精确2.1.0源及厂家仓库提交 `def6edd0b6e1925ed09702eed01a2f181afdf8c1` 的原版音频／显示例程、板配置。当前厂商原理图URL对应V2式接线，另存PDF和摘要，不将它的GPIO8中断线套入原版背光。25项原文／URL／SHA-256归档在忽略目录 `evidence/ws349-source-20261110/` 并记入research的两变体同组finding。

| 接线 | 原版 | V2 |
| --- | --- | --- |
| LCD复位 | GPIO21 | TCA9554 P5 |
| 背光PWM | GPIO8低有效 | GPIO42低有效 |
| 背光使能 | 无独立本地控制 | TCA9554 P1，显示／PWM就绪后使能 |
| GPIO8用途 | 背光 | 扩展器中断，不作背光输出 |

固定V2 README直接说明上述修订；厂家原版例程支持GPIO21／8，当前PDF标示P5／P1／GPIO42／GPIO8。PDF还显示ESP32-S3R8和XM25QH128外置16MB Flash，固定发行defaults为16MB／Octal PSRAM；仍需实际设备容量与修订确认。

新增 `waveshare_349_board.h/.cpp`、两套manifest／defaults。共用已有 `waveshare_35_display.cpp` 的AXS整帧路径，按选中板型保留3.5B旋转传输并加入3.49原生传输；公共改动限CMake、main失败处理、board_profile／tft_board、board_power和双codec音频分支。保留其他板profile、既有research内容及用户并发改动，不改规则文件或项目整体LICENSE。只使用两条标准LCD协议命令和已有组件API，未复制完整厂家板类或UI类。

## 原生172×640显示与DMA

两板QSPI3 mode3／40MHz，CS9／CLK10／D0=11／D1=12／D2=13／D3=14，RGB565，172×640原生竖屏，无XY交换／镜像／gap。板级执行高30ms、低250ms、高30ms的硬件复位，随后用AXS组件初始化；保留固定两条厂商表命令SLPOUT等100ms、DISPON等100ms，不在该硬件复位后额外发SWRESET。组件原生MADCTL／COLMOD前序由驱动执行，显式IO检查原生轴、inversion off和display on。

复用FULL模式的220,160字节PSRAM帧和22,016字节内部DMA缓冲。每帧10条172×64行条带从row0顺序发送；只在复制到DMA时交换RGB565字节，不对原生画布旋转。每条检查CASET，再使用与官方驱动相同的QSPI RAMWR／RAMWRC framing，等实际完成事件后再复用条带，包括最后一条。保留源板硬件复位后的默认行窗口，不假定组件会发送RASET；实际172×640窗口仍需硬件确认。

共享3.5B分支仍使用320×20物理条带与90度映射；3.49使用原生行复制，编译期检查帧高可被条带行数整除。错误／等待超时后熄背光并停止后续刷新，保留可能仍被DMA占用的条带，LVGL源帧与DMA独立。等待窗口一秒，SPI总线排队可能另有阻塞，不宣称严格端到端期限。3.5B与新增两版均因缺AXS依赖未目标编译，不能由旧板回归证明这段共享路径正确。

背光采用5kHz／10位低有效PWM，启动GPIO先高，LEDC初始1023。原版用GPIO8，V2仅用GPIO42，V2 P1在PWM更新前关闭、成功后按亮度是否大于0使能。UI未就绪保持暗；失败尝试切PA并停止本次启动，不清配置或panic重启。

## 扩展器和手动电源键

TCA9554地址0x20，共用codec I2C0。固定两版源都将P6／P7置高；当前PDF分别标为SYS_EN和NS_MODE，P7连接NS4150B CTRL。这里只通过既有寄存器位更新封装操作所拥有的位，预装P6高保持供电、P7低关闭PA，再设置方向；V2额外拥有P5复位／P1背光使能。其余位保留，不调用整端口FF／FF复位构造函数，原版不触碰V2的P1／P5，也不在V2对GPIO21做LCD写入。

输出和方向通过真实寄存器读回核对掩码，PA另读input寄存器核对引脚电平；运行期TCA更新共用100ms互斥，单次I2C调用100ms超时。未接AXP2101或充电参数编程，不因为有电池电路就返回电量／充电状态。

PWR16低有效，启动先等待松开，再采用项目已有的2秒连续长按策略。在同一互斥内请求P6／P7同时拉低，之后禁止PA重新开启直到重启；USB供电时设备可能仍有电，掉电也可能阻止读回，因此日志只写关机请求和返回值，物理掉电未确认。上游Button传long_press_time=0，实际阈值由库默认决定；本地2秒不声称与那个默认值完全相同。没有自动睡眠或定时断电。BOOT0另外承担初始释放后的说话／卡片输入。

## 触摸坐标与错误处理

触摸用独立I2C1 SDA17／SCL18，300kHz，地址0x3b。固定源11字节命令请求14字节却读取32字节；本地用一次100ms超时的组合transmit_receive读取所请求的14字节，保留原始count与event，不经过会丢掉多指信息的一点AXS通用接口。包长度／重复起始兼容性仍待实物验证。

只接受一个down／contact点；up或无点释放，多指、保留event、读取失败或越界均取消LVGL按压并等待释放。初始必须先观察释放，不把失败包或边缘钳位当成有效点击。固定源在交换坐标前按172／640限幅，随后输出pointY／640-pointX，导致范围不一致；本地按rawX 0–639、rawY 0–171校验，使用 `x=raw_y`、`y=639-raw_x`，保持交换／镜像意图及零起点端点，方向仍待物理标定。

## 双codec音频与功能边界

Codec I2C0 SDA47／SCL48，ES8311输出＋ES7210输入，24kHz，MCLK7／BCLK15／WS46／DIN6／DOUT45。复用已安装codec驱动与TDM4封装，只取slot0 MIC1送单声道软件AEC，输出mono，输入默认30dB；不把BoxAudioCodec的I2C1默认套入这两板，不宣称启用两路硬件参考AEC。

P7仅在实际DAC解除静音回调成功后使能，静音先关PA，底层格式／enable、gain、DAC mute实际返回值沿用既有检查。PA极性、供电保持、手动电源、时钟／槽位／增益／静音重开／AEC和唤醒仍待实物。

未接SD、RTC、IMU、电池ADC／电量、充电策略、完整stock MCP及硬件AEC。联网、手机配对、提醒与同板OTA走现有基础终端源码，仍需逐板构建和实机验收；当前只有微雪1.8实物，屏控修订未确认。

## 实际命令与结果

两新板分别执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile <profile> --destination I:\xy_board_builds_20261109\<profile>-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

均退出1：缺 `espressif/esp_lcd_axs15231b 2.1.0`，目的目录不存在，没有编译、镜像或组件安装。记录为 `native_dependency_preflight_20261110_ws349.json`，build均not_run。

已有Korvo2和微雪Audio Board分别同步源码／刷新变化文件时间，实际执行：

```powershell
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -C build/<app>.elf
```

两组构建／镜像检查／最终符号读取均退出0，实际重编main／audio_io／board_power／waveshare_spi_audio与旧显示入口。Audio Board首次nm命令路径漏了一段目录、未执行；纠正完整路径后重新执行退出0并生成实际符号日志，未使用首次打印的遗留exit值。两板仍选各自ES8311／ES7210、显示／按键封装，未编译新349 helper或共享AXS显示，不验证新增电源／触摸／DMA／V2分支。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| espressif-esp32s3-korvo2-v3 | 2,260,112 | 885,616 |
| waveshare-audio-board | 2,256,096 | 889,632 |

模型均2,203,819字节，在4MB模型分区内。各157个main文件集合、164项输入摘要、编译宏／生成配置、镜像项目身份／16MB／checksum／validation hash和工作区匹配；各17项构建归档产物与源码快照在 `evidence/ws349-builds-20261110/`，记录为 `cached_build_attempts_20261110_ws349_<profile>.json`。仍使用既有IDF5.5.6-dirty、17个local override和Python offline_guard，不是干净发行依赖构建。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：95官方源码变体／96本地入口含1历史入口／93待源码；累计52变体曾缓存编译通过。当前2 passed（Korvo2／Audio Board）、50 stale、6历史failed、130 not_run（其中37已有源码），全部188实机not_run。

未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。headless_ui、3.5板级封装、4B封装／显示、网关和运行时文件摘要保留；全量目标继续进行。

收尾实际执行 `git diff --check` 退出0；22项本批新增／变更文件（含Git普通diff看不到的未跟踪公共源）逐项 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1代表新增差异。25项来源、两组各164项输入／17项构建归档产物摘要匹配；两份固定表的11／29命令及100ms延时一致，原有94个profile和既有research保留。矩阵research／registry摘要与新目标not_run状态匹配，6项保护文件摘要相同。对照本批before快照审阅最终diff，回归进程均已结束；共享AXS新旧目标仍需依赖准备后的实际编译与验机。
