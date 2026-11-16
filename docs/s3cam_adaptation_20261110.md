# 面包板S3 CAM与Zhengchen CAM：基础语音入口

日期：2026-11-10，Asia/Shanghai。固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，接入两个独立发布变体 `bread-compact-wifi-s3cam`、`zhengchen-cam`。本批覆盖各自默认屏幕和语音路径，不宣称摄像头识物或全部原厂外设功能已实现；两板均未实机验收。

## 面包板S3 CAM

发布config.json显式选 `CONFIG_LCD_ST7789_240X320=y`，实际板类使用SPI3／40MHz。默认是240×320竖屏、RGB／RGB565、反色、无轴交换／镜像、gap0／0。引脚MOSI20／CLK19／DC47／CS45／RST21／背光38高有效；复用现有竖屏布局、字体及ST7789驱动。没有将其误接为普通面包板OLED或其他尺寸配置。

数字音频使用独立I2S0采集和I2S1播放：16kHz麦克风WS1／BCLK2／DIN42，24kHz扬声器BCLK40／WS41／DOUT39。固定 `NoAudioCodecSimplex` 的LEFT／32位槽由既有digital_audio路径承接，采集明确转换为PCM16，播放转32位槽，沿用软件音量、真实PCM静音、重采样／软件AEC／唤醒。没有假设PA使能脚，也不调用数字麦不支持的模拟增益。

按键只有BOOT0，首次释放后接已有按住说话和操作卡；不读取NC的触摸／音量键，不套用普通面包板GPIO47／40／39按键配置。GPIO39／40／41在本板属于扬声器。LED48、lamp14、原厂BOOT点击切换聊天／启动期重配网手势、其他屏幕选项及OV2640摄像头未实现。

固定README明确说明19／20已被占用。本地板级defaults关闭 `ESP_CONSOLE_USB_SERIAL_JTAG`，选择 `ESP_CONSOLE_UART_DEFAULT` 和 `ESP_CONSOLE_SECONDARY_NONE`，避免沿用根配置的USB控制台与LCD冲突。UART0的默认TX43／RX44以本机SDK为准；运行日志和后续接线／下载方式需另行确认，本轮没有操作设备或烧录。构建配置只能证明控制台选项，不能证明实际USB电路、ROM下载阶段或焊接的屏幕线路互不干扰。 本机SDK的SPI GPIO矩阵路径调用gpio_func_sel，S3的gpio_ll_func_sel会在选择19／20时清除USB_PAD_ENABLE；UART0常量确认TX43／RX44。相关SDK源已归档，GPIO／UART文件在固定HEAD无改动；这是源码证据，仍不等于设备线路验收。

应用身份为 `assistant_bread_s3cam`。16MB Flash／Octal继承固定release默认，不是实物模组容量证明；任意叫“S3 CAM”的商品不能直接套用这些引脚。

## Zhengchen CAM

实际板类将 `lcd_display/lcd_mode` 默认设为1，选择320×240横屏；本地只实现这个默认入口，没有移植运行时横竖屏切换。ST7789 RGB／RGB565，反色，swapXY、mirrorX、gap0／0；SPI3 mode0／80MHz，MOSI40／CLK41／DC39，CS与ESP复位脚NC。背光42低有效，以既有反向PWM保持0为关闭、100为最亮；面板与LVGL保持同一方向，使用320横屏布局和16px中文字体。

I2C1 SDA1／SCL2，PCA9557地址0x19：P0为LCD控制、P1为功放。复用既有 `pca9557_box` 读改写／回读校验和100ms超时，先锁存LCD高、功放低，再仅配置这两位输出；软复位后拉低LCD控制再初始化面板。保留P2及其他位，不复制上游整端口0x03／0xf8写入。P2没有被本地操作不等于物理摄像头已关闭，尤其软重启可能保留扩展器状态。

音频使用ES8311＋ES7210（地址参数0x82），MCLK38／BCLK14／WS13／DIN12／DOUT45，输入输出24kHz。复用TDM4／64时钟帧，启用四个物理ADC而只取slot0／MIC1的单声道，使用软件AEC；没有宣称完整移植原厂硬件参考输入AEC或双击切换模式。静音／关闭先关PCA功放，实际DAC解除静音成功后才使能；检查底层data set_fmt／enable、MIC1增益和DAC mute结果。

BOOT0、音量加3、音量减46均低有效，首次释放才接受，多键和按住切换输入交给既有取消逻辑；GPIO3／46单独配置输入上拉。没有假装GPIO47是应用按键，它在上游属于电源管理输入。默认入口无LVGL触摸pointer，不启用SD缓存。

应用身份 `assistant_zhengchen_cam`。Flash16MB／Octal继承固定release默认；README中的N16R8商品标题只作为来源描述，不视作当前实物确认。相机、拍照／识物、LED48、电池／温度、MCP扩展、首次双击拍照、后续双击AEC、长按重配网及动态横竖屏未实现。config.json中的Wi-Fi核心／缓冲调优没有未经测量照搬，本地仍使用统一网络路径。

## 共同边界与来源

来源为两板的config.h、config.json、实际板类与README，以及固定NoAudioCodec／BoxAudioCodec源；URL和SHA-256记在 `board_research_v2.5.2.json` 的各自finding，公开原文归档于忽略目录 `evidence/s3cam-source-20261110/`。没有复制整份上游板类应用、安装组件或改变项目整体许可。

仍须验证真实模组／屏幕修订、SPI信号与色序／方向／背光、USB或UART线路、GPIO键、电源／功放控制、音频时钟／LEFT槽或MIC1槽、增益／静音／重开、软件AEC／唤醒、联网／绑定／提醒及同板OTA。现有实物只有微雪1.8，屏控修订尚未确认。编译与镜像身份通过也不构成这些项目的硬件验收。

## 实际构建与证据

两新板分别实际执行以下完整缓存构建（Board依次为 `bread-compact-wifi-s3cam`、`zhengchen-cam`），均退出0：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board <profile>
```

Stage分别 `I:\xy_board_builds_20261109\xy_cached_c4d627eefaa0` 和 `I:\xy_board_builds_20261109\xy_cached_92f2f53207df`。已有立创profile在原stage实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build`，退出0，日志实际重编main／audio_io／waveshare_spi_display等公共输入。

初次构建后仅收紧registry的描述，将未证实的RST-PCA0改称RSTNC／LCDcontrol-PCA0，功能源码没有再变化。保留三份初次registry快照和初次日志；同步最终registry后，三profile分别实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，均退出0。最终126项输入与119个main文件集合匹配；初次到最终registry只有该描述字段变化，未以旧快照冒充最终输入。首次完整编译没有新增非弃用warning，原有LVGL弃用提示保留。

| Profile | 最终应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| bread-compact-wifi-s3cam | 2,213,776 | 931,952 |
| zhengchen-cam | 2,256,128 | 889,600 |
| lckfb-lichuang-dev | 2,263,472 | 882,256 |

三镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0；项目名分别assistant_bread_s3cam／assistant_zhengchen_cam／assistant_lichuang，16MB、checksum／validation hash匹配。新面包板实际链接digital_audio与dummy codec，不链接ES8311／ES7210／PCA9557；Zhengchen实际链接双codec／TDM／PCA9557，不链接digital_audio；新板均没有FT触摸或camera_init。立创仍链接原有FT触摸与PCA路径。

实际生成配置已核对：面包板UART_DEFAULT／SECONDARY_NONE开启、主次USB_SERIAL_JTAG关闭，16MB／Octal和dummy codec匹配；Zhengchen及立创16MB／Octal／ES8311／ES7210匹配。模型均2,203,819字节，在4MB模型分区内；编译宏、应用内板型字符串、合并defaults／分区和源摘要匹配。记录见 `cached_build_attempts_20261110_s3cam_<profile>.json`，初次／最终日志、registry快照、配置／编译命令／依赖锁、最终源码与产物归档在忽略目录 `evidence/s3cam-builds-20261110/`。

三次构建只使用既有SDK5.5.6-dirty与17个本地组件override，保留SDK原有修改；Python offline_guard拒绝外网，不是干净依赖发行构建。实际SDK HEAD为880a203ae603d08a94d06aa01e0d6761958e5e9e，另核对GPIO／UART／SPI接管的三份SDK源在HEAD无改动；没有修改SDK。

实际 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：76官方源码变体／77本地profile含1历史入口／112待源码；累计47变体曾各自缓存编译通过。当前3 passed／44 stale／6历史依赖预检失败／135 not_run，其中23项not_run已有源码；全部188实机not_run。全量目标仍进行中。

本轮未运行测试套件／独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA；没有修改人工决策标记的无屏配对函数、并发网关／运行时或自然插话实现。

收尾实际 `git diff --check` 退出0；本批新增文件逐个 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异）。三份各126输入／18项构建归档产物，面包板8项／Zhengchen6项固定与SDK源摘要匹配；矩阵研究／registry摘要及两新板构建passed／实机not_run状态匹配。对照本批编辑前快照审阅最终diff，三个构建进程均已结束。
