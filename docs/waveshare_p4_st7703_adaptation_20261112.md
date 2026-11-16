# 微雪P4／P4X 4B ST7703源码候选（2026-11-12）

本批新增 `waveshare-esp32-p4-wifi6-touch-lcd-4b` 和 `waveshare-esp32-p4x-wifi6-touch-lcd-4b` 两个源码入口。屏幕与驱动已按固定原板接入公共MIPI路径；**目标P4分支尚未编译或实机验收**。源码入口不代表可刷写的交付固件。

## 板型配置与实现

来源为小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行清单154目录／188变体。两份配置分别使用 `assistant_ws_p44b`／`assistant_ws_p4x4b` 应用身份、16MB Flash，旧P4最低修订1.0／P4X最低3.0，保留独立库选择与同板OTA边界。

| 项目 | 固定4B配置 |
| --- | --- |
| 面板／像素 | ST7703，720×720，RGB565 |
| MIPI／DPI | 双通道各480Mbps，46MHz |
| H同步／后肩／前肩 | 20／80／80 |
| V同步／后肩／前肩 | 4／12／30 |
| 复位／背光 | RST27低有效，BL26低有效；启动先熄背光 |
| 触摸 | GT911，共享I2C1 SDA7／SCL8，仅0x5D／0x14探测，无坐标变换 |
| 缓冲 | 一份1,036,800字节PSRAM帧＋三份20行缓冲共86,400字节 |

公共显示层仅增加面板参数、driver include和创建分支；CMake补精确组件要求，board_profile补独立身份。原四个面板参数、两份vendor命令表和许可逐字节保留，130个旧profile对象不变。研究清单从原混合组中只拆出4B两项，保留10.1、3.4C、4C和8寸等调查元数据。

精确驱动为 `waveshare/esp_lcd_st7703 2.0.0`，源码提交 `9daffb7168b3c093a330da700988a22092294eef`。使用默认初始化表；`mipi_config`只有dsi_bus和dpi_config，没有lane_num；flags.use_mipi_interface设true。该驱动有mirror(false,false)实现，swap_xy返回不支持；公共LVGL软件旋转与PPA关闭避免调用后者。驱动描述中的7inch不能覆盖固定4B的720×720设置，实际面板修订仍待验收。未发现HX8394式重复I2C1及0x45初始化段。

共享路径继续负责GT911单点范围检查、读错／多点取消和松手门控、BOOT35操作、UI内存分配前关闭PA／背光。音频为ES7210 TDM4 MIC1 slot0与ES8311 DAC 24kHz，I2S0 MCLK13／BCLK12／WS10／DIN11／DOUT9；PA53 INPUT_OUTPUT读回，codec PA设NC，数字静音检查成功后才启PA。mn7唤醒、软件AEC、C6 Hosted SDIO、HTTP绑定、WebSocket媒体和OTA均复用既有路径，尚无P4运行证据。

## 实际命令与结果

本批对全部10个共享MIPI profile重新实际执行下列只读检查，逐项完整命令、退出码与日志摘要见 `evidence/waveshare-p4-st7703-source-20261112/preflight-results.json`：

```text
python -B tools/prepare_cached_firmware_build.py --profile <板型> --destination I:/xy_board_builds_20261109/st7703_review_<板型>_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

10项均退出1并且未创建stage。两项4B首缺ST7703精确2.0.0；其余首缺各自EK79007、ILI9881C、ST7701或HX8394。GT9111.2.1、Hosted2.12.11、Remote1.6.4与完整传递依赖仍待准备。20份精确驱动／固定原板／共享SDK参考已归档和核对SHA256；参考包未安装或加入override。

此前授权的旧板缓存回归已实际完成，终态退出码均0，包装命令及会话证据见 `build-command-results.json`：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app为2,263,824字节，RLCD为2,228,000字节，各3MB分区；model均2,203,819字节／4MB。每项210个main文件集合与218项输入摘要匹配本批最终源码，各23份产物归档。实际 `esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d` 和SDK分区解码均退出0，核对应用身份、16MB、NVS／双OTA／model、codec及FT或TE符号。使用既有IDF5.5.6-dirty、17个override、offline_guard和每stage并发度2；未安装依赖。

这两次S3构建未选择P4 MIPI源文件，不能证明ST7703／GT911／mn7／Hosted库能在P4编译。缓存构建也不是干净依赖发布构建。

使用本机既有Python执行 `tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory` 退出0，清单保持154目录／188变体。`close_batch.py`核对本批源码／快照／来源／日志／产物／计数及保护文件，`git diff --check`退出0；完整结果见本批 `final-audit.json`。

## 当前进度与未解决项

当前131个官方变体有源码入口，另1历史入口，共132本地profile；57项待源码。P4族27项中14项源码候选、13项待实现。累计64个官方变体曾缓存构建通过，当前2 passed／62 stale／6历史failed／118 not_run，其中61项有源码。全部188项本轮实机not_run，唯一现有微雪1.8不能代替4B验收。

4B仍需验证板／屏修订、默认初始化、时序／带宽、复位与背光极性、GT911边角和多点、PSRAM与唤醒模型、麦克风槽位／采样／静音／AEC、C6固件和SDIO、配网绑定、WS媒体及同板OTA。相机、SD缓存、蓝牙、电池与原厂手势未实现。未运行测试套件、安装SDK／依赖、启动Docker、写Git、刷机、OTA、上传或发布。
