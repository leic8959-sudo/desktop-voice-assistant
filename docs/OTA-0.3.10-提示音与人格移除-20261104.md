# 小燕子 OTA 0.3.10：自由点云、安静提示音、移除人格

日期：2026-11-04（Asia/Shanghai）。牛牛明确要求把自由点云OTA到设备，同时自行设计有区分度且限频的提示音，并删除全部人格。本轮合并实施，使用原OTA通道实际升级；绑定和凭据保持。

## 实际发布结果

- 设备 `已配对样机` 发布前在线0.3.9。
- **15:16:47（北京时间）**，网关推送0.3.10，设备局域网下载 `/api/ota/firmware` 返回200。
- **15:17:04**，设备报告固件 **0.3.10** 并在线；后续设备接口复查相同ID在线。屏幕协议仍有回执，未以回执替代真人视觉验收。
- `runtime/pairing-sessions.json` 发布前后SHA-256一致；没有解绑、重新配对或改设备凭据。没有擦除NVS、模型、存储或分区表。
- 网关从PID24172切换到PID54168，沿用原Python、工作目录与 `gateway.py --config config.local.json`，本轮新日志Traceback数量0。
- 已实读 `/api/status` 不再含persona / personas能力，`/api/personas`和`/api/persona`返回404，线上助手HTML不含persona表单；旧偏好文件及对应pyc已清理。

## 自由点云

包含已确认的待机、聆听、正在回答三态调整：内部体积密度不均、非对称连续流场、缓慢漂移、非匀速转动；回答复用原音频播放包络产生局部伸缩。点数仍为768，复用固定PSRAM缓冲、正弦查表，无逐帧对象分配。自由态系数平滑过渡，保留其他状态、卡片操作、语音与提醒优先级。不是全帧随机重采样，避免噪点跳闪。

本次发布针对实际固件这三种点云；此前聊天中全部语义图案的预览不等同所有图案已逐像素移植。文字卡片和按钮仍按现有固件呈现。实际点云帧率、唤醒并发与观感需现场验收。

## 提示音方案与限频

| 事件 | 音符 / 总时长 | 播放规则 |
| --- | --- | --- |
| 普通待机、唤醒、聆听、思考、搜索、正在处理、每轮回答、普通查询成功 | **不发声** | 删除原Thinking / Executing进入和回答结束后的自动叮咚，不把每轮对话当提示音事件 |
| 到期提醒 | 660→880Hz，两声，300ms | 同一提醒卡在真正显示并投递ACK后请求一次；同类至少20秒间隔 |
| 倒计时到期 | 784→988→1175Hz，三声，360ms | 计时ID去重；服务端和本地到期同步不会因同ID重复响。同类至少20秒间隔 |
| 可见操作失败、权限拦截、结果待核实 / 基础Error | 392→294Hz，两声，185ms | 短下行音；相同卡片ID不重复触发；同类至少60秒间隔 |

全局任意两组音至少相隔 **8秒**，单次播放一组，不循环报警。提醒/计时最多等待60秒，错误最多等待5秒；三种固定待播槽合并同类请求，过时丢弃，不无限积压。连续错误不会每次响，受60秒冷却和5秒过期双重限制；因此屏幕有错误提示时也可能安静，这是有意减少打扰。

音量通过PCM峰值从原6000降到3000（约降低6dB的信号幅度，**不代表喇叭实际声压或系统音量百分比**），短音头8ms渐入、尾28ms渐出，音符间留短静音避免硬切。

复用 `audio_play_pcm24` 和原AEC播放参考，不改codec总音量。说话、收听、活动对话或OTA进行中先让路；活动对话内错误通常只保留屏幕，错误音过期丢弃。正在播放提示音时若新语音开始，按256样本小块尽快退出；采集/唤醒路径跳过提示音和短尾音，防止提示音触发自己。没有新增依赖、TTS、真实云请求或新的音频供应商。

三种离线试听从固件共用 `notification_cues.h` 音符表生成，分别为 `evidence/ota-0310-cue-timer.wav`、`cue-reminder.wav`、`cue-error.wav`；不是实机录音。摘要 `ota-0310-cue-summary.json` 记录时长、峰值、冷却与过期参数。现场音色、音量、AEC与收音竞态仍需真人验收。

## 移除全部人格

- 删除 `persona_settings.py` 的全部人格/方言预设和固定彩蛋，以及旧专用目录测试 `tests/test_persona.py`；不再导入、加载或拼接persona_prompt。
- 删除runtime初始化、锁、状态字段、personas能力、`GET /api/personas` / `PUT /api/persona`、共享屏幕反馈中的人格项。
- 删除助手页人格表单、app.js渲染/读取/保存/清理代码、console_settings请求/选项、示例配置persona_id。更新脚本版本，避免页面继续使用已删控件的旧脚本。
- 清除实际 `runtime/persona-preference.json` 和已知persona_settings/test_persona的Python缓存。未读取文件内容。
- 现有含秘密配置文件未手工读取或重写；若旧文件还有persona_id，统一配置默认边界直接pop掉，旧偏好不生效也不因损坏阻止启动。历史文档/维护副本保留过去事实，不当作活动人格功能。
- 保留小燕子名称、中文、简洁自然回答和原工具/安全/确认规则；不再强制角色口吻、推广彩蛋或方言切换。移除决策按牛牛要求在相关配置边界标注人工决策时间。

## 实际文件

修改：`firmware/assistant_espidf/main/display_ui.cpp`（自由点云及按事件请求提示音）、`control_protocol.cpp`（移除轮次叮咚、短音合成与限频、音频让路/尾音隔离）、`version.txt`（0.3.10）；新增 `notification_cues.h`（同一音符表/冷却门槛）。

修改：`llm_providers.py`、`assistant_runtime.py`、`control_api.py`、`src/assistant_app/application/screen_feedback.py`、`config.example.json`；`web/assistant.html`、`app.js`、`console_settings.js`、`admin_console.js`、`admin_console_demo.html`。

测试：新增 `tests/test_persona_removed.py`、`tests/notification_cues_check.cpp`；修改 `test_console_configuration.py` 与 `console_configuration_ui.mjs` 移除人格保存预期，保留其他检查；删除旧人格功能测试。

编辑前副本 `%TEMP%/xiaoyanzi-0310-before`。当前发布前0.3.9工件保存在 `runtime/ota-0310-before/`，未宣称已执行完整回滚演练。

## 构建和验证

- 既有ESP-IDF5.5.6环境下，执行 `xtensa-esp32s3-elf-g++.exe -std=c++17 -fsyntax-only tests/notification_cues_check.cpp`：通过。编译期static_assert直接调用固件共用策略，验证8秒全局、20秒计时/提醒、60秒错误的边界，三种音符方向/时长和固定缓冲上限。不是另写一套镜像策略测试。
- 执行 `python <IDF>/tools/idf.py -C firmware/assistant_espidf -B <项目>/build_official build`：退出0，编译/链接/镜像/分区大小通过；仍有既有LVGL API弃用与CMake警告，无编译错误。未安装依赖。
- `python -m unittest tests.test_persona_removed tests.test_screen_feedback tests.test_desktop_ota tests.test_console_configuration tests.test_gateway_v1 tests.test_pairing_persistence tests.test_assistant_core tests.test_chat_pipeline tests.test_gmail_api tests.test_llm_providers -q`：**89项通过，33.253秒**。全部隔离临时目录/模拟服务，不访问真实账号。新增人格移除测试验证旧配置无效、旧损坏偏好不加载、接口404、能力消失、提示词不含预设/彩蛋并保留执行安全规则。
- `node tests/console_configuration_ui.mjs`：通过，设置页4张实际表单，保留模型/音色/搜索/配置操作检查；`node --check web/app.js`、`web/console_settings.js`、`web/admin_console.js`：退出0。
- AST与HTML字面ID核对、18个目标文件 `git diff --no-index --check`：通过、0空白诊断；保留既有换行风格。确认已无Thinking/Executing触发start_chime路径和人格UI/API运行引用。
- 发布前解析app_desc魔数、版本0.3.10、尺寸和SHA，原子替换固定OTA下载工件；构建/发布副本/下载文件三份哈希相同。
- 实机网络日志：0.3.9握手→推送0.3.10→下载200→0.3.10重连；本机管理员接口回读同一设备在线、绑定哈希一致、人格接口404。未创建真实用户计时/提醒，未读真实业务内容。

| 固件工件 | 值 |
| --- | --- |
| 大小 | 2581968字节，OTA分区剩余约18% |
| SHA-256 | `1f7de3d3d3e0c24efa389bff063a59d120b7a453985371fd78e6b04445b1a248` |
| 构建 | `build_official/assistant_espidf.bin` |
| 固定发布 | `firmware/releases/0.3.10/assistant_espidf.bin`、`manifest.json` |
| OTA接口读文件 | `firmware/assistant_espidf/build/assistant_espidf.bin` |

证据：`evidence/ota-0310-build.log`、`ota-0310-backend-checks.log`、`ota-0310-cue-policy-check.log`、`ota-0310-artifact.json`、`ota-0310-binding-check.json`、`ota-0310-device-readback.json`；新网关日志 `runtime/ota-0310-gateway.stderr.log` / stdout。

## 未完成验收和外部状态

OTA和后端加载已实际完成；自由点云观感/帧预算、三种提示音实机声压/音色、活动语音让路/尾音屏蔽与长时间多事件限频需要牛牛现场体验，不能由构建和协议回读替代。未把真人未听过的声音写成“听感已通过”。当前没有真实第三方授权、发送、写内容或执行家电动作；没有改绑定凭据、串口擦除、Git写操作、提交、外部上传或公开发布。
