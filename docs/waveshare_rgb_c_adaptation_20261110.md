# 微雪4.3C／7C：800×480 RGB触摸语音源码候选

后续更新：4.3C／7C已分别完成首次整板缓存构建，复用CH32输出与原GT911轮询，保留独立复位、时序和codec差异；CAM共享CH32回归通过，实机未验收。见[本批说明](rgb_c_native_20261113.md)。

日期：2026-11-10，Asia/Shanghai。新增官方变体 `waveshare-esp32-s3-touch-lcd-4.3c`、`waveshare-esp32-s3-touch-lcd-7c`，独立应用身份 `assistant_ws43c`、`assistant_ws7c`。两款目标都缺本地组件，未编译、无新板镜像、未实机验收。同批4B仍为待实现。

## 固定来源与差异

来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 中三款微雪板各自config.h／config.json及实际板类；7C另核对其 `waveshare_audio_codec.cc/.h`。CH32与GT911 API沿用先前归档的公开固定版本，音频驱动接口按本机codec_dev1.5.11核对。没有根据屏幕相同分辨率合并硬件接线。

| 部分 | 4.3C | 7C |
| --- | --- | --- |
| Flash配置 | 固定发布默认16MB | 固定发布显式32MB |
| I2C | I2C0，SDA8／SCL9 | I2C0，SDA47／SCL48 |
| RGB数据D0–D15 | 14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40 | 14,38,18,17,10,39,0,45,9,8,21,1,2,42,41,40 |
| 输出编解码器 | ES8311，mono | ES8389 DAC，地址参数0x20，stereo |
| 输入 | ES7210，TDM4／MIC1 slot0 | ES7210，TDM4／MIC1 slot0，源指定37.5dB增益 |
| 水平时序：脉宽／后沿／前沿 | 4／4／8 | 4／8／8 |
| 垂直时序：脉宽／后沿／前沿 | 4／4／8 | 4／8／8 |

两板均为800×480原生横屏RGB565，PCLK16MHz负边沿，HSYNC46／VSYNC3／DE5／PCLK7，disp使能NC。使用两份PSRAM帧缓冲、10行bounce缓冲及既有LVGL port的RGB直接帧／帧完成同步；没有SPI显示IO，不注册不存在的SPI传输回调，不在帧缓冲原地交换字节。源码BIGENDIAN宏不等于实际RgbLcdDisplay启用了字节交换，本地跟随实际RGB路径的原生字节序。

两帧共1,536,000字节、两个DRAM bounce缓冲共32,000字节；帧大小可被bounce大小整除。按时序算出的名义帧率为4.3C约39.532Hz、7C约39.024Hz；真实像素时钟、撕裂、DMA下溢、帧完成等待和内存预算均未实测。专用800×480布局使用16px中文字体、可见“按住说话”按钮及现有触摸卡片交互。

**GPIO0是两板的RGB DATA6**，不能照搬BOOT宏把它配置为输入／上拉或读取按钮。本地listen_button设NC，不启用硬件卡片键，使用触摸。GPIO43／44又是两板音频DIN／BCLK，不能改成UART0日志；保留USB Serial/JTAG控制台，未在这里复用19／20。

7C声明32MB，但目前使用项目既有16MB范围内的双应用／模型分区布局，剩余高地址空间没有新功能或写入，不宣称利用完整32MB。32MB生成配置、实际镜像与设备容量仍需目标构建确认；不会由registry字段推断已经完成。

## CH32、背光和触摸复位

正式依赖 `waveshare/custom_io_expander_ch32v003: 2.0.0`，地址0x24。复用厂家组件，不将它误当TCA9554；该版本构造函数重置**整个端口**direction=0xff／output=0x00，输出和方向读取是软件缓存。不能宣称保留无关供电位或完成物理电平读回。组件I2C调用超时1000ms；本地运行期功放和PWM共用100ms互斥锁，逐项检查API返回值。ACK或缓存成功不等于硬件读回。

板级P1是触摸复位、P2背光使能、P3功放。启动请求P3关闭，P2使能且PWM255保持暗；P1低200ms，INT4主动拉低200ms，再P1高200ms，最后INT4改回输入。这在外置复位路径中选择GT911地址0x5d，随后GT911配置的rst／int都为NC，避免驱动再次复位并失去地址选择。7C实际源还请求P6高，本地保留该动作，但尚未独立确认其物理作用，不能据此推断充电／电池政策。

背光PWM为 `(100-percent)*255/100`，待显示／触摸注册成功后才由UI设置亮度。功放仅在实际DAC解除静音回调成功后请求使能；静音／关闭先请求关闭P3，失败不继续启用。启动失败尝试关功放并停止本次启动，不清配置、不panic重启。整端口初始化对电源接管的影响、P6作用、复位时序与实际极性都待实机验收。

GT911声明固定1.2.1、I2C400kHz、16位寄存器IO、100ms传输超时，固定0x5d，不添加0x14回退。使用源中的无镜像横屏坐标，但范围严格限定0–799／0–479，未复制源中x_max=799作为尺寸的混用。初始必须释放，只接受单指；多指、越界、I2C／数据读取错误触发LVGL INDEV_RESET并等待释放；错误时仍消耗驱动旧点缓存，不把失败当成功点击。触摸初始化或pointer分配失败停止启动，不建立“有屏但无法操作”的空成功路径。

## 音频两种输出

共用I2S0 MCLK6／BCLK44／WS16／DIN43／DOUT15，输入输出24kHz。既有双codec封装增加两板I2C0分支，避免套用BoxAudioCodec的端口1假设；启用四个ES7210物理ADC，TDM4／64时钟帧仅取slot0／MIC1作为单声道，使用软件播放参考AEC，不宣称已移植原厂两路硬件参考AEC。

4.3C继续用ES8311输出与mono格式。7C单独创建ES8389 DAC，打开stereo输出并在写入边界把协议mono样本复制到左右声道，使用固定512个PCM16元素缓冲分块转换；不按包创建增长的vector。逻辑音量由codec_dev应用到实际双声道包，共用录音／软件AEC接口仍保持mono。保留7C实际源要求的输入37.5dB和两codec关闭时不禁用策略；启停／共享时钟行为仍待硬件验证。

底层data set_fmt／enable、MIC1增益、DAC mute真实回调沿用现有检查，ES8389与ES8311分别调用各自已安装驱动，不伪装7C仍是ES8311。双声道复制、新ES8389 DAC路径和32MB分支尚未经过目标编译，旧板回归不能证明它们可编译或运行。

未接入BQ27220参数编程／电量、充电USB策略、电源自动休眠、SD、相机、原厂重配网MCP和完整手势。只有触摸语音／卡片源码候选；实际Flash／PSRAM、RGB信号／色序／背光、CH32固件／电源接管、触摸／坐标／错误取消、PA极性／静音／重开、24kHz槽位／增益／AEC／唤醒、联网／绑定／提醒及同板OTA均需验机。现有实物仍只有微雪1.8，屏控修订未确认。

## 同批4B前置项

4B是480×480 ST7701 RGB，并非CH32的800×480方案：TCA9554地址0x20经三线IO控制ST7701，另有AXP2101。其源PMIC构造函数会关闭其他DC／LDO、修改DC1／ALDO1、掉电阈值、充电电压与电流，不应为接入基础语音直接整份复制。

4B仍需核对ST7701 vendor表、18位控制参数／COLMOD0x66与16位RGB565总线关系、三线扩展器IO释放、触摸／按键／SD检测位，并实现保留其他电源和充电配置的最小MIC供电封装。本轮已归档四项固定源码并写入research finding，保留inventory_only，未创建profile、构建或写PMIC。

## 真实预检与旧板回归

两款新板分别实际执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile <profile> --destination I:\xy_board_builds_20261109\<profile>-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

均退出1：`Required component is not installed: waveshare/custom_io_expander_ch32v003; nothing was staged`，目的目录不存在。GT9111.2.1也在本机缓存中缺失，但预检在第一个CH32依赖处停止，GT911只是静态缺失证据。记录见 `native_dependency_preflight_20261110_ws_rgb.json`。没有安装依赖，新目标没有编译或镜像。

旧Korvo2和微雪Audio Board分别同步最终源码／刷新变化文件时间，实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，均退出0，日志包含main、audio_io、waveshare_spi_audio和显示编译。它们没有选择新RGB-C helper／display或链接CH32、RGB LVGL和7C单声道扩展函数；只提供既有路径回归证据。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| espressif-esp32s3-korvo2-v3 | 2,260,112 | 885,616 |
| waveshare-audio-board | 2,256,096 | 889,632 |

两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0，独立项目名／16MB／checksum／validation hash匹配，仍选择各自ES8311／ES7210与旧显示。每份150项输入摘要／143个main文件集合、编译宏、生成配置与合并defaults和工作区匹配；模型均2,203,819字节，在4MB模型分区内。

证据为 `cached_build_attempts_20261110_ws_rgb_<profile>.json`；日志／配置／编译命令／依赖锁、镜像／ELF、源码快照与摘要在忽略目录 `evidence/ws-rgb-builds-20261110/`。使用既有IDF5.5.6-dirty与17个组件override，Python offline_guard拒绝外网，不是干净发行依赖构建。

4.3C共14项、7C共15项固定公开／已安装接口证据，4B四项固定来源的URL与SHA-256记在 `board_research_v2.5.2.json`，原文及引脚／名义时序对照位于忽略目录 `evidence/ws-rgb-source-20261110/`。本地只编排既有组件，没有复制上游充电／BQ27220政策或整份应用板类，不改项目整体许可。

未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。无屏六位绑定码人工决策函数、网关／运行时与自然插话文件未修改。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：90官方源码变体／91本地profile含1历史入口／98待源码；累计52变体曾缓存编译通过。当前2 passed／50 stale／6历史依赖预检失败／130 not_run，其中32项not_run已有源码；全部188实机not_run。4B继续inventory_only，全量目标仍进行中。

收尾修正公共音频注释：不再声称所有扩展器更新都会硬件读回，各后端保留自己的ACK／缓存／读回限制。保存初次回归日志后，两款旧板同步最终源码再次执行相同reconfigure build，均退出0、实际重编audio_io，随后重新检查镜像与最终摘要；功能逻辑未变。两份证据均保留初次／最终构建命令与日志。

实际 `git diff --check` 退出0，本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。最终两份各150输入／17项构建归档产物，4.3C14项／7C15项／4B四项来源摘要、两预检日志匹配；逐项对照RGB16数据脚／控制脚与固定header，帧大小可被bounce大小整除。矩阵研究／registry摘要、新目标not_run和4Binventory_only状态匹配，headless_ui摘要与上批一致。对照编辑前快照审阅最终diff，两项回归进程均已结束。
