# 千问语义自然插话候选（2026-11-09）

后续状态：牛牛反馈不可用后，已核对旧运行版本，完成独立回归、固件构建、网关加载和0.3.24一次应用OTA，详见[实际加载与验收边界](natural_barge_in_readiness_20261109.md)。下文保留首次源码候选时的检查范围，不代表当前仍未加载。

牛牛明确要求：小燕子回答期间，直接开口插话即可中止旧回答并继续对话，不需要再次说唤醒词。本文描述本地源码候选；没有加载网关、构建／刷写固件或完成实机验收。

## 实现范围

- 原生设备 hello 新增 `natural_barge_in_v1`，仅在现有软件 AEC 已初始化时报告。AEC 初始化不是消回声效果验收。
- 网关把能力传给本次 VoiceSession。首轮完整唤醒验证沿用既有规则；手动按键录音不启用持续收音。
- 仅对设备支持且模型名为 Qwen3.8-Omni-Realtime 系列的会话请求 `semantic_vad`。其他模型及旧设备保持原路径；不宣称 DeepSeek／千问文本或浏览器手动录音已支持自然插话。
- 初始配置或首句验证后的配置必须取得 `session.updated`，确认 `turn_detection.type=semantic_vad` 和 `interrupt_response=true`。若缺失，不放行自然插话；不会静默把服务端配置成功当成已确认支持。
- 完成首轮验证后，网关发送带会话身份的 `conversation_mode`。设备核对当前会话、自动会话状态、手动录音状态和 AEC，回传 `conversation_mode_ack`；网关收到本会话有效回执后才按自然插话处理输入。
- 设备在播放及结束尾音期间连续上传现有 AEC 输出，不再将这一路上行整体清零。不会启用旧的响度硬打断／两阶段本地阈值，不要求本地唤醒词触发。
- 千问返回 `input_audio_buffer.speech_started` 时，网关清空旧 PCM、使旧播放代次失效，向设备发送带 playback_id 的 interrupt；服务端自动取消能力已经确认，不重复发送 response.cancel。即使云端生成已结束、设备还有缓存，也中止本地剩余音频。
- 新人声起点立即使旧工具链的输入身份过期。已执行的动作不撤销、不重试；旧回答迟到的音频／工具结果不能重新启动旧播放。
- 协商成功的语义语音回合可以进入原有指令／记忆入口，短句不再仅因开始于播放期间或旧尾音时长窗口而被丢弃。记忆具体保存内容仍由既有规则决定。
- 断线、会话结束、重新连接对话及明确本地停止时清除设备模式；旧会话的启用消息和回执不用于新会话。

## 文件

- `gateway.py`：语义 VAD 配置确认、设备模式协商、输入身份、旧播放取消与短句接话。
- `assistant_runtime.py`：传递设备能力，不改变配对、账号或存储。
- `firmware/assistant_espidf/main/control_protocol.cpp`：能力上报、会话模式及 AEC 音频持续上行、interrupt 后丢弃旧音频。
- `tests/test_natural_barge_in.py`：假设备／假云的专项回归源码。

当前工作区已有多板适配改动，包括同一个 control_protocol.cpp；本轮只叠加以上协议修改，没有回滚或宣称那些修改已验证。

## 官方依据

- [实时对话](https://help.aliyun.com/zh/model-studio/realtime)：语音轮次检测、用户插话及本地播放缓存处理示例。
- [客户端事件](https://help.aliyun.com/zh/model-studio/client-events)：semantic_vad、semantic_vad_ec、response.cancel。此候选先使用面向日常交互、较保守的 semantic_vad；没有假定所有模型都支持。
- [服务端事件](https://help.aliyun.com/zh/model-studio/server-events)：session.created／updated 示例包含 interrupt_response；候选要求实际回执为 true。
- [接入协议比较](https://help.aliyun.com/zh/model-studio/omni-realtime-api)：WebSocket 的回声消除与降噪由客户端处理，语义检测不能替代 AEC。

## 验证状态与后续验收

遵守牛牛默认不主动执行构建／测试／类型检查／服务的规则，本轮只阅读源码、检查 Git diff 及新增回归源码，未执行下面命令，不能把用例数量写成通过数量：

```powershell
python -m unittest tests.test_natural_barge_in tests.test_barge_in tests.test_self_turn_guard tests.test_input_observation tests.test_wake_clip tests.test_wake_ack tests.test_realtime_tool_identity tests.test_realtime_send_lifecycle
```

专项源码覆盖：端／模型能力约束、云端自动取消确认、首轮唤醒门禁、首句模式确认后按序补发音频、旧会话／AEC缺失回执、短句插话、云端完成但本地未播完、迟到旧回答与工具、旧设备和手动录音边界。它没有运行，也不代替固件协议、真实云端或声学测试。

实际执行 `git diff --check -- gateway.py assistant_runtime.py firmware/assistant_espidf/main/control_protocol.cpp` 返回 0。对本说明和新测试分别执行 `git diff --no-index --check -- NUL <文件>` 返回 1，表示相对空文件存在新增差异；均未报告空白错误，仅提示 LF 后续可能转换为 CRLF。另通过 `git diff`／`git status --short` 核对修改范围，没有暂存文件。

下一步须先按当前板型完成固件构建和网关离线回归，然后分别核对真实千问配置回执、设备模式回执、插话首字保留及扬声器停止延迟。实机至少覆盖：无人说话完整播放；播放中说“等等／换一个／不是这个”和较长新请求；不同音量和距离；背景电视／附和声；连续多轮；网络抖动／断线；旧回复生成完成但尚未播完；按键切换。记录误触发、漏检、停止延迟与转写，不能只以设备日志代替本人听感。

最大的未决项是现有单麦软件 AEC 在扬声器播放和尾音阶段能否保留人声并抑制回声。本轮没有调整音频算法、麦克风增益或参考时序，没有声称已修复历史自问自答；真实效果不合格时应先根据声学证据修正 AEC，不能不断降低阈值或直接开启旧响度强打断。

尚未确定按实际已播放内容截断模型历史的可靠公开接口；取消播报不代表云端会话历史精确停在用户听到的位置，该项需真实对话验证。本候选不假定支持其他厂商的 conversation.item.truncate。

本轮没有安装依赖、读取真实凭据或私人记忆、调用付费模型、操作家电、改数据库、重启服务、构建／烧录／OTA、Git add／commit／push、上传或发布。
