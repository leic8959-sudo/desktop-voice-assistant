# ZECTRIX NOTE4黑白4.2电子纸适配（2026-11-12）

仅适配小智v2.5.2固定提交49b4380b3faa0ceadd28a3e78aaf0f311c46c57e的`zectrix-s3-epaper-4.2`基础语音终端。NOTE4C不在此入口；RTC、NFC、商用NOTE4应用与云服务没有因此成为已支持功能。

## 固定来源与前提纠正

小智板目录15文件，以及Zectrix Lab公开MIT参考工程12文件归档在`evidence/zectrix-research-20261112/`，27来源均记录SHA256和固定URL。参考提交`ca285c98ed0641f86780edb1f5ec77b0335fe649`：https://github.com/itopinion/zectrix-note4-epd-demo 。README明确黑白400×300 SSD2683，sdkconfig.defaults为16MB Flash／Octal PSRAM；实物PSRAM容量和SKU修订仍未量测，未把推测写成8MB已验收。

纠正前批研究：小智的`CheckDualButtonForOtaSwitch`检测UP39＋DOWN18持续500ms后设置另一个OTA启动分区并重启，并不清空NVS。先前记录的“NVS恢复出厂”判断错误，现已在权威研究中更正。本地未移入这个启动分区切换函数。

厂家参考与小智固定源码有差异，采用厂家明确黑白NOTE4的设置：OTP panel `00=2F,0E`，写时钟20MHz（小智为2F,2E／40MHz），局刷明确设置50=77、E0=00和83窗口。原始来源均保留，最终方向、边框和波形效果等待实机。

## 实现范围

新增独立profile `zectrix-s3-epaper-4.2` → `assistant_zectrix`，宏`ASSISTANT_BOARD_ZECTRIX=1`。使用根16MB分区，NVS仍9000/4000，app0 20000/300000、app1 320000/300000、model C00000/400000，不复制商用双固件布局。

- 复用现有`paper_display.cpp`的后台worker、最新帧合并和generation完成机制；Maza与微雪1.54保留独立面板分支。LVGL只做有界转换和拷贝，不在显示锁内等待SPI/BUSY。
- 400×300 RGB565整帧240000字节PSRAM；三个15000字节1bit快照共45000字节PSRAM；一份15000字节内部DMA传输快照、一份持久100字节DMA行，发送30000字节原生2bit帧。没有每行vector／malloc或在途栈payload。
- SPI3 mode0，MOSI13／CLK12／CS11／DC10／RST9／BUSY8低忙。IDF原生`SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX`使用同一MOSI/SDIO收发，温度读8MHz、写20MHz，transaction.override_freq_hz显式切换；只有同一个worker操作SPI，不销毁／重建bus或设备。
- 全刷前reset/OTP，40命令实际读取温度后映射E0/E6并A5激活，10写入2bit像素；每个新像素放低位，高位0。SPI读错向上传播，不伪造25度继续成功。原始8位温度>127按固定小智寒冷区232处理。
- 局刷同样先reset/OTP，50=77、E0=00、A5、83全屏窗口0..399／0..299，10发送旧像素高位、新像素低位的变化编码。只在上次完整刷新成功之后使用旧图；10次局刷或变化达到30%时重建全刷。
- 04内部上电、12/00刷新。必须观察BUSY低忙再回高空闲，随后02/00内部断电完成才提交generation。超时不再发送02或任何后继SPI，保留transaction和所有DMA存储，显示停更且不提前回报shown；外部EPD供电与后台保持。BUSY输入下拉让断线先失败，而非立即表现为空闲。
- 三路17电池锁存、6屏幕、42音频均高有效：先预装输出latch再解除旧hold、启用pad、重申电平并读回、保持三路hold。PA46先低且解除旧hold，随后交给codec静音／开关，不对PA加hold。供电失败传播错误，不把设置调用当电气验收。
- ES8311单codec／I2C0 SDA47 SCL48；16kHz双工MCLK14、BCLK15、WS38、DIN16、DOUT45、PA46。复用网络24→16FIR以及实际送往扬声器的PCM软件AEC参考，不初始化ES7210或dummy数字codec。
- 白底黑字，384px正文分页，44px操作按钮。BOOT0先松开再接受说话／卡片短按选项、长按确认。UP39、DOWN/PWR18仅保留上拉输入，未实现原TODO翻页、关机长按或OTA切换。
- 显示初始化失败时保持锁存供电、PA低并停止启动；亮度／熄屏偏好保留反射屏图像，不宣称能像液晶一样黑屏。

LED3、充电1/2、ADC电量、PCF8563 RTC51/INT5、NFC55/FD7/供电21、休眠关机、商用待办业务均未初始化；电量状态保持未知。没有复制上游无限等PWR18松手、20秒/300秒睡眠切断供电或显示锁内刷新行为。

适配的必要SSD2683命令和编码逻辑保留厂家MIT许可于`main/third_party/zectrix_epd/LICENSE`及来源记录；未链接厂家完整demo、灰阶波形或自测应用。

## 构建与实际检查

新目标完整构建、旧微雪1.54 V2与Maza各自`reconfigure build`均退出0。源码从新板首次完整构建到归档期间未再修改，没有用旧产物代替最后源码；旧入口同步后各执行一次实际回归。

| 入口 | stage末尾 | app字节 | app剩余字节 | model字节 |
| --- | --- | ---: | ---: | ---: |
| NOTE4 `assistant_zectrix` | `xy_cached_9b9e6695f508` | 2,196,928 | 948,800／3MB | 2,203,819／4MB |
| 微雪V2 `assistant_wspaper2` | `xy_cached_c00524edf944` | 2,196,400 | 556,112／0x2A0000 | 2,203,819／0x240000 |
| Maza `assistant_maza29` | `xy_cached_c6346a24a1d1` | 2,195,248 | 950,480／3MB | 2,203,819／4MB |

三份`esptool image_info --version 2`、`xtensa-esp32s3-elf-nm -S --size-sort -C`、`xtensa-esp32s3-elf-size -A -d`和`gen_esp32part.py`均退出0。实际应用名／Flash容量／checksum及验证hash正确，app和model均在各自分区范围。NOTE4确实选择新宏、单codec16kHz／纸屏分支，ELF含100字节持久DMA行、3个15000字节快照及温度／局刷／物理BUSY错误路径；不含Maza／微雪波形或ES7210/dummy/上游CustomLcdDisplay。

每个stage的191个main文件集合、198份输入摘要与最终工作区一致；每项21份归档产物，共63份hash核对。12份厂家Git blob及保留MIT许可也核对一致。LVGL弃用API等既有告警仍存在，不据此宣称干净发行构建。

详细记录为`firmware/assistant_espidf/cached_build_attempts_20261112_zectrix_*.json`和`evidence/zectrix-source-20261112/`。

实际新目标命令：`tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board zectrix-s3-epaper-4.2`，stage `I:\xy_board_builds_20261109\xy_cached_9b9e6695f508`。只用既有17组件override、SDK5.5.6-dirty和offline_guard；未安装或联网解析依赖。

旧入口使用`xy_zectrix_regression_20261112.ps1 -Stage <各入口stage> -Board <profile>`同步源码后`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。镜像／ELF／分区／输入摘要以最终记录为准，旧入口成功不替代新板分支编译。

## 未完成验收

仅现有微雪1.8实物可用，NOTE4的温度SDIO电气方向、BUSY时序、OTP0E与20MHz设置、屏幕方向、残影、供电保持、PA噪声、增益AEC、配网绑定／提醒／OTA均未实机验收。构建与静态来源检查不能替代硬件运行。

没有运行测试套件、安装依赖、Git写操作、上传发布、刷机或OTA。
