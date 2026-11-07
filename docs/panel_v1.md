# 面板 v1

## 目标

这是桌面和手机都能用的个人助手面板。首屏展示今日任务、下一件事、对话入口和运行状态；语音技术日志放在折叠诊断区。页面使用原生 HTML、CSS、JavaScript，无第三方依赖，控件最小点击区域为 44px。

## 连接与边界

- `device_token` 只从输入框读取，保存在当前页面内存；不写入 `localStorage`、Cookie，也不放入 URL。
- REST API 请求统一发送 `Authorization: Bearer device_token`。接口失败时展示“离线或不可用”和服务端错误，不生成假任务、假记忆或假用量。
- “连接”按钮只认证 REST API，不自动占用语音会话；首次录音或发送文字时才建立 `/debug/ws`，并等待服务端进入 `listening` 后发送首帧。断开会同时关闭 API 状态和语音会话。
- 页面可连接现有同源 `/debug/ws`：首帧 `{type:"auth",token}`；上行文字 `{type:"text",text}`，录音使用 `listen start/stop` 和 16-bit PCM，打断使用 `{type:"abort"}`；下行 24kHz PCM 直接播放。
- 断开、页面关闭和录音结束时释放 MediaStream tracks、AudioWorklet、音频节点及待播放节点。

## REST 对接

页面使用 root-relative 路径：

- `GET /api/status`：桥接、设备、云端、通知和可选用量。状态轮询仅在页面可见且 API 已认证时运行，间隔 2500ms。
- 状态中的用量按服务端最近一轮返回展示，不当作当天累计；能力字段为 false 时不显示。
- `GET /api/tasks?limit=30`、`POST /api/tasks`、`POST /api/tasks/{id}/complete`、`POST /api/tasks/{id}/cancel`。
- `GET /api/memories?limit=30`、`PUT /api/memories/{key}`、`DELETE /api/memories/{key}`。
- 记忆区提供 key/value 新增表单，使用同一 `PUT /api/memories/{key}` 幂等保存入口。
- `GET /api/home`、`POST /api/home/{alias}`。
- `POST /api/notifications/{id}/ack`。
- `GET /api/voices`、`PUT /api/voice`：音色只使用服务端返回的 `id`，保存结果标记为下次对话生效，不自动创建试听会话。

`datetime-local` 输入按 `Asia/Shanghai`（UTC+08:00）解释并转换为带时区偏移的 ISO 8601 字符串；展示使用 `Asia/Shanghai`。新增任务携带随机 `idempotency_key`。

## 音色选择

连接后加载服务端提供的12个音色，选择并保存；下次云端对话生效，进行中的回答保持原音色。偏好保存在桥接端独立文件，重启可恢复。选择音色和保存不会自动建立云端语音会话，也不会自动试听。

当前`gateway.py`已注册上述REST路由。状态中的未知字段按“未知”显示，避免将HTTP 200或缺省值误认为云端已连接。
