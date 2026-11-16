# 太极派 STD／PDM 的固定 QSPI 路径与构建记录

后续更新：VoCat已完成首次完整缓存构建，复用已审阅QSPI流程并保留两版PCB复位差异；太极派STD/PDM共享路径回归通过，实机仍未验收。见[本批说明](vocat_qspi_native_20261112.md)。

本批推进已有 `taiji-pi-s3` 和 `taiji-pi-s3-pdm` 两个源码入口。此前两款因ST77916 2.0.2缺失，在预检阶段退出，没有编译过实际太极派显示／音频分支。此次新增板级固定360×360／RGB565／QSPI面板路径，保留原初始化表、时序和外围接口，再分别构建两个真实目标；其它ST77916板型仍按各自依赖和范围登记。

## 来源和协议

只读核对 `espressif/esp-iot-solution@91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f` 的ST77916 2.0.2 SPI/QSPI实现、公开头文件和Apache-2.0许可。实际 `gh api` 命令、Git blob和SHA256见 `evidence/taiji-qspi-native-20261112/references.json`，每份内容均按Git blob重新核对；没有把整套驱动包复制进managed_components或安装依赖。

SPI实现的Git blob为 `d52eae7ef4f0c7a1f940f6c46cc56bd94d67c263`，SHA256为 `a464c88fcfd485bd418e42cfdd099cede661936ffe81ddcb0f46b3d466cc7f67`。新增 `main/taiji_panel.cpp/.h` 仅覆盖固定太极派路径，保留原Espressif版权及Apache-2.0全文；原命令表仍保留小智MIT通知。

| 项目 | 本批保持的行为 |
| --- | --- |
| QSPI IO | SPI2 mode0／40MHz，CS10／CLK9／D0..3为11／12／13／14，32命令位／8参数位 |
| 命令与像素 | 参数为 `0x02<<24 \| command<<8`；像素为 `0x32<<24 \| 0x2c<<8`，继续借用同一个panel IO |
| 复位 | GPIO47低10ms，高后120ms；实际GPIO操作检查返回值 |
| 初始化 | 原MADCTL缓存初始00／COLMOD55前导，再逐条发送STD216或PDM184条原表，顺序、字节与延时保持，包括零tick让出 |
| Bank语义 | 按F0切换user bank，只有user bank中的MADCTL写入更新方向缓存；两表实际没有user bank MADCTL／COLMOD覆盖，结束都回到user bank |
| 绘图／方向 | 原2A／2B窗口末坐标减一、每像素2字节、双镜像、gap0、偶数刷新边界；固定画布范围检查后发送像素 |
| 完成通知 | LVGL仍登记原panel IO，保留SPI颜色传输完成通知与双DMA缓冲流程 |
| API范围 | mirror／swap／invert／on-off保留；原ST77916也不提供sleep／brightness回调，背光仍由原GPIO15 PWM负责 |

没有套用SDK普通SPI初始化、追加SLPOUT或替换成默认面板表。新路径只支持当前两款所需的RGB565、gap0、直接低有效复位；不提供通用18位、任意gap、MIPI、Waveshare面板ID探测或其它板型的默认表。现有上层仅使用已覆盖配置，没有以缩小这两款终端功能替代缺失的驱动。

## 保留的终端能力与边界

`taiji_panel_init.h`、数字音频、完整音频流水线、控制协议／人工决策、UI、配网绑定和OTA源码摘要与本批开始时一致。STD继续24kHz标准I2S采集；PDM继续24kHz PDM采集；二者输出仍为默认BOTH两个32位时隙，GPIO48静音逻辑未改。没有把24kHz配置称作麦克风时钟或声学性能已验收。

原CST816S I2C1／RST40／IRQ41、坐标 `359-x,359-y`、触摸按住说话和卡片入口保持。既有CST816S1.1.2单点缓存及事件位遗漏未由此次面板实现修复；多点／首次按住／错误恢复、圆屏边界、真实方向仍需单独验收。

只有现有微雪1.8实物。两款太极派批次号、屏幕表／颜色／波形、Flash／PSRAM、电源、PDM／I2S时钟和时隙、静音、AEC／完整唤醒／自然插话、媒体、配对与同板双OTA均未实机验收；电池／低功耗／其它菜单组合的既有范围也保留。

## 实际执行

证据目录为 `evidence/taiji-qspi-native-20261112/`。`std/`、`pdm/`是独立新工程，`chd28/`是已有SPI／CMake默认选择器回归。环境为现有SDK5.5.6-dirty、Xtensa14.2.0、17份现有本地override、Python联网保护，各2jobs；没有安装SDK或组件。

构建脚本分别在prepare和IDF参数中指定真实profile，核对 `.assistant-board`；禁止覆盖已有新stage。每个目录保存完整命令、cwd、退出码、配置、日志和摘要，不能用一个变体的结果证明另一个。

实际执行：

```text
python -B evidence/taiji-qspi-native-20261112/std/build_cached.py
python -B evidence/taiji-qspi-native-20261112/pdm/build_cached.py
python -B evidence/taiji-qspi-native-20261112/chd28/build_cached.py
python -B evidence/taiji-qspi-native-20261112/chd28/collect_build.py
python -B evidence/taiji-qspi-native-20261112/std/collect_build.py
python -B evidence/taiji-qspi-native-20261112/pdm/collect_build.py
```

CHD2.8回归退出0，应用2266816字节、模型2203819字节，280项当前输入及24份产物归档；它不编译太极派QSPI分支，不向太极派继承显示／音频结果。

| 真实IDF目标（均带 `-DIDF_TARGET=esp32s3`） | 应用字节 | 模型字节 | 结果 |
| --- | ---: | ---: | --- |
| `-DASSISTANT_BOARD=taiji-pi-s3 build` | 2235664 | 2203819 | 一次退出0，3MiB应用槽剩910064字节 |
| `-DASSISTANT_BOARD=taiji-pi-s3-pdm build` | 2237712 | 2203819 | 一次退出0，3MiB应用槽剩908016字节 |
| `-DASSISTANT_BOARD=chd-esp32s3-box reconfigure build` | 2266816 | 2203819 | 退出0 |

只读直接依赖报告已实际执行：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/firmware_dependency_report.py --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --output evidence/taiji-qspi-native-20261112/direct-dependencies.json
```

退出0：153本地／152官方、949条直接声明，87本地／86官方满足直接条件，缺组件声明143项。两款只移除了被本板固定实现替代的ST77916依赖；没有将其它ST77916入口登记就绪。直接条件不证明传递解析、编译、启动或实机通过。

两款实际readelf／nm／size、esptool image_info、分区解码及SDK Git只读查询均退出0。每款280项当前输入与stage一致、26份产物快照；checksum／validation hash有效，项目名分别为assistant_taijistd／assistant_taijipdm，编译宏分别确认STD和PDM身份。双3MiB OTA与4MiB模型分区保留；ELF含原控制／HTTP／WebSocket、AEC／唤醒、CST816、数字音频及新的Taiji panel入口，无外部ST77916工厂。

直接检查两款 `digital_audio.cpp.obj` 未定义调用：二者都有标准I2S输出API，只有PDM对象含 `i2s_channel_init_pdm_rx_mode`。因此确认实际音频编译分支不同，不仅是项目名不同；这不证明实际采集时钟或声音已验收。两款初始化数据保持原文，collector不执行面板／音频代码或声学测试。

三份ELF仍含四个SDK既有强制UND：`__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。真实nm结果、链接器-u参数及现有SDK来源快照保留，未修改已安装SDK或称零UND／可启动／干净发行环境。构建未出现此前的GCC内部错误，原LVGL弃用等警告保留。

最终归档命令：

```text
python -B evidence/taiji-qspi-native-20261112/inspect_sdk_limits.py
python -B evidence/taiji-qspi-native-20261112/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/taiji-qspi-native-20261112/finalize.py
git diff --check
git diff --no-index --check -- NUL <本批新增源码或许可>
```

两款矩阵依赖改为resolved_local_overrides，旧预检失败及研究状态保留在历史记录，没有删除失败事实。当前152／188官方源码入口、153本地profile含1历史、36无源码、4项明确必需功能缺口不变。3 passed_cached_sources／83 stale_cached_sources／0 failed_cached_sources／102 not_run，累计86个官方变体曾缓存构建通过。83项历史成功因当前源码摘要变化需要重建，不能算当前通过；0项failed表示现有失败项后来构建成功，不代表没有剩余阻碍。188项本轮实机均not_run，148项没有被显式关闭规则标出缺口不等于功能已完整验收。

最终audit核对修改仅在共享QSPI显示／CMake、两个新增panel文件和许可、两个profile说明／manifest及导出许可条件；其它main、表、音频／人工决定等摘要保持。导出脚本增加两份许可复制，未执行Docker或实际导出。共享root_managed_components依赖锁事后快照另存，不称所有构建写入仅限stage。未运行独立测试套件、类型检查、服务或声学／触摸操作，未安装依赖、Git写入、刷机、OTA、上传或发布。

归档脚本首次误把新文件 `git diff --no-index --check` 的差异退出码1当成失败而停止，实际只有CRLF提示、没有空白错误；初次日志保存在initial-audit。纠正归档断言后重新核对，未重跑固件。普通 `git diff --check` 退出0；新增源码和许可的no-index检查分别记录真实退出码1和无空白诊断，不把它们写成退出0。
