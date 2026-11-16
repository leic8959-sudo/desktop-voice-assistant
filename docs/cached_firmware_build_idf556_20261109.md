# 已安装IDF5.5.6缓存构建：单入口编译结果

后续更新：缓存PowerShell包装器已用于两款[T-Circle独立构建](tcircle_adaptation_20261109.md)并退出0，32kHz PDM／FIR分支也已有该板的编译证据；本页以下保留历史CO5300入口当时的执行记录。

2026-11-09，Asia/Shanghai。全量目标继续推进，在没有安装依赖或启动Docker的前提下，对**历史CO5300兼容入口 `waveshare-amoled-1.8`**进行了独立本机构建。首次完整命令退出0，完成应用编译、链接、镜像生成及应用分区尺寸检查。该结果只属于本次本地输入快照，**不等于8个官方变体均编译通过、干净环境安装通过、设备可刷／可用或实机验收完成**。

## 工具链和输入的实际身份

- SDK为已存在的 `C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6`，`idf.py --version`返回 **ESP-IDF v5.5.6-dirty**，Git HEAD为 `880a203ae603d08a94d06aa01e0d6761958e5e9e`。只读status列出NimBLE子模块的既有改动及examples/build_system/cmake/idf_as_lib下的示例shell文件改动；本轮不覆盖／清理它们，也不把该SDK称为纯净上游版本。
- 编译器 `esp-14.2.0_20260121`、CMake3.30.2、Ninja1.12.1、ROM ELF工具20260528均为该SDK tools.json推荐且已安装的版本；沿用既有IDF Python3.12.9环境。没有执行SDK安装器、pip安装、拉取镜像或下载子模块。
- 上一轮5.5.2缺SPIFFS子模块的[失败记录](cached_firmware_build_20261109.md)继续保留。本轮用完整已安装5.5.6独立目录，没有向5.5.2复制其他版本子模块，也没有以5.5.6结果冒充5.5.2通过。
- 隔离目录为 `C:\Users\<user>\AppData\Local\Temp\xy_idf556_bd2c09a60bde`。由 `prepare_cached_firmware_build.py`复制固件源文件和现有17个组件快照，manifest使用本地override，没有改写原SDK、原managed_components或原工作区build产物。
- Python审计守卫只允许本机loopback连接／名称，拒绝外部连接及DNS；未连接在用网关、设备、模型或账号。组件快照不是新安装，也不是完整干净安装／可公开发行的传递依赖锁。

## 实际命令和结果

子进程环境显式设置SDK路径、工具根、IDF Python环境、含现有GCC／CMake／Ninja的PATH、`PYTHONDONTWRITEBYTECODE=1`、指向隔离网络守卫的PYTHONPATH及 `ESP_IDF_VERSION=5.5.6`。命令在隔离目录执行，两个-D参数作为完整字符串传入：

```text
<已安装IDF Python> -B C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6\tools\idf.py '-DIDF_TARGET=esp32s3' '-DASSISTANT_BOARD=waveshare-amoled-1.8' build
```

完整命令退出0，应用名 `assistant_espidf`，源码版本0.3.21，目标esp32s3。2210步构建及bootloader子构建完成，实际生成：

| 产物 | 实际大小／检查 |
| --- | --- |
| assistant_espidf.bin | 2,614,640字节，`0x27e570` |
| 最小应用分区 | `0x300000`，剩余 `0x81a90`＝531,088字节，日志显示17%余量 |
| assistant_espidf.elf | 23,603,076字节，调试ELF，不是可直接OTA的镜像 |
| srmodels/srmodels.bin | 2,203,819字节，须和本次分区／模型配置对应 |
| bootloader.bin | 日志报告 `0x5060`，bootloader空间检查通过 |

首次构建配置阶段有 `ESP_ROM_ELF_DIR`缺失的调试符号生成错误，被SDK按告警继续处理；应用构建最终确实退出0。还有LVGL API弃用告警，本轮未为消除告警做无关的全量API迁移。不能因为最终退出0就抹去这些日志。

该ROM ELF目录实际已安装，后续将其显式设置为 `C:\Espressif\official-5.5.6\tools\esp-rom-elfs\20260528`，在同一隔离目录执行 `reconfigure build`，再次退出0；重跑日志不再出现ROM环境变量缺失错误，应用和bootloader尺寸检查再次通过。两个实际结果及原始日志摘要分别记录在 `cached_build_attempts_20261109_idf556.json`，未覆盖初次告警日志。

另实际执行只读镜像检查 `python -B -m esptool --chip esp32s3 image_info --version 2 <隔离目录的assistant_espidf.bin>`：退出0，镜像内应用名assistant_espidf／版本0.3.21／SDK v5.5.6-dirty与本次身份一致，ESP32-S3、16MB DIO80MHz，checksum及内嵌validation hash均为valid。该检查未连接串口，不是刷机或设备启动验证。

## 可复用的本机构建入口

新增 `tools/build_cached_firmware.ps1`，参数显式要求SDK路径、工具根及现有Python环境，先从板型登记表和SDK工具清单读取本次匹配版本，检查必要文件，不存在就停止。新建临时目录、调用缓存准备器，设置网络守卫、ROM ELF和子进程环境后运行构建，最后恢复调用进程环境；不调用烧录或发布。

`prepare_cached_firmware_build.py`增加可选 `--idf-path`，在复制前核对IDF入口、版本和SPIFFS／MbedTLS／lwIP等已确认前置文件，避免再次把不完整5.5.2当成可用环境。这里是必需路径预检，不是完整SDK完整性审计。

**新PowerShell包装入口只完成源码审查，尚未通过该入口执行构建**。本次实际执行的是上面的显式原生命令；不能用它的成功推断包装入口各失败分支／环境恢复已验证。后续可在对应已安装环境使用：

```powershell
powershell -File tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board waveshare-amoled-1.8
```

该示例仅用于历史CO5300缓存入口，不推荐陌生用户据此购买任意名为“1.8”的板。新板的BSP和特定版本未在本机缓存时仍需牛牛准备，脚本不会下载安装，也不会把缓存的CO5300组件代替SH8601。

## 证据与尚未完成

可审阅记录为 `firmware/assistant_espidf/cached_build_attempts_20261109_idf556.json`，含SDK身份／dirty边界、30份输入源文件／配置摘要、两次退出0、镜像只读检查、镜像及日志摘要；收尾核对这30份固件文件与工作区匹配。原始日志、输入快照及8个选定构建产物／配置保存在被Git忽略的 `evidence/cached-firmware-build-idf556-20261109/`；没有复制到网关自动OTA下载位置，也未改变设备固件。构建包装脚本的后续新增不在固件快照摘要范围中，不能据固件构建推断包装脚本已执行通过。

只有当前历史兼容入口被构建，官方8个变体矩阵的各自build／device_acceptance状态不因该结果自动改成通过。32kHz FIR模板没有当前板型实例化，不能声称32kHz／PDM分支已经编译或声学验证。T-Circle、其他180个未实现变体和全部实机验收继续推进。

只读查询 `idf-env config get` 曾意外在SDK工具根创建一个347字节空 `esp_idf.json`；确认它是本轮新建、无已装环境记录／选择项后，仅删除该空文件，恢复查询前状态，没有安装、卸载或改写既有配置。本轮其他Git／运行服务／私人配置／并发改动保持原状；未执行测试套件、刷机、OTA、上传、发布或Git写操作。
