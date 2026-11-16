# 微雪ESP32-C5-Touch-LCD-1.69源码入口（2026-11-12）

后续更新：已在现有SDK5.5.6-dirty上完成首次真实C5整板缓存构建，移除原仅按上游版本设置的SDK6.1限制；固定“你好小智”、AEC、触摸、双OTA保留，实机未验收。见[本批说明](c5_lcd169_build_20261113.md)。

新增 `waveshare-esp32-c5-touch-lcd-1.69` 原生源码候选，应用身份 `assistant_c5lcd169`、目标esp32c5。这是第一项C5整板入口；**尚未解析完整依赖、目标编译、链接或实机验收**。官方源码入口从148增为149／188，仍39项待源码；加1历史入口共150本地profile。

## 来源和板型身份

板类、config.h/json、README、芯片及全局defaults固定在小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，源码按Git blob核对。ESP-SR2.4.7、codec_dev1.5.11、CST816S1.1.2和本机SDK接口参考均记录SHA256；本机库不是目标SDK6.1的编译证明。

Flash16MB来自固定全局defaults，QUAD PSRAM来自该板config.json；未将其写成硬件实测容量。板型要求SDK `>=6.1.0,<6.2.0`，保持与本批固定上游SDK系列一致。保留现有NVS区域、双3MiB OTA和model分区布局；实际镜像大小、Flash／PSRAM规格和修订仍待验收。

## 已接入源码

| 模块 | 当前实现 |
| --- | --- |
| 显示 | ST7789，240×280 RGB565，SPI2 mode0 24MHz；CS27／MOSI7／CLK23／DC24，软件复位，RGB／反色，gap0,20，不换轴或镜像 |
| 显示内存／任务 | LVGL任务core0，双20行内部DMA条带共19,200字节；屏幕亮度26 PWM启动保持0，界面初始化后设亮度 |
| 共享I2C | I2C0 SDA8／SCL9，codec、TCA9554、触摸各自设备句柄，显式有限I2C时限 |
| 功放／复位 | TCA9554 0x20，P0 PA／P1触摸reset；仅改拥有的位，先低电平锁存、清对应输入反相、设输出，再读回PA输入；触摸低100ms／高100ms |
| 触摸 | CST816S0x15，IRQ5配置但轮询；原始5字节count／event／12位坐标，初始先释放，多点／非法事件／越界／I2C错误取消当前按压并等松手 |
| 音频 | ES8311 BOTH，复用已有checked data封装和单IN_OUT codec设备；I2S0 mono24kHz，MCLK0／BCLK1／WS3／DOUT10／DIN2；codec PA=NC，由板级P0控制 |
| 静音／增益 | 通用静音先切外部PA，核对真实DAC callback，再允许PA启用；麦克风增益也核对底层callback，不把codec_dev丢弃的返回值当成功 |
| AFE／AEC | FIR24→16kHz保留；C5选择AFE_TYPE_SR、core0、PSRAM模式；独立软件AEC采用C5公开16kHz/filter2/SR_LOW_COST路径，保留播放参考，AEC创建／buffer失败停止启动 |
| 按键／界面 | BOOT28先松手再接受PTT及卡片操作；240×280既有布局，界面显示实际固定唤醒词 |
| 网络／绑定／媒体／OTA | 复用当前WiFi、六位绑定、控制WS／PCM媒体及HTTPS OTA路径；未新增明文TLS旁路或改变已有NVS／音乐授权 |

新板驱动是 `main/c5_lcd169_board.h/.cpp`；共享audio_io及waveshare_spi_audio只增加C5条件分支。C5的单ES8311单声道路径沿用现有单codec实现，不使用额外ES7210，也不创建第二个I2S控制器。

屏幕驱动在失败后拒绝重复部分初始化，要求重启；未实现运行时销毁／重建。功放、触摸和亮度的写入／回读仅表示本地API及寄存器确认，不证明实际声音／光线／触点响应。没有执行BQ27220初始化或电池封印／设计容量写入，不启用上游自动睡眠／断电流程；电量保持未确认。

## 固定词差异及启动门槛

本板使用 **“你好小智”／wn9s_nihaoxiaozhi**；原有S3／P4／S31仍沿各自自定义“你好，小燕子”路径。源于牛牛已确认的C3／C5／C6固定词取舍，逐板字段已写入profile和研究。固定后端从上批候选原样接入，按完整模型名及模型元数据核对词，要求16kHz／mono／单词，固定1024样本拼帧、单次输入≤2048。

启动在音频／控制通道前先检查固定词模型。缺失、不匹配或创建失败会显示错误并停止后续启动，不能只留下按键功能当成正常语音入口。之后control初始化仍调用同一个幂等后端，不重复创建模型。

阈值使用模型自身基线，播放期暂加0.05；这是未声学校准的技术候选，不能复用S3 MultiNet的0.20／0.30分数含义。原有“你好，小燕子”PCM正样本不能验证此词，固定后端明确拒绝沿用旧探针。

C5语音库没有S3 FD AFE／AEC_NLP接口：本板显式使用SR前端及SR AEC，不调用不存在的FD/NLP符号。**不能据此宣称其回声抑制、播放期识别率、自然插话或实时性与微雪S3相同**；这些仍需声学验收。没有移除播放参考、AEC或原有自然插话调度。control_protocol.cpp及其中人工决定保持本批前原文；内部wake事件保留既有“小燕子”服务标识，界面提示和实际本地词为“你好小智”。

## 构建工具与实际检查

CMake、Python和PowerShell板型白名单加入C5，仅允许已登记的C5 LCD1.69实现宏及固定词/QUAD配置。缓存构建脚本对C5选择现有riscv32工具链和C5 ROM ELF，避免沿用S3工具；本机确认存在相应ROM文件不代表SDK6.1工具链完整。

实际执行只读预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile waveshare-esp32-c5-touch-lcd-1.69 --destination I:/xy_board_builds_20261109/c5_lcd169_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

退出1：`Installed SDK 5.5.6 does not satisfy ... requirement >=6.1.0,<6.2.0; nothing was staged`。目标目录没有创建，没有安装、暂存或开始编译。兼容SDK需由牛牛准备，本批未安装任何依赖。

另外执行：

```text
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/c5-lcd169-source-20261112/close_research.py
git diff --check
```

检查范围为源文件、注册表、来源／输入摘要和差异关系，不执行候选C++、模型推理、构建、类型检查或测试套件。149个旧profile原文保留；含人工决定的控制协议、自定义唤醒、绑定、OTA、存储等保护文件按本批前摘要核对。共享文件变化使此前64项缓存构建记录标记stale；未重跑这些构建，不能用旧构建推断当前源码通过。

## 当前状态

149官方源码入口＋1历史入口＝150本地profile，39项待源码。构建账本0 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／118 not_run；新C5目标build、依赖解析、实机均not_run。188项本轮实机仍全not_run，仅现有微雪1.8可做物理验收。

待完成：目标SDK／组件解析和链接、镜像及峰值内存预算、C5单核音频／WiFi／TLS／UI并行性能、PA／触摸／颜色／屏幕间隙、固定词声学及播放期插话、同板HTTPS OTA与实体Flash／PSRAM身份。BQ27220／电池、省电、上游MCP和原厂附加功能未移植。本批未写Git、启动服务／Docker、刷机、OTA、上传或发布，完整适配目标继续进行。
