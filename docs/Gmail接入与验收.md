# Gmail V1 接入与验收

## 已交付接口

`gmail_service.py` 提供 `GmailService(config, aiohttp_client, runtime_dir, token_vault)`：

- `status()`：返回 `configured`、`connected`、`authorized`、授权后的 `email`、`send_enabled` 和固定 `redirect_uri`，没有配置或没有令牌时不返回凭据。
- `start_authorization()` / `complete_authorization(state, code)`：Google 官方 OAuth2 网页应用流程，固定配置的回调地址，PKCE S256，一次性 state 五分钟过期。仅接受 HTTPS 或 loopback HTTP 回调地址，不从请求 Host 推导地址。
- 令牌默认使用 Windows DPAPI 加密落盘到运行目录；非 Windows 明确不可用。测试可注入 `MemoryTokenVault`。`disconnect()` 清除令牌文件。
- `search(query, page_token, limit)`：limit 强制 1–20，返回 `items` 与 `next_page_token`，不做全量分页。
- `read_message(id)`：输出标题、发件人、收件人、日期、thread_id、正文和附件元数据。正文优先 `text/plain`，HTML 仅作文本 fallback，最多 12000 字符，返回 `body_untrusted: true`；不下载附件。
- `list_drafts` / `get_draft` / `create_draft` / `update_draft`：使用标准 `email.message.EmailMessage` MIME；校验收件地址与所有 header 换行，回复草稿可从源邮件生成 `In-Reply-To`、`References`、`threadId`。更新草稿使待发送确认失效。
- `prepare_send(draft_id)`：返回当前草稿摘要（含 To/Cc/Bcc、主题和完整纯文本正文）、`revision`/raw SHA-256、短期一次性 `confirmation_id`。为避免误发，含附件、HTML-only、正文截断或无法完整预览的外部草稿会拒绝进入确认流程；更新同样拒绝覆盖此类草稿。
- `confirm_send(draft_id, confirmation_id[, raw_sha256])`：只供认证面板调用；要求 `gmail_send_enabled=true`、确认未过期且草稿 hash 未改变。发送尝试写入 `gmail_send_attempts.json`，超时记录 `UNKNOWN`，不自动重试；重启后同草稿 hash 也不会重复发送。结果状态为 `sent`、`unknown` 或 `already_sent`。
- 发送接口对 3xx、无效 JSON、HTTP 200 缺少 message id 一律按未知失败处理，不标记 `SENT`；`disconnect()`/`reconfigure()` 会递增授权 epoch，阻止旧回调在重新配置后复活。

Google API 和令牌端点均为代码固定 URL，HTTP 请求有 15 秒超时。日志只保留内部堆栈和状态，不记录 client secret、授权 code、令牌或邮件全文。

## 语音工具边界

`gmail_tools.py` 导出原生 function-call `TOOL_SCHEMAS` 与 `execute_tool`，包含搜索、读邮件、列出/读取/创建/更新草稿六个工具，不包含发送或发送确认。创建和更新仅由上层在确认用户明确请求后调用；邮件正文始终按不可信数据处理。当前不是完整 MCP 协议实现，后续 MCP 可复用同一 service。

## 本地验证

执行：

```text
python -m unittest tests.test_gmail_service -v
```

结果：6 个 mock 测试通过，覆盖 PKCE/state 一次性校验、搜索上限、正文不可信标记、header injection 拒绝、MIME 草稿、附件/坏 ledger 拒绝、raw 变更确认失效、超时 UNKNOWN 和缺少 message id 不标记 SENT。测试使用注入令牌保管器与假 HTTP 客户端，不连接真实 Gmail。

## 尚未验证

尚未进行真实 Google 授权、真实邮件读取、真实草稿创建或发送；OAuth 回调和面板已接入，当前缺少尚未创建的Google项目及OAuth凭据，需完成下述步骤再验收。新语音工具已在模拟实时会话中注册，尚未核对真实模型调用Gmail工具的效果。生产环境使用 Windows DPAPI，不能把 `MemoryTokenVault` 用于生产。当前为原生Function Calling接入，没有新增独立MCP协议服务端。

集成完成后的实际验证：`python -m unittest discover -s tests -v` 共44项通过；`node --check web/gmail.js`、`node --check web/app.js` 以及新增Python模块编译检查通过。最后的发件人头和空threadId修正后，6项Gmail服务测试再次通过。面板API测试实际验证Windows DPAPI加密读写（模拟凭据）、未认证401、ngrok转发不能提交OAuth凭据、回调state和模型无发送工具。

助手已重启加载Gmail模块，硬件已重连。本地及当前ngrok HTTPS地址的Gmail状态接口均通过未认证401和认证200检查，无Google配置时安全返回未配置、未授权、发送关闭。浏览器实际核对了Gmail区域、OAuth表单、固定回调和发送开关；尚未在浏览器提交Google凭据。没有新增依赖、Git提交、推送或正式发布。验证记录：`evidence/gmail-v1-verification-20261029.json`。

## Google Cloud 准备边界

牛牛当前没有 Google Cloud OAuth 应用，先按以下步骤准备（这些账号操作尚未代为执行）：

1. 打开 https://console.cloud.google.com/ ，新建项目，例如“小燕子个人助手测试”。
2. 在“API 和服务 → 库”搜索 Gmail API 并启用。
3. 在 Google Auth Platform／OAuth 同意屏幕完成应用名称、支持邮箱、开发者联系邮箱；个人测试选择 External，发布状态保持 Testing，并把自己的 Gmail 地址加入 Test users。组织账号是否能选 Internal 取决于 Workspace 管理策略。
4. 在 Data Access／权限范围中选择 `https://www.googleapis.com/auth/gmail.readonly` 和 `https://www.googleapis.com/auth/gmail.compose`。
5. 在 Clients／凭据中新建 OAuth 客户端，应用类型选择“Web 应用”。“已获授权的重定向 URI”精确填写 `http://127.0.0.1:8765/gmail/oauth/callback`，不能填成 `/api/gmail/callback`，不能漏端口或改主机名。
6. 打开本机 http://127.0.0.1:8765 ，用现有助手设备令牌连接。在 Gmail 管理 → OAuth 配置中填写 Client ID、Client Secret，回调地址保持上述值。先不勾选“允许发送”。这些值只在本机面板填写，不发送到聊天。
7. 保存配置，点击“授权 Gmail”，在 Google 官方页面选择测试账号并授权。浏览器回到本机后，关闭结果页，在面板点击“刷新状态”。Client ID/Secret 不是 Gmail 密码，不需要提供 Gmail 密码。
8. 授权后先搜索并读取一封自己选定的邮件，再请求小燕子按明确收件地址创建草稿，并在 Gmail 中核对。真实邮件读取、草稿写入均需要这一步授权；尚不能从模拟测试推定成功。

不需要为个人本机回调配置 ngrok。若以后明确改用公网 HTTPS 回调，必须在 Google 与本机配置同时登记完全一致的地址，不能直接复用其它应用的 OAuth Client ID。若 Google 返回 `redirect_uri_mismatch`，检查第5步；`access_denied` 检查测试账号、应用状态及组织策略。Google 页面若要求额外审核或出现安全限制，应按其流程处理，不绕过。

网页应用本机配置和“允许发送”开关只接受 loopback 请求；通过 ngrok 进入面板可查看授权状态、邮件和草稿，但不能提交 Client Secret 或修改发送开关。凭据和邮箱授权令牌均用当前 Windows 用户的 DPAPI 加密保存于被忽略的 runtime；复制到其它用户或云服务器不能直接解密。

当你要求“读某封邮件／总结”时，该邮件的受限文本会作为工具结果进入当前语音模型以回答请求；没有后台全邮箱同步。Google 的授权并不自动完成对所用模型服务的数据处理条款审核，正式商业发布需另行确认。

草稿保存会同步写入 Gmail，不会自动发送。发送需本机显式开启开关，再在面板点击“预览发送”和“确认发送”；模型无发送工具。确认会展示 To、Cc、Bcc、主题和正文；首版面板仅直接编辑 To/主题/正文，已存在的Cc/Bcc保留并展示，工具/API可设置Cc/Bcc。附件、正文截断和无法完整预览的富文本草稿拒绝在助手内修改或发送，请到 Gmail 处理。发送后只能确认 Gmail 接口已接受，不能承诺收件人已收到。

每页搜索最多20项，正文最多12000字符。令牌和发送记录绑定当前单设备原型；这不是已完成多用户隔离的商用邮箱服务。发送记录本地上限1000条，达到上限停止发送并提示人工归档；UNKNOWN记录不自动重试或静默清除。

首次接入需要在 Google Cloud 新建项目，启用 Gmail API，创建“网页应用”OAuth client，并把固定回调地址登记为 HTTPS 或本机 loopback。开发阶段可使用 OAuth consent screen 的 Testing 状态和测试账号；Google 对 Testing 项目的 refresh token 可能约 7 天过期，不能把本地测试结果当成长期可用性。

本版本只申请 `gmail.readonly` 与 `gmail.compose`，不申请 `gmail.modify` 或 `mail.google.com`。这些 Gmail scope 属于受限制或敏感权限，面向商业用户公开发布时可能需要 Google verification、隐私政策和安全评估，不能在未完成审核前承诺可商用。当前文档与 mock 测试没有完成任何真实授权或真实邮件副作用。

## 授权回调连接 Google 超时的修复

2026-10-29实际排查：Google项目 `xiaoyanzi-gmail-test`、专用Web客户端、测试账号和两项scope已配置；Client ID/Secret已保存到本机DPAPI配置。用户完成Google页面操作后，助手在交换令牌的 `POST https://oauth2.googleapis.com/token` 阶段超时，原回调统一显示“授权未完成或已过期”，掩盖了网络原因。

本机Windows已有代理 `127.0.0.1:7890`，浏览器使用它访问Google，但aiohttp默认没有使用该系统代理。已新增单一配置键 `gmail_proxy_url`，本机设置为 `http://127.0.0.1:7890`；只对Gmail服务的固定Google端点生效，包含授权交换、刷新和邮件接口，不改变家居和语音连接。不开启全局代理，不关闭TLS证书验证，不修改系统安全设置。其它部署无代理需求时留空。

真实无凭据网络探测：经过该代理，Google令牌端点GET返回404（此端点本应POST），Gmail profile无认证GET返回401，均证明网络可达；这两个结果不代表账号已授权。相关测试命令 `python -m unittest tests.test_gmail_service tests.test_gmail_api -v`：15项通过，覆盖token和邮件请求传递proxy、禁止跟随重定向、无效代理不发送凭据、回调区分网络错误并转义HTML。网络失败后不能刷新原回调页，需从面板重新发起一次OAuth。
