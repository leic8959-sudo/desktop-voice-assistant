# 本机缓存依赖固件构建：实际失败记录

本页保留精确IDF5.5.2的失败证据。后续使用另一套已安装SDK取得的单入口构建结果见[IDF5.5.6-dirty缓存构建记录](cached_firmware_build_idf556_20261109.md)；不以该成功覆盖或改写本页失败。

2026-11-09，Asia/Shanghai。全量目标明确包含构建，本次复查本机已安装工具后实际推进隔离构建，未将此前的静态检查冒充编译结果。**构建失败，没有产生本轮可交付镜像，也没有应用源码编译或实机通过证据。**

## 新确认的环境

Docker CLI存在，但 `docker image inspect espressif/idf:v5.5.2 --format '{{.Id}}'` 仍返回1，当前配置的Linux引擎管道 `dockerDesktopLinuxEngine`不存在。本轮未启动Docker服务或拉取镜像。

继续只读核对标准安装目录后发现：

- `C:\Espressif\frameworks\esp-idf` 的 `tools/cmake/version.cmake`为5.5.2。
- `%USERPROFILE%\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe`已存在。
- 本机xtensa工具为 `esp-14.2.0_20251107`，CMake3.30.2、Ninja1.12.1，均与该SDK的tools.json推荐值对应。
- 设置本次子进程环境后执行现有Python与 `tools/idf.py --version`，退出0、返回 `ESP-IDF v5.5.2`；SDK无Git元数据，工具会提示Git版本不可用后读取源码版本，未因此写入SDK。

另有 `C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6`，其SPIFFS目录存在；本轮没有把它的子模块复制进5.5.2，也没有暗中切到5.5.6后仍声称验证5.5.2。

## 隔离输入与操作边界

新增 `tools/prepare_cached_firmware_build.py`，使用IDF已有Python环境里的PyYAML读取本地组件清单，不安装依赖。实际执行：

```text
<既有IDF Python> -B tools/prepare_cached_firmware_build.py --profile waveshare-amoled-1.8 --destination C:\Users\<user>\AppData\Local\Temp\xy_cached_08ff6ca86738
```

返回0，复制固件main、登记表／CMake／默认配置／分区以及17个现有managed_components到该新目录；不是在原构建目录操作。该次选择的是**历史CO5300兼容入口**，不是SH8601上游原版或其他新板。

隔离manifest将17项依赖指向本次复制的本地override，保留版本声明；没有访问用户config.local.json、runtime、记忆库、录音或账号。原SDK、组件目录、工作区构建目录不改写。复制已有组件属于构建输入快照，不是安装或下载新依赖；本地override锁也不能作为官方干净安装依赖锁。

构建Python子进程注入本轮专用sitecustomize审计：允许127.0.0.1／::1回环，以兼容Windows asyncio socketpair；拒绝外部connect及域名解析。没有为成功构建放开付费服务或远程下载。

## 两次尝试与结果

环境变量只对本次子进程设置：`IDF_PATH`、`IDF_TOOLS_PATH`、`IDF_PYTHON_ENV_PATH`、包含既有工具的PATH、`PYTHONDONTWRITEBYTECODE=1`、指向离线守卫的PYTHONPATH。未修改用户／机器永久环境。

实际重试命令（PowerShell中的两个-D参数按字符串传入，保留profile小数点）：

```text
<既有IDF Python> -B C:\Espressif\frameworks\esp-idf\tools\idf.py '-DIDF_TARGET=esp32s3' '-DASSISTANT_BOARD=waveshare-amoled-1.8' build
```

| 尝试 | 退出码 | 最终结果 |
| --- | --- | --- |
| 1 | 1 | 初版网络守卫连本机socketpair也拒绝，IDF在启动CMake子进程前失败；没有编译，保留日志。初次PowerShell未给-D字符串加引号，还丢失profile尾部 `.8`，重试时已修正 |
| 2 | 2 | 修正守卫和参数后读取17项本地组件及IDF5.5.2、生成SDK配置并识别应用assistant_espidf／0.3.21，随后CMake报告SDK的 `components/spiffs/spiffs/src`不是目录，配置失败，尚未进入应用编译 |

第二次命令确实运行到终态，不把观察超时当作进程停止。没有无限重试、下载缺失子模块或通过删除SPIFFS依赖掩盖SDK不完整的问题。IDF自身配置过程中有编译器特性探测，这不是本项目测试套件通过。

## 证据与后续条件

- 可审阅摘要：`firmware/assistant_espidf/cached_build_attempts_20261109.json`，含两次退出码、阶段、理由、工具版本、隔离目录、30份自有源文件／配置摘要及日志摘要，明确 `build_passed=false`。
- 原始本轮非业务构建日志：`evidence/cached-firmware-build-20261109/attempt-1.log`、`attempt-2.log`；输入组件路径／版本：同目录 `cached-build-inputs.json`。evidence仍被Git忽略，不视为远端仓库会自动携带。
- 原始临时目录保留，未删除或覆盖原工作区旧产物。此次未发布任何产物，也未刷机、OTA、改数据库、网关、家居或第三方账号。

要继续此精确5.5.2构建，需要牛牛补齐匹配的SDK子模块或提供完整已安装环境；不能通过混合另一版本文件来伪称验证原版。若决定改用现成完整5.5.6，应作为独立工具链／版本声明的构建路径记录。新板还各自缺BSP依赖，仍需牛牛准备；未获准自行安装。

本轮同时完成麦克风和播放参考采样率分离源码，但32kHz分支尚无原生板型实例化、应用编译没有发生，不能据缓存解析成功宣称它或T-Circle已验证。全量仍为8个官方变体有源码入口、180个待实现，构建／实机目标保持未完成。
