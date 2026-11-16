# 四款默认 GC9A01 屏幕入口的构建接入

本批推进四个已有源码入口：Spotpear 1.28 BOX、Movecall Moji、Movecall 璀璨、Electron。它们使用 GC9A01 2.0.4 默认初始化，改为复用现有 SDK 公共 DCS 操作并保留固定命令序列。硬件型号仍记录为 GC9A01，没有将调用工厂名作为型号依据；源码入口数量不变。

## 固定来源与行为

公开 esp-bsp 固定提交 `618b05297dae1aa3fab986d5ca7b89b328e1919a` 的 `components/lcd/esp_lcd_gc9a01/esp_lcd_gc9a01.c` 已经通过 GitHub blob 和 SHA256 核对，与此前归档的 2.0.4 源码逐字一致。只读下载单份公开源码到 evidence 作对照，没有安装组件或下载其二进制包。

原默认表的42条命令、数据、顺序和零长度参数完整转为 `main/gc9a01_default_commands.h`，包括 FE／EF 的零长度。Apache-2.0版权标识和许可全文保留。初始化回调继续执行 SLPOUT／100ms、BGR MADCTL=0x08、RGB565 COLMOD=0x55，再发送42条命令并逐条保留零tick让出。SDK默认ST7789初始化不执行，避免混入其RAMCTRL写入。

SDK构造器的BGR／RGB565状态与默认表一致，编译期检查默认表不覆盖MADCTL／COLMOD且载荷不越界。复用相同的硬件复位10ms／10ms或软件复位20ms、gap、镜像、换轴、反色、开关、0x2a／0x2b窗口与0x2c像素传输，继续检查并传播color TX错误，与原GC9A01驱动一致。原GC9A01工厂没有sleep／brightness回调，因此新工厂对应回调置空，背光仍由板级路径负责。

| 入口 | 保留的差异 |
| --- | --- |
| Spotpear 1.28 BOX | RTC3供电、专属62／63时序和C3／C4电压后置调参、反向背光及可选CST816D触摸路径保留 |
| Movecall Moji | 独立SPI／音频引脚和正向背光，默认16MB分区 |
| Movecall 璀璨 | 独立SPI／音频引脚，保留8MB双OTA／模型分区，不继承16MB尾地址 |
| Electron | 原mirrorY／圆屏对齐、数字音频和已有运动owner行为保留，本批不新增电机动作或复位 |

AtomS3、AtomS3R两款底座和Magiclick 2.5需要GC9107专用表或硬件识别，继续保留外部GC9A01依赖与原分支。没有把它们套用默认表，也没有改变未知屏幕ID时停止初始化的行为。

修改仅限共享显示文件的上述默认分支、CMake与四板manifest依赖、四个profile的实现说明，新增命令表和许可。音频、完整唤醒、自然插话／媒体人工决策、绑定、配网、凭据保存和OTA代码保持原文。

## 构建与来源核对

证据在 `evidence/gc9a01-default-sdk-20261112/`，四目标分别使用 `moji/`、`cuican/`、`spotpear/`、`electron/` 保存真实命令／日志／源码和产物；另以 `freenove/` 保存共享显示默认分支回归。每个工程以现有SDK5.5.6-dirty、Xtensa14.2.0、本地组件override和Python外网保护构建，设置 `IDF_PY_BUILD_JOBS=2`，未安装依赖。

实际执行入口为各目标目录内的 `build_cached.py` 和 `collect_build.py`，以及本批 `inspect_sdk_limits.py`、`finalize.py`。板型参数、工作目录和完整命令数组保存在每项build.json，不把一板构建用作另一板证明。

所有目标实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应入口> build`，最终退出码均为0：

| 入口 | 应用字节 | 模型字节 | 实际应用槽 |
| --- | ---: | ---: | --- |
| movecall-moji-esp32s3 | 2247968 | 2203819 | 双3MiB |
| movecall-cuican-esp32s3 | 2088176 | 2203819 | 双2688KiB，完整布局位于8MB内 |
| spotpear-sp-esp32-s3-1.28-box | 2259072 | 2203819 | 双3MiB |
| electron-bot | 2209408 | 2203819 | 双3MiB |
| freenove-esp32s3-display-2.8-lcd（回归） | 2258560 | 2203819 | 双3MiB |

每板271项当前源码／配置摘要、24份产物快照。实际readelf／nm／size、esptool和分区解码命令全部退出0，项目身份、checksum／validation hash、应用／模型容量核对通过。璀璨应用槽剩664336字节，模型位于2304KiB分区内；没有使用16MB尾地址。其余板各自分区来源与stage逐字核对。四新板ELF保留GC9A01默认回调、各自音频／AEC／唤醒和HTTP／WS路径，未链接外部GC9A01工厂。

旧的三项cached失败（缺依赖）和Electron未构建记录保留，新建独立通过记录。Freenove回归覆盖共享显示文件的既有ILI9341分支，应用大小保持2258560字节；这不代替另外四个目标的独立构建。

直接依赖报告另执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/gc9a01-default-sdk-20261112/direct-dependencies.json
```

结果为153本地／152官方、963条声明，74本地／73官方满足当前直接条件，缺组件声明157项。其余4个GC9A01缺项仍为AtomS3/S3R和Magiclick专用路径，未消除阻塞；此报告不等于目标编译或实机通过。

归档实际执行：

```text
python -B evidence/gc9a01-default-sdk-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/gc9a01-default-sdk-20261112/finalize.py
```

五板的实际nm均有四个SDK既有强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`；链接参数、SDK源码、实际配置和日志分别保存。没有修改SDK或宣称零UND。现有LVGL弃用等警告继续保留，不扩大到无关UI修改。

## 验收边界

只有现有微雪1.8实物可用。四板实际屏幕修订、42条默认表、色序／gamma／反色／圆边／窗口、GPIO和电源、可选触摸、音频／AEC／完整唤醒／自然插话、媒体、提醒与同板OTA仍需实机验收。Electron已有运动代码也未因本次显示改动获得运行验收。

SDK既有强制UND与警告继续分别记录，不把缓存构建退出0视为可启动、零UND或干净发布环境。IDF可能更新安装目录下root_managed_components的共享缓存锁元数据，本批保存事后快照，不宣称全部文件写入仅限stage。

未运行独立测试套件、类型检查、服务或声学验证；没有Git写入、安装、刷机、OTA、上传或发布。

当前152／188官方源码入口、153本地profile、36项待源码。构建账本为5 passed_cached_sources／68 stale_cached_sources／3 failed_cached_sources／112 not_run；当前通过为四个本批目标及Freenove回归，累计73个官方变体曾缓存构建通过。四个本批目标当前依赖记录为resolved_local_overrides，历史阻塞保留。188项本轮实机均not_run。

证据描述修正：完整复核2.0.4源码确认原GC9A01的draw_bitmap已用ESP_RETURN_ON_ERROR检查color TX；本页早先将这项说成新增错误传播不准确。实现和已有构建记录未因此改变，后续GC9107批次补充了此对照。
