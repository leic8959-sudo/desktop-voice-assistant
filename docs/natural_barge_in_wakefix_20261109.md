# 自然插话升级后唤醒不回复：修复与加载

2026-11-09，Asia/Shanghai。状态：修正版网关已加载，牛牛已确认“能正常回复了”。设备仍为0.3.24，本轮没有再次刷固件；自然插话协议已实际协商成功，但物理打断效果仍需单独验收。

## 原因与证据

旧自然插话网关的三次真实唤醒均识别到完整唤醒词并完成首句提交，但在 `finish_wake_verification()` 中更新千问配置时收到error，随后抛“云端拒绝本地问候上下文”，会话在“我在”缓存播放之前结束。

上轮真实检查只覆盖空会话配置，漏掉了首句音频append／commit之后的状态约束。直接复现后，千问返回：

```text
invalid_request_error
param: session.turn_detection.silence_duration_ms
message: turn_detection.silence_duration_ms cannot change after audio append
```

原因是首句采用turn_detection=None，云端检测时长为默认800ms；旧候选在提交音频后切为semantic_vad时还试图指定600ms。空会话中该配置可接受，上传音频后则拒绝。设备唤醒、网络联通和缓存音频本身不是这次回归的故障点。

## 修改

- `gateway.py` 的自然插话检测配置只指定 `type=semantic_vad`，沿用云端已建立的时长等参数；首句之后不再写silence_duration_ms、prefix_padding_ms或threshold。继续确认云端的semantic_vad及interrupt_response=true。
- 配置拒绝日志补充错误类型与参数名，保留完整异常堆栈；不输出真实转写、记忆、提示词或凭据。
- `tests/test_natural_barge_in.py` 新增“音频append后检测参数锁定”的回归，覆盖完整缓存问候返回。新用例在修改前实际复现相同错误，修改后通过。
- 工作区修改只涉及以上网关修正、专项测试与本说明；现有并行多板改动保留。

## 实际验证

1. 新增单例回归：`python -B -m unittest tests.test_natural_barge_in.NaturalBargeInTests.test_cached_wake_switch_keeps_timing_locked_after_audio_append -q`。修复前退出1，复现“云端拒绝本地问候上下文”。
2. 工作区定向回归：`python -B -m unittest tests.test_natural_barge_in tests.test_wake_clip tests.test_wake_ack tests.test_self_turn_guard tests.test_input_observation -q`。退出0，54项通过，4.401秒。
3. 独立冻结候选：`python -B evidence/check-natural-barge-wakefix-20261109.py`。退出0，120项通过，0失败／错误／跳过，19.627秒；包含自动记忆、旧回合工具身份和发送生命周期回归。模块来自候选，前后摘要一致，不加载未迁移项目工具，网络守卫禁止外网和在用业务端口。
4. 真实千问时序复现：首句无音频的两种配置均接受；使用已有合成问候音频上传并提交后，原配置拒绝。移除检测时长字段后真实回执接受，silence_duration_ms沿用800、semantic_vad和interrupt_response=true。没有提交工具或请求生成回复。
5. 真实修正版后端：`python -B evidence/probe-wakefix-backend-20261109.py`。退出0。直接使用冻结候选的RealtimeBackend，上传75,840字节的已有合成音频，提交并取得转写完成事件，再调用真实finish_wake_verification(True)，成功确认语义模式、保留“我在”缓存并退出首句模式。未上传真人录音、未发送工具或response.create。ASR使用真实服务，不声称这些检查账单为零。音频重采样仅在本轮诊断脚本中用既有Python3.12 audioop，出现弃用提示；没有新增业务依赖。
6. `python -B runtime/release-candidates/natural-barge-in-wakefix-20261109/load_and_ota.py` 退出0，核对114文件、120回归、固件身份与模块来源；默认只检查，不启动。`git diff --check -- gateway.py assistant_runtime.py`退出0，仅有Git的换行转换提示。

## 恢复与加载

| 北京时间 | 结果 |
| --- | --- |
| 15:55:12／15:55:15／15:55:20 | 原自然插话网关三次识别唤醒后配置拒绝，失败证据保留在旧候选日志 |
| 15:56:41 | 确认idle后停止原PID47084，临时恢复11月7日独立记忆网关，PID36264；设备0.3.24兼容旧协议，没有降级固件 |
| 16:02:06起 | 真实修正版后端完整首句配置流程验证成功 |
| 切换前 | 新目录备份5个数据库及15份配置／状态，SQLite backup API生成一致快照并quick_check通过；原备份未覆盖 |
| 16:03:16 | 空闲维护窗口内停止PID36264，隐藏启动修正版独立网关，PID11680；自动OTA关闭，沿用原数据目录 |
| 16:03:21 | 实体0.3.24回连，natural_barge_in_v1=True |
| 16:03:44 | postflight退出0，设备online／idle，15份配置／状态文件一致，5库业务表摘要一致，仅排除正常调度租约p_lease |
| 16:04:09 | 真人唤醒完整口令验证成功，缓存命中；从检测到首音频1094ms，设备随后报告56帧、缺帧0、溢出0 |
| 16:04:09.534 | 真实设备natural_barge_in_ack enabled=True，完成本会话端云协商 |
| 随后 | 两轮云端回复有首音频及设备播放统计，缺帧／溢出均为0；牛牛明确确认“能正常回复了” |

当前入口：`runtime/release-candidates/natural-barge-in-wakefix-20261109/load_and_ota.py --load --config config.local.json`。新候选基于上个自然插话快照，仅修正网关；0.3.24应用镜像复用原已校验产物，没有重建／再次OTA、串口、NVS或语音模型写入。

原失败候选、临时恢复记录和修正版分别保留，不通过覆盖旧日志抹去失败。备份为 `runtime/reload-backups/natural-barge-in-wakefix-20261109/`；当前候选内有offline-check、real-backend-wake-check、pre-reload、process-switch、post-reload与网关日志。

## 尚未解决或未验收

此次确认的是正常唤醒回复恢复与真实自然插话模式协商成功；不能据此把回声、误打断、漏打断、首字保留、停止延迟和对话历史截断宣称为通过。

这轮正常对话的日志另记录自动记忆提取ValueError和Music Assistant 127.0.0.1:8095拒绝连接。它们未被计入“启动零异常”，也没有归为本次唤醒参数错误；本轮没有启动音乐依赖或改记忆业务来掩盖这些独立问题。

没有安装依赖、执行数据库迁移、恢复旧库覆盖新记录、修改家居状态或Git写操作，没有上传或公开发布。
