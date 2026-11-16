# 微雪4B基础语音终端源码适配

后续更新：微雪4B现已保留板级GT911复位/0x5D地址选择并接入固定容量轮询，首次整板构建通过；其它GT911入口及功能缺口候选没有因此完成适配。最新来源、触摸状态与实机限制见[本批记录](gt911_ws4b_20261112.md)。

日期：2026-11-10，Asia/Shanghai。新增 `waveshare-esp32-s3-touch-lcd-4b` → `assistant_ws4b`，为源码候选。实际依赖预检缺GT9111.2.1，目标尚未编译；没有4B实物，不能宣称已完成适配。原无屏板六位绑定码手机配网页方案保留。

## 来源与改动范围

沿用小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的4B config.h／config.json／板类／README及MIT许可。补充核对[微雪4B原理图](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B/ESP32-S3-Touch-LCD-4B.pdf)，图面RGB Panel V1.1，模块标为ESP32-S3-WROOM-1-N16R8；原理图是可变URL，已归档当前PDF与SHA-256，没有由此确认用户实物修订。

ST7701组件API和初始化前序来自esp-iot-solution提交 `754ed7f0df316250c0aaaec650de0610b27154b6` 的2.0.2~2源。实际本地用已安装三线IO1.0.1~1发送命令，不另行引入／安装ST7701组件或复制完整驱动。已安装TCA9554 2.0.3、esp_io_expander1.2.1、LVGL port2.9.0接口也归档，合计18项来源及URL／摘要记在原有4B research finding；未重复追加finding。

本批新增 `waveshare_4b_board.h/.cpp`、`waveshare_4b_display.cpp`、`waveshare_4b_commands.h`、板级manifest／defaults与 `third_party/xiaozhi_ws4b/` 许可说明。公共改动限CMake、board_profile／tft_board、board_power、audio_io／waveshare_spi_audio及main的4B启动失败处理。编辑前快照、原始diff及保护文件摘要在忽略目录 `evidence/ws4b-source-20261110/`，未修改项目整体LICENSE或规则文件。

## 屏幕、背光与输入

480×480原生RGB565，16位并行数据线：40／41／42／2／1／21／8／18／45／38／39／10／11／12／13／14；HS46、VS3、DE17、CLK9，PCLK16MHz、非负边沿。H脉宽／后沿／前沿10／10／20，V为10／10／10，名义刷新60.332Hz。两个PSRAM帧缓冲共921,600字节，20行双bounce缓冲共38,400字节。LVGL port RGB直接使用内部帧与帧完成同步，不做SPI字节交换、镜像或XY交换。

TCA9554地址0x20，共用I2C0 SDA47／SCL48。三线IO为P0 CS／P2 SCL／P1 SDA。屏控先SWRESET等待120ms，再bank00／MADCTL00／COLMOD60，随后保留全部35条厂商命令的顺序、payload和延时，包括最终COLMOD66。原厂控制器参数18位与RGB565 16位总线的组合原样保留，颜色与信号必须验机，不把表改成55来消除表面差异。命令数据保留MIT版权和原始许可，新增修改说明。

初始化IO无论命令成功或失败都尝试删除，保留CS inactive；不把已删除IO传入LVGL。显示运行期无三线IO更新，音频在显示初始化返回后才启动，避免共享扩展器端口读改写并发。

GPIO4低有效背光在板级初始化先拉高保持暗，随后5kHz／10位LEDC初始duty1023，亮度 `(100-percent)*1023/100`，UI初始化成功后才设置实际亮度。GPIO0是独立BOOT按钮，不在RGB数据线内；先等释放，再按既有说话／卡片逻辑工作，也保留触摸入口。

## 扩展器、触摸与功放

厂商原理图确认P3为PA_CTRL、P4为SYS_OUT、P5为TP_RST、P6为TP_INT、P7为LCD_RST。没有把P4搬成原厂的短按切换factory分区，也不将P6误作SD检测或ESP GPIO6；后者是本板音频DOUT。

已安装TCA9554 2.0.3构造函数会整端口重置directionFF／outputFF，输出／方向getter只读取软件缓存；不能宣称保持启动前的端口状态。初始化通过公开底层输出回调预装P3／P5／P6低，再设输出方向，P7高保持LCD复位释放。随后确认目标引脚的input寄存器读回电平；这不同于输出缓存，仍不等于实物已验收，构造阶段浮空／上拉对PA的瞬时影响待验机。

P5低、P6主动低保持200ms，再P5高保持200ms，最后P6改回输入，选择GT911地址0x5d；P4保持输入。GT911配置rst／int为NC，防止驱动第二次复位，I2C400kHz、16位寄存器、100ms IO超时，不添加猜测地址回退。初始触摸必须释放，只接受0–479范围的单指；多指、越界、读取错误均取消LVGL按压并等释放，错误时消耗旧点缓存，不把残留点触发为点击。触摸／pointer失败停止本次启动。

P3初始关闭，实际DAC解除静音回调成功后才使能；静音先关P3，再沿用既有真实DAC返回值检查。运行期更新使用100ms互斥锁，逐次通过input寄存器读回核对P3。TCA组件单次I2C超时仍为其固定1000ms，不把100ms锁等待说成总操作期限。启动失败尝试切PA并返回，不清配置或panic重启。PA极性／上电声／静音重开仍待硬件验收。

## 最小供电与音频

AXP2101地址0x34，复用公共供电封装：只设置ALDO1电压寄存器0x92为28（3.3V）并读回，0x90在原使能值基础上OR1并读回，保留其他使能位，释放I2C设备后等10ms。不复制上游关闭其他DC／LDO、DC1调压、充电电流／CV电压、关机按键／自动掉电政策。未实现电量显示，电池状态继续未知。

ES8311 DAC＋ES7210 ADC，共用I2C0和I2S0，24kHz，MCLK5／BCLK16／WS7／DIN15／DOUT6，ES7210四物理ADC／TDM4的slot0 MIC1送入既有单声道／软件AEC管道；输出mono。TX从初始化时就保持64时钟帧，底层格式／enable结果沿用现有检查。保留30dB默认MIC增益；未宣称实现原厂两路硬件参考AEC。PA不使用ESP GPIO，交由TCA9554封装门控。

未接SD、RTC、IMU、充电策略、电量、休眠、原厂重配网MCP或factory分区切换。只登记N16R8基础语音终端候选，实际容量／RGB时序色序／触摸复位坐标／ALDO1供电／音频槽位时钟／AEC唤醒／联网绑定提醒／同板OTA均待实机验收；当前只有微雪1.8实物，屏控修订仍未确认。

## 实际验证命令与结果

新板实际执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile waveshare-esp32-s3-touch-lcd-4b --destination I:\xy_board_builds_20261109\ws4b-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1：`Required component is not installed: espressif/esp_lcd_touch_gt911; nothing was staged`，目的目录不存在，没有编译、镜像或安装。记录为 `native_dependency_preflight_20261110_ws4b.json`；矩阵build继续not_run。

已有Kevin Box2和Korvo2分别通过本批归档的PowerShell脚本同步源码、刷新变化文件时间，然后实际执行：

```powershell
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -C build/<app>.elf
```

两组构建、镜像检查与符号读取均退出0，实际日志包含board_power／audio_io／waveshare_spi_audio重编；Kevin Box2仍编译ALDO2和OLED入口，Korvo2仍选原板显示／ADC按键。它们不选择4B的新helper／display／ALDO1分支，不能证明4B可编译或运行。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| kevin-box-2 | 2,188,768 | 956,960 |
| espressif-esp32s3-korvo2-v3 | 2,260,112 | 885,616 |

两模型均2,203,819字节，在4MB模型分区内。镜像项目名／16MB／checksum／validation hash、编译宏、生成配置、全部149个main文件集合及各156项输入摘要和工作区一致。每组17项构建归档产物、源码快照在 `evidence/ws4b-builds-20261110/`，记录为 `cached_build_attempts_20261110_ws4b_<profile>.json`。使用既有IDF5.5.6-dirty和17个本地override组件、Python offline_guard阻断外网，不是干净发行依赖构建。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：91官方源码变体／92本地入口含1历史入口／97待源码；累计52变体曾缓存构建通过。当前2 passed（Kevin Box2／Korvo2）、50 stale、6历史failed、130 not_run（其中33已有源码）；全部188实机not_run，全量目标继续进行。

未运行测试套件／独立类型检查、安装依赖、启动宿主长期服务、Git写操作、上传、发布、刷机或OTA。headless_ui、网关和运行时文件摘要与本批编辑前一致。

收尾实际执行 `git diff --check` 退出0；12项本批新增文件逐一 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1代表新增差异。18项来源、两份各156项输入／17项构建归档产物摘要匹配；35条厂商命令及16条RGB数据脚与固定来源逐项一致。矩阵research／registry摘要、4B源码候选与not_run状态匹配，headless_ui／运行时／网关保护摘要相同；两回归进程均已结束。
