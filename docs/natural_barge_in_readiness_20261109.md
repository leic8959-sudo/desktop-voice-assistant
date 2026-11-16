# 自然插话不可用：运行版本核对与独立候选

后续回归与修复：0.3.24升级后曾出现唤醒不回复，原因是上传音频后修改千问检测时长被拒绝。现已加载网关修正版并获牛牛确认正常回复，详见[唤醒回归修复与加载](natural_barge_in_wakefix_20261109.md)。下文保留首次加载记录。

2026-11-09，Asia/Shanghai。牛牛反馈语音打断不可用后，本轮核对运行状态、封装独立候选，并完成实际网关加载及一次应用 OTA。**设备已回连 0.3.24 并报告自然插话能力；真人插话效果仍待验收。**

## 维护前已确定的原因

- 8765 的监听进程为 PID 23732，启动入口为 `runtime/release-candidates/auto-memory-closure-20261107/launch_gateway.py`。它从冻结目录加载 11 月 7 日独立记忆版本，不会因为工作区 gateway.py 修改而自动更新。
- 经本机认证的 GET `/api/devices` 回读，实体在线，固件版本为 **0.3.23**。该版本没有 `natural_barge_in_v1` 及本次自然插话协议。
- GET `/api/status` 显示千问 Realtime 为当前供应商、模型 `qwen3.8-omni-flash-realtime`；设备 idle、云会话未连接。GET `/health` 返回 200。在线和健康不表示新打断能力已加载。
- 因此当前故障首先是**候选没有加载到实际网关和设备**。本轮尚无依据把此次不可用归因于千问不支持自然插话，也不能据此认定历史回声问题已经解决。

此前说明中“已实现”是源码候选状态。要让实体可用，需要把经过验证的网关与固件实际安装，并核对语义检测／设备模式的回执；不能只重启旧入口或仅更改一个开关。

## 本轮候选

位置：`runtime/release-candidates/natural-barge-in-20261109/`。

网关从当前运行的 114 文件快照开始，仅叠加 gateway.py、assistant_runtime.py 的自然插话改动。文件摘要逐项检查，保留记忆修复和原数据目录，不包含尚未迁移的项目工具。没有把工作区其他功能整体加载。

固件从已加载 0.3.23 的冻结源码／Git 基线组装，沿用现有 1.8 CO5300 硬件入口，仅叠加 control_protocol.cpp 的自然插话协议，候选版本 **0.3.24**。并行多板适配的源码改动没有合入这个固件候选。现有音频算法、显示修复和唤醒入口保留；用已安装的 17 个组件源码和本机 ESP-IDF v5.5.6-dirty 隔离构建，未安装依赖或下载 SDK。这不是官方干净工具链构建，也不是所有板型通过。

## 实际验证

1. 工作区执行：

   `python -B -m unittest tests.test_natural_barge_in tests.test_barge_in tests.test_self_turn_guard tests.test_input_observation tests.test_wake_clip tests.test_wake_ack tests.test_realtime_tool_identity tests.test_realtime_send_lifecycle -q`

   退出 0，**91 项通过**，耗时 10.491 秒。预期失败场景会记录模拟错误堆栈，最终失败／错误为 0。

2. 冻结独立候选执行：

   `python -B evidence/check-natural-barge-candidate-20261109.py`

   首次 **119 项通过**，0 失败、0 错误、0 跳过，耗时 19.625 秒。随后为核对实机增加设备能力日志，重新冻结后执行同一命令，最终 **119 项通过**，18.047 秒；模块来自候选、测试前后源码摘要一致、未加载项目工具。包含自动记忆回归。网络审计禁止外网及在用端口 8765／8123／8095／8097，仅使用临时数据和假云／假设备。91 与 119 是两次不同源码范围的结果，不能相加为唯一测试数。

3. 真实千问配置握手：

   `python -B evidence/probe-natural-vad-settings-20261109.py`

   退出 0，收到 session.created 和 session.updated，实际回执 `turn_detection.type=semantic_vad`、`interrupt_response=true`、`create_response=true`。只使用本机凭据在内存认证，不输出凭据；上传音频 0 字节、未发送文字项／工具／response.create。它证明该账号与模型接受配置，不证明麦克风实听、回声抑制、停止延迟或自然接话通过；未据此声明账单费用为 0。

4. 固件构建：

   `python -B evidence/build-natural-barge-candidate-20261109.py`

   退出 0，612.406 秒，镜像为 assistant_espidf／0.3.24，2,613,648 字节，应用分区剩余 532,080 字节，SHA256 `f73388fd07a6c0dce0b64dea0b461121b2b004240d2fc228f382c9464d925a8a`。源码前后摘要一致；SDK为现有 v5.5.6-dirty。原始结果为 `firmware-build.json`／`firmware-build.log`。

   使用已有 IDF Python 执行 `-B -m esptool --chip esp32s3 image_info --version 2 <候选bin>`，退出 0，ESP32-S3、16MB、DIO80MHz、版本0.3.24，checksum 与 validation hash 均为 valid。只读镜像检查没有接触串口。

5. 加载入口检查：

   `python -B runtime/release-candidates/natural-barge-in-20261109/load_and_ota.py`

   退出 0；114 文件、119 项回归、云端配置、镜像摘要与模块路径核对通过，默认只检查，不启动服务或 OTA。实际启用须显式传 `--load`，一次 OTA 另传准确的已配对目标；持久 marker 在发送前消费，失败也不盲目重试。下载沿用原管理员／已配对设备认证。

## 实际启用结果

| 北京时间 | 操作与结果 |
| --- | --- |
| 15:47:40 | 设备online／idle、云语音断开、媒体stopped、计时cancelled；未来15分钟无临近提醒或调度事项，允许维护 |
| 切换前 | `python -B evidence/natural-barge-maintenance-20261109.py backup` 退出0，备份5个数据库、15份配置／授权／状态、旧入口和旧0.3.23镜像；SQLite backup API生成一致快照并quick_check=ok |
| 15:51:36 | 再次核对空闲、旧PID23732身份和8765归属，停止该进程，隐藏启动独立入口，新PID47084；沿用原config和runtime |
| 15:51:41 | 收到实体0.3.23 hello，一次OTA marker消费，发送0.3.24应用更新；旧固件报告natural_barge_in_v1=False |
| 15:51:59 | 实体重新连接，hello=0.3.24、natural_barge_in_v1=True；没有再次推送 |
| 15:52:42 | `python -B evidence/natural-barge-maintenance-20261109.py postflight` 退出0；设备online／idle，15份文件严格一致，5个数据库的业务表逐表摘要一致（仅排除正常调度租约p_lease）；新网关启动堆栈0 |

启动入口：`runtime/release-candidates/natural-barge-in-20261109/load_and_ota.py --load --ota-target <本机目标身份> --config config.local.json`。没有改写根固件产物、根version.txt或并行多板改动；OTA下载由本次独立入口绑定该候选镜像，只推送指定旧版目标一次。没有刷NVS、分区表、语音模型或串口，没有安装依赖、操作家电、Git暂存／提交／推送或公开发布。

备份位置：`runtime/reload-backups/natural-barge-in-20261109/`。证据在候选目录：manifest、offline-check、cloud-settings-check、firmware-build、pre-reload、immediate-pre-reload、process-switch、ota-once-consumed、post-reload以及网关日志。只保存本机，含配置／授权的备份不得上传。

若需回退，先确认无活跃语音／工具，再使用原独立记忆入口启动；不用工作区全量源码，也不恢复旧数据库覆盖维护后的新记录。旧0.3.23镜像已备份，设备降级属于独立操作，不自动反复OTA。

尚待真人验收：首次完整唤醒验证成功、实际会话的千问语义配置确认、conversation_mode_ack 的自然插话／AEC均为true；随后由牛牛在扬声器回答中直接说“等等”“换一个”等，核对旧声音停止、新请求首字及结果。无人插话完整播放、背景声误触发、距离／音量、连续多轮及会话历史截断也须分别验收。AEC就绪回执不证明声学效果，不将本次升级成功写成自然插话实机已通过。
