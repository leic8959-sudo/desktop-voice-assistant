# 微雪ESP32-S3-RLCD-4.2适配（2026-11-12）

范围：小智v2.5.2固定提交49b4380b3faa0ceadd28a3e78aaf0f311c46c57e的`waveshare-esp32-s3-rlcd-4.2`基础语音终端。反射式液晶不是电子纸。本批新增独立profile／ST7305显示分支／板级音频接线，实机验收仍未完成。

## 来源与实际差异

16来源归档于`evidence/rlcd42-source-20261112/source-artifacts.json`：固定小智板目录6文件、微雪厂家代码5文件（d8bf7813d3b53251577ff03c03596ad6ad3acae0）、商品／文档／资料3页及原理图／ST7305规格书2份PDF。厂家明确N16R8：16MB Flash／8MB Octal PSRAM；ST7305／400×300反射式单色液晶。

原理图第1页和ST7305第64页已渲染视觉核对，PDF第45页、100页文字表核对扫描信号与SPI时钟。规格书单4线SPI写周期最小30ns、H/L各最小15ns；上游40MHz只有25ns周期，本地采用20MHz／50ns，不为相同源配置而使用超规格时钟。TEON35=00输出垂直消隐，38选择高功率扫描模式，规格书HPM标称32Hz；实际边沿周期留待硬件测量。

## 实现

新增`waveshare-esp32-s3-rlcd-4.2` → `assistant_wsrlcd42`，宏`ASSISTANT_BOARD_WS_RLCD=1`。根16MB分区保留NVS9000/4000、双app20000/300000与320000/300000、model C00000/400000。

- 复用现有`paper_display.cpp`的有界后台帧任务、最新帧合并和generation完成机制，使用独立ST7305命令／扫描协议；没有复用纸屏LUT、BUSY极性、F7/FF或旧图差分波形。
- `board_profile::electronic_paper`在RLCD为false，新增`reflective_display`表达白底黑字、分页和实际显示完成回执。旧电子纸的该值等于原electronic_paper，功能保持原分支行为。
- SPI3 mode0／20MHz，MOSI12／CLK11／CS40／DC5／RST41，TE6。固定小智的27条有效命令、全部数据及200ms退睡眠延时逐项保留于`rlcd_panel_commands.h`。36=48、3A=11、21反相、2A=12..2A、2B=00..C7、35=00、38/29；复位50/20/50ms。
- 400×300 RGB565整帧240000字节PSRAM；三个15000字节单色快照共45000字节PSRAM，一份15000字节内部DMA传输；按像素计算原生2×4排列，移除上游240000字节索引LUT＋120000字节bit LUT的需求。
- 原生映射：`index=(x/2)*75+(299-y)/4`，`bit=7-(((299-y)%4)*2+x%2)`，最大索引14999，正好15000字节；并非按38字节列补齐的面板。
- TE6使用GPIO上升沿ISR和受ESP-IDF任务／ISR临界区保护的32位计数器。初始化后先等两个TE边沿；每帧写前等待下一次垂直消隐，SPI发送结束后再观察两个TE边沿，才提交generation。首个后继消隐可能只是写入期间已开始的一轮扫描结束，第二个边沿才覆盖完整后继扫描。没有TE就500ms超时，停止显示提交，不把SPI完成等同已显示，也不宣称实机无撕裂。
- 任务最小帧间隔100ms；LVGL锁内只做有界单色转换与复制，不等待SPI／TE。transaction、DMA和初始化长参数均持久存活，排队／完成超时各1秒，失败后不再提交任何SPI并保留在途内存。
- 16px中文字库、14px标题，384px分页正文、44px按钮；反射式界面禁用连续滚动动画。BOOT0先松开再接受说话／卡片短按选择、长按确认。未接触摸控制器。
- 既有`waveshare_spi_audio.cpp`后端接入本板独立I2C0 SDA13/SCL14与I2S0 MCLK16/BCLK9/WS45/DIN10/DOUT8，ES7210 TDM4取MIC1＋ES8311 DAC24kHz，延续实际PCM软件AEC。图纸双麦和MIC3/4回采网络不等于已经验收硬件回声消除，本地不宣称双麦波束／硬件AEC。
- PA46高有效，启动先低、解除旧hold、启用pad读回；实际DAC解除静音成功后才启用PA，GPIO结果和实际增益／静音回调检查复用现有流程。实现独立GPIO46分支，未调用摄像头板CH32扩展器。
- KEY18仅保留上拉输入。图纸BC2104／RTC供电电路持有硬件电源，不把GPIO18误设为软件锁存输出；未改ETA充电芯片／稳压器。显示初始化失败维持后台供电和PA低；反射屏无背光，亮度／熄屏偏好保留图像，不宣称真实黑屏。

未接SD／RTC／SHTC3／触摸／电量ADC、充电LED、休眠或长按关机业务；原厂BOOT双击AEC模式切换未移入。原源代码充电恒false与ADC电压多项式不当作硬件事实，电量保持未知。

## 构建与检查

新目标完整构建及最终ISR临界区修改后的reconfigure build均退出0；旧3.97纸屏和微雪Audio Board双codec公共回归退出0。源码从首次构建失败的原子断言修正后重新完整构建，最终镜像只使用最终工作区源码。

| 入口 | app字节 | app分区 | 剩余字节 | model字节／容量 |
| --- | ---: | ---: | ---: | ---: |
| RLCD `assistant_wsrlcd42` | 2,227,808 | 3,145,728 | 917,920 | 2,203,819／4,194,304 |
| 3.97 `assistant_wspaper397`回归 | 2,738,624 | 3,145,728 | 407,104 | 2,203,819／4,194,304 |
| Audio Board `assistant_wsaudio`回归 | 2,256,176 | 3,145,728 | 889,552 | 2,203,819／4,194,304 |

三份`esptool image_info`、nm、size和gen_esp32part检查退出0；应用身份／Flash容量／checksum/hash有效，app/model均在范围内。RLCD ELF选择TE ISR、27条ST7305命令、2×4像素映射、ES7210 TDM4＋ES8311；不含纸屏波形、SSD2683两位行或摄像头功放helper。每项193个main文件、200份输入摘要与最终工作区匹配，各21份归档产物共63份SHA256匹配。两次初次编译失败的原始日志保留：首次stage仍含摄像头PAhelper，第二stage因原子lockfree断言失败；后续最终源改为独立PA46与IDF临界区后通过。工具链不保证原子操作无锁，因此最终TE计数采用ESP-IDF临界区，ISR不调用C++原子库锁函数。

新目标实际命令：`tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board waveshare-esp32-s3-rlcd-4.2`，最终stage `I:\xy_board_builds_20261109\xy_cached_6a44e7d05419`，前两份失败stage日志保留。只用既有17组件override、SDK5.5.6-dirty、offline_guard和2作业，不安装依赖。

回归以`xy_rlcd42_regression_20261112.ps1 -Stage <stage> -Board <profile>`复制最终main／配置并更新变化文件时间，执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。新板功放和ISR修改后已同步最终源码再构建，最终记录不使用早期stage产物。TE ISR对象反汇编另核对到IDF临界区调用，未调用C++原子函数。

## 验收边界

只有现有微雪1.8实物可用，RLCD的20MHz电气时序、TE实际扫描／断线错误、2×4像素方向和反相、光照下文字可读性、MIC1/TDM时钟／增益／PA噪声／AEC、按键与配网绑定／提醒／OTA均未实机验收。图纸与构建不能替代硬件运行。

没有运行测试套件、安装、Git写操作、上传发布、刷机或OTA。
