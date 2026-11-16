# C3／C6固定词与语音库目标链接

本批首次把当前`main/fixed_wake_word.cpp`原文放入独立ESP32-C3、ESP32-C6工程，使用对应芯片真实ESP-SR2.4.7预编译库和现有SDK5.5.6重新编译／链接。两目标`idf.py build`均退出0，固定词仍是牛牛已接受的**“你好小智”／wn9s_nihaoxiaozhi**。**工程app_main为空，没有采音或运行识别，不是小燕子整板固件。**固件main、native registry及生产分区本批未改。

## 实际覆盖

两项目位于`I:/xy_board_builds_20261109/c3_c6_speech_link_20261112/esp32c3`和`esp32c6`。复制本机既有ESP-SR2.4.7、ESP-DSP1.8.0、dl_fft0.8.0、cJSON1.7.19~2到该隔离根目录的vendor，使用本地override及Python外网保护，没有安装或下载组件。

| 项目 | C3 | C6 |
| --- | ---: | ---: |
| 实际目标构建退出码 | 0 | 0 |
| 应用镜像字节 | 386640 | 359520 |
| 单模型包字节 | 125951 | 125951 |
| 实际编译源文件记录 | 661 | 697 |
| DRAM data字节 | 11988 | 11632 |
| DRAM BSS字节 | 6320 | 6248 |
| PSRAM配置 | 未启用 | 未启用 |

DRAM数字来自ELF指定段，只代表这个组件工程的静态占用，不是WakeNet／AFE／AEC运行峰值，也没有包含完整终端Wi-Fi、TLS、音频DMA、UI、任务和协议缓存。`size`中的dummy段是地址预留，不应全部当作真实RAM数据。C3镜像最小芯片修订为v0.3；不同板卡芯片修订仍需实际核实，不能靠链接给全部修订背书。

每个工程通过不执行的链接根保留当前固定后端的init／reset／threshold／feed，并显式保留`afe_config_init/free`、`esp_afe_handle_from_config`、`aec_create/process/get_chunksize/destroy`。最终nm中这些入口、`esp_sr_wakenet9s_quantized`、单麦AFE接口表均存在；map实际引用对应`lib/esp32c3`或`lib/esp32c6`的库，而非S3库、假接口或纯按键实现。没有单独增加AFE／AEC候选运行路径；这些保留根只证明现有API和库能链接，不能证明分派后的create成功、参数兼容、内部任务调度或声学效果。

两份sdkconfig选择`CONFIG_SR_WN_WN9S_NIHAOXIAOZHI=y`、`CONFIG_SR_MN_CN_NONE=y`和Flash模型；固定后端源码摘要与当前固件main相同，头文件来自真实芯片include。原库packer在构建中各生成125951字节、仅含一个模型的srmodels.bin，与此前逐文件核对的模型包逐字节相同，SHA256均为`fe559fe9ed38dc041ecfbcde6568d4cdc6abb3c7073a60747e67b6083d78ce2b`。这是模型包装证据，不是运行RAM占用或识别成功证据。

检查布局在4MB内保留两份1856KiB应用槽、128KiB模型、192KiBstorage，以及NVS／otadata／phy。SDK分区容量检查与二进制解码退出0，仅证明这个空入口组件工程和模型在该布局内可放下；不能推断完整终端也能放下或OTA、NVS迁移已验收。本批没有替换生产板卡分区、关闭本地唤醒、改为纯按键或删除第二OTA槽。

两个ELF均有当前dirty SDK用`-u`强制留下的`start_app`未定义符号，实际`esp_startup_start_app`存在，未出现其他未定义名。沿上一批已核实的SDK CMake旧名与startup实现新名差异如实记录；本批未修SDK，不宣称零未定义、可启动或干净SDK发行构建通过。

## 执行和证据

真实命令如下，Python为既有IDF环境：

```text
python -B evidence/c3-c6-speech-link-20261112/build_isolated.py esp32c3
python -B evidence/c3-c6-speech-link-20261112/build_isolated.py esp32c6
python -B evidence/c3-c6-speech-link-20261112/inspect_link.py esp32c3
python -B evidence/c3-c6-speech-link-20261112/inspect_link.py esp32c6
python -B evidence/c3-c6-speech-link-20261112/close_research.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/c3-c6-speech-link-20261112/close_research.py
```

两份build.json保存完整idf命令数组、工作目录、真实工具版本、开始／结束时间及日志摘要。inspect实际运行RISC-V readelf／nm／size、esptool image_info和SDK分区解码，检查命令和退出码见各`*.commands.json`；两镜像checksum／validation hash有效。每芯片39项产物／库输入记录，其中28份工程和产物快照、11份芯片预编译库摘要；直接编译输入逐项记录源码摘要、命令和cwd，不能当作所有传递头文件完整清单。四份本地组件全部文件摘要在vendor-inputs.json，原组件和隔离副本保持一致。

核对脚本早期因检查文本使用`Validation Hash`而esptool v2实际输出`Validation hash`，审计断言失败；修正大小写判断后实际重跑通过。构建本身两次均一次通过，没有修改候选源码来规避编译问题。没有执行独立测试套件、识别／声学／错误注入测试或类型检查。

## 后续与清单状态

C3/C6的20个变体仅追加本次共享组件证据，仍inventory_only。本批没有把“支持库存在”当成整机支持；完整引擎现有五项缓存载荷下限533376字节，尚未包含算法、TLS、任务等，仍显式依赖PSRAM。此前内部RAM手动分支不保留完整唤醒／AFE／AEC，不能拿它直接登记C3/C6。下一步需要保留功能地调整缓存和流控、接入芯片AEC模式与内部RAM分配，再逐板接入真实音频／显示、核对双OTA和进行整板构建。

现有C5四个入口保持独立，不把C3/C6结果重复算成C5整板通过。当前152／188官方变体有源码入口，153本地profile含1历史入口，36项待源码；账本仍0 passed_cached_sources／65 stale_cached_sources／6 failed_cached_sources／117 not_run，188项本轮实机全部not_run。只有现有微雪1.8实物，不能替代这些芯片验收。本批未安装、启动服务、写Git、刷机、OTA、上传或发布。
