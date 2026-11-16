# LCDWiki ES3C35P：ST77922 QSPI与TDDI触摸源码候选

后续更新：LCDWiki已保留ST77922分页／原63条表／旋转和独立触摸路径，完成首次整板缓存构建；CAM1.83回归通过。实机及声学仍未验收。见[本批说明](lcdwiki_native_20261112.md)。

日期：2026-11-10，Asia/Shanghai。发布变体 `lcdwiki-es3c35p`，独立应用名 `assistant_lcdwiki`。本批已建立显示／触摸／语音源码入口；本机缺ST77922组件，新板未编译、无镜像、未实机验收。同批Kevin 376×960 RGB板继续保留待实现。

## 固定硬件与屏幕命令

来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 `main/boards/lcdwiki-es3c35p/` 配置、实际板类与README。源声明ES3C35P／ES3C35P-NS、N16R8／16MB Flash＋8MB OPI PSRAM；本地采用固定16MB／Octal候选，不视作用户实际模组或无喇叭版本已确认。

| 部分 | 本地接入 |
| --- | --- |
| 屏幕 | ST77922 TDDI，320×480竖屏，QSPI2 mode0／80MHz |
| QSPI脚 | CS10／CLK12／D0-11／D1-13／D2-14／D3-9；LCD RST为NC |
| 背光 | GPIO41高有效PWM，初始化先暗 |
| 命令 | 保留固定板类63条vendor命令／全部数据／顺序／延时，最大16字节负载并静态检查长度 |
| 布局 | 独立320×480布局／16px中文字体，复用现有卡片与BOOT操作 |
| 触摸 | I2C0 SDA38／SCL39，地址0x55，RST48／INT47；按实际16位寄存器协议读取 |
| 音频 | 单ES8311，I2C0；MCLK17／BCLK18／WS21／DIN16／DOUT15，输入输出24kHz mono、使用MCLK |
| 功放 | GPIO1低有效，先锁存高后输出；静音／关闭先关PA，实际DAC解除静音成功后才使能 |

屏幕初始化数据位于 `main/lcdwiki_panel_commands.h`，改编来源的MIT版权／条款与说明保留于嵌套 `main/third_party/xiaozhi_lcdwiki/`，没有复制板类中的MCP重配网应用。本地复用官方组件 `espressif/esp_lcd_st77922: 2.0.2`；固定公开源码来自esp-iot-solution提交 `91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f`，header／manifest／一般接口驱动已阅读归档，未安装或修改组件。

构造参数保留BGR，但vendor在CMD1页写MADCTL0x00，2.0.2驱动同步覆盖缓存，后续无镜像也不重置色序位。这里不能只根据BGR构造参数就宣称最终BGR；本地保留整套源行为和false反色，实际颜色／gamma／IPS显示效果需要验机。vendor COLMOD0x01、窗口0–319／0–479和其他数据均保留，没有“修正”为另一个芯片的默认值。

QSPI脏区通过已安装LVGL port的rounder回调将X起点向下取4像素边界、X终点向上补满4像素组；320可被4整除，保留合法边界。以320×20行内部DMA条带、双缓冲接入现有刷新，不声称照搬80行就必然适合本项目的内存预算。

组件2.0.2的 `swap_xy` 无条件返回不支持，连false也一样；本地不调用它。显式设置面板无镜像，并选LVGL port的软件旋转模式，使当前rotation0保持物理竖屏且跳过端口自动硬件swap／mirror调用。已安装port2.9.0会额外分配一份条带旋转缓冲，在rotation0不做旋转计算；不是完整帧缓冲。运行时旋转功能没有作为产品能力提供，也没有宣称面板支持物理90／270度。新路径的缓冲分配与实际刷新仍未执行。

## 触摸协议与取消语义

固定板类虽然注释称“FT6336G-compatible”，实际地址与寄存器布局不同于现有FT5x06驱动，不以该名称套用驱动。本地只实现源中明确的协议：先发送两字节大端寄存器地址，单次I2C传输100ms超时；0x0009读取最大点数，0x0010的bit3指示数据，0x0014起每点7字节，bit7有效，X／Y由各自低6位高字节加低字节组成14位坐标。

RST48先锁存低再输出，低10ms、高100ms；INT47仅配置输入，使用轮询，不建立ISR。最大点数只接受1–5；读取失败／无效容量停止启动，不像上游默认回退1并打印成功。读取所有支持点后只接受一个活动点且在320×480内，初始必须先释放；多指、越界、读取失败会触发LVGL INDEV_RESET、等待释放，不把错误当成功点击。没有复制上游小于4px不更新坐标的抖动滤波，避免首个近原点点击沿用旧坐标。协议状态位的真实时序、点记录有效性及坐标方向仍需实物确认。

触摸用于既有可见卡片按钮，BOOT0首次释放后按住说话／操作卡；同时保留硬件卡片输入能力。没有把触摸存在自动等同“点任意区域说话”或添加重配网手势。背光只有显示／触摸初始化和LVGL注册成功后才由UI设置；失败尝试关闭PA并停止本次启动，不panic重启、不清配置。

音频复用单ES8311引擎、重采样／软件AEC／唤醒、已安装codec_dev实际增益和mute回调结果检查；不是双ES7210／ES8311方案。SD卡、RGB40、电池ADC8／充电、额外MCP重配网、原厂点击切换聊天／启动重配网均未实现。

真实模组容量／PSRAM、80MHz信号、初始化表／色序／反色／对齐／内存预算、触摸协议／XY／多指／错误取消、PA1极性／静音／重开、24kHz时钟／槽位／增益／AEC／唤醒、BOOT、联网／绑定／提醒及同板OTA仍须验机。现有实物只有微雪1.8，屏控修订尚未确认。

## 同批Kevin 3.13 RGB屏的前置项

`kevin-yuying-313lcd` 实际为GC9503V 376×960、RGB565 16位并行数据，PCLK16MHz，HSYNC6／VSYNC5／DE15／PCLK7，16数据脚包含19／20，背光4高有效。三线SPI仅用于初始化，CS48／SCL17／SDA16随后与RGB脚复用；其驱动 `auto_del_panel_io=1` 会在构造阶段删除三线IO，不能保留该失效指针给后续刷新回调。

它使用两帧PSRAM缓冲、10行bounce缓冲，时序H脉宽8／前后沿30、V脉宽8／前后沿16，宏虽然命名60HZ，不能直接当作实际60Hz；还需核对376×960扫描与配置中swapXY／mirrorX的关系。RGB数据19／20与项目默认USB Serial/JTAG冲突，需明确控制台／GPIO接管和实际接线。源中的IDF6字段与本机IDF5.5.6接口也必须重新核对。

本轮只完成固定源与生命周期调查，未新增Kevin profile或构建。需要先实现RGB显示生命周期、刷新／同步／缓冲／字节序与方向布局，再接单ES8311的16kHz输入输出、PA45和BOOT0；不能用现有SPI条带路径或相近分辨率当作适配完成。六项固定源证据与这些阻塞已写入矩阵research finding，继续inventory_only。

## 真实预检与已有板回归

新板实际执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile lcdwiki-es3c35p --destination I:\xy_board_builds_20261109\lcdwiki-es3c35p-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1：`Required component is not installed: espressif/esp_lcd_st77922; nothing was staged`，目的目录不存在，新板没有进入编译。传递依赖cmake_utilities0.*解析也尚未执行；公开组件源码只读，未安装依赖。记录在 `native_dependency_preflight_20261110_lcdwiki.json`，新显示、触摸和PA1宏分支均不能以旧板结果宣称通过。

现有 `chd-esp32s3-eye` 与 `lckfb-lichuang-dev` 在各自既有隔离stage同步最终文件／刷新变化文件时间，分别实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，均退出0。日志实际重编main／audio_io／waveshare_spi_display等公共输入；没有选lcdwiki_display或链接ST77922。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| chd-esp32s3-eye | 2,244,624 | 901,104 |
| lckfb-lichuang-dev | 2,263,472 | 882,256 |

两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0，独立项目名／16MB／checksum／validation hash匹配；EYE仍是单ES8311／ST7789，立创仍是双codec／PCA9557／FT5x06触摸路径。每份143项输入摘要／136个main文件集合、编译宏、生成配置与合并defaults和工作区匹配，模型均2,203,819字节，在4MB模型分区内。

记录为 `cached_build_attempts_20261110_lcdwiki_<profile>.json`；日志／配置／编译命令／依赖锁、产物／ELF、源快照及SHA-256在忽略目录 `evidence/lcdwiki-builds-20261110/`。SDK5.5.6-dirty与17个本地组件override保持原样，Python offline_guard拒绝外网，不是干净发行依赖构建。

公开固定源／API和已安装LVGL port共11项LCDWiki来源、六项Kevin来源的URL／SHA-256在 `board_research_v2.5.2.json`；原文在忽略目录 `evidence/lcdwiki-source-20261110/`。本轮未运行测试套件／独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA；人工决策配对函数、网关／运行时和自然插话文件未修改。

实际 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：87官方源码变体／88本地profile含1历史入口／101待源码；累计51变体曾缓存编译通过。当前2 passed／49 stale／6历史依赖预检失败／131 not_run，其中30项not_run已有源码；全部188实机not_run。Kevin继续inventory_only，全量目标仍进行中。

收尾实际 `git diff --check` 退出0，本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。两份各143输入／16项构建归档产物、LCDWiki11项／Kevin六项来源、预检日志摘要匹配；63条vendor表的代码／数据／长度／延时逐项与固定源一致。矩阵研究／registry摘要与LCDWiki未构建／Kevin待实现状态匹配，headless_ui摘要与上批一致。对照编辑前快照审阅本批diff，两项回归进程均已结束。
