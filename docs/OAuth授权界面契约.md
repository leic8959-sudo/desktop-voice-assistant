# OAuth 授权界面契约

管理面板读取 `GET /api/connectors` 后，可根据以下字段生成授权卡片：

- `configured`：当前连接器是否已配置
- `authorization`：`oauth` 或 `webhook_or_token`
- `oauth_scopes`：供应商需要的权限范围
- `actions`：连接器能力及写入确认要求

当前授权回调仍由各供应商适配器负责，面板不得读取或回显 Access Token。完成正式 OAuth 流程后，只需让适配器写入 `OAuthStore`，连接器调用契约不变。

Gmail 已提供统一入口：`POST /api/connectors/gmail/oauth/start`、`GET /api/connectors/gmail/status` 和 `POST /api/connectors/gmail/disconnect`。
