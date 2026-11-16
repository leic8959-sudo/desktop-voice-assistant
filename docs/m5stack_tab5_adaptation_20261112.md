# M5Stack Tab5／Tab5 P4X源码候选（2026-11-12）

新增 `m5stack-tab5`、`m5stack-tab5-p4x` 两个独立源码入口，覆盖同一机型的ILI9881C、ST7121、ST7123三种面板路径、PI4IOE电源和ES8388＋ES7210音频。**Tab5目标尚未编译或实机验收，旧S3回归不能证明这些新分支通过。**

## 面板与触摸识别

基线为小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的完整11份Tab5源码，下载后按Git blob SHA1核对。显式区分以下设置，全部画面内存为720×1280 RGB565：

| 面板／触摸 | DSI双通道速率／DPI | H同步／后肩／前肩 | V同步／后肩／前肩 | 帧数／初始化 |
| --- | --- | --- | --- | --- |
| ILI9881C／GT911 0x14 | 900Mbps／60MHz | 40／140／40 | 4／20／20 | 2帧，独立195条表，driver bits16 |
| ST7121／ST触摸0x55 | 965Mbps／70MHz | 2／40／40 | 20／24／200 | 1帧，固定驱动默认表，driver bits24 |
| ST7123／ST触摸0x55 | 965Mbps／70MHz | 2／40／40 | 2／8／220 | 1帧，独立27条表，driver bits24 |

ST的driver bits24与DPI RGB565是原板类的不同配置层，未将它们混写。ST7121／ST7123的C驱动和头文件按固定提交纳入本板目录，保留Apache许可；ILI使用精确 `espressif/esp_lcd_ili9881c 1.1.0`。默认方向沿固定初始化，ST7123的mirror和display回调实际为空操作，不能把返回ESP_OK当作方向／开屏验收。熄屏使用GPIO22背光，PPA关闭，LVGL软件旋转避免调用DPI不支持的交换轴操作。

先通过PI4IOE复位LCD与触摸，再探测已知0x55地址。两次触摸固件版本读取一致且为1时选择ST7121，为3时选择ST7123。只有明确ESP_ERR_NOT_FOUND才转入GT911 0x14／ILI；超时、读失败、未知或不稳定版本停止暗屏，不按错误猜面板。所有触摸100kHz轮询，无IRQ回调，单点范围检查、多点／读错取消并等待松手；BOOT0也保留启动松手门控和卡片操作。

每帧1,843,200字节；ILI两帧3,686,400字节，ST一帧1,843,200字节。LVGL两份20行绘制＋一份软件旋转缓冲共86,400字节，实际带宽和剩余PSRAM仍待测量。大屏沿项目既有矩形布局，视觉密度／文本／配对等还未实机验收。

## 上游边界缺陷及本地修正

固定ST7123配置及驱动默认表的0xA3只有39字节却声明40，0xE8只有14字节却声明15，会越界读取。候选的显式静态C++表和C驱动默认表均将发送长度限于实际39／14字节，不猜测额外字节；原始来源完整保存在evidence。这能消除源码越界，**不能证明硬件命令本来应是39／14字节**，还需面板规格或实物验收。

精确 `esp_lcd_touch_st7123 1.0.2`（提交 `c29b0042d4011d907f78a484f12ed187e65cbeab`）会把控制器报告的触点数直接用于读取固定10点栈缓冲。候选按此版本保存局部驱动，在发起坐标读取前拒绝超过10点的值，避免长度溢出和越界访问。依赖只引用公共 `esp_lcd_touch 1.2.1`，不再同时引用注册表ST驱动，避免重复符号或安装后丢失修正。

两份板级表转为静态C++字节数组并保留MIT许可。归档脚本逐项核对命令顺序、字节、声明／应用长度和延时，明确记录上述两处差异；未将修正后的表冒称与原表完全相同。

## 电源、音频与C6

I²C1 SDA31／SCL32共用PI4IOE1 0x43、PI4IOE2 0x44、codec与触摸。使用已有寄存器位更新封装，100ms I²C超时及互斥锁，读改写后回读拥有的位；不全局reset扩展器。0x43只拥有P0内置天线选择、P1 PA、P4 LCD复位、P5触摸复位，0x44只拥有P0 WLAN电源。保留相机、USB、外部5V、充电等其他位，不照搬原板启动时全部启用这些外设的动作。

[厂家Tab5资料](https://docs.m5stack.com/en/core/Tab5)明确要求ST屏LCD_RST释放时切输入上拉，持续推挽高电平可能影响I²C。候选在未知面板探测前统一采用低电平输出100ms、LCD脚输入上拉释放、触摸脚输出高、再等100ms。背光22和PA在UI内存分配前关闭。GPIO／寄存器回读只是控制状态证据，不是光学或声学验收。

音频按实际 `Tab5AudioCodec` 使用ES8388 DAC＋ES7210 TDM4，未信任config.h中残留的ES8311宏。I2S0 MCLK30／BCLK27／WS29／DIN28／DOUT26，24kHz、MIC1 slot0；保留项目PCM软件AEC和mn7。原板类同时将ESP和DAC设master；候选按既有ES8388公共从模式使ESP单独驱动时钟，仍须验证采样／增益／AEC。数字静音实际回调成功后才开启PI4IOE PA，静音／关闭时先切PA并回读。

SDIO slot1 CMD13／CLK12／D0..3=11／10／9／8；厂家资料说明C6复位在GPIO15，**不能沿用微雪／Hosted默认54**。厂家UserDemo归档配置还明确使用RESET_ACTIVE_LOW、15和HEX 200MHz PSRAM；候选据此单独设置，并启用Hosted mempool优先PSRAM。资料称16MB Flash／32MB PSRAM，实际容量与芯片仍待验收，未仅凭“Octal”文字更改SDK的HEX模式。厂家示例不是本项目解析后的sdkconfig。

应用身份为 `assistant_tab5`／`assistant_tab5x`，旧P4最低1.0、P4X最低3.0；IDF范围5.5.6至5.6之前，采用当前公共mn7库选择、C6 Hosted2.12.11／Remote1.6.4和16MB根分区／NVS保留。HTTP绑定、WS媒体及同板OTA沿既有链路。

## 实际命令与结果

两项新Tab5实际执行：

```text
python -B tools/prepare_cached_firmware_build.py --profile <Tab5板型> --destination I:/xy_board_builds_20261109/tab5_review_<Tab5板型>_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

均退出1，首缺ILI9881C精确1.1.0，没有创建stage、解析完整依赖或P4编译。GT911／Hosted／Remote及传递依赖仍待准备。其余20个MIPI入口的上一批预检是历史记录，本批没有重新执行20次。43份源码／驱动／厂家／SDK参考均有来源摘要；注册表参考包未安装。

旧S3缓存回归实际终态均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app2,263,824字节、RLCD2,228,000字节，各3MB；model均2,203,819字节／4MB。每项224个main文件、232输入摘要匹配最终工作区，23产物归档；实际image_info、nm、size、SDK分区解码均退出0，核对身份／16MB／分区／codec和FT或TE。使用既有IDF5.5.6-dirty、17个override、offline_guard、每stage并发度2；这些构建没有编译Tab5源文件和面板驱动。

142个旧profile对象未改，本地宏条件下142项旧音频文本比较相同；该比较未展开include／SDK，不是编译。控制协议人工决策、OTA、配网、唤醒、display_ui和微雪公共MIPI两文件共七个保护摘要未变。主入口、板级电源、音频选择和CMake仅增加Tab5路径。

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0；`close_batch.py`检查来源、精确派生驱动差异、命令表、触摸边界、profile／研究保留、日志、快照和产物；`git diff --check`退出0。全部证据位于 `evidence/m5stack-tab5-source-20261112/`。

当前143官方源码入口＋1历史，共144本地profile；45官方变体待源码。P4族26候选／1待实现，剩CoreP4X的官方要求为IDF≥6.0，仍需独立SDK／库调查。累计64官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（73有源码）；188项本轮实机全not_run，仅现有微雪1.8。

相机、SD缓存、RS485、USB host、充电控制、蓝牙、电池与原厂手势未实现，不宣称整板全部功能交付。本轮未运行测试套件、安装SDK／依赖、启动Docker、写Git、刷机、OTA、上传或发布。
