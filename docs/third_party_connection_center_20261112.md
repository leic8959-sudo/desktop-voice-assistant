# 第三方连接中心（2026-11-12，本地源码交付）

## 本轮改变

沿用既有连接器注册表、管理员认证、Windows DPAPI 配置、TokenRefreshService、ConfirmationStore 与审计。入口仍为控制台的“连接器中心” `/static/connectors.html`。未新建独立服务，未安装依赖，未修改固件或冻结运行候选，未发布。

连接状态分为：未配置／待授权、已配置但待检查、可用、令牌到期或权限不足、连接错误、已撤销访问。可用只来自管理员主动的只读检查，有效期15分钟，进程重启后重新检查。保存配置、变更访问许可、令牌或工具定义都会使相关可用证据失效；检查失败立即替换旧成功状态。配置与检查串行，避免把旧凭据检查结果记到新配置上。检查成功只证明本次读取权限，不推断全部写入范围或物理设备可控。

日历（Google／Microsoft）、Home Assistant、Todoist 提供明确参数定义与服务器验证。日期必须携带时区；列表有数量、游标和响应大小边界。原生日历与 Todoist HTTP 响应最多512 KiB，注册表结果最多256 KiB，参数最多64 KiB。Todoist 改用官方 API v1，列表采用 `results`／`next_cursor`；筛选采用 `/tasks/filter?query=...`。不把截断列表称作完整召回。旧连接器允许继续沿用既有协议，逐步补参数定义。

通用连接器语音入口只公布最近检查成功、未停用且已允许的只读动作，带具体参数定义。模型不能自己提供确认 ID 来执行写入。原有家居设备与电脑控制仍遵循其既有独立授权和意图校验；本轮没有改变它们的人工决策或控制语义。

控制台写入先生成预览，再由管理员点击确认。需要确认由 `requires_confirmation` 或任何写入／发送／设备控制权限共同判定，外部声明不能自行取消。确认绑定管理员、动作和规范化 JSON 参数，单次消费，重复请求只返回已有结果。配置、授权变化会作废待执行确认。关键写入没有自动重试；超时或结果不明时先到提供商核查。

## 配置、授权与撤销

1. 在现有连接中心完成管理员登录。
2. 填写连接器配置并安全保存。秘密不回显，空输入保留，明确勾选清除才清除。远程秘密提交必须 HTTPS；本机只接受直连 loopback。
3. Google／Microsoft 日历在同一卡片展开“授权凭据与续期”，复用现有 TokenRefreshService。先在提供商完成真实授权，再填写客户端 ID、Access Token、Refresh Token 和带时区到期时间。Microsoft 日历与邮件仍共享配置。可生成续期预览，确认后请求提供商续期，再重新检查日历连接。
4. Gmail 沿用既有浏览器 OAuth 和独立发送预览确认，不宣称其他提供商已实现通用浏览器 OAuth。
5. 主动点击“执行只读连接检查”。页面加载只读取本地配置／授权快照，不自动请求远端业务或付费查询接口。高德检查仍由管理员选择城市主动执行。
6. 展开具体动作填写参数；修改类动作点击生成预览，核对后再确认。家居连接检查只读取平台可达性，设备许可继续通过既有家居管理完成。
7. “撤销小燕子访问”持久保存本地禁用状态并作废待执行确认。重新允许访问后必须重新检查；MCP 能力必须重新逐项允许。撤销也拦截原有 Gmail／家居 API、语音调用和相关后台直连读取入口。

本地撤销不等于提供商账号远端撤销：安全存储中的凭据会保留，提供商令牌／授权仍需管理员在提供商账号设置中另行撤销；凭据清除按既有表单与提供商流程处理。已经发出的外部操作无法撤回，需核对提供商实际结果。进程内确认适用于当前单实例家庭部署，重启后确认失效；没有宣称分布式幂等。

## 外部 MCP 服务支持范围

首版支持管理员设置的**一个固定 Streamable HTTP 服务**，使用已有 aiohttp，无完整 MCP SDK、任意动态插件或模型自选 URL／请求头。支持协议版本 `2025-06-18` 与 `2025-03-26` 的初始化／版本协商、初始化完成通知、工具列表、工具调用、JSON 与 SSE 请求响应、会话 ID、后续协议版本请求头、DELETE 会话清理。不实现 stdio、旧 HTTP+SSE 传输、主动 GET 事件流、事件重放、自动会话重试、资源／提示词、采样、elicitation、交互式 MCP OAuth。

地址必须无用户名密码、查询串或 fragment。远端服务必须 HTTPS，本机 loopback 可 HTTP；家用局域网 HTTP 服务请先配置 HTTPS 反向代理。不跟随重定向。可设置管理员持有的 Bearer Token，仅服务端保存；需要交互式 OAuth 且不能使用受支持服务令牌的对端当前不可接入。不能把 URL 或 Authorization 交给模型决定。

每次操作新建并清理会话；工具列表最多64项、8页，每次响应最多256 KiB，单工具参数定义最多32 KiB。调用前在同一会话重新读取工具列表核对定义，改变后拒绝执行，不自动重放。服务会话404或网络错误要求管理员重新检查；写操作不重试。DELETE 404／405视为对端不支持或会话已结束；其他清理失败记录日志。

MCP 参数定义首版只接受基本 JSON Schema 子集：object、array、string、boolean、integer、number、null，properties、required、additionalProperties、items、enum、说明／标题／default、数量／长度／数值边界及 date-time。拒绝 `$ref`、组合 schema、其他 format 等未支持定义；不能宣称完整 JSON Schema 支持。数量、大小和深度还受本地硬边界约束。若列表中某项不兼容，检查失败并说明原因，不悄悄批准部分清单。

操作步骤：保存服务地址／令牌 → 主动检查 → 展开“允许外部能力” → 默认全部不允许 → 管理员逐项选择“允许只读”或“每次预览确认” → 保存许可。外部 `readOnlyHint` 等 annotations 不授予权限，未知工具默认不允许；只有管理员明确批准为只读才进入语音工具。能力批准绑定当前看到的清单版本、工具名称／说明／参数定义、固定地址及凭据身份，避免旧清单审批新定义。改地址或凭据会清除批准；工具定义变更后需检查并重新允许。外部返回标记为不可信内容。

## 验证状态与建议命令

本任务实现者执行了源码读取、公开协议原文核对与 `git diff --check`（退出0；仅有仓库换行符提示）。按本任务授权边界，未自行执行构建、测试、类型检查或启动长期服务。

新增离线回归源码 `tests/test_connection_center.py`，覆盖配置／检查／TTL／失败状态、检查与保存串行、撤销持久和旧预览作废、参数绑定和重复确认、写权限不能取消确认、旧 API 停用、MCP 初始化／协商／JSON／SSE／会话清理、只读提示不自动授权、客户端清单版本审批、单次确认调用、定义与凭据变化、大小与数量限制、畸形参数定义。仅使用临时目录、虚构凭据和 loopback 模拟服务。

由父任务／获授权操作者执行：

```powershell
python -m unittest tests.test_connection_center tests.test_console_configuration
python -m unittest tests.test_natural_barge_in tests.test_wake_ack tests.test_wake_clip
Get-Content -Raw web/connectors.js | node --input-type=module --check
node tests/console_configuration_ui.mjs
```

上述是建议命令，不能据此推断其他验证已经通过。

父任务已执行的隔离验证（本地模拟环境，不代表真实账号验收）：

- `python -B -m unittest tests.test_connection_center tests.test_console_configuration -q`：最终34项通过，包含追加的 MCP 控制台集成用例；不累加重复执行次数。
- `python -B -m unittest tests.test_connection_center.ConnectionCenterTests.test_mcp_discovery_approval_revision_and_confirmed_call_use_one_permission_path -q`：追加的1项独立通过，确认完整检查、清单审批和单次写入路径。
- `python -B -m unittest tests.test_natural_barge_in tests.test_wake_ack tests.test_wake_clip tests.test_self_turn_guard tests.test_input_observation -q`：54项通过。
- `python -B -m unittest tests.test_important_events tests.test_response_progress -q`：最终23项通过。初次两个响应进度用例仍使用没有新权限接口的假注册表，已改为真实注册表和明确可用状态；仍断言模型确认 ID 不能升权，以及外部权限错误保留结果不确定语义。
- JavaScript 模块语法检查：父任务通过输入重定向执行 `node --input-type=module --check`，退出0。
- `node tests/console_configuration_ui.mjs`：最终通过，含旧后端控件门控、MCP 默认不允许／清单版本审批、显式 false、修改参数作废预览、单次确认；初次失败因旧夹具假设“检查一定是最后请求”，已改为核对实际目标请求，并保留刷新行为。
- 本轮范围 `git diff --check`：退出0，仅有仓库 LF／CRLF 提示。

仍未验收：真实 Google／Microsoft／Todoist 账号、Home Assistant 服务与物理设备、真实第三方 MCP 服务、真实模型是否接受和选择全部动态参数定义、浏览器交互布局、外网回调和首次 OAuth 授权、提供商端远程撤销、端云及物理语音闭环。未读取私人运行数据或真实令牌，未进行这些外部操作。

最近已确认的部署入口来自冻结候选 `runtime/release-candidates/natural-barge-in-wakefix-20261109/source`。父任务收尾只读检查未发现 Python gateway／load_and_ota 进程，不能把历史部署入口当作当前在线证据；本轮未启动、停止或切换网关。根源码修改不会自动加载；本交付未重启服务、打包、OTA、上传或发布。后续需父任务审阅和必要隔离验证，再决定维护窗口发布，不应直接把包含并发改动的根工作区整体上线。

根 `web/` 静态资源仍可能由旧网关直接提供，所以本次页面改动可先于后端被看到。新访问许可和授权管理控件以新版后端返回的状态字段门控，旧后端不会显示调用不存在接口的新按钮。根静态资源修改也会使旧启动器的 Web 哈希核对不再匹配；升级应产生新的冻结候选并核对清单，当前不切换候选。MCP 配置卡只是第12个控制台配置入口，未配置时注册表仍为既有11个原生连接器。

`disabled_connectors`、`mcp_approved_tools` 是连接中心管理字段，不从环境变量读取；在安全存储或启动配置边界校验结构，避免字符串被误当成许可列表。其他凭据仍沿用已有环境变量优先级。

## 公开依据（本轮读取）

- MCP 生命周期：<https://modelcontextprotocol.io/specification/2025-06-18/basic/lifecycle>
- MCP Streamable HTTP：<https://modelcontextprotocol.io/specification/2025-06-18/basic/transports>
- MCP 工具与 annotations 信任边界：<https://modelcontextprotocol.io/specification/2025-06-18/server/tools>
- Todoist API v1：<https://developer.todoist.com/api/v1/>
