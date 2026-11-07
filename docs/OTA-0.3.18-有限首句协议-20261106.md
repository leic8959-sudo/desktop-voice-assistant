# OTA 0.3.18：有限首句协议（2026-11-06）

牛牛明确要求“ota”。已构建并通过既有应用OTA更新设备，未打开串口。北京时间08:55:42推送一次，08:55:59重新hello报告0.3.18、wake_clip_v1=True。当前网关PID46376，入口`runtime/releases/wake-clip-v1-20261106/launch_gateway.py`。

设计、公开助手调研与已完成回归见`有限首句协议与助手唤醒调研-20261106.md`。该文件描述源码候选时的状态；本记录补充实际构建和发布结果，不用改写被冻结的设计文档来冒充先前已经发布。

## 构建与工件

冻结44文件哈希逐项通过；隔离项目main副本与当前固件main逐文件一致。已有组件和工具链复用，无依赖安装。

实际执行（PowerShell，项目根目录）：

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif\official-5.5.6'
$env:IDF_PYTHON_ENV_PATH='C:\Users\29248\.espressif\python_env\idf5.5_py3.12_env'
$env:IDF_COMPONENT_MANAGER='0'
& 'C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6\export.ps1'
& 'C:\Users\29248\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe' 'C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6\tools\idf.py' -C 'runtime/native-wakeclip-0318' -B build -DIDF_COMPONENT_MANAGER=0 build
python -m unittest tests.test_wake_clip -q
python evidence/wake-clip-maintenance-20261106.py preflight
python evidence/wake-clip-maintenance-20261106.py backup
python evidence/wake-clip-maintenance-20261106.py postflight
```

构建退出0，2210项构建步骤完成，应用链接、镜像及分区容量检查通过。`-B build`实际相对于调用目录解析，因此输出位于根目录`build/`，不是`runtime/native-wakeclip-0318/build/`。CMakeCache确认CMAKE_HOME_DIRECTORY仍为隔离项目`runtime/native-wakeclip-0318`，编译的是冻结的隔离源码。没有调用flash命令，构建输出中的整机烧录示例没有执行。

- 版本：app_desc 0.3.18；魔数核对通过。
- 应用镜像：2536752字节。
- SHA256：`a1dea4b1972abaf1da73734dfdd7ff0553de1c042c9fe5536c48b72d573764b3`。
- 3MiB应用分区剩余608976字节，约19%。
- 日志：`evidence/wake-clip-build-env-20261106.log`、`evidence/wake-clip-build-20261106.log`。
- 为发布验收新增一条不含敏感数据的`native_device_capabilities`日志；变更后12项有限片段/实际设备入口回归再次通过，2.028秒，退出0，日志`evidence/wake-clip-ota-entry-20261106.log`。

先保存旧0.3.17镜像副本及摘要，再保存新镜像发布副本并原子替换现有OTA下载工件。使用本机认证GET `/api/ota/firmware`核对HTTP200及下载内容SHA256相同，见`artifact-verification.json`。

## 实际加载与保留

预检两次确认设备在线idle、云未连接、实体与电脑媒体停止、队列和pending空、计时取消、无待执行调度。私人SQLite一致性备份及保护文件副本在`runtime/reload-backups/wake-clip-v1-20261106/`，不上传。原镜像在release目录`previous-0.3.17.bin`，不是已验证的整机一键回滚包。

再次验证当前监听进程PID53488及入口，停旧网关后隐藏启动PID46376。启动包装校验冻结源码和镜像，仅提供0.3.18一次OTA，后续重连不继续推送。设备认证下载，17秒后重启报告新版本；本轮只更新备用应用分区，不写引导、分区表、模型或NVS。

08:56:18（Asia/Shanghai）postflight：

- 健康200、设备在线idle、固件0.3.18、新协议能力True，云未连接。
- 九个保护文件哈希全部保持：配置、绑定、设备偏好、音色、计时、音乐状态及三份DPAPI凭据。
- tasks/memories按100行分批与一致性备份比较相同；添加提醒/修改标题能力保持。
- 冻结44文件哈希全部匹配，OTA工件与本次镜像摘要一致，缓存“我在”可用。
- 启动Traceback0、OTA推送1次、发布标记确认；显示偏好display=true。该回执不是人眼屏幕验收。

发布工件、manifest、process、preparation、pre/post-reload和后续deployment记录均位于`runtime/releases/wake-clip-v1-20261106/`。业务日志`runtime/wake-clip-gateway.stderr.log`，发布标记在同名stdout。

## 验收边界

没有主动发起真实设备麦克风会话或执行工具，真人安静/背景声/连说指令、实际起音耗时和屏幕仍待牛牛现场使用。不能由版本/能力回执推断漏唤醒根因已解决。此前137项关联回归、主机26场景119断言以及合成口令真实云500ms仅各自代表对应范围，不替代新固件实机语音验收。

未安装依赖、修改规则文件、Git写入或远程上传。源码目录不是Git仓库；本次仅本地构建与本机网关/设备OTA。
