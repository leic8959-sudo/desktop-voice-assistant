# 微雪P4／P4X 5寸 HX8394源码候选（2026-11-12）

本批新增 `waveshare-esp32-p4-wifi6-touch-lcd-5`／`waveshare-esp32-p4x-wifi6-touch-lcd-5` 两个源码入口，并将7B、7寸ILI9881C、4.3寸ST7701的重复MIPI流程合并为 `waveshare_p4_mipi_display.cpp`。**新旧P4显示分支均未目标编译或实机验收，旧S3回归不能代替这两项证明。**

## 新增5寸设置及复用范围

基线为小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，实际发行清单154目录／188变体。按固定原板5寸类接入HX8394默认初始化表，单独保留其时序与极性：

| 面板 | 分辨率 | DSI双通道速率／DPI | H同步／后肩／前肩 | V同步／后肩／前肩 | 复位／背光 |
| --- | --- | --- | --- | --- | --- |
| 新5寸HX8394 | 720×1280 | 700Mbps／58MHz | 20／20／40 | 4／10／24 | RST27高有效，BL26高有效 |
| 既有7B EK79007 | 1024×600 | 900Mbps／52MHz | 10／160／160 | 1／23／12 | RST33低有效，BL32低有效 |
| 既有7寸ILI9881C | 720×1280 | 1000Mbps／80MHz | 50／239／33 | 30／20／2 | RST27低有效，BL26高有效 |
| 既有4.3寸ST7701 | 480×800 | 500Mbps／30MHz | 12／42／42 | 8／2／60 | RST27低有效，BL26低有效 |

三个既有面板的尺寸、时序、极性、复位与背光管脚已和本批前源码字面值逐项比较；ST7701的39条、ILI9881C的196条命令及许可保持原字节。比较记录为 `panel-equivalence.json`，只是静态对应关系，不是屏幕刷新或驱动运行验收。

所有四种面板使用同一份GT911已知地址探测、I2C1 SDA7／SCL8、单点范围检查、错误／多点取消、松手门控、LVGL20行PSRAM缓冲和亮度控制。选择面板的配置在 `waveshare_p4_mipi_board.h`，编译时要求恰好一个已实现面板；驱动创建和vendor字段仍按实际驱动分别赋值。旧六个重复的显示／板级文件已合并移除，原文件完整保存在本批 `before/main`。

5寸需要一份1,843,200字节PSRAM帧，以及两份20行绘制和一份软件旋转缓冲，共86,400字节。公共路径显式关闭PPA，初始化前按各板极性关闭背光、PA53；新增pad准备调用发生在UI内存分配前。7B、ILI、ST维持其既有无镜像命令；HX8394精确驱动没有mirror操作，因此只对它跳过该调用，依赖厂商默认初始化方向，实际方向仍待验收。

声音复用现有ES7210 TDM4 MIC1／slot0与ES8311 DAC的24kHz共享时钟、PCM软件AEC及PA53检查路径；I2S0 MCLK13／BCLK12／WS10／DIN11／DOUT9。BOOT35仍先松手后按住说话／操作卡。C6 Hosted SDIO、HTTP绑定、WebSocket音频与媒体、mn7唤醒及OTA保持现有路径。新应用名 `assistant_ws_p45`／`assistant_ws_p4x5`，16MB Flash；旧P4最低修订1.0与P4X最低3.0使用独立镜像／库选择。

## 精确驱动与I2C边界

固定小智对HX8394使用通配版本。本批选择并读取 `waveshare/esp_lcd_hx8394 2.1.0` 官方源码包，对应提交 `61fab435ced80f45895ffa59cbdb94785ef7bc03`；它的独立运行兼容路径会创建I2C1并向0x45发送未检查的初始化写入，不能与本项目已创建的共享总线并行初始化。

厂家5寸BSP `1.0.4`（提交 `9c772a150234f47725eec7398b55ce9960c1635d`）明确选择 `ESP_LCD_HX8394_SKIP_I2C_INIT`，BSP自身管理I2C、LDO3和面板复位，不重复那段0x45写入。本候选按该BSP边界设置 `CONFIG_ESP_LCD_HX8394_SKIP_I2C_INIT=y` 并增加CMake约束；没有照搬BSP的整个音频／USB／相机流程。跳过段落的依据是匹配厂家BSP源码，仍须验证对应硬件修订，不宣称所有HX8394模块都无需0x45序列。

本地既有LVGL9.6和LVGL port继续使用，未加入厂家BSP要求的LVGL9.5／adapter。精确驱动、BSP及旧板／SDK共36份参考的来源和SHA256在 `source-artifacts.json`。这些源码包仅放evidence供读取，没有安装、变成managed_components或加入目标override；仍缺驱动及其 `i2c_bus` 等完整传递依赖。

## 实际执行和限制

本批重新对8个共享MIPI入口实际执行只读准备检查，完整参数、退出码和日志见 `preflight-results.json`。命令格式为：

```text
python -B tools/prepare_cached_firmware_build.py --profile <对应板型> --destination I:/xy_board_builds_20261109/hx8394_review_<对应板型>_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

两项新5寸均退出1，缺 `waveshare/esp_lcd_hx8394`（精确2.1.0）；其余六项分别首缺EK79007、ILI9881C、ST7701。8个目标目录均未创建，没有P4编译、链接或新应用镜像。GT911、Hosted、Remote及完整传递依赖还需由牛牛准备。

最终已授权旧板缓存回归命令均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app为2,263,824字节、RLCD为2,228,000字节，各3MB分区；model均2,203,819字节／4MB。各210个main文件集合、218项输入摘要匹配最终工作区，23份产物归档。`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0，确认身份、16MB／双OTA／NVS／model、codec及FT或TE分支。

这两次使用既有IDF5.5.6-dirty、17个override、offline_guard和并发度2，没有安装依赖。缓存同步仅在两个明确的任务stage中移除已归档的六个旧文件；编译定义／选中源码排除新的P4公共显示实现。**共用P4 runtime的合并和HX8394字段仍未被目标编译覆盖。**

`git diff --check`退出0；控制协议人工决策、OTA、配网、唤醒和旧SPI显示保持原摘要；板级电源只新增P4 MIPI的提前pad准备。未运行测试套件、安装依赖／SDK、运行Docker、写Git、刷机、OTA、上传或发布。

当前129个官方变体有源码入口，另1个历史入口，共130个本地profile；59项待源码。P4族27项中12项源码候选、15项待实现。累计64个官方变体曾缓存构建通过，当前2 passed／62 stale／6历史failed／118 not_run，其中59项有源码；全部188项本轮实机not_run。相机、SD缓存、蓝牙、电池和原厂专属能力仍未完成，唯一现有微雪1.8不能替代P4验收。

全部来源、前后快照、精确参数比较、命令日志、构建产物和本批diff保存在 `evidence/waveshare-p4-hx8394-source-20261112/`。
