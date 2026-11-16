# 板型证据修正与Spot C5当前源码回归

本批修正上一轮报告中不一致的前提和状态，没有新增板型。权威矩阵为152／188官方源码入口、153本地profile（含1历史入口）、36项待源码；188项本轮实机均not_run。

- Sensair已在注册表中，选择正常284×240显示和ADC／PDM驱动，**不是headless**。移除本轮误加的headless分支条件；注册表保留原板级实现。修正研究中的“未注册”状态和误写的模型名，实际固定词为“你好小智”／wn9s_nihaoxiaozhi。
- 恢复all_boards中Spot阶段与Sensair ADC调查阶段的历史数字，避免以当前数量改写历史。Sensair页面修正重复的152→152和实际按住说话描述。
- 旧Spot构建记录仍代表旧源码，不将其修改成当前通过。新增独立当前源码回归记录，并归档262项源输入摘要、29项日志／产物摘要和完整main快照。

本次实际执行：在已有隔离目录、既有SDK5.5.6-dirty及16个缓存override下，`idf.py -DIDF_TARGET=esp32c5 -DASSISTANT_BOARD=espressif-esp-spot-c5 build`退出0。逐份比对当前main及板级defaults／registry／partition与暂存输入相同；Spot选择器不编译Sensair专用文件，所以不作为Sensair目标编译证据。

应用1,550,400字节，固定模型125,951字节。esptool image_info、RISC-V ELF header和分区二进制解析分别退出0；应用checksum及validation hash有效，模型SHA256仍为fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b。缓存构建成功不证明实时性、识别率、实际I/O或服务器业务。

新增权威构建记录为`firmware/assistant_espidf/cached_build_attempts_20261112_sensair_spot_regression.json`。旧记录保留，矩阵根据新记录与当前源码摘要恢复Spot的passed_cached_sources；其他64项历史缓存通过继续stale，6项历史失败，117项not_run，总数188。

Sensair目标依赖解析／编译／实机仍为not_run，缺少ILI9341 2.0.2；没有安装组件或用其他板编译替代。`evidence/sensair-native-source-20261112/final-audit.json`中此前的部分状态不一致，以本批来源和构建记录以及当前matrix为准。为补齐之前缺失的可执行审计，脚本位于`evidence/board-evidence-reconcile-20261112/close_research.py`。

另完成ESP-Hi C3十份固定源码调查，纠正其“无屏”前提并量化当前引擎载荷和4MB双OTA预算缺口，见[ESP-Hi记录](esp_hi_c3_readiness_20261112.md)。不新增C3 profile，不静默改为手动语音或单应用OTA。

未安装依赖、执行独立测试套件／类型检查、刷机、OTA、上传、发布或Git写操作。整体目标保持进行中。
