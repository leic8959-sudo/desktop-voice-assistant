# 微雪P4／P4X 4.3寸 ST7701源码候选及前批纠正（2026-11-12）

新增 `waveshare-esp32-p4-wifi6-touch-lcd-4.3` 和 `waveshare-esp32-p4x-wifi6-touch-lcd-4.3` 源码入口。**两个新P4目标缺驱动，未编译、链接或实机验收。**本批还修正7寸ILI9881C候选的复制错误，并补齐7B完整NVS保留范围；不能把旧S3构建通过理解成这些P4路径已验证。

## 4.3寸实现与固定来源

基线为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154目录／188变体。屏幕配置和原始命令表均读取该固定提交，ST7701精确参考包为 `2.0.2~2`，对应仓库提交 `b9b4cc9fa85345b46af366a002785e7c6e8ef45d`。48份参考资料的来源及SHA256在 `evidence/waveshare-p4-st7701-source-20261112/source-artifacts.json`；参考包未安装或链接到目标。

| 项目 | 源码候选设置 |
| --- | --- |
| 屏幕 | ST7701 MIPI，480×800 RGB565，无交换轴／镜像 |
| DSI／DPI | 双500Mbps，30MHz；H12/42/42，V8/2/60 |
| 供电与背光 | PHY LDO3＝2500mV，RST27，BL26低有效，初始化为关闭的1023占空比 |
| 内存 | 一帧768,000字节；两份20行绘制缓冲及一份软件旋转缓冲，共57,600字节PSRAM；PPA关闭 |
| 触摸 | GT911，I2C1 SDA7/SCL8、400kHz，只探测0x5D／0x14，无RST／IRQ／坐标变换；读错误、越界、多触点取消后等待松手 |
| 声音 | ES7210 TDM4输入取MIC1／slot0，ES8311 DAC输出，24kHz；I2S0 MCLK13/BCLK12/WS10/DIN11/DOUT9，PCM软件AEC |
| 功放与按键 | PA53由公共路径在DAC静音结果检查后开启并读回，codec PA设NC；BOOT35先松手后说话／操作卡 |

ST7701的vendor结构使用 `flags.use_mipi_interface`，没有ILI9881C的 `lane_num` 字段。本批已按实际头文件改正。上游4.3寸分支的39条初始化命令，按字节、长度和延时保留；数据改为静态C++数组，避免临时数组地址和不相关面板分支。原始／生成后的命令逐项映射见 `command-table-extraction.json`，来源MIT许可放在 `main/third_party/xiaozhi_p4_panels/LICENSE`。

旧P4最低修订1.0、P4X最低3.0，使用独立应用名 `assistant_ws_p443`／`assistant_ws_p4x43` 和对应ESP-SR库。两个配置均声明16MB Flash、mn7中文模型、Hosted2.12.11／Remote1.6.4 C6 SDIO，复用现有HTTP绑定、WebSocket PCM媒体及OTA。芯片修订、模型、SDK配置和原协议仍需专属目标构建与实物验证；相机、SD缓存、蓝牙、电池和原厂手势没有完成。

## 同时修正的前批错误

- 7寸ILI9881C的profile为16MB，但板型默认文件误继承7B的32MB和重复修订选项。现已统一为16MB，清除重复项。
- 原7寸命令表错误提取到后面的8寸／10.1寸条件分支。现仅包含7寸的196条有效命令，全部使用静态字节数组，不保留游离的`#elif`或其他控制器类型。
- 7寸BL26是高有效；初始化、熄屏及亮度计算已统一，修正残留的“低有效”注释和错误缓冲尺寸日志。ILI9881C1.1.0确实实现`disp_on_off`；此前“没有该操作”的说明不准确，当前仍通过背光熄屏。
- P4模型／Hosted／芯片修订检查现覆盖全部登记P4入口，MIPI候选要求PPA关闭且至少报告两个触点；ILI／ST显示失败沿已有P4分支返回关闭PA，不进入通用断言重启路径。
- 7B的烧录保留范围补为全部200KB工厂NVS和840KB配置NVS，替代默认16KB保护范围。该修改未触发任何设备写入；7B32MB适配分区保持不变。

此前旧S3构建并未编译这些P4分支，所以未发现上述问题。旧日志和构建记录保留，本批建立新的来源、快照和回归证据。另从已归档固定来源恢复了被前批整组过滤误删的12个MIPI变体研究记录；这些变体仍待源码，没有计作新增适配。`review-start/`快照创建时已包含本轮早期ST7701草稿，**不能当作动手前基线**；`baseline-scope.txt`明确记录这个范围，`final-inputs/`提供最终候选供审阅。

## 实际检查

`preflight-results.json`保存了6条真实命令和完整输出，格式为：

```text
python -B tools/prepare_cached_firmware_build.py --profile <对应板型> --destination I:/xy_board_builds_20261109/st7701_review_<对应板型>_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

两项4.3寸均退出1，缺 `espressif/esp_lcd_st7701`，要求`2.0.2~2`；两项7寸退出1缺ILI9881C，两项7B退出1缺EK79007。6个目标目录均未创建。完整依赖解析、P4编译及新目标镜像均未执行。早期对尚未登记4.3入口的调用退出2（参数不接受），不是缺组件证明；以本批这6条最终实际预检为准。

最终已授权S3缓存回归命令均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创应用2,263,824字节，RLCD应用2,228,000字节；各3MB app分区，模型2,203,819字节／4MB。各214个main文件集合及222项源码／配置摘要匹配最终工作区，23份产物归档。`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0，确认身份、16MB、校验、分区、双codec及FT5x06或TE路径。使用既有IDF5.5.6-dirty、17份本地override、offline_guard和并发度2；没有安装依赖或运行测试套件。

这些编译定义和选中源码排除了所有P4显示文件，**ST7701／ILI9881C／P4库／mn7／Hosted没有被旧板编译覆盖**。命令表映射是静态核对，不是驱动或屏幕验收。`git diff --check`退出0；控制协议人工决策、OTA、配网和唤醒保持原摘要。无Git写操作、刷机、OTA、上传或发布。

当前127个官方变体有源码入口，另1个历史入口，共128个本地profile；61个官方变体待源码。P4族27项中10项有源码候选，17项待实现。累计64个官方变体曾缓存构建通过，当前2 passed／62 stale／6历史failed／118 not_run，其中57项有源码。全部188项本轮实机not_run，唯一现有微雪1.8不能代替P4验收。全量适配目标保持未完成。


后续HX8394批次：该面板已迁入共享 `waveshare_p4_mipi_display.cpp`／`waveshare_p4_mipi_board.h`，原重复显示及板级文件在新批before快照保留后移除；本记录描述原批次，不证明共享P4目标已编译。参数和vendor字节的静态比较、最新回归及范围见[公共MIPI与5寸记录](waveshare_p4_hx8394_adaptation_20261112.md)。
