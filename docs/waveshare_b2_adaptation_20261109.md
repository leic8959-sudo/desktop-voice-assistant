# B2微雪1.32／2.06／2.16源码适配

后续更新：原版1.8 SH8601已切换本地驱动，复核原版复位时序并完成首次16MB整板缓存构建；1.32共享回归通过。CO5300历史/v2配置独立保留，现有微雪1.8实物修订仍待确认，未刷机或实机验收。见[本批说明](amoled18_sh8601_native_20261113.md)。

后续更新：1.32历史SH8601版已接入本地CST820轮询与单ES8311，首次8MB整板缓存构建通过并核对双OTA/model余量，2.06共享回归通过；不是后期CO5300同名版本或实机验收。见[本批说明](amoled132_native_20261113.md)。

后续更新：2.06已接入本地SH8601适配/FT5x06/双codec及共享LVGL，首次整板缓存构建通过；410x502/gap22/32MB及专用接线保留，2.16 CO5300回归通过，实机未验收。1.32与1.8SH不继承本板结果。见[本批说明](amoled206_native_20261113.md)。

后续更新：2.16已用现有CO5300/LVGL port及本地CST9217轮询接入，完成首次整板缓存构建；固定厂商路径与CST9220修订差异继续记录，C5LCD1.69共享回归通过，实机未验收。1.32/2.06不继承本结果。见[本批说明](amoled216_board_20261113.md)。

后续LCD进展见[1.54／1.83板级记录](waveshare_lcd_adaptation_20261109.md)：当前16个本地profile对应15个官方变体、173个待源码。以下保留本页对应阶段的数量和结果。

2026-11-09，Asia/Shanghai。继续全部188个官方发布变体的目标，本轮新增3个实际源码候选入口；现有14个本地profile对应13个官方变体，另含1个历史兼容入口，175个官方变体仍待源码。新三板缺少本机BSP，未编译或实机验收。牛牛目前只有现有微雪1.8，不能据此认定其具体屏控修订。

## 本轮入口及硬件边界

| profile／应用名 | 固定来源 | 硬件与实现 |
| --- | --- | --- |
| `waveshare-amoled-1.32`／`assistant_amoled132` | 厂家源码`22733e088a533378680a03e398279b2f7ab09a73`，Git依赖而非注册表版本号 | 466×466圆屏，SH8601／CST820，8MB Flash／8MB Octal PSRAM；单ES8311输入输出句柄、24kHz，GPIO18电源保持、GPIO17 PWR输入、GPIO0说话键，无SD |
| `waveshare-amoled-2.06`／`assistant_amoled206` | BSP2.0.0注册表关联提交`9daffb7168b3c093a330da700988a22092294eef` | 410×502，SH8601／FT5x06，32MB Flash／8MB PSRAM；ES7210输入／ES8311输出；保留BSP横向gap0x16，专用触摸／复位／SD引脚 |
| `waveshare-amoled-2.16`／`assistant_amoled216` | BSP2.0.1注册表关联提交`bfeb6e6d5737178cdde78b630c8118074da0a657` | 480×480，CO5300／CST9217驱动，16MB Flash／8MB PSRAM；ES7210输入／ES8311输出、LVGL adapter0.6.0；专用触摸／复位引脚 |

小智固定提交仍为`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。1.32的历史BSP1.0.0提交使用SH8601，但注册表同名1.0.0指向的提交`fce4e97136419bc264972c47b2d1b01fa8704195`已经换为CO5300。两份实际源码和manifest已读取比较，所以SH8601入口使用明确Git提交，不因版本号相同就冒称相同驱动。后续缓存准备器也拒绝将普通注册表缓存当成Git提交证据。

2.16当前厂家产品比较表列出CST9220，而本轮固定BSP／小智使用CST9217驱动；须核对购买实物的触控芯片和协议兼容性，不能用商品名证明所有修订可用。1.43C在所读厂家固定源码树没有找到对应BSP，仍保持inventory_only；不拿1.32单codec或1.75引脚替代。

## 最小实现

- `board_profile.h`选择三个专用BSP头、确切屏幕大小、单双codec关系和GPIO0；`display_layout.h`为410×502、480×480分别设置布局，1.32复用同尺寸466圆屏布局。大屏路线箭头安排在标题和详情之间。
- `board_power.cpp`在显示初始化前处理必要供电。1.32先保持GPIO18，独立PWR任务忽略开机时的按住状态，只在新一次持续2秒按下GPIO17时释放电池保持；USB仍供电时不会伪称设备已关机。2.06将ALDO1／2设3.3V并按位启用；2.16及旧1.8只设置ALDO1。电压和使能读回核对，不重设充电电流、不整字覆盖其他电源开关，也不关闭无关电源轨。
- 旧1.8的ALDO1代码收拢进同一供电入口，消除重复PMIC实现。1.32沿用单ES8311原生音频初始化并使用其WS引脚；新双codec板保留BSP初始化与现有24kHz／软件参考路径。模拟输入增益只作用于麦克风。
- BSP2.0.1的`bool bsp_display_lock()`直接返回adapter的`esp_err_t`，会把ESP_OK变成false、错误变成true。2.16入口直接调用已核对的`esp_lv_adapter_lock()`并比较ESP_OK，保留错误语义。没有修改厂家原文件的函数声明。
- QSPI补丁按明确BSP名和面板类型处理。1.32历史源码和2.06将RGB注册改为SPI完成回调路径、使用局部刷新并保留亮度IO错误；1.32另修正零值max_transfer_sz。2.16保留adapter路径，仅修正亮度错误传递。只接受已审阅的四种BSP，未知源码即停止。Docker和缓存构建入口均传入所选组件，缓存只修改隔离副本。

## 容量与恢复

登记表新增`flash_size_mb`，现有入口明确16，新三板分别8／32／16。CMake实际配置与板型容量不符就报错；IDF早期依赖预展开阶段不读取尚未生成的Kconfig值。各板默认配置显式选择所需容量，构建工具支持板型目录自己的partitions.csv。

1.32使用8MB分区：两份应用各`0x2A0000`，model为`0x240000`，保留原NVS／otadata位置，总范围到`0x800000`。这是待编译的候选布局，真实应用／模型尺寸仍要在新板依赖齐备后检查。2.06暂复用原分区地址，声明实际32MB容量；未为了用满空间额外移动模型或存储。

OTA在原3MB上限之外进一步受实际目标应用分区大小限制。烧录工具按所选容量检查完整备份、镜像设置和写入范围，继续保护NVS；8MB板不能再用16MB范围或错误备份通过检查。本轮没有运行烧录脚本或OTA。

## 已执行的命令与边界

读取工作区／Git现状、固定板源码、厂家Git树／提交历史、BSP注册表关联源码及Apache许可、厂家产品页／Wiki。1.32注册表源码与初始提交存在差异；2.06和2.16所读发行源码与前述调查提交一致。资料摘要已写入`board_research_v2.5.2.json`，没有复制完整BSP到工程或安装依赖。

实际在公开源码的临时副本运行三次：

```text
python -B tools/patch_waveshare_qspi.py <隔离目录> --panel <sh8601或co5300> --component <对应组件> --component-directory <公开源码副本>
```

三个补丁操作均退出0，证明对应已读取源码可应用补丁，不代表固件编译／屏幕运行通过。

实际执行IDF分区生成：

```text
<已有IDF Python> -B <IDF5.5.6>/components/partition_table/gen_esp32part.py --flash-size 8MB firmware/assistant_espidf/boards/waveshare-amoled-1.32/partitions.csv <临时输出.bin>
```

退出0，输出Parsing CSV／Verifying table；只证明CSV结构、对齐及容量合法，不证明应用或模型装得下。

三个新profile分别执行：

```text
<已有IDF Python> -B tools/prepare_cached_firmware_build.py --profile <对应profile> --destination <新临时目录> --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

均退出1：缺少对应BSP，未创建目标目录，未启动编译。日志在Git忽略的`evidence/waveshare-b2-20261109/`。1.32后续还需要对应Git源码提交及codec1.3.1；2.06需要SH8601驱动，2.16需要adapter0.6.0及CST9217驱动等传递依赖。依赖须由牛牛准备，本轮不下载安装。

为核对公共改动，使用此前三份现有SDK5.5.6-dirty／本地17组件override构建副本进行增量构建。两份T-Circle副本先后发现缺少组件目录及partitions.csv，失败均保留日志；从工作区既有文件补回，没有下载。微雪1.8-v2首次增量退出0，最终源码三份增量结果将在下方记录。不得把既往成功日志套成当前源码已通过。

全部实机仍待验收；只有现有微雪1.8实物，未刷写它。没有运行测试套件／独立类型检查、启动长期服务、Git写操作、上传或发布。

## 最终公共代码构建结果

最终分别在三份既有隔离目录、同SDK和离线环境执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> reconfigure build`，全部退出0。显式reconfigure清除了分区CSV缺失时生成的失败规则；修复缺失输入后仅运行build的失败日志同样保留，没有把它写成源码编译错误。

| 既有入口 | 当前应用字节 | 3MB分区剩余 | 最新镜像检查 |
| --- | --- | --- | --- |
| T-Circle原版 | 2,507,152 | 638,576（20%） | 应用名／checksum／validation hash有效 |
| T-Circle V1.1 | 2,510,128 | 635,600（20%） | 应用名／checksum／validation hash有效 |
| 微雪1.8-v2 CO5300 | 2,614,816 | 530,912（17%） | 应用名／checksum／validation hash有效 |

三次镜像检查的实际命令为`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0；未连接串口。每份最终记录比对40份源码／配置，均与工作区匹配。SDK HEAD重新只读核对为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，原NimBLE及idf_as_lib shell修改仍在，不能称干净SDK。LVGL现有弃用API告警仍保留。

当前证据是`firmware/assistant_espidf/cached_build_attempts_20261109_b2_<profile>.json`，原始失败／成功日志、镜像、ELF、模型、配置、输入快照和许可均保存在Git忽略的`evidence/waveshare-b2-20261109/<profile>/`。新三板依赖预检另记`native_dependency_preflight_20261109_b2.json`，矩阵保持build=not_run，dependency_resolution=blocked_missing_local_sources。旧阶段构建记录保留，最新矩阵仅在输入摘要匹配当前工作区时记passed_cached_sources。

当前13个官方变体有源码，其中3个当前缓存构建通过、10个尚未编译，175个尚无源码，本轮实机验收188项均未执行。新三板的独有BSP、8／32MB实际编译路径、PWR任务、触摸／显示／电源和烧录行为仍待验收；这三份旧板构建不覆盖它们。Git、上传、发布、设备和运行服务均未操作。

最终编辑审查执行`git diff --check -- firmware/assistant_espidf/main/CMakeLists.txt firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/ota_update.cpp tools/build_firmware.ps1 tools/flash_native.py tools/patch_waveshare_qspi.py`，退出0；28份相关未跟踪文件分别运行`git diff --no-index --check -- NUL <文件>`，无空白错误，仅Git的LF／CRLF提示。这些是diff审查，不是测试或类型检查。并发网关diff在本轮继续变化，本轮未编辑、覆盖或回滚它；运行时与control_protocol的并发修改同样保留。
