# ESP-SensairShuttle C5源码候选（2026-11-12）

后续更新：本页保留初次源码候选的历史范围；当前已移除本板外部ILI9341依赖，并完成首次Sensair C5整板构建。最新改动、产物及限制见[SDK屏幕接入与构建记录](sensair_sdk_panel_20261112.md)。

本批新增 `espressif-esp-sensairshuttle` 源码入口，151／188项有源码入口增至152／188，36项待源码。它使用片内ADC麦克风和差分PDM扬声器，不能复用ES8311或Spot的I2S音频。**源码接入和Spot回归已检查；Sensair自身未编译，未实机验收。**

## 接线和音频

官方固定板文件声明：ADC1通道5，输入16kHz；PDM输出24kHz，正端GPIO7、负端GPIO8、PA控制GPIO1；I2C0 SDA2/SCL3；屏幕SPI2 MOSI23/CLK24/DC26/CS25，触摸CST816D地址0x15、SDA2/SCL3；BOOT28。板config声明C5-WROOM-1-N16R8和QUAD PSRAM，Flash实体容量仍待确认。

新增 `sensair_audio`：

- 使用现有 `esp_codec_dev` ADC数据接口，但把ADC句柄创建、格式设置、启停、读取、停止和deinit放到同一owner任务。
- ADC为16kHz、ADC1 channel5、12bit、12dB、TYPE2、单pattern；读操作使用固定内部1024字节缓存，超时后锁存失败并拒绝复用。
- PDM TX由本板直接创建，24kHz、`up_sample_fs=480`、四个scale设为MUL_4；每次codec `set_fmt`重新写回480，防止通用codec层改成`sample_rate/100=240`。
- GPIO8按显式GPIO输出配置并连接到PDM输出信号矩阵，避免沿用上游硬编码GPIO10的错误；当前未证明反相信号与板级功放波形已经正确。
- PA1先关闭并读回，只有实际DAC静音／解除静音结果成功后才允许启用；PDM发送错误、超时或短写都会锁存错误并关闭功放。PDM未启用时写入静音缓冲，不把静音软件状态当成扬声器已静音。
- 复用已有软件音量处理，但codec_dev对回调返回值有丢失边界；本候选的mute路径再直接核对实际callback结果。

ADC owner关闭前必须由上层join公开调用。候选没有导出完整cleanup join句柄；关闭等待超时或底层deinit失败后要求重启设备再初始化，避免晚到ADC任务访问已释放接口。这个限制暂时阻止完整板级生命周期验收。

## 显示、按键和资源

新增 `sensair_board`：284×240 RGB565，复用固定19条ILI9341公开初始化命令，SPI mode0 40MHz、gap36/0、换轴、mirrorY、反色；显示任务core0，双20行DMA。资料将芯片写成ST7789但类使用ILI9341公开API，本批保留这个来源差异，不把API名称当成芯片型号结论。

CST816D被作为触摸按键读取：读6字节状态，检查接触数、事件和错误，单点触摸按住期间为Listen，松开结束输入；没有把坐标映射成屏幕按钮。多点、BOOT同时按下、非法事件或I2C错误会取消并等待释放。当前没有真实PWM背光引脚；显示偏好只允许黑屏／显示切换，拒绝中间亮度和夜间调光。

板上外部资源配置指向wn9模型和字体资产。本项目使用已核实的本地`wn9s_nihaoxiaozhi`固定词模型，不下载外部资产，不把远端URL成功当作资源已存在。传感器／ShuttleBoard、RGB、原厂MCP、额外电源和电池未接入。

## 固定词和通用语音链路

本板使用 **“你好小智”／wn9s_nihaoxiaozhi**，UI提示和启动模型检查复用前轮固定词后端；缺模型或创建失败会在音频／控制协议前停止启动。输入ADC经现有16kHz AFE_SR、C5 SR AEC filter2和播放参考路径，不能据S3 FD/NLP回声抑制经验推断播放期自然插话通过。ADC采样噪声、PDM幅度、AEC实时性、固定词阈值和服务端PCM仍需声学及服务器验收。

## 依赖预检与共享回归

Sensair自身只读预检命令：

```text
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp-sensairshuttle --destination I:/xy_board_builds_20261109/sensair_c5_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

SDK版本5.5.6满足约束，但现有缓存缺少`espressif/esp_lcd_ili9341`，退出1且没有创建目标目录；组件2.0.2的固定来源与registry commit已归档，未安装。

由于共享源码扩展，本批在先前已通过的Spot C5隔离目录复跑回归编译：首次缺GPIO头文件的错误已修正后，`idf.py -DIDF_TARGET=esp32c5 -DASSISTANT_BOARD=espressif-esp-spot-c5 build`仍退出0；应用、模型、ELF和分区结果保持此前摘要。该回归只证明Spot选择器在当前本地SDK／组件快照仍可链接，不证明Sensair选择器可构建。

实际矩阵同步和静态证据脚本分别记录来源、研究字段、Spot回归输入哈希、Sensair未构建状态。188项实机全not_run；只有现有微雪1.8可用于物理验收。

当前152官方源码入口、153本地profile、36项待源码。构建账本1 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／117 not_run；Sensair dependency_resolution/build/device_acceptance均not_run。未安装依赖、写Git、刷机、OTA、上传或发布，全量适配继续推进。

2026-11-12证据修正：上一段Spot回归日志属于此前暂存源码；其旧构建记录已随共享源码变化成为stale。当前回归和来源摘要以evidence/board-evidence-reconcile-20261112为准。本页不构成Sensair目标构建证据。
