# OTA 0.3.15：唤醒缓存上传完整性（2026-11-05）

牛牛明确要求“直接发布固件把”，本次执行既有候选的构建与OTA。长期持续目标仍暂停，不恢复自动扩展。此前已同意的5.12秒数字采音没有执行，按最新发布指令先部署固件。

本轮生产源码仅control_protocol.cpp及version.txt：缓存按32000字节以内分块，每块必须返回完整字节数；负值、零或短写立即停止后续缓存与live，不把失败当完整上传、不重试已送前缀。失败候选隔离至网关idle或断线确认，捕获同步回调而不丢释放信号，再清缓存/身份允许新唤醒；旧listening、manual和detect不能提前恢复。保留既有4秒prefix/2秒suffix、采样率和门禁，未改采音增益、模型、绑定或NVS。session_end发送失败且无结束确认时保持隔离，不宣称已恢复；同步发送导致AFE缓冲溢出的另一候选未在本轮改造，也不能声称本次已定位完整唤醒失败的根因。

主代理独立diff审阅并实际执行 `python tools/check_wake_upload_host.py`，提取12个实际C++函数，用现有WSL Ubuntu g++编译：20场景、98检查、0失败，日志evidence/ota-0315-wake-upload-host.log。覆盖原字节顺序/环形边界/32000分块、负/零/短写，以及隔离、同步idle/disconnect和下一候选恢复。此前14场景红测9失败5对照通过保留在evidence/wake-upload-host-20261105-before.log；不是Python重写算法镜像，也不代替FreeRTOS实机并发验收。

实际激活现有ESP-IDF5.5.6环境并执行：

```powershell
python C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6/tools/idf.py -C firmware/assistant_espidf -B build_official build
```

构建退出0、链接/镜像/分区容量通过，保留已有LVGL弃用警告。0.3.15镜像2593184字节，应用分区3145728字节，剩余552544字节（18%）；SHA256 `012b304b7c18087690946defe8878ac285662600753061dc6c4f35a60b840f0c`。日志evidence/ota-0315-build.log，发布源码逐文件哈希与镜像位于runtime/releases/firmware-0.3.15-20261105/manifest.json。未安装依赖。

发布前备份旧0.3.14镜像c9a8866d...及配置/配对/授权/偏好/计时/音乐、SQLite一致性快照。空闲两次检查通过后原子替换本机下载工件，并以带认证GET确认HTTP200及镜像哈希一致。停止原网关PID56832，隐藏启动一次性OTA包装；当前PID28988。包装仅首次设备hello提供冻结0.3.15清单，后续重连不再推送，不修改常规网关源码。

17:48:03推送一次OTA，设备从认证接口下载备用分区，17:48:20重新hello报告0.3.15。17:48:41只读核验：健康、设备online/idle、新版本0.3.15、云未连接，保护文件哈希保持，tasks/memories按100行分批与备份一致，网关14源码与round24基线匹配，无启动Traceback；日志仅一次ota_pushed。设备升级流程校验服务器提供的SHA256，未读取或擦写NVS/模型分区。设备新hello证明已启动新版本，未以此推断真人语音/显示/声音验收通过。

证据：runtime/releases/firmware-0.3.15-20261105/{preparation,manifest,pre-reload,post-reload,process,deployment}.json；evidence/ota-0315-maintenance.py；runtime/ota-0315-gateway.stderr.log。私人数据备份runtime/reload-backups/firmware-0.3.15-20261105，不上传，不宣称为已验证一键全项目回滚。

没有采音、主动真实云对话、家居/电脑工具或真实任务写入，没有Git写入或公网发布。完整唤醒的实际改善仍待牛牛自然使用确认，本次固件OTA已完成。
