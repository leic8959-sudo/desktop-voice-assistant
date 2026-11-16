# Movecall Moji2 C5源码入口（2026-11-12）

后续更新：Moji2 C5复用原184条表与共享QSPI面板，已完成首次真实C5整板缓存构建；固定“你好小智”、单codec和无触摸差异保留，VoCat S3回归通过，实机未验收。见[本批说明](moji2_qspi_native_20261112.md)。

新增 `movecall-moji2-esp32c5` 原生源码候选，应用身份 `assistant_moji2c5`。官方源码入口从149增至150／188，38项待源码；另1历史入口，共151本地profile。**仅完成源码登记和本地差异／来源核对，尚未完成依赖解析、编译、链接或实机验收。**

## 固定来源与取舍

板类、config.h/json和中英文README固定在小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。板类文件名仍为movecall_moji2_esp32s3.cc，但配置、类名及实际芯片目标均是C5，不按文件名选成S3。

README声明SDK5.5，因此本板依赖约束为 `>=5.5.6,<5.6.0`，与前轮微雪C5板的6.1系列约束分别保存。Flash16MB来自固定全局默认，QUAD80MHz PSRAM来自本板配置，不是实体容量测量。沿用现有NVS及双3MiB OTA／model分区，实际镜像和运行RAM预算尚未验证。

ST77916固定2.0.2的注册页声明提交 `91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f`；本批按该提交读取公共header、SPI实现、接口分派、MIPI实现、manifest与license，并核对Git blob。资料只作为接口参考，未安装组件，不能当作完整依赖解析完成。

## 源码接入

| 模块 | 当前实现 |
| --- | --- |
| 显示 | 360×360 RGB565，ST77916、QSPI2 mode0 40MHz；CS3／CLK0／D0..3=9/8/7/6／RST1，32位命令、8位参数，无镜像／换轴，gap0 |
| 命令表 | 本板184条初始化的字节、长度、顺序、延时与现有VOCAT_PANEL_COMMANDS逐项相同，复用原表及其保留的MIT条款，不另拷贝一套；含反色21、退出休眠11及NOP等待120ms |
| 画面内存／任务 | 复用现有QSPI显示层，两像素X对齐、双20行内部DMA条带共28,800字节，LVGL core0；采用现有360圆屏布局内缩，实际可视区域待实物 |
| 触摸 | 固定板类没有触摸入口，BSP_CAPS_TOUCH=0，不创建触摸设备／I2C读／LVGL触摸指针；这不是对未知硬件修订的物理断言 |
| 电源 | 新板级helper先关PA5并设INPUT_OUTPUT读回、背光2先保持暗；100ms互斥保护PA更新及pad读回。实际codec解除静音成功后才启PA，codec自身PA=NC避免抢占引脚 |
| 音频 | I2C0 SDA26／SCL27；ES8311 BOTH、单checked IN_OUT设备、mono24kHz；I2S0 MCLK25／BCLK11／WS24／DOUT23／DIN12，真实增益／静音callback复用C5路径 |
| 语音 | 复用前轮FIR16kHz／AFE_TYPE_SR core0／C5 SR_LOW_COST AEC filter2和软件播放参考，AEC失败拒绝启动；固定“你好小智”／wn9s_nihaoxiaozhi及模型校验、界面提示、缺模型拒绝启动门槛保留 |
| 输入 | BOOT28先松手，再接受PTT及既有卡片操作；无触摸时由硬件卡片输入完成控制 |
| 网络与保存 | 复用当前WiFi、绑定、控制WS／PCM媒体、HTTPS双OTA及NVS，不改控制协议和授权数据 |

共享QSPI代码只按BSP_CAPS_TOUCH裁剪无屏幕触摸的分支，并增加Moji2独立引脚，未执行微雪的TCA9554复位或CS21屏幕ID探测。共享单codec路径仅增加Moji2选择，既有S3／P4／S31和微雪C5配置保留。

## UART控制台冲突

本机SDK的C5 uart_pins.h明确UART0 TX11／RX12，分别与本板I2S BCLK11／DIN12重叠。io_mux_reg.h明确USB Serial/JTAG的DM13／DP14。因此本板defaults覆盖芯片级UART默认，选择USB Serial/JTAG13／14，CMake配置门槛拒绝UART默认或未选USB控制台的组合。

这些只是芯片布线和配置依据；USB连接器、板级走线、控制台日志、下载／重启方式仍未实机验收。没有通过随意指定其他UART脚消除冲突，也没有修改微雪C5不冲突的控制台选择。

## 未实现与验收边界

LED10、电池ADC4及5.1MΩ分压遥测、睡眠／省电、原厂MCP切换未移植，不报告未经硬件确认的电量。固定词阈值和C5 SR AEC延续前轮未声学校准边界，不能宣称与S3 FD/NLP回声抑制或插话效果等价。

新板驱动失败后要求重启，不实现运行时销毁重建。GPIO读回不等于实际扬声器发声；显存容量、40MHz信号质量、色彩／gamma／复位和可视区域均待实物。仅现有微雪1.8可做物理验收，Moji2全部实机项not_run。

## 实际执行

```text
python -B tools/prepare_cached_firmware_build.py --profile movecall-moji2-esp32c5 --destination I:/xy_board_builds_20261109/moji2_c5_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

实际退出1，SDK版本门槛通过后报 `Required component is not installed: espressif/esp_lcd_st77916; nothing was staged`。目标目录未创建，未编译或安装；精确组件和其cmake_utilities0.*传递依赖仍需准备、解析及构建。

```text
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/moji2-c5-source-20261112/close_research.py
git diff --check
```

源码审阅脚本核对固定来源、命令表完整等同、注册表仅新增Moji2、改动范围、保护文件和最终输入摘要，以及所选择的QSPI／无触摸／PA／USB分支。该检查不执行候选C++，不属于编译、协议、声学、时序或内存测试。control_protocol.cpp及其中人工决定、fixed_wake_word.cpp、微雪C5独立驱动、绑定、OTA和存储等均保持本批前原文。

当前150官方源码入口＋1历史＝151本地profile，38项待源码。构建账本仍0 passed_cached_sources／64 stale_cached_sources／6 failed_cached_sources／118 not_run；新目标dependency_resolution/build/device_acceptance均not_run。未重跑其他固件构建、执行测试／类型检查、启动服务／Docker、写Git、刷机、OTA、上传或发布，全量目标继续进行。
