# 自有桌面助手 V1

## 2026-11-08 仓库源码快照

本仓库包含小燕子的 Python 网关、Web 控制台、ESP32-S3 固件源码、离线测试、工具脚本和项目文档。首次使用与上传范围见 [仓库备份与恢复说明](docs/仓库备份与恢复说明-20261108.md)。

当前源码包含自动记忆修复及项目待办候选；本机最近加载的是独立记忆修复版本，项目待办尚未执行真实数据库迁移，源码快照不代表全部能力已上线或设备验收通过。新环境使用项目待办前，须按 [项目管家交付说明](docs/项目与日常事务管家首轮交付-20261107.md) 显式准备数据库结构。

`config.local.json`、运行数据、私人记忆、账号授权、真实录音、构建目录、下载的 SDK 和固件二进制均不入库。示例配置不含真实凭据。下方按日期排列的内容保留历史记录，其版本和验收结论只适用于相应时间点。

2026-11-02 更新：0.3.7 OTA 已准备，等待设备开机自动升级。待机后的语音新会话先复核完整“你好，小燕子”，普通首句不放行回答或工具；正常唤醒后仍保留30秒连续接话，物理按键单独允许。已验证真实识别正负样本、入口黑盒与OTA文件散列，尚未确认设备已安装。详见[待机唤醒入口复核](docs/待机唤醒入口复核0.3.7.md)。

2026-11-01 更新：多 LLM 供应商接入。新增三供应商注册表——千问 Realtime（原链路，默认）、千问文本、DeepSeek（后两者走 DashScope ASR + chat completions + CosyVoice TTS 网关侧管道），建连失败自动降级到其他已配置供应商；`/api/llm` 与面板"大脑"选择器可切换，偏好存 `runtime/llm-preference.json` 下次会话生效。固件无变化。真实云端冒烟通过（TTS 合成→ASR 逐字转写回环、流式+工具实测可用、`enable_thinking:false` 降低首字延迟）；DeepSeek 待配置密钥后实测。详见[多LLM供应商接入](docs/多LLM供应商接入_20261101.md)。

2026-10-31 更新：ESP-SR 2.4.7 固件已更新。为修复回答被自身回声打断，当前只保留完整“你好，小燕子”主动打断，任意语句自动插话停用；24秒本地播放检查无候选暂停、无确认取消，设备报告缺帧0、溢出0。安装、当前行为与早期记录见[ESP-SR 2.4.7安装与打断验收](docs/ESP_SR247安装与打断验收.md)。

Gmail V1：已接入查询、读取和纯文本草稿的6个语音工具，以及面板 OAuth 配置、Google 授权和独立发送预览确认。默认关闭发送；当前尚无 Google OAuth 凭据，未进行真实邮箱授权或真实邮件操作。[新建Google应用和验收步骤](docs/Gmail接入与验收.md)。原生工具当前复用服务层，不宣称已实现独立 MCP 服务端。

**2026-10-29 当前版本：小燕子固件0.1.7已编译、烧录并通过写入散列校验。** 已对齐240MHz、板端自定义唤醒、独立采音/FIR/AFE、软件播放参考AEC。牛牛已确认“你好，小燕子”可免按键唤醒对话。0.1.6修复QSPI提前复用DMA缓冲；0.1.7移除残余回声VAD误触发中断，改为完整唤醒词中断。牛牛最终确认“回答完整，点云稳定”。静默30秒待机已接入并通过模拟回归，真人唤醒词中断效果仍待专门验收，不宣称自然双讲通过。[音频对齐与AEC实机记录](docs/小燕子音频对齐与AEC实机记录.md)。

点云768粒子主题及早期实现见[点云主题0.1.3](docs/点云主题0.1.3.md)；该页中的帧率是历史实测，不代表当前连续采音时的帧率。

音色入口：[管理面板](http://localhost:8765) → 填写`config.local.json`中的`device_token` → 连接 → 选择音色 → 保存音色。提供12个选项，下一次云端对话生效，当前对话保持原音色。偏好仅写入`runtime/voice-preference.json`，不改写云端密钥配置；刷新页面、重启桥接后保留。默认仍为甜甜（Tina）。Tina、Serena、Zane已通过真实云端会话配置检查，尚未逐个试听全部音色。

音频保留0.1.1修复：取消播报期间的阻塞麦克风读锁竞争，桥接按累计PCM样本时钟下发，终端约100ms起播缓冲。上轮实际6秒/300帧测试：缺帧0、缓冲溢出0；牛牛已反馈播放恢复流畅。详见`evidence/audio-smooth-result-20261029.json`。本轮16项单元/集成测试通过，构建命令`tools/build_firmware.ps1`，烧录及验证见`evidence/ui-voice-result-20261029.json`。

0.1.2实机复核：音频队列载荷移入PSRAM，修复初版启动时偶发内部内存不足；最终连续25秒启动日志无崩溃。重新播放6秒/300帧合成音，缺帧0、缓冲溢出0。正常桥接已恢复。

个人事务核心、独立提醒投递、可编辑/删除记忆、家居执行确认、自有PCM终端协议及管理面板已接入。现有小智设备继续兼容。自有固件工程位于`firmware/assistant_espidf`；早期安装记录见[自有固件0.1.0安装烧录记录](docs/自有固件0.1.0_安装烧录记录.md)。下文原型准备部分是历史快照。

完整交付、运行边界、SDK安装及验收见 [自有助手V1交付与验收](docs/自有助手V1_交付与验收.md)。

米家 M40 临时测试：已补充扫地机器人状态读取、按平台能力开放的清扫/暂停/回充，以及面板和语音工具。独立 Home Assistant Docker 配置、固定版本小米集成安装脚本、ngrok 步骤见 [米家M40本地与ngrok测试](docs/米家M40_本地与ngrok测试.md)。代码通过模拟不代表真实 M40 已接通；需完成官方账号授权和单次动作验收，商业许可另行确认。

空调接入补充：已加入 `xiaomi.airc.h10h00` 的模式、设定温度和状态适配。M40与空调均已通过本地及ngrok公网只读查询；真实动作和语音模型实际调用仍需分别验收。当前配置不自动导入家庭其他设备。

家居通用接入重构：已增加HA设备/房间自动发现、物理设备聚合、页面启停授权、统一能力schema与通用模型工具。现有M40和空调迁移授权，新发现设备默认未启用；新增已支持标准能力的设备无需编辑配置、重启或刷固件。范围与独立验收见 [家居通用接入与独立验收](docs/家居通用接入_独立验收.md)，上面的逐设备接入记录为早期测试历史。

```powershell
python gateway.py
# 电脑打开 http://localhost:8765；连接面板使用config.local.json里的device_token。
# 面板认证不会自动启动云端语音；录音或发消息时才连接。
```

- 个人事务：`assistant_core/`。
- 管理 API：`control_api.py`，统一 Bearer 认证与服务端设备身份。
- 设备协议：`/device/v1/`；旧小智兼容：`/xiaozhi/v1/`。
- 提醒与语音独立，不因结束聊天而丢失；屏幕投递和实际语音播报分开。
- 记忆权威副本当前在电脑 SQLite；SD 是自有固件的有界快照缓存。
- 本轮实际执行本地单元/集成检查、17项兼容回归及浏览器待办交互，详见交付文档。

## 以下为2026-10-26原型准备记录

以下进度是历史快照；配置、云端联通和功能状态以本页顶部及 V1 交付文档为准。

小智协议 v1 设备 / 电脑浏览器 → 本桥接 → 阿里 Realtime(qwen-omni)。
配套方案文档:`../桌面语音助手_AMOLED版_实施与装机测试方案.md`。
当前状态与到货前交付进度见文末"P01~P09 对照"。

## 目录结构

```text
gateway.py            桥接服务:认证、协议适配、会话、工具、看门狗、供应商选择与降级
llm_providers.py      多 LLM 供应商注册表、偏好持久化、统一系统提示与 schema 转换
chat_pipeline.py      DeepSeek/千问文本管道:DashScope 实时ASR → chat.completions → CosyVoice TTS
audio_codec.py        Opus(小智)↔ PCM(阿里)转换
memory_store.py       本机 SQLite 记忆(pc_prototype;SD 卡版本未实现)
web/                  浏览器测试台(录音、文字问答、表情预览)
tools/init_config.py  生成 config.local.json(随机令牌)
tools/preflight.py    离线体检:依赖、配置、编解码、固件散列
tools/local_session_test.py  本地会话模拟测试(不联网、不调用计费接口)
tools/aliyun_probe.py 阿里真实音频探测(可能产生费用,由牛牛主动运行)
tools/chat_pipeline_probe.py 多LLM管道真实冒烟:TTS→ASR回环+兼容接口流式(可能产生费用,由牛牛主动运行)
tools/prepare_sources.py     下载固定版本上游固件与源码证据
firmware/prebuilt/    小智 v2.5.2 两板型固件(SHA-256 已核验)
firmware/flash_tool/  乐鑫 Flash Download Tool
evidence/             上游源码/清单/测试结果/烧录指南/Wiki 存证
```

## 首次安装(由牛牛执行,脚本不自动安装)

1. 安装 Python 3.11+(本机已有 3.12.9);
2. `pip install -r requirements.txt`(aiohttp、av;本机 2026-10-26 已装,重装环境时执行);
3. `python tools/init_config.py --lan-ip <电脑局域网IP>` 生成 `config.local.json`。
   **本机已生成**(2026-10-26,IP `192.168.3.8`),无需重复;换网络/换 IP 后编辑 `config.local.json` 的 `host` 与 `public_base_url` 两处即可,建议在路由器上把电脑 IP 绑定静态。

## 必填配置(config.local.json)

| 字段 | 说明 |
|---|---|
| `aliyun_api_key` | 阿里百炼 API Key(北京) |
| `aliyun_workspace_id` | 北京业务空间 ID |

其余字段已按原型默认值生成;`device_token`/`provisioning_token` 为已生成的随机令牌,浏览器测试台连接时填 `device_token`。

## 启动与验证

```powershell
# 1. 离线体检(不联网、不启动服务)
python tools/preflight.py

# 2. 本地会话模拟测试(内置模拟阿里服务器;17 项,约 30 秒)
python tools/local_session_test.py

# 3. 启动桥接
powershell -File start.ps1        # 或 python gateway.py

# 4. 浏览器测试台
# 打开 http://localhost:8765 → 填 device_token → 连接 → 文字/录音问答

# 5. 阿里真实链路探测(填好阿里配置后;产生费用,主动运行)
python tools/aliyun_probe.py      # 结果存 runtime/probe/reply.wav
```

设备(开发板)侧接入:烧录见 `evidence/FLASHING_GUIDE.md`;OTA 配置地址填
`http://<电脑局域网IP>:8765/xiaozhi/v1/` 前的引导地址 `http://<电脑局域网IP>:8765/xiaozhi/ota/<provisioning_token>`,
或直接在固件里配置 WebSocket 地址 `ws://<电脑局域网IP>:8765/xiaozhi/v1/`(令牌为 device_token)。
防火墙只放行 8765 端口,不关整机防火墙。

## 运行时约束(已实现)

## 第三方连接器

企业控制台 Demo：`http://127.0.0.1:8765/admin`；若当前网关进程尚未重启，也可直接打开 `http://127.0.0.1:8765/static/admin_console_demo.html`。

管理 API 已提供统一连接器入口 `GET /api/connectors` 和 `POST /api/connectors/{connector_id}/{action}`。当前可用连接器包括 Gmail、Google Calendar、Microsoft 365 日历、Microsoft 365 邮件、飞书机器人、Slack、高德地图、Notion、GitHub、Todoist 和 Home Assistant。连接器权限、确认规则和配置项见 [连接器配置](docs/连接器配置.md)。

查询动作可以直接执行；发送邮件、发送飞书消息、创建日程和控制家居设备必须带 `confirmation_id`。第三方凭据可放入本地配置，也可通过 `.env` 中的 `ASSISTANT_*` 环境变量注入。

最有价值的跨服务示例是 `POST /api/workflows/meeting-travel`：查询日历会议地点，自动解析地址并规划驾车路线，详见 [跨服务工作流](docs/跨服务工作流.md)。

语音模型通过 `invoke_connector` 工具调用这些服务，参数包含 `connector_id`、`action` 和 `arguments`。查询动作可以直接执行；发送邮件、创建日程、写入 Notion、创建 GitHub Issue、发送飞书消息和控制家居设备必须先获得用户确认并传入 `confirmation_id`。

语音模型还可以通过 `invoke_workflow` 调用跨服务流程：`meeting-travel`、`meeting-notes`、`github-issue` 和 `todoist-task`。写入型工作流同样必须获得确认。

- 会话最长 600 秒、空闲 60 秒强制结束;并发 ≤4(默认 1);
- 工具仅允许 `remember_fact` / `recall_memory` / `get_home_state` / `set_home_state`(灯/开关 on|off,超时不自动重试写);
- 记忆上限 100 条,存本机 `runtime/memory.sqlite3`(180 天过期),**不是设备 SD 卡**;
- 日志轮转 2MB×3,不记录原始音频与密钥;访问日志关闭(避免令牌入日志);
- 首轮仅一台样机,无多租户;对外销售前必须补 TLS、独立凭据、配额与升级机制(方案 4.2 节)。

## P01~P09 到货前交付对照(2026-10-26)

| 编号 | 状态 | 说明 |
|---|---|---|
| P01 商家确认型号/驱动 | ⬜ 人工 | 询问清单已备好:`evidence/WAVESHARE_WIKI_NOTES.md` 第 2 节(两版驱动差异已从源码核实) |
| P02 原厂示例与恢复资料 | 🔶 部分 | Wiki 要点、原理图/示例链接已存证;PDF 与示例包下载及"原厂固件是否有备份"待人工/商家 |
| P03 烧录包、地址表、版本 | ✅ | 两板型固件 SHA-256 与上游一致;镜像 16MB/DIO/分区表已解析,见 `evidence/FLASHING_GUIDE.md` |
| P04 烧录环境、USB 排查 | ✅ | Flash Download Tool 已下载校验;USB 排查与下载模式操作已写明;实际串口到货后确认 |
| P05 桥接与配置模板 | 🔶 大部分 | 代码完成;本地模拟测试 17/17 通过(`evidence/bridge_local_test.json`);真实阿里链路未测 |
| P06 阿里账号与北京空间 | ⬜ 人工 | 密钥待填入 config.local.json;`tools/aliyun_probe.py` 由牛牛主动运行(产生费用) |
| P07 电脑端语音测试入口 | 🔶 | 浏览器测试台完成,服务端会话逻辑已模拟验证;**麦克风实听未测**(需真人说话,到货前建议自测一轮) |
| P08 单家居设备接入 | ⬜ | 需牛牛提供 Home Assistant 地址、长期令牌与实体 ID,填 config.local.json 后验证 |
| P09 SD 记忆与主题固件 | ⬜ | 未开始;需 ESP-IDF 环境,方案允许在基础联调后交付 |

✅ 完成 / 🔶 部分完成 / ⬜ 未完成或需人工、硬件。**以上不代表云端联通、实机音频或硬件通过。**
