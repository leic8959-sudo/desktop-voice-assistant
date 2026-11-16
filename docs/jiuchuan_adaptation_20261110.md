# Jiuchuan S3 专用240×296面板与单codec源码候选

2026-11-10，Asia/Shanghai。固定基线为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个发布目录／188个变体。本批新增 `jiuchuan-s3` 基础语音终端源码候选，应用身份 `assistant_jiuchuan`；其他板型按各自记录判断，不把一份编译推广至全量支持。

## 固定来源与面板差异

[固定Jiuchuan目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/jiuchuan-s3)中的config、实际板类、面板驱动、电源控制与README。公开副本位于忽略目录 `evidence/jiuchuan-public-20261110/`，沿上一批公开来源读取，不重新下载依赖。根sdkconfig默认16MB Flash／Octal PSRAM，当前板config.json没有覆盖；不是实物容量测量。

驱动文件名为 `esp_lcd_panel_gc9301.c`，实际创建函数及实现名为GC9309NA，头文件注释又误写ST7789。有效代码编程240×296地址窗口、COLMOD=0x05、MADCTL=0x48、B4=0x80；不能据文件名套用ST7789或宣称已识别物理芯片。

本地 `jiuchuan_panel_commands.h` 从有效初始化代码逐项提取48条command／data，最大12字节，保留sleep-out后80ms和RAMWR后10ms两处延时；注释命令未启用。提取索引包含原始源码SHA256和全部命令，放在公开证据目录。COLMOD仍为固定源码的0x05，未按其他屏幕改成0x55；B4及字节序保留，需真实颜色验收。

来源文件有Apache-2.0声明、Espressif2021–2024版权及第二段“2024 Your Name”占位版权，本地数据头保留。嵌套 `main/third_party/xiaozhi_jiuchuan/LICENSE` 保存已安装ESP-IDF的Apache2.0条款，README说明修改范围；固定源码树未列该板嵌套NOTICE／LICENSE。不修改项目整体LICENSE，不据占位文字推断其他来源权利。

`jiuchuan_panel.cpp` 提供本地ESP-IDF面板接口，检查每次传输、像素范围和格式，绘制只传有效矩形的RGB565字节。原始驱动的mirror／swap接口直接返回成功，本地实际写MADCTL，写成功后才更新缓存；默认BGR／mirrorX=true／swap=false保持0x48。LVGL注册使用同一镜像。未提供证据的非零gap返回不支持。板无物理复位脚，沿来源只等待120ms，不添加未经核实的复位命令；这不证明掉电、软重启或异常屏状态恢复正确。

## 引脚与终端行为

| 项目 | 本地候选配置 |
| --- | --- |
| 屏幕 | SPI3 mode3／80MHz，CLK41／MOSI40／DC39／CS9，RST=NC、背光46；240×296、BGR565、invert=false、mirrorX=true／mirrorY=false／swapXY=false、gap0 |
| 布局 | 专用240×296布局，16px中文，顶部内容缩进30像素、正文宽192、240×120点云及固定动作区；真实外轮廓和文字裁切未验收 |
| 音频 | 单ES8311、输入／输出24kHz；I2C1 SDA1／SCL2，I2S0 MCLK38／BCLK14／WS13／DIN11／DOUT12／PA42，MCLK作codec时钟 |
| 电源 | RTC GPIO5解除旧hold后初始化并置高，在显示初始化前保持供电；GPIO3输入下拉、高有效，先释放再连续按3秒，将RTC5拉低，20ms有界轮询 |
| 通话／卡片 | BOOT0先释放再按住说话；现有卡片短按选择、长按松手确认；不复用电源键为录音，避免长按关机时提交音频 |
| 音量 | GPIO6／7上拉低有效，先释放、多键／跨键取消沿现有输入逻辑；采用项目统一10%初次步长、保持2秒后5%重复，而非原上游8／80映射 |

上游创建I2C1却把旧I2C_NUM_0参数与该handle一起传给codec，本地按实际初始化统一使用I2C1端口及handle。沿已有单codec的模拟增益、软件音量／静音、24→16kHz采样、唤醒及软件AEC；配置里的硬件参考flag未被单codec构造消费，不宣称硬件参考已接通。待机30dB／播放12dB及codec电压增益沿既有候选策略，不能声称此板已校准。

长按电源只记录供电释放尝试，不能把GPIO调用成功当作物理关机；USB供电和电池锁存均待实物验收。电源键短按切换通话／三击重新配网、ADC4电量／充电、LED10、自动省电／睡眠唤醒、TF卡功能尚未接入，不输出虚假电量或卡状态。当前不是整板所有专属功能已完成。

## 下一批调查

三款继续保持调查状态，没有登记native profile或继承Jiuchuan构建结果，公开副本及逐文件URL／摘要在 `evidence/jiuchuan-next-public-20261110/`：

- 正点原子ATK-DNESP32S3：320×240 ST7789、ES8388，16bit XL9555控制屏相关输出，不能套8bit PCA9557／TCA9554；无直接背光GPIO，需要查清bit8／bit2的硬件含义。
- 立创SZPI：320×240 ST7789、ES8311＋ES7210及PCA9557，实际FT5x06触摸，与近似BigSmart的GT911不同；来源明确MIC1增益28dB／MIC3参考槽2，不把普通软件AEC当硬件参考已接通。
- AiPi-Lite：128×128、ES8311 BCLK衍生时钟；面板实际40MHz而配置宏为20MHz，需明确采用依据。其[固定README](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/xorigin/aipi-lite/README.md)明确原厂EUI／凭据位于0x9000起的0x4000区域，当前NVS也从0x9000开始。后续必须先设计原厂信息保留、备份恢复及分区方案，不能直接提供覆盖原厂信息的刷写入口。本轮仅读公开源码，没有读取或复制真实原厂凭据。

## 实际构建与边界

新板实际调用：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board jiuchuan-s3
```

使用既有SDK5.5.6-dirty／匹配工具／Python和17个本地组件override，Python外网守卫拒绝外网socket／DNS。SDK保留原NimBLE／idf_as_lib修改，override包含无关BSP，不是干净发行依赖锁。没有安装依赖。DuChatX、Kevin SP V4及微雪LCD0.85分别在原隔离目录运行临时 `xy_jiuchuan_regression_20261110.ps1 -Stage <目录> -Board <profile>`，同步完整main／third_party和配置后执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，覆盖已有数字、16kHz单codec／I2C1和双codec分支。

Jiuchuan完整缓存构建及最终同目录 `reconfigure build` 均退出0，日志确认 `jiuchuan_panel.cpp` 被编译／链接。三款既有入口回归也均退出0；本轮不把它们未选中的Jiuchuan专用源码算作已编译。既有LVGL9.6弃用API警告仍在，未为此改无关实现。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| jiuchuan-s3 | 2,247,840 | 3,145,728 | 897,888（约29%） |
| du-chatx | 2,207,152 | 3,145,728 | 938,576 |
| kevin-sp-v4-dev | 2,244,288 | 3,145,728 | 901,440 |
| waveshare-lcd-0.85 | 2,252,112 | 2,752,512 | 500,400 |

四款实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0；项目身份、0.85的8MB／其余16MB设置、checksum及validation hash有效。模型均2,203,819字节，0.85模型分区2,359,296字节，剩155,477字节，其余为4,194,304字节。核容量使用0.85专用分区表，不套根16MB表。

一次性 `xy_record_jiuchuan_20261110.py` 退出0，逐项核82个源码／配置摘要、完整75文件main集合、合并defaults、实际选择源、生成配置和应用／模型容量。四份记录为 `firmware/assistant_espidf/cached_build_attempts_20261110_jiuchuan_<profile>.json`；日志、镜像、ELF、配置及源码副本在忽略目录 `evidence/jiuchuan-builds-20261110/`。新板记录保留完整与最终构建日志，三个旧入口保留本次实际回归日志。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前48个本地profile对应47个官方变体（另1历史兼容入口），141项尚无源码；累计27个官方变体曾分别通过缓存编译。按当前源码摘要，4项通过、23项历史通过证据过期、5项依赖预检失败、156项未构建（其中15项已有源码）；累计通过数不能替代当前源码状态。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/CMakeLists.txt`，退出0。对本批未跟踪源码、许可、配置及文档执行 `git diff --no-index --check -- NUL <文件>`，返回1表示与NUL存在差异，未输出空白错误，保留LF／CRLF提示。未运行测试套件或独立类型检查，未连接硬件、运行声学／屏幕／手机验收、安装依赖、刷机／OTA、启动宿主长期服务、Git写入、上传或发布。牛牛只有现有微雪1.8，屏控修订尚未确认，全部188项实机状态仍为not_run；并发网关／运行时和自然插话修改保留。
