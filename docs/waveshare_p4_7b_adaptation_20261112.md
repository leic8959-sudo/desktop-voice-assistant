# 微雪P4／P4X WiFi6 Touch LCD 7B源码适配（2026-11-12）

新增两个独立源码候选：`waveshare-esp32-p4-wifi6-touch-lcd-7b`／`waveshare-esp32-p4x-wifi6-touch-lcd-7b`。本批接入7B专用MIPI显示、GT911、双编解码器及既有联网／协议路径。**两新目标缺EK79007依赖，尚未目标编译、链接或实机验收。**旧S3回归不能证明P4专用分支可运行。

## 固定硬件与实现

上游为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`；实际发行范围154目录／188变体。7B与功能评估板虽然同为1024×600，背光、复位、方向和音频硬件不同，使用独立显示封装 `waveshare_p4_7b_display.cpp`。

| 项目 | 本批源码配置 |
| --- | --- |
| 显示 | EK79007、1024×600、RGB565、无镜像／交换轴 |
| MIPI | DSI0、双900Mbps通道，PHY LDO3为2500mV |
| DPI | 52MHz；H同步10／后肩160／前肩160；V同步1／后肩23／前肩12 |
| 管脚 | LCD复位33，背光32低电平点亮，PWM5kHz／10bit |
| 缓冲 | 一份1,228,800字节PSRAM帧；两份20行绘制缓冲及一份20行旋转缓冲，共122,880字节 |
| 触摸 | GT911，仅探测0x5D和0x14；I2C1 SDA7／SCL8、400kHz，无独立RST／IRQ，坐标不镜像／换轴 |
| 音频 | ES7210输入＋ES8311 DAC输出；I2S0，MCLK13／BCLK12／WS10／DIN11／DOUT9，24kHz |
| 功放／按键 | PA53高电平开启且保留输入读回；BOOT35先松手后启用，复用说话与卡片操作 |

双编解码器复用现有 `waveshare_spi_audio.cpp`：ES7210配置TDM4，公共单声道管道选择MIC1／slot0，并使用PCM软件AEC。固定原板的硬件参考配置不代表本批已经使用参考通道。输出共享时钟初始使用匹配64 clocks/frame的槽设置；实际时钟／采样率、MIC接线与声学效果仍须实物验收。

7B将ES8311对象的PA引脚设置为NC，由板级GPIO53及公共静音路径负责开启。原因是精确codec GPIO接口会用OUTPUT模式重配PA，关闭输入读回；本批保持GPIO53为INPUT_OUTPUT，在DAC数字静音调用成功后才通过公共路径开启PA并读回引脚。其他板保留既有行为，未改动已安装codec源码。

背光先预装高电平关闭状态，再初始化PWM／LDO／屏幕；所有返回值检查失败都尝试保持暗屏和PA关闭，部分对象保留到重启以保护在途回调。精确EK79007／DPI驱动没有通用 `disp_on_off` 或 `swap_xy` 操作，因此不调用不存在的显示开关；初始化检查后用背光实现屏幕熄灭。LVGL配置使用软件旋转路径避免其调用不支持的swap API，并显式关闭PPA，使用有界的额外20行缓冲，避免PPA分配断言路径。未对用户提供新的旋转设置。

GT911是必需输入：读错误、越界或多触点会取消当前按压并等待松手，启动也先等待松手；单板明确保留5触点上限，读取最多2点以识别多触点。没有全总线扫描或“触摸缺失仍算成功”的行为。坐标、松手、错误恢复和卡片点击仍无物理证据。

主启动仅新增7B显示失败条件分支，失败会记录背光／PA结果并返回，不进入通用显示断言重启流程。控制协议中的人工决策、唤醒词、网络配网及OTA实现保持本批前摘要。

## 芯片与联网边界

旧P4入口最低1.0、使用旧修订库；P4X最低3.0，使用对应新库。应用名分别为 `assistant_ws_p47b`／`assistant_ws_p4x7b`，不互刷。官方7B配置要求32MB Flash；本项目适配分区保留200KB只读`nvsfactory`、840KB `nvs`、4MB双app、18MB `storage`和4MB `model`，让当前`esp_srmodel_init("model")`契约继续有效。官方原始32MB表使用`assets`标签，已作为来源归档，未直接套用。mn7模型包曾单独生成，不等于本板的模型运行证据。

联网精确声明Hosted2.12.11／Remote1.6.4，C6 SDIO slot1四线，采用精确组件的P4默认GPIO14–19／复位54。实际板卡走线、C6固件与协议匹配尚未验收，未刷写C6。HTTP绑定、WebSocket音频／媒体和同板OTA沿现有公共路径。没有初始化相机、SD缓存、蓝牙或电池遥测；不能把原厂这些能力从完整适配要求中删除。

35份固定原板／组件／现有SDK、LVGL、触摸、codec和32MB分区参考来源、SHA256见 `evidence/waveshare-p4-7b-source-20261112/source-artifacts.json`。参考文件只读归档，没有安装为依赖或加入构建override。

## 实际执行与结果

两个新目标真实命令均退出1：

```text
python -B tools/prepare_cached_firmware_build.py --profile waveshare-esp32-p4-wifi6-touch-lcd-7b --destination I:/xy_board_builds_20261109/ws_p47b_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
python -B tools/prepare_cached_firmware_build.py --profile waveshare-esp32-p4x-wifi6-touch-lcd-7b --destination I:/xy_board_builds_20261109/ws_p4x7b_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

均报 `Required component is not installed: espressif/esp_lcd_ek79007; nothing was staged`。精确要求EK79007 `2.0.2~1`、GT911 `1.2.1`，Hosted／Remote及其传递依赖也未准备完整；没有目标暂存目录、P4应用编译或镜像。

已授权旧板最终缓存回归均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

| 回归目标 | 镜像身份 | app字节／3MB |
| --- | --- | --- |
| 旧立创FT5x06双codec | assistant_lichuang | 2,263,824 |
| 旧RLCD4.2双codec | assistant_wsrlcd42 | 2,228,000 |

环境为既有SDK5.5.6-dirty、17个本地组件override、offline_guard和并发度2。两镜像model均2,203,819字节／4MB。各207个main文件集合与最终工作区一致，215项输入摘要匹配，23份产物归档。`esptool image_info --version 2`、`nm -S --size-sort -C`、`size -A -d`、SDK分区解码均退出0，核对旧板16MB身份、校验、双OTA／NVS／model及旧板音频、触摸或TE／帧缓冲选择；这些回归不覆盖7B的32MB分区。

最终功放隔离和7B失败返回已纳入这些回归输入；选中编译定义／源码明确排除7B专用显示文件。新7B的DSI／GT911／PA53／LDO3／P4 TDM管脚／mn7／Hosted／修订选择均未被旧S3回归编译覆盖。

此前122项SDK／直接缓存预检在profile对象、工具、配置源、组件集合／manifest和SDK基础版本摘要一致后复用，原有两项SPI3.5 manifest另与其上批最终diff对照。加入本轮两项实际检查后，当前124入口中官方123项：64直接预检通过、46首缺组件、11首缺BSP、1精确版本不符、1 SDK不符；历史入口另1通过。**这是2项新执行＋122项输入等价复用，不是本轮新跑124项或新增构建通过。**原始日志与复用证明均保留。

`git diff --check`退出0。8个保护文件（控制协议、OTA、配网、唤醒、板级电源、旧SPI显示与桥接端）保持SHA256；主启动的7B条件改动单独记录。未运行测试套件、独立类型检查、长期服务或Git写操作；未安装依赖／SDK、刷机、OTA、上传或发布。

## 当前总量

123个官方变体有源码入口，另1个历史入口，共124本地profile；65个官方变体待源码。P4族27项中6项有源码候选、21项仍待实现。累计64个官方变体曾缓存构建通过；当前2 passed、62 stale、6历史failed、118 not_run，其中53项有源码。全部188项本轮实机not_run，现有微雪1.8不能代替7B验收。

后续仍需完整依赖、P4目标编译和容量／内存验证、对应芯片修订与屏幕实物、C6连线／固件、音频时钟／PA／AEC／唤醒、触摸／亮度、联网恢复、媒体和同板OTA验收。源码、正式构建记录、日志、镜像／ELF、摘要及最终diff见 `evidence/waveshare-p4-7b-source-20261112/` 和 `cached_build_attempts_20261112_ws_p47b_*.json`。


后续HX8394批次：该面板已迁入共享 `waveshare_p4_mipi_display.cpp`／`waveshare_p4_mipi_board.h`，原重复显示及板级文件在新批before快照保留后移除；本记录描述原批次，不证明共享P4目标已编译。参数和vendor字节的静态比较、最新回归及范围见[公共MIPI与5寸记录](waveshare_p4_hx8394_adaptation_20261112.md)。
