# SenseCAP Watcher基础语音源码候选（2026-11-12）

后续更新：SenseCAP已保留SPD2010默认402条表、工厂区和旋钮输入，完成首次整板缓存构建；LCDWiki回归通过。实机及声学仍未验收。见[本批说明](sensecap_spd_native_20261113.md)。

新增独立SenseCAP基础语音入口，包含工厂区保护、LCD、音频、电源和旋钮按键。缺SPD2010组件，尚未目标编译／刷机／实机验收。HX6538 AI相机、触摸、旋钮转动及整机原厂功能继续单独待适配。

## 固定来源与核对

16来源归档在`evidence/sensecap-source-20261112/source-artifacts.json`：小智固定提交49b4380b3faa0ceadd28a3e78aaf0f311c46c57e板目录9文件、Seeed OSHW README/原理图（3ca69aefa8441f57d38993a39df714048633d032）、SPD2010组件登记页／版本参考页，以及固定驱动3文件（12f6ca1182ec48889b17ec570fadaaf267cb336e）。驱动参考文件只作接口调查，没有安装或链接evidence内容。

原理图第5页确认ESP32-S3R8（8MB Octal PSRAM），固定config明确32MB Flash；第8页确认ADC物理0x14、DAC物理0x18、麦克风接ADC右通道，左侧为DAC回采网络。第10页确认TCA9555@0x21和独立LCD／系统／PA电源控制。第8、10页已用Poppler渲染核对，原始PDF及PNG留在来源目录。

## 分区与设备信息保护

原厂备份命令读取0x9000起204800字节，即0x32000，末端为0x3B000（不含）。里面含EUI等生产信息。通用NVS9000/4000会覆盖它，不能用于本板。

专用`boards/sensecap-watcher/partitions.csv`：

| 分区 | 起点 | 大小 | 处理 |
| --- | --- | --- | --- |
| nvsfactory | 0x9000 | 0x32000 | raw subtype0x40／readonly，不挂为default NVS |
| nvs | 0x3B000 | 0x4000 | 小燕子自己的默认NVS |
| otadata | 0x3F000 | 0x2000 | 双应用OTA状态 |
| phy_init | 0x41000 | 0x1000 | PHY数据 |
| app0 | 0x50000 | 0x300000 | 专用应用地址 |
| app1 | 0x350000 | 0x300000 | 另一应用地址 |
| storage | 0x650000 | 0x5B0000 | 存储预留 |
| model | 0xC00000 | 0x400000 | 当前模型容量 |

32MB Flash上仅使用前16MB范围；后16MB未分配。本表须作为新固件专用布局，不等同原厂固件分区。

`native_board_profiles.json`显式声明保留区9000/32000和新NVS3B000/4000，现有`flash_native.py`按擦除扇区检查写入重叠；完整32MB备份、板卡身份和用户明确刷写授权仍是刷机前提。没有执行备份或刷机。

启动先调用`sensecap_partition_layout_valid()`检查工厂区只读属性、起点／大小、默认NVS、OTA、app0/app1和模型位置；不匹配则在NVS初始化、设备配置读取以及所有板级写入之前返回。新默认NVS不复制原厂凭据，生产EUI只保留，不导出、不挂载。没有移入上游`nvs_flash_erase()`或10秒按键恢复出厂。

SDK `gen_esp32part.py`对本CSV单独编码和解码均退出0，解码明确readonly factory200K、NVS16K、两app3M和model4M。此检查证明分区格式有效，不证明SenseCAP整目标编译／运行。

## 基础终端实现

- 新宏`ASSISTANT_BOARD_SENSECAP=1`、应用`assistant_sensecap`；复用已有LVGL port、音频管道与I2C寄存器掩码读回封装，新增独立SenseCAP板级／显示／分区检查文件。
- TCA9555只拥有全局P9 LCD、P10系统保持、P12功放以及P3旋钮按下输入。先预装输出锁存，再切方向；系统高、LCD先低后高、PA低，必要读回失败停止。其余AI/SD/Grove/电池ADC、充电输入、P13等位原值保留，不照搬上游整组口清零和全电源开启。
- 图纸音频3.3V LDO默认保持开启；PA控制只开关功放，不切断MIC/codec电源。PA经TCA9555 P12，由实际DAC解除静音成功后再开启；普通关闭保留I2S时钟并静音，避免ES7243E共用时钟失效。
- 输入芯片ES7243E：物理0x14，codec控制API使用0x28。输出ES8311@0x18。I2C0 SDA47/SCL48，I2S0 MCLK10/BCLK11/WS12/DIN15/DOUT16，标准stereo16／24kHz／RIGHT输入，27dB待机增益（上游设置），沿既有实际PCM软件AEC；不把左路回采当已验收硬件AEC，不使用ES7210的TDM4设置或K10替代地址探测。
- SPD2010默认驱动、RGB412×412圆屏；QSPI3 mode3／40MHz，CS45、CLK7、D0=9/D1=1/D2=14/D3=13，DC_NC和RST_NC（源GPIO1是QSPI数据线）。软件命令32位、参数8位，4像素X边界取整，两个20行内部DMA缓冲，RGB565字节交换，硬件mirrorfalse/false。亮度GPIO8启动暗，成功后用5kHz/10位PWM；失败时不关闭系统供电。
- 412圆屏专用文字／卡片布局、44px按钮；旋钮按下采用TCA9555 P3。开机按住不会无限阻塞，必须先松开；读取失败取消当前动作，恢复后重新松开才接受按住说话或短按换项／长按确认。页面提示写“旋钮”，不指示不存在的BOOT应用键。
- 本批通过旋钮按下提供基础交互，`BSP_CAPS_TOUCH=0`；I2C1触摸、A41/B42旋钮转动调音量、HX6538/SSCMA AI相机、SPI2、RGB LED40、RTC、SD、Grove、电量/充电策略均未初始化，不能把基础终端入口解释为整机功能完成。原厂60秒睡眠／300秒自动关机未移入。

## 真实预检和回归

实际依赖预检：`python -B tools/prepare_cached_firmware_build.py --profile sensecap-watcher --destination I:\xy_board_builds_20261109\sensecap_preflight_20261112 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6`，退出1：缺`espressif/esp_lcd_spd2010=2.0.0~1`，没有stage、目标编译或SenseCAP镜像。基础终端以已有TCA9555寄存器封装实现，不另依赖扩展器组件；未接入AI相机，因此SSCMA不是此基础构建的依赖。

独立CSV编码／解码命令见`partition-format-check.json`，两条命令退出0。旧RLCD与微雪Audio Board最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<各profile> reconfigure build`均退出0。两份esptool image_info、nm、size、gen_esp32part均退出0，项目身份／16MB容量／checksum/hash正确，应用和模型在各自分区内。

| 旧入口 | app字节 | app3MB余量 | model字节／分区 |
| --- | ---: | ---: | ---: |
| RLCD `assistant_wsrlcd42` | 2,227,808 | 917,920 | 2,203,819／4,194,304 |
| Audio Board `assistant_wsaudio` | 2,256,176 | 889,552 | 2,203,819／4,194,304 |

每项199个main文件集合、206份输入摘要与最终工作区相同，各21归档产物共42份SHA256匹配；选源和ELF明确没有SenseCAP板级、显示、工厂区检查或ES7243E新分支。旧入口使用`xy_sensecap_regression_20261112.ps1`同步全部main／registry/defaults后构建，未改SDK或组件缓存。

记录：`cached_build_attempts_20261112_sensecap_*.json`、`native_dependency_preflight_20261112_sensecap.json`及`evidence/sensecap-source-20261112/`。旧目标不会选择SenseCAP的分区检查、TCA9555、ES7243E、SPD2010或32MB分支；旧目标通过不能代替新板编译。

本轮还撤回未经用户选择的V1手动说话／USB-only实验入口，实验记录保留在`evidence/epaper-v1-withdrawal-20261112/`。V1保持待方案确认，不计入当前已适配或已构建总数。

## 未完成验收

只有现有微雪1.8实物。SenseCAP的工厂EUI恢复／新NVS布局、屏幕方向/4像素对齐、TCA电气读回、ES7243E右声道／时钟／PA噪声／AEC、旋钮按键、配网绑定／提醒／OTA、触摸／AI相机等均未实机验收。旧构建或分区表格式检查不能替代它们。

没有运行测试套件、安装依赖、Git写操作、备份、刷机、上传或发布。
