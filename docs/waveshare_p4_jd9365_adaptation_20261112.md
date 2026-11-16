# 微雪P4／P4X JD9365四尺寸源码候选（2026-11-12）

新增3.4C、4C、8寸、10.1寸各P4／P4X两项，共八个源码入口。共享MIPI实现按尺寸选择独立时序、背光极性和命令表。**八项目标均未编译或实机验收，不是可直接刷写的发布固件。**

## 固定配置与差异

采用小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的原板config、InitializeLCD和lcd_init_cmds四个分支。全部使用双通道各1500Mbps、RGB565、RST27低有效；数据不同的表分别保存，没有按驱动名合并。

| 尺寸 | 分辨率 | DPI | H同步／后肩／前肩 | V同步／后肩／前肩 | BL26 | 命令数 |
| --- | --- | --- | --- | --- | --- | --- |
| 3.4C | 800×800 | 46MHz | 20／20／40 | 4／12／24 | 低有效 | 197 |
| 4C | 720×720 | 46MHz | 20／20／40 | 4／12／24 | 低有效 | 197 |
| 8 | 800×1280 | 52MHz | 20／20／40 | 4／10／30 | 高有效 | 174 |
| 10.1 | 800×1280 | 52MHz | 20／20／40 | 4／10／30 | 高有效 | 200 |

精确驱动为 `espressif/esp_lcd_jd9365 2.0.2`，提交 `bd85f52467353dae3bd5f551054795287b0042a4`，声明IDF≥5.4、P4和cmake_utilities依赖。vendor_config使用init_cmds／size和dsi_bus／dpi_config／lane_num=2；不套用ST7703的flags字段。驱动有mirror命令，沿用false／false；继承的DPI交换轴操作仍由LVGL软件旋转绕开，PPA关闭。

四表转为C++静态字节数组，避免临时compound literal的生命周期和C++兼容问题；保留原MIT许可。`convert_commands.py`只提取四个明确预处理分支，记录命令／字节／长度／延时；`close_batch.py`再从原表和生成头文件独立解析，逐项核对，并确认四表互不相同。此为静态核对，不是目标编译。

每块一份PSRAM帧：3.4C为1,280,000字节、4C为1,036,800字节、8／10.1为2,048,000字节。两份20行绘制＋一份旋转缓冲分别96,000／86,400／96,000字节，实际带宽及剩余堆待目标测量。

3.4C／4C先采用中央460×460内容区，全部主体文字、卡片、按钮和点云按此配置；在最小720圆形遮罩内仍有几何余量。round标志在这里表示保守UI选择，**不构成物理屏幕／外框形状确认**。厂家com商品／英文Wiki返回403，net两款Wiki正文为空，8／10.1链接404，未据此猜容量或外形。具体外框、坐标、文本、配对和路线等仍需实物验收；大屏沿既有矩形布局，视觉密度尚未验收。

## 共用能力与保留边界

18个MIPI入口共用GT911 I2C1 SDA7／SCL8、0x5D／0x14探测、错误／多点取消及松手门控、BOOT35；显示初始化前关闭背光与PA。ES7210 TDM4 MIC1 slot0／ES8311 DAC24kHz复用I2S0 MCLK13／BCLK12／WS10／DIN11／DOUT9；PA53保留输入读回，数字静音检查成功后再启用。软件AEC、mn7、C6 Hosted2.12.11／Remote1.6.4 SDIO slot1及绑定、WS媒体、OTA沿现有路径。

八项应用身份、旧P4最低1.0／P4X最低3.0独立；候选使用16MB根分区及NVS保留策略，实际板卡容量／库／芯片修订还需核实。相机、SD缓存、蓝牙、电池和原厂手势未实现。

原132个profile对象、五种旧面板参数、两vendor表及许可不变；研究清单只拆出本批八项，不抹掉其他组的来源。control_protocol人工决策、主入口、OTA、配网、唤醒、board_power、双codec及audio_io八文件摘要未变。

## 实际执行结果

重新对18个MIPI入口执行完整只读预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile <板型> --destination I:/xy_board_builds_20261109/jd9365_review_<板型>_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

18项均退出1、未创建stage；八项JD9365首缺精确2.0.2，其余首缺各自面板驱动。GT9111.2.1／Hosted／Remote与完整传递依赖仍待准备。归档24份驱动、原板、SDK参考，下载包仅供读取，没有安装或加入override。

已授权的两项旧S3缓存回归实际退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

每项211个main文件、219输入摘要匹配最终源码，23产物归档；立创app2,263,824字节、RLCD2,228,000字节，各3MB分区；model均2,203,819字节／4MB。`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0，核对身份／16MB／分区／codec及FT或TE分支。使用既有IDF5.5.6-dirty、17个override、offline_guard及每stage并发度2。**未选中P4显示源，因此不证明新driver、JD命令表、mn7或Hosted的P4编译。**

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0；`close_batch.py`核对命令表、来源、保护文件、旧profile、布局几何、输入快照与产物；`git diff --check`退出0。完整命令／结果／摘要见 `evidence/waveshare-p4-jd9365-source-20261112/`。

当前139个官方源码入口＋1历史，共140本地profile，49官方变体待源码；P4族22候选／5待实现。累计64个官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（69有源码）。188项本轮实机全部not_run，只有微雪1.8实物。未运行测试套件、安装依赖／SDK、启动Docker、写Git、刷机、OTA、上传或发布。
