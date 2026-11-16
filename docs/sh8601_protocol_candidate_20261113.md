# SH8601 RGB565 适配基础（2026-11-13）

本批完成独立SH8601协议适配候选的编译、链接和镜像检查，为尚未构建的1.8原版、2.06与1.32准备共同显示基础。候选只在 `evidence/sh8601-protocol-20261113/candidate/`，**没有接入main／registry、安装依赖或新增正式整板通过项**。三板各自电源、触摸、音频、SD和整机资源仍需继续接入。

## 固定依据

取得三套厂商BSP和SH8601驱动，共47份文本，均按Git blob及SHA256核对：

| 参考 | 固定提交 | 初始化命令数 |
| --- | --- | ---: |
| 1.8 SH8601 BSP1.1.4 | c77caf968fa6b11f3b6a416c853c578d012a8cea | 9 |
| 2.06 BSP2.0.0及SH8601驱动2.0.0 | 9daffb7168b3c093a330da700988a22092294eef | 11 |
| 1.32历史SH8601 BSP | 22733e088a533378680a03e398279b2f7ab09a73 | 11 |

1.32的同名注册表版本后来曾换成CO5300，本批继续固定历史SH8601提交，不混用相同版本号的新内容。三套实际表分别保留命令、长度、顺序和延时，唯一差异是每套一个初始0x51=FF改成00。2.06保留410×502、X偏移22；1.32为466×466、X偏移6；1.8为368×448、无偏移。这些是候选配置，不是运行测量。

原驱动使用02参数／32颜色的32位QSPI封装；GPIO reset低10ms／释放后150ms，软件复位01后80ms。它仅支持X镜像，Y镜像和XY交换返回不支持，不能仅凭DCS命令相似套用CO5300的方向能力。实际SH8601绘图函数还丢弃了颜色传输的返回值；本候选保留错误传播，不据原函数返回OK推断真实传输完成。

## 候选实现

`sh8601_rgb565`借用项目已有SDK ST7789对象的RGB565窗口、绘图、X镜像和显示开关操作，前置一个只转发I/O的适配对象：参数命令为 `(02<<24)|(cmd<<8)`，颜色为 `(32<<24)|(cmd<<8)`。SDK原始init被替换，不发送其ST7789 RAMCTRL默认初始化，也不调用CO5300工厂。

适配只接受RGB565、RGB或BGR、1..1024的画布；初始和后续窗口／偏移不得超过16位地址范围，命令不超过128条、载荷不超过16字节、单条延时不超过1000ms。MADCTL／COLMOD若在表中出现，必须与创建时缓存一致，避免硬件值与复用SDK的内部缓存分离。三份表的实际发送结构和原文逐项比较，只有上述亮度变化。

GPIO／软件复位按SH8601参考时序实现；亮度0..100映射0..255，通过真实原始I/O返回结果。X镜像复用SDK，Y镜像为true或swap为true明确返回不支持；swap=false作为幂等无操作允许LVGL初始注册，不宣称实现了换轴。未接入未经确认的sleep实现。

命令数组与原始IO都由调用方持久持有；adapter拥有SDK panel与自己的context，保留panel的 `user_data`，不删除或关闭原始IO。删除前调用方必须停止并join所有使用者、排空原始IO；对象删除不是硬件传输完成屏障。颜色异步缓冲和传输完成通知继续归原始IO与后续板级LVGL拥有，未创建第二套完成回调或把缓冲提前释放。

原始IO的公开句柄不能证明它实际配置了32位QSPI，后续板级必须按真实引脚、mode、频率、DMA容量配置并核对。SDK自身SPI获取／排队／回收有无限等待边界，本候选的参数上限不构成整个硬件操作有限时长保证。

## 实际构建与证据

新隔离stage：`I:/xy_board_builds_20261109/sh8601_protocol_candidate_20261113`。现有SDK5.5.6-dirty、Xtensa14.2.0、offline_guard、2个编译任务，不安装外部驱动。工程空 `app_main()`，未执行链接根；该根仅保证三个表、adapter和实际SDK SPI对象被链接，不是模拟协议或生命周期测试。

```text
python -B evidence/sh8601-protocol-20261113/build_candidate.py
python -B evidence/sh8601-protocol-20261113/build_final.py
python -B evidence/sh8601-protocol-20261113/review_contract.py
python -B evidence/sh8601-protocol-20261113/inspect_candidate.py
python -B evidence/sh8601-protocol-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/sh8601-protocol-20261113/finalize.py
```

实际 `idf.py -DIDF_TARGET=esp32s3 build` 初次与最终均退出0；候选镜像207,088字节。readelf／nm／size、esptool镜像校验、分区解析和SDK只读查询均退出0；558项编译源、21份产物快照归档。ELF含候选创建、SDK DCS窗口／亮度／绘图和真实SPI pre／post回调，无外部SH8601工厂、HTTP／控制协议或音频入口。

4MB／单factory／无PSRAM配置仅用于组件链接，不能代替三块真实板的8／16／32MB、PSRAM、模型或双OTA布局。SDK现有强制UND `__cxx_fatal_exception`、`start_app`、`start_app_other_cores` 原样记录；不是干净发行、启动、实际外设或颜色验收。

最终空白检查首轮发现生成表末尾多一行空白，记录脚本退出1。已归档初次构建记录／产物，清理该行并同步stage，重新构建、采集和刷新研究证据摘要；最终检查退出0。正式固件没有因此变化。一次文件读取因未显式设置工作目录而找不到参考，已按项目绝对工作目录重新读取，未据此推断资料缺失。

## 状态与下一步

本轮只为三个变体追加研究字段；main、registry与所有正式构建账本按本批前摘要保持原值。最终范围／摘要审计和矩阵离线同步退出0，`git diff --check`退出0；三份新候选整文件 `--no-index --check`返回1且无空白错误。

仍为154目录／188变体、152个官方源码入口、153本地profile、36项无源码、4项明确必需功能缺口；累计112个官方变体曾缓存构建通过，当前4通过／108历史源码已变化／76未构建。三个SH8601板型的整板构建仍是not_run，188项实机均not_run。

后续需分别接入原始QSPI配置、GPIO／扩展器复位、供电与PA、FT5x06／CST820触摸、单／双codec音频、SD能力、LVGL局部刷新、模型／双OTA与完整资源预算，再做整板构建。仅有现有微雪1.8，不能推断其具体修订或替代其它实物。没有执行测试套件、类型检查、模型推理、长期服务、Git写操作、安装、刷机、OTA、上传或发布，完整适配目标继续进行。
