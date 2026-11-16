# 微雪 AMOLED 2.16 面板候选（2026-11-13）

本批为已有 `waveshare-amoled-2.16` 入口补充可编译链接的显示候选，尚未接入正式 main／profile，也没有新增整板通过记录。正式入口仍缺 BSP2.0.1、adapter0.6.0 和触摸传递依赖；不以组件镜像代替整板固件。

## 固定来源及修正

厂家 BSP2.0.1 固定在 `bfeb6e6d5737178cdde78b630c8118074da0a657`，从完整非截断 tree 读取 12 份 BSP 文件、6 份 CST9217 文件，逐份校验 Git blob 与 SHA256。固定源码明确为 **CO5300／CST9217**，不是 SH8601；原 research 里把其它修订的 SH8601 漂移问题笼统放入本板 blockers 不够准确。本批追加准确来源说明，保留旧记录供追溯。

本批只读取得源码参考，没有安装厂家 BSP、触摸组件或 adapter，没有修改已安装 SDK／组件。候选使用已有 CO5300 **2.2.0** 与 cmake_utilities **0.5.3**。CO5300 的当前公开工厂、QSPI封装、reset／初始化、窗口／像素传输和亮度 API 已读取；没有自行实现第二份 CO5300 驱动。

## 显示候选

文件在 `evidence/amoled216-native-20261113/candidate/amoled216_panel.h/.cpp`。其接口仅创建面板并调节亮度，调用方必须先完成板级供电；不创建 LVGL、触摸、音频、PMIC或后台任务。

- 480×480 RGB565，SPI2 mode0／40MHz，CS12、CLK38、D0..3=4／5／6／7，LCD RST39。初稿使用 SPI3，在读取完整 BSP 头确认 SPI2 后已于首次编译前更正；未把不同 host 记成原始配置。
- QSPI 32位命令及 02／32 参数／像素封装由现有驱动宏处理，队列深度3。最大 SPI 单次传输设为 480×20×2＝19,200 字节；这是总线限值，没有据此声称已分配 LVGL 条带或整帧缓冲。
- 固定 BSP 的 **15条初始化命令**按顺序、长度和延时逐项对比，唯一差异是 0x51 的 FF 改成 00，使界面就绪前保持暗屏。0x11／0x29 的各600ms及其它窗口值原样保留。
- MADCTL保留原表的 **A0**。没有在初始化后调用通用 `mirror(false,false)` 或 `swap_xy(false)` 覆盖方向，也没有把同为480×480的Mosaico方向当作本板方向。
- 初始化只尝试一次，失败传播实际错误并尝试暗屏；可能被面板回调持有的部分对象保持存活，要求重启，不在失败后重复占用 SPI。首次成功后可取回同一面板和IO句柄。
- Apache-2.0许可和来源通知放在本批 `LICENSE`、`NOTICE.md`；没有修改项目整体许可或规则文件。

当前驱动的 GPIO复位为低10ms／高后150ms，随后先发MADCTL／COLMOD，再执行这15条表；实际电平、时间和方向均未实物验证。亮度API成功只表示本地I/O返回，不证明实际亮度或屏幕已显示。

## 触摸不能直接替换的依据

固定 CST9217 驱动地址为 **0x5A**，读取 **0xD000起10字节**，检查第6字节AB，count位于第5字节低7位，按status6解析12位坐标。固定BSP触摸方向为swapXY=true／mirrorX=false／mirrorY=true，RST40／IRQ11。

该驱动先把count截断为1，调用者不能据一个返回坐标认定实际仅有一个触点；配置读取只记录checkcode／resolution／chipType，没有据其数值验证芯片身份，也没有明确切回普通模式。I/O配置默认没有有限transaction timeout，读取失败会重试和复位。这些是当前代码的行为与接入风险，不能等同于硬件上已复现故障。

前批Mosaico的CST9220参考代码另有具体芯片ID检查、普通模式恢复、legacy／HYN212协议区分、部分协议ACK、双点报文和坐标处理。商品资料中CST9220与固定BSP的CST9217差异仍需真实板修订、身份／分辨率和报文确认；不能因相同0x5A地址或相似坐标格式就互换，也不能用CST816S替代。此批仅比较原文，没有导入或执行CST9220驱动。

完整板接入仍需有限I/O、保留原始触点数、错误取消并等释放、初始化退出模式及正确坐标变换；恢复策略必须结合确切触控器协议。PMIC／音频／PA／SD／LVGL和整板资源、模型、双OTA仍在待办中。

## 实际构建与检查

隔离目录 `I:/xy_board_builds_20261109/amoled216_panel_candidate_20261113` 使用现有 SDK5.5.6-dirty、Xtensa14.2.0、两份本地override、offline_guard和2个编译任务。工程 `app_main()` 为空；未执行的链接根只保证面板候选和CO5300方法被链接。

实际入口：

```text
python -B evidence/amoled216-native-20261113/build_candidate.py
python -B evidence/amoled216-native-20261113/review_contract.py
python -B evidence/amoled216-native-20261113/inspect_candidate.py
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/amoled216-native-20261113/direct-dependencies.json
python -B evidence/amoled216-native-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/amoled216-native-20261113/finalize.py
```

目标命令 `idf.py -DIDF_TARGET=esp32s3 build` 一次退出0，候选工程镜像 **218,112字节**。readelf／nm／size、esptool镜像校验、分区解码和SDK只读查询均退出0；561项编译源记录、20份产物快照已归档。ELF含候选创建／亮度、真实CO5300工厂及驱动亮度API，不含触摸工厂、adapter显示、音频或控制协议。所有候选与暂存源、两份组件输入摘要匹配。

该工程采用4MB／单factory／无PSRAM，完全用于组件链接，不是本板16MB／PSRAM／双OTA的生产配置。SDK三个强制UND名称 `__cxx_fatal_exception`、`start_app`、`start_app_other_cores` 原样记录，未改SDK，不声称启动或干净发行验收。

初始化表记录脚本首轮正则误将外层数组当成字节载荷，退出1；修正为只匹配不含嵌套花括号的字节数组后退出0，逐项仍仅有上述亮度差异。这个修正只涉及记录脚本，没有修改固件或候选来迁就检查。

直接依赖报告本次为 **153本地／152官方入口，927条声明，106本地／105官方满足直接条件**；这不是传递依赖解析、固件构建或运行证明。AMOLED2.16本身仍缺两项直接组件及唯一BSP来源；触摸是BSP的传递依赖，不能因为直接表只有两个缺组件就认为触摸已就绪。

## 本轮状态

最终审计退出0，正式 main、registry和全部旧构建记录保持原值，只为2.16追加研究字段并离线同步矩阵。`git diff --check`退出0；两份候选新文件的`git diff --no-index --check -- NUL <文件>`返回1且无空白错误。

仍为152个官方源码入口、153本地profile、36个无源码入口、4个明确功能缺口；构建账本仍1当前缓存通过／104历史源码已变化／83未构建，累计105项曾缓存构建通过。188项本轮实机均未验收，仅有现有微雪1.8，不能替代2.16实物。

未运行测试套件、模型推理、外设／网络业务、类型检查或长期服务，未安装依赖、执行Git写操作、刷机、OTA、上传或发布。全量适配目标保持进行中。
