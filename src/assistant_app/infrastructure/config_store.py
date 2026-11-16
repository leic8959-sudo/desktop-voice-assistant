"""连接器配置白名单、校验与安全持久化边界。"""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
from pathlib import Path
from typing import Any, Mapping
from urllib.parse import urlsplit

from gmail_service import DpapiTokenVault, GmailError


CONNECTOR_CONFIG_FIELDS: dict[str, tuple[dict[str, Any], ...]] = {
    "mcp": (
        {"key": "mcp_url", "label": "外部工具服务地址（单个服务）", "secret": False, "help": "Streamable HTTP 的固定地址；仅管理员设置。远程地址需 HTTPS，本机可 HTTP。"},
        {"key": "mcp_token", "label": "服务访问令牌（可选）", "secret": True, "help": "仅服务端加密保存，不回显。需要交互 OAuth 的服务暂不支持。"},
        {"key": "mcp_approved_tools", "label": "允许的能力", "secret": False, "managed_by": "connection_center", "help": "检查连接后在下方逐项允许；外部服务的只读提示不会自动授权。"},
    ),
    "connections": ({"key": "disabled_connectors", "label": "已撤销访问的连接器", "secret": False, "managed_by": "connection_center", "help": "由连接中心的允许访问 / 撤销访问管理。"},),
    "gmail": (
        {"key": "gmail_client_id", "label": "Google OAuth Client ID", "secret": True, "managed_by": "gmail", "help": "沿用 Gmail 专用 DPAPI 配置与 OAuth 回调。"},
        {"key": "gmail_client_secret", "label": "Google OAuth Client Secret", "secret": True, "managed_by": "gmail", "help": "只在本机 Gmail 配置表单提交，不回显。"},
        {"key": "gmail_redirect_uri", "label": "OAuth Redirect URI", "secret": False, "managed_by": "gmail", "help": "必须使用既有 /gmail/oauth/callback 回调。"},
        {"key": "gmail_send_enabled", "label": "允许发送", "secret": False, "managed_by": "gmail", "help": "发送仍需 Gmail 面板预览和既有确认凭证。"},
        {"key": "gmail_proxy_url", "label": "Google 连接代理（可选）", "secret": True, "managed_by": "gmail", "help": "只接受无用户名密码、无查询参数的 HTTP(S) 代理地址；留空保留。"},
    ),
    "amap": ({"key": "amap_web_service_key", "label": "高德 Web 服务 Key", "secret": True, "help": "只能填写高德 Web 服务 Key；地图 JS Key 不支持服务端 POI / 路线接口。"},),
    "google_calendar": ({"key": "google_calendar_access_token", "label": "Google Calendar Access Token", "secret": True, "managed_by": "calendar_oauth", "help": "请到控制台「电脑与媒体」管理日历授权与续期；原有 Access Token 继续兼容读取，避免两个配置入口互相覆盖。"},),
    "microsoft_calendar": ({"key": "microsoft_calendar_access_token", "label": "Microsoft Graph Access Token", "secret": True, "managed_by": "calendar_oauth", "help": "请到「电脑与媒体」统一管理；与 Microsoft 邮件共享授权，需要具备相应日历和邮件权限。"},),
    "microsoft_mail": ({"key": "microsoft_calendar_access_token", "label": "Microsoft Graph Access Token（与日历共享）", "secret": True, "managed_by": "calendar_oauth", "help": "与日历共用「电脑与媒体」中的安全授权配置；不在此重复保存 Token。"},),
    "notion": ({"key": "notion_access_token", "label": "Notion Integration Token", "secret": True, "help": "还需要在 Notion 中把目标页面或数据库分享给该 Integration。"},),
    "github": ({"key": "github_access_token", "label": "GitHub Access Token", "secret": True, "help": "令牌权限由 GitHub 控制；保存成功不代表仓库权限或连接已验证。"},),
    "todoist": ({"key": "todoist_access_token", "label": "Todoist Access Token", "secret": True, "help": "保存成功不代表 Todoist API 当前可用。"},),
    "slack": ({"key": "slack_webhook_url", "label": "Slack Incoming Webhook URL", "secret": True, "help": "Webhook 只有发送动作，本页不发送测试消息。"},),
    "feishu": ({"key": "feishu_webhook_url", "label": "飞书机器人 Webhook URL", "secret": True, "help": "Webhook 只有发送动作，本页不发送测试消息。"},),
    "home_assistant": (
        {"key": "home_assistant_url", "label": "Home Assistant 地址", "secret": False, "help": "只接受不带凭据的 HTTP(S) 地址。"},
        {"key": "home_assistant_token", "label": "Home Assistant Access Token", "secret": True, "help": "等待当前家居操作结束后安全更新控制器并清除旧缓存；保留既有设备授权。"},
        {"key": "home_entities", "label": "实体别名（维护项）", "secret": False, "managed_by": "maintenance", "help": "别名迁移涉及既有人工授权决定，后台只读；设备授权继续使用原家居页面。"},
    ),
    "models": (
        {"key": "aliyun_api_key", "label": "阿里百炼 API Key", "secret": True, "help": "Realtime、语音识别、语音合成、千问文本及搜索共用。"},
        {"key": "aliyun_workspace_id", "label": "阿里百炼北京业务空间 ID", "secret": False, "help": "与 API Key 配套。保存后下次语音会话生效。"},
        {"key": "deepseek_api_key", "label": "DeepSeek API Key", "secret": True, "help": "替换文本推理；语音识别和合成仍需要阿里凭据。"},
        {"key": "aliyun_model", "label": "Realtime 模型 ID", "secret": False, "help": "填写账号实际支持的模型 ID。"},
        {"key": "qwen_chat_model", "label": "千问文本模型 ID", "secret": False, "help": "填写账号实际支持的模型 ID。"},
        {"key": "deepseek_model", "label": "DeepSeek 模型 ID", "secret": False, "help": "填写账号实际支持的模型 ID。"},
        {"key": "chat_tts_voice", "label": "文本模型语音合成音色", "secret": False, "help": "Realtime 音色在音色偏好中选择；这里是文本模型管道的 CosyVoice 音色。"},
        {"key": "qwen_chat_thinking", "label": "千问文本思考参数", "secret": False, "options": [{"id": "false", "name": "关闭"}, {"id": "true", "name": "开启"}, {"id": "null", "name": "不发送此参数"}], "help": "仅支持提供商实际允许的模式；下次会话生效。"},
        {"key": "deepseek_thinking", "label": "DeepSeek 思考参数", "secret": False, "options": [{"id": "disabled", "name": "关闭"}, {"id": "enabled", "name": "开启"}, {"id": "unset", "name": "不发送此参数"}], "help": "不代表当前账号模型已支持或联网验证。"},
        {"key": "chat_max_tokens", "label": "文本模型最大输出 Token", "secret": False, "kind": "integer", "minimum": 128, "maximum": 4096, "help": "128～4096，下次会话生效。"},
        {"key": "chat_turn_silence_ms", "label": "文本模型轮次静音毫秒", "secret": False, "kind": "integer", "minimum": 300, "maximum": 2000, "help": "300～2000 毫秒，下次会话生效。"},
    ),
}

CONFIG_KEYS = frozenset(field["key"] for group, fields in CONNECTOR_CONFIG_FIELDS.items() if group != "gmail" for field in fields)
ENV_KEYS = {key: f"ASSISTANT_{key.upper()}" for key in (CONFIG_KEYS - {"mcp_approved_tools", "disabled_connectors"}) | {"device_token", "provisioning_token", "gmail_client_id", "gmail_client_secret", "gmail_redirect_uri", "gmail_proxy_url", "gmail_send_enabled", "enable_search"}}
_TOKEN_PATTERN = re.compile(r"^[^\s\r\n]{8,8192}$")


class ConfigStoreError(ValueError):
    """配置保存或读取失败。"""


def config_field_metadata(connector_id: str, config: Mapping[str, Any]) -> list[dict[str, Any]]:
    sources = config.get("_config_sources", {}) if isinstance(config.get("_config_sources", {}), dict) else {}
    result = []
    for field in CONNECTOR_CONFIG_FIELDS.get(connector_id, ()):
        key = field["key"]
        source = sources.get(key, "config_file")
        editable = source != "environment" and (not field.get("managed_by") or field.get("managed_by") == "gmail")
        present = bool(config.get(key)) if field["secret"] else key in config
        row = {"key": key, "label": field["label"], "secret": bool(field["secret"]), "help": field["help"], "configured": present, "source": source, "editable": editable, "managed_by": field.get("managed_by")}
        if not field["secret"] and field.get("managed_by") != "connection_center":
            row["value"] = config.get(key, {} if key == "home_entities" else "")
        row.update({name: field[name] for name in ("kind", "minimum", "maximum", "options") if name in field})
        if key == "chat_tts_voice":
            from llm_providers import CHAT_TTS_VOICES
            row["options"] = CHAT_TTS_VOICES
        result.append(row)
    return result


def _validate_url(value: str, key: str) -> str:
    parsed = urlsplit(value)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ConfigStoreError(f"{key} 必须是无凭据的 HTTP(S) 地址")
    return value.rstrip("/")


def validate_connector_values(connector_id: str, values: Mapping[str, Any], current: Mapping[str, Any]) -> dict[str, Any]:
    fields = {field["key"]: field for field in CONNECTOR_CONFIG_FIELDS.get(connector_id, ())}
    unknown = set(values) - set(fields)
    if unknown:
        raise ConfigStoreError("不允许保存未列入白名单的配置字段")
    result: dict[str, Any] = {}
    for key, raw in values.items():
        field = fields[key]
        if field["secret"]:
            if not isinstance(raw, str):
                raise ConfigStoreError(f"{key} 必须是字符串")
            if not raw:
                continue
            if not _TOKEN_PATTERN.fullmatch(raw):
                raise ConfigStoreError(f"{key} 格式无效或长度超限")
            if raw.upper().startswith("REPLACE"):
                raise ConfigStoreError(f"{key} 不能使用示例占位值")
            if key == "amap_web_service_key" and not re.fullmatch(r"[0-9a-fA-F]{32}", raw):
                raise ConfigStoreError("高德 Web 服务 Key 应为32位十六进制字符串；无法从格式判断 Key 类型，请在高德控制台选择 Web 服务")
            if key in {"slack_webhook_url", "feishu_webhook_url"}:
                parsed = urlsplit(raw)
                expected = {"slack_webhook_url": {"hooks.slack.com", "hooks.slack-gov.com"}, "feishu_webhook_url": {"open.feishu.cn", "open.larksuite.com"}}[key]
                prefix = "/services/" if key == "slack_webhook_url" else "/open-apis/bot/v2/hook/"
                if parsed.scheme != "https" or parsed.hostname not in expected or parsed.username or parsed.password or parsed.query or parsed.fragment or not parsed.path.startswith(prefix) or not parsed.path[len(prefix):]:
                    raise ConfigStoreError("Webhook 必须是对应提供商的 HTTPS 机器人地址")
            result[key] = raw
        elif key in {"home_assistant_url", "mcp_url"}:
            if not isinstance(raw, str):
                raise ConfigStoreError(f"{key} 必须是字符串")
            result[key] = _validate_url(raw, key) if raw else ""
            if key == "mcp_url" and raw:
                parsed = urlsplit(raw)
                if parsed.scheme != "https" and parsed.hostname not in {"localhost", "127.0.0.1", "::1"}:
                    raise ConfigStoreError("外部工具的远程地址必须使用 HTTPS；仅本机地址允许 HTTP")
        elif key == "home_entities":
            candidate = raw
            if isinstance(raw, str):
                try:
                    candidate = json.loads(raw)
                except json.JSONDecodeError as error:
                    raise ConfigStoreError("home_entities 必须是合法 JSON 对象") from error
            if not isinstance(candidate, dict) or len(candidate) > 100 or any(not isinstance(alias, str) or not 1 <= len(alias) <= 100 or not isinstance(entity, str) or not re.fullmatch(r"[a-z_]+\.[a-z0-9_]+", entity) for alias, entity in candidate.items()):
                raise ConfigStoreError("home_entities 必须是别名到 entity_id 的对象")
            result[key] = candidate
        elif key == "qwen_chat_thinking":
            normalized = {"true": True, "false": False, "null": None}.get(raw, raw) if isinstance(raw, str) else raw
            if normalized is not None and type(normalized) is not bool:
                raise ConfigStoreError("qwen_chat_thinking 必须为 true / false / null")
            result[key] = normalized
        elif key == "deepseek_thinking":
            if raw not in {"disabled", "enabled", "unset"}:
                raise ConfigStoreError("deepseek_thinking 必须为 disabled / enabled / unset")
            result[key] = raw
        elif field.get("kind") == "integer":
            if type(raw) is not int or not field["minimum"] <= raw <= field["maximum"]:
                raise ConfigStoreError(f"{key} 必须在允许的整数范围内")
            result[key] = raw
        else:
            if not isinstance(raw, str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", raw):
                raise ConfigStoreError(f"{key} 必须是有效的 ID（最多128字符）")
            if key == "chat_tts_voice":
                from llm_providers import CHAT_TTS_VOICE_IDS
                if raw not in CHAT_TTS_VOICE_IDS:
                    raise ConfigStoreError("请选择已支持的 CosyVoice 音色")
            result[key] = raw
    merged = dict(current)
    merged.update(result)
    if connector_id == "home_assistant" and bool(merged.get("home_assistant_url")) != bool(merged.get("home_assistant_token")):
        raise ConfigStoreError("Home Assistant 地址和令牌必须同时配置或同时清除")
    return result


class SecureConfigStore:
    """使用 Windows DPAPI 保存连接器秘密；非 Windows 不降级为明文。"""

    def __init__(self, path: str | os.PathLike[str], vault=None):
        self.path = Path(path)
        self.vault = vault
        self.available = sys.platform == "win32" if vault is None else True

    def _vault(self):
        if not self.available:
            raise ConfigStoreError("当前运行环境没有安全秘密存储；非 Windows 不降级保存为明文")
        if self.vault is None:
            self.vault = DpapiTokenVault(self.path)
        return self.vault

    def load(self) -> dict[str, Any]:
        if not self.available and not self.path.exists():
            return {}
        try:
            value = self._vault().load()
        except (RuntimeError, GmailError, OSError) as error:
            raise ConfigStoreError("安全配置存储不可读取") from error
        if not isinstance(value, dict):
            return {}
        return {key: value[key] for key in value if key in CONFIG_KEYS}

    def save(self, values: Mapping[str, Any], clear: list[str] | tuple[str, ...] = ()) -> None:
        unknown = (set(values) | set(clear)) - CONFIG_KEYS
        if unknown:
            raise ConfigStoreError("不允许保存未列入白名单的配置字段")
        current = self.load()
        current.update(values)
        for key in clear:
            # 空值是显式清除的覆盖标记，防止重启后旧配置文件的秘密重新出现。
            current[key] = {} if key in {"home_entities", "mcp_approved_tools"} else [] if key == "disabled_connectors" else ""
        try:
            self._vault().save(current)
        except (RuntimeError, GmailError, OSError) as error:
            raise ConfigStoreError("安全配置保存失败，旧配置保持不变") from error


PREFERENCE_KEYS = frozenset({"enable_search"})


def load_runtime_preferences(runtime_dir: str | os.PathLike[str]) -> dict[str, Any]:
    path = Path(runtime_dir) / "console-preferences.json"
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigStoreError("后台偏好文件不可读取") from error
    if not isinstance(value, dict):
        raise ConfigStoreError("后台偏好文件格式无效")
    return {key: value[key] for key in value if key in PREFERENCE_KEYS}


def save_runtime_preferences(runtime_dir: str | os.PathLike[str], values: Mapping[str, Any]) -> None:
    if set(values) - PREFERENCE_KEYS or any(type(value) is not bool for value in values.values()):
        raise ConfigStoreError("只允许保存白名单中的布尔型后台偏好")
    directory = Path(runtime_dir)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "console-preferences.json"
    current = load_runtime_preferences(directory)
    current.update(values)
    descriptor, temporary_name = tempfile.mkstemp(prefix="console-preferences.", suffix=".tmp", dir=directory)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(current, stream, ensure_ascii=False, separators=(",", ":"))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def load_secure_overrides(runtime_dir: str | os.PathLike[str]) -> dict[str, Any]:
    path = Path(runtime_dir) / "connector-config.dpapi"
    if not path.exists():
        return {}
    return SecureConfigStore(path).load()


def apply_configuration_overrides(config: dict[str, Any], runtime_dir: str | os.PathLike[str], environment: Mapping[str, str] | None = None) -> None:
    # 所有环境读取集中在本边界；不在 handler 或适配器内重新读取环境。
    environment = os.environ if environment is None else environment
    sources = {key: "config_file" for key in config if not key.startswith("_")}
    for values, source in ((load_runtime_preferences(runtime_dir), "saved_preference"), (load_secure_overrides(runtime_dir), "secure_override")):
        config.update(values)
        sources.update({key: source for key in values})
    for key, variable in ENV_KEYS.items():
        raw = environment.get(variable)
        if not raw:
            continue
        if key in {"enable_search", "gmail_send_enabled"}:
            if raw.lower() not in {"true", "false"}:
                raise ConfigStoreError(f"{variable} 必须为 true 或 false")
            config[key] = raw.lower() == "true"
        elif key in {"qwen_chat_thinking", "deepseek_thinking", "chat_max_tokens", "chat_turn_silence_ms"}:
            value = int(raw) if key in {"chat_max_tokens", "chat_turn_silence_ms"} else raw
            config[key] = validate_connector_values("models", {key: value}, config)[key]
        elif key == "home_entities":
            config[key] = validate_connector_values("home_assistant", {key: raw}, {})[key]
        else:
            config[key] = raw
        sources[key] = "environment"
    config["_config_sources"] = sources
