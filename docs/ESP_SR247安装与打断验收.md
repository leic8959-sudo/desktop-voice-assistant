# ESP-SR 2.4.7 安装与打断验收

## 2026-10-31：回答误打断修复

牛牛最新确认“唤醒还好，输出会被打断”。当前运行日志中出现多次 `barge_in_candidate/resume`，并在 `16:47:40Z` 收到确认插话而取消回答；同期播放缺帧、溢出均为0。已停用尚未通过声学验收的任意人声自动候选路径，只保留完整“你好，小燕子”主动触发打断；唤醒模型、阈值与增益不变。

本轮仅修改 `main/control_protocol.cpp`：删除自动人声候选创建和失去用途的累计计数，设备能力上报增加 `barge_in_trigger=wake_word`。完整唤醒词仍沿用既有候选/确认、取消旧回答和前置音频路径。任意语句自然插话目前不启用，不能宣称该能力已验收。

实际验证：`python -m unittest tests.test_barge_in tests.test_tool_activity -v` 10项通过；`powershell -NoProfile -File tools/build_firmware.ps1 -Volume assistant-idf-sr247-v1` 编译通过；仅写0x20000应用分区并通过写后散列校验。24秒本地实物播放结果：候选0次、确认0次、设备报告缺帧0、溢出0，播放耗时23979ms。证据为 `evidence/output-continuity-24s-20261031.json`，不调用云模型或家电。

旧应用备份位于 `runtime/output-interruption-before-20261031/assistant_espidf.bin`。当前应用SHA256：`0c5b324498c8ea6f289be634ad68318496bb708594946bb0ecfdf4d406c8ad8c`。NVS与模型分区保留；没有Git提交推送、外部上传发布。以下各节为安装及早期失败检查记录，以本节为最新行为。

牛牛明确授权安装依赖、编译、更新设备，并要求 Luna 执行、优先复用 GitHub 现成实现。三个 gpt-6-luna 分别处理构建流程、开源复用与独立审查、设备验收工具；主代理完成实际安装、整合、烧录和设备检查。

## 已安装与更新

- 官方 ESP-SR 固定为 `2.4.7`，隔离构建卷 `assistant-idf-sr247-v1`；原 `assistant-idf-native-v1` 保留。
- IDF 保持已有 `5.5.2`。官方 ESP-SR 2.4.7 要求 IDF `>=5.0`，实际已编译通过；小智整仓的 `>=6.0.1` 不是该组件的要求。
- ESP-SR 包来自官方组件注册表，目录散列核验为 `809d0041cdddd98a278f0d5afef7bb60a451290577b98cf718dfffc91bdcbd9b`。Docker 下载中断后经 Windows 下载完整包，并使用 IDF Component Manager 的目录散列算法验证，再恢复组件元数据；没有跳过内容校验。
- `main/idf_component.yml` 固定组件版本；`dependencies.lock` 保存解析结果。`tools/build_firmware.ps1` 支持 `-Volume`，先解析依赖再应用既有 QSPI 补丁，最后编译及导出。实际组件安装在隔离卷，宿主旧 `managed_components` 不参与该脚本的编译。
- 语音前端使用小智同款 `AFE_TYPE_FD`；保留自定义“你好，小燕子”和当前模型分区。播放参考仍是软件 PCM，不是硬件回采。

构建命令：

```powershell
powershell -NoProfile -File tools/build_firmware.ps1 -Volume assistant-idf-sr247-v1
```

## 打断相关改动

- 回答携带 `playback_id` 不再禁止完整唤醒词检测。
- 候选暂停期间继续识别完整唤醒词；命中时进入已有确认协议。
- 候选被冷却或暂停失败拒绝时，完整唤醒词不会被静默丢弃。
- 播放结束清理播放 ID；候选确认仍沿用旧回答代次失效、停止下发、取消云端生成、保留前置音频的路径。
- 修复候选门槛可能超过 PCM16 幅度范围的问题；候选只是可恢复暂停，最终取消仍需暂停后的持续语音确认。
- 待机麦克风 30dB，播放与候选暂停期间 12dB；回待机恢复30dB。修复恢复增益的锁顺序反转，并为写失败增加5秒退避。
- 实机发现播放任务 4KB 栈溢出，已扩到8KB PSRAM栈；2KB播放参考缓冲移出栈；增加播放任务剩余栈日志。

公开源码来源、固定提交及取舍见 [唤醒与打断开源复用核查](唤醒与打断_开源复用核查20261030.md)。没有引入整套 LiveKit 服务；其运行环境与当前 ESP32/Qwen 协议不同。当前两阶段本地阈值不是小智的原始参数，不宣称直接复制后即可保证自然双讲。

## 实际验证

| 检查 | 结果与范围 |
|---|---|
| `python -m unittest discover -s tests -v` | 74 项通过，日志 `evidence/sr247-python-regression-20261030.log` |
| 固件编译 | SR2.4.7+IDF5.5.2通过；新版AFE const接口已适配 |
| 固定正负样本在板上推理 | `negative=0 positive=1`，正样本置信度0.9321；日志 `evidence/sr247-probe-boot-20261030.log` |
| 完整16MB设备备份 | `firmware/backups/before-sr247-20261030.bin`，SHA256 `4aa3059d36fa40564a6d50d941b0d64cd38d5f12bcac3e07dc17ab60845d2c44` |
| 应用分区更新 | 只写0x20000应用分区，写后散列校验通过；NVS与模型分区未写 |
| 播放崩溃定位 | 日志明确 `stack overflow in task playback_pcm`，并非仅凭断线推断；已修复栈预算 |
| 最新无人插话播放检查 | 无错误确认取消，缺帧0、溢出0；但约11.66秒语音发生9次候选暂停/续播，耗时约17.49秒，听感未达标 |
| 真人自然插话 | 尚未通过；不能把准备时间窗口当作已证实人声来源 |
| 真人唤醒率 | 仍待实际收音诊断，板内固定PCM命中不等于真人声学通过 |

无人插话结果：`evidence/barge-far-only-20261031-002733.json`。现场曾有约5次说词只观察到1次唤醒，但时间窗可能未覆盖全部尝试，因此不换算正式命中率。

`tools/wake_device_check.py --capture-audio` 已支持显式5.12秒、三通道的本地诊断录音及RMS/峰值/削波比例统计；只有明确启用该选项才录制，不上传云端。当前待牛牛确认方便时再执行，不能把工具存在当作已完成录音。

## 当前边界

依赖安装、固件更新、协议及样本测试已完成；真人唤醒可靠性与自然插话听感仍未通过。不得将本页交付概括为“唤醒和任意语句打断全部解决”。正常桥接恢复并确认在线后才结束维护。未修改规则文件，未Git提交推送、外部上传发布或执行家电动作。
