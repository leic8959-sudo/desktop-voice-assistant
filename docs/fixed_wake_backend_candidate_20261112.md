# C3／C5／C6固定词后端候选（2026-11-12）

牛牛已授权这些芯片采用库支持的固定词、逐板标明差异。本批核实小智v2.5.2三个芯片默认配置均选择 `CONFIG_SR_WN_WN9S_NIHAOXIAOZHI=y`，因此统一采用 **“你好小智”／wn9s_nihaoxiaozhi** 作为24项板型的技术候选。原人工决定保持原文，具体模型选择属于本次源码取舍；仍需目标和声学验收。

候选实现为 `evidence/fixed-wake-source-20261112/patches/candidate/fixed_wake_word.cpp`，复用项目既有wake_word.h接口；仅存于evidence，**未接入main/CMake、注册板型、编译、链接或执行识别测试**。本批不把唤醒单模块进度算成整板源码支持。官方支持数量仍148／188，40项待源码。

## 核实结果

现有组件为ESP-SR2.4.7，manifest声明提交 `2f8c4b0459db5bbb39abd77adae27962d6d94bcb`。组件文件与芯片库按本机摘要归档；没有声称本地依赖是经过干净上游重建的发行物。小智来源固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，九份公开板级／唤醒／芯片文件按固定tree的Git blob核对。原raw下载曾连接重置，后改用已认证gh只读blob接口成功，没有改网络或关闭证书验证。

| 模型目录 | Kconfig显示词 | 元数据词 | 当前结论 |
| --- | --- | --- | --- |
| wn9s_nihaoxiaozhi | 你好小智 | 你好小智 | 三芯片官方默认，本批候选 |
| wn9s_hilexin | Hi,乐鑫 | 嗨乐鑫 | 有配置和模型，未运行识别 |
| wn9s_hiesp | Hi,ESP | Hi,ESP | 有配置和模型；元数据类型标为wakenet9，保留该原文差异 |
| wn9s_hijason | Hi,Jason | Hi,Jason | 有配置和模型；元数据类型标为wakenet9，README后缀与目录也不同，不推断声学等价 |
| wn9s_alexa | 无对应配置项 | Alexa | 有本地文件，不能据此当作直接可选Kconfig词 |

本批实际运行现有riscv32-esp-elf-objdump，分别读取C3／C5／C6的esp_wn_handle_from_name、esp_afe_handle_from_config和aec_create，九次均退出0。WakeNet分派实际引用esp_sr_wakenet9s_quantized，AFE实际引用单／双麦接口，不是此前MultiNet的直接NULL返回。这只是静态机器码／重定位依据，不证明create成功、占用内存、实时性或识别效果。

发现新的实际接入差异：三芯片AEC头文件提供SR／VOIP模式，缺少当前S3音频代码使用的AEC_MODE_FD_LOW_COST和aec_set_nlp_level。库实现来自esp_aec_c5.c；aec_create路径还对滤波参数作芯片内处理。因此不能把现有S3的AFE／AEC路径直接打开后就登记C5支持。具体AEC模式、参考时序、性能与声学插话须独立实现和验收，不静默删除该能力。

## 后端候选与模型产物

固定后端精确查找wn9s_nihaoxiaozhi，不用子串命中后回退到其他词；读取_MODEL_INFO_解析词并核对“你好小智”，释放分配的元数据文本。创建DET_MODE_90，要求16kHz、单通道、单词、模型帧不超过1024样本；缺模型或不匹配返回错误，不退成纯按键模式。

使用1024样本固定数组，输入每次最多2048样本，逐段凑满模型帧后推理；超限清状态，检测词ID1才触发并清模型。没有无界vector，未实现独立线程或新音频采集链路。既有“你好，小燕子”正样本与此词不同，因此PCM probe明确报不支持，不套用旧通过结果。

模型创建／reset／feed均限定在识别consumer任务；set_threshold只更新原子意图，consumer在下次feed应用。保留既有调用方0.20待机／0.30播放意图，**不将MultiNet数值直接写进WakeNet**。待机采用模型运行时默认阈值，播放候选加0.05并限制≤0.9999；这是尚未声学校准的技术候选。设置失败会停止检测并日志报错，不默默沿用错误阈值。未修改含人工决定的control_protocol.cpp。

使用组件原始pack_model.py，在独立模型目录生成仅含该词的模型包：

```text
evidence/fixed-wake-source-20261112/packed/srmodels-wn9s_nihaoxiaozhi-sr247.bin
size: 125951 bytes
sha256: fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b
```

包含三个原始文件和160字节包头；逐项读取包中offset／length并与原文件对照。按4KiB向上取整为126,976字节，只是该包的分区容量下限，不含擦写策略／额外模型／应用和OTA空间，也不等于运行RAM用量。本批没有写入任何设备或更改实际分区表。

## 各板下一步

机器可读逐板记录见 `evidence/fixed-wake-source-20261112/board-wake-candidates.json`，包含24项full_name／芯片／候选词／模型及当前状态。10项C3和10项C6需要完整内部RAM音频、显示、TLS与双OTA预算；不可沿用现有internal-RAM manual-only入口冒充语音唤醒实现。

四项C5中，微雪1.69与Moji2官方config声明QUAD PSRAM，Sensairshuttle声明SPIRAM及外部资产，ESP-Spot声明8MB Flash。这些配置是来源声明，不是实测容量。Sensairshuttle外部资产文件名指向wn9，需要单独处理，不能与本批wn9s包自动互换。

本批进一步读取微雪C5 1.69完整板类：240×280 ST7789、SPI2 mode0 24MHz、y gap20、RGB／invert；I2C0 SDA8/SCL9；单ES8311 24kHz I2S0 MCLK0/BCLK1/WS3/DOUT10/DIN2；TCA9554 P0 PA、P1触摸复位；CST816S及BOOT28、背光26。上游初始化默认同时拉高PA与触摸reset，本项目接入必须先PA关闭、真实codec静音后再启用并核对回读。BQ27220上游配置会改变电池参数，本批未执行或照搬。具体显示、音频、电源、触摸和板型入口未实现，不能据GPIO映射算整板完成。

## 实际执行与验收边界

本批执行公开blob读取、现有objdump静态反汇编、原组件模型打包及包结构／摘要检查；没有执行候选C++或模型推理。来源／输入与研究差异的检查命令：

```text
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/fixed-wake-source-20261112/close_research.py
git diff --check
```

24项仅追加本批研究（原有4项追加字段、新增20项调查记录），矩阵同步相同研究；候选词不计native profile。240个main代码文件和native registry与前快照相同。当前149本地profile含148官方源码入口；构建账本保持2 passed_cached_sources／62 stale_cached_sources／6 failed_cached_sources／118 not_run，本批未重跑。188项本轮实机全not_run，只有现有微雪1.8可实机验收。

未安装SDK／依赖、启动服务／Docker、写Git、刷机、OTA、上传或发布；完整逐板适配目标继续进行。
