# OTA 0.3.21：上行解耦与真实唤醒样本（2026-11-06）

牛牛明确要求OTA，已将Luna核心上行修复构建并加载。网关PID29612，入口`runtime/releases/wake-uplink-0321-20261106/launch_gateway.py`。10:17:26推送一次，10:17:44设备重新hello报告0.3.21、wake_clip_v1=True、wake_input_ready_v1=True。10:18:44 postflight健康200、设备online/idle、九个保护文件哈希保持、任务/记忆分批比较相同、启动Traceback0、OTA1次、源码/镜像匹配。

## 构建

已有ESP-IDF5.5.6环境，使用冻结main副本的隔离项目`runtime/native-wakeclip-0318`，该目录名不代表此次镜像版本；main和原0.3.20应用镜像已另存。启用项目已有组件选项CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK=y，生成`build/config/sdkconfig.h`确认为1，并保存发布副本。

实际命令（激活已有环境后）：

```powershell
& 'C:\Users\29248\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe' 'C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6\tools\idf.py' -C runtime/native-wakeclip-0318 -B 'I:\new find\desktop_voice_assistant\build' -DIDF_COMPONENT_MANAGER=0 build
python evidence/wake-uplink-0321-maintenance-20261106.py preflight
python evidence/wake-uplink-0321-maintenance-20261106.py backup
python evidence/wake-uplink-0321-maintenance-20261106.py postflight
```

构建退出0，应用编译/链接/镜像及分区检查通过，保留既有LVGL弃用警告。镜像2548128字节、3MiB分区剩597600字节（约19%）、app_desc0.3.21，SHA256 `1c4fd8fbdfdc770ddf6d11388fad8e785999be33a7bad26b4f2eabff5f1749b9`。日志`evidence/wake-uplink-0321-build-20261106.log`。

53文件冻结候选合并了并行导航常驻指示、显示暖启动复位和首批唤醒优化。此次网关仅额外增加新能力日志，行为修复与前轮父复核一致：111项Python关联回归、20个实际C++函数39场景263断言通过，详见`唤醒上行核心修复父复核-20261106.md`。

## 发布

空闲确认后私人SQLite一致性备份及保护文件保存于`runtime/reload-backups/wake-uplink-0321-20261106/`；原0.3.20镜像在release目录`previous-0.3.20.bin`。认证下载GET200及摘要检查通过后，原子替换本机OTA工件，核对原PID60736入口并停止，隐藏启动新PID29612。仅一次应用分区OTA，不刷引导、分区、模型或NVS；未开串口、安装依赖、Git写入或远程上传。

## 真人样本与用户纠正

用户最初选择“屏幕又黑了”，随后明确纠正“刚刚我说了一下你看下，屏幕没黑”。以最新现场确认屏幕未黑为准，不把前一选择作为已确认的0.3.21黑屏复发。

10:23:12这次实际唤醒会话`3d1d58c4956a42d7b09c25f87dd53d31`：

- 设备检测后68ms收到传输ready，71ms开始首个上传；首消息发送完成4ms。
- 真cloudready在设备检测后390ms，设备明确报告upload_before_cloud_ready=1，证明该次上传与握手重叠。
- 设备553ms结束采集，标记随后6ms发送；累计发送344ms，最长单次34ms。
- 网关收到检测12.035、云ready12.361、commit12.544、完整口令verified12.808、缓存首包12.810；日志单调wake_to_first_audio_ms=766、after_speech_ms=266。
- 转写为“你好，小燕子。”，缓存“我在”命中，56帧，underruns0、overflows0；设备tts至首写31ms，首PCM接收至首写17ms，首帧显示1ms。
- 10:23:44正常回idle并结束会话，没有session_failed或异常重连。

相较0.3.20的09:36单次3031ms样本，网关首包指标减少2265ms（约74.7%）；相较此前1109–1203ms样本也有改善，但网络和说话条件不同，不能视作严格受控性能实验、平均值或p95。766ms从网关检测事件起算，不包括更早的本地检测和说口令时长；首写完成不是声学起音测量。

这次证据支持核心队列与早上传路径已经实际执行且取得单样本改善；长期成功率、背景声、连说指令和电池待机仍需自然使用观察，不能把一次成功写成所有唤醒问题已彻底解决。原0.3.19显示恢复修复代码仍保持。
