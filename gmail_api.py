"""Gmail 面板边界：认证操作、固定 OAuth 回调与独立的发送确认。"""

import asyncio
import html
import ipaddress
import logging
from pathlib import Path
from urllib.parse import urlsplit

from aiohttp import web

from assistant_runtime import RUNTIME_KEY
from gmail_service import DpapiTokenVault, GmailError, GmailService

LOGGER = logging.getLogger("assistant")
DEFAULT_REDIRECT_URI = "http://127.0.0.1:8765/gmail/oauth/callback"
# [人工决策-2026-10-29 22:38:43] 牛牛要求先接入Gmail；发送独立于读信/草稿工具，仅由面板确认当前内容后执行。
SETTING_KEYS = ("gmail_client_id", "gmail_client_secret", "gmail_redirect_uri", "gmail_send_enabled", "gmail_proxy_url")


def create_gmail_service(config, client, runtime_dir):
    settings = {key: config[key] for key in SETTING_KEYS if key in config}
    settings["gmail_proxy_url"] = config.get("gmail_proxy_url", "")
    settings.setdefault("gmail_redirect_uri", DEFAULT_REDIRECT_URI)
    path = Path(runtime_dir) / "gmail_settings.dpapi"
    if path.exists():
        try:
            stored = DpapiTokenVault(path).load()
            if stored:
                settings.update({key: stored[key] for key in SETTING_KEYS if key in stored and config.get("_config_sources", {}).get(key) != "environment"})
                config.setdefault("_config_sources", {}).update({key: "secure_override" for key in stored if key in SETTING_KEYS and config.get("_config_sources", {}).get(key) != "environment"})
        except (GmailError, RuntimeError):
            LOGGER.exception("gmail_settings_load_failed")
            settings = {"gmail_redirect_uri": DEFAULT_REDIRECT_URI}
    config.update({key: value for key, value in settings.items() if key in SETTING_KEYS})
    return GmailService(settings, client, runtime_dir)


def _public_status(service):
    return {**{key: value for key, value in service.status().items() if key != "email"}, "redirect_uri": service.config.get("gmail_redirect_uri", DEFAULT_REDIRECT_URI)}


def _require_local(request):
    try:
        host = urlsplit("http://" + request.host).hostname
        peer_is_local = ipaddress.ip_address(request.remote or "").is_loopback
        host_is_local = host == "localhost" or ipaddress.ip_address(host or "").is_loopback
    except ValueError:
        host_is_local = host == "localhost" if "host" in locals() else False
        peer_is_local = request.remote in ("127.0.0.1", "::1")
    if not peer_is_local or not host_is_local or any(name in request.headers for name in ("Forwarded", "X-Forwarded-For", "X-Forwarded-Host", "X-Forwarded-Proto")):
        raise web.HTTPForbidden(text="请在本机 http://127.0.0.1:8765 配置 Gmail 凭据与发送权限")


def _validate_settings(payload, current):
    if not isinstance(payload, dict):
        raise ValueError("配置必须为对象")
    settings = dict(current)
    if set(payload) - {"client_id", "client_secret", "redirect_uri", "send_enabled", "clear_fields", "proxy_url"}:
        raise ValueError("Gmail 配置包含未允许的字段")
    clear = payload.get("clear_fields", [])
    if not isinstance(clear, list) or any(key not in {"client_id", "client_secret", "proxy_url"} for key in clear):
        raise ValueError("Gmail clear_fields 只允许明确清除客户端 ID 或 Secret")
    for field, key in (("client_id", "gmail_client_id"), ("client_secret", "gmail_client_secret")):
        value = payload.get(field, "")
        if not isinstance(value, str) or len(value) > 2048 or "\r" in value or "\n" in value:
            raise ValueError("OAuth 凭据格式无效")
        if value.strip():
            settings[key] = value.strip()
        if field in clear:
            if value.strip():
                raise ValueError("不能同时填写并清除同一字段")
            settings[key] = ""
    uri = payload.get("redirect_uri") or settings.get("gmail_redirect_uri", DEFAULT_REDIRECT_URI)
    if not isinstance(uri, str) or len(uri) > 2048:
        raise ValueError("OAuth 回调地址无效")
    parsed = urlsplit(uri)
    if parsed.path != "/gmail/oauth/callback" or parsed.query or parsed.fragment or parsed.username or parsed.password:
        raise ValueError("OAuth 回调路径必须为 /gmail/oauth/callback")
    if parsed.scheme != "https" and not (parsed.scheme == "http" and parsed.hostname in ("127.0.0.1", "localhost", "::1")):
        raise ValueError("OAuth 回调需 HTTPS 或本机回环 HTTP 地址")
    if not parsed.hostname or "\r" in uri or "\n" in uri:
        raise ValueError("OAuth 回调地址无效")
    settings["gmail_redirect_uri"] = uri
    proxy = payload.get("proxy_url", "")
    if not isinstance(proxy, str):
        raise ValueError("代理地址必须为字符串")
    if proxy:
        parsed_proxy = urlsplit(proxy)
        if len(proxy) > 2048 or any(character.isspace() for character in proxy) or parsed_proxy.scheme not in {"http", "https"} or not parsed_proxy.hostname or parsed_proxy.username or parsed_proxy.password or parsed_proxy.query or parsed_proxy.fragment:
            raise ValueError("代理地址需为无凭据和查询参数的 HTTP(S) 地址")
        settings["gmail_proxy_url"] = proxy
    if "proxy_url" in clear:
        if proxy:
            raise ValueError("不能同时填写并清除代理地址")
        settings["gmail_proxy_url"] = ""
    if "send_enabled" in payload:
        if type(payload["send_enabled"]) is not bool:
            raise ValueError("发送权限必须为布尔值")
        settings["gmail_send_enabled"] = payload["send_enabled"]
    settings.setdefault("gmail_send_enabled", False)
    if not clear and (not settings.get("gmail_client_id") or not settings.get("gmail_client_secret")):
        raise ValueError("请填写 Google Web 应用的 Client ID 和 Client Secret")
    return settings


async def gmail_panel(request):
    from control_api import body_object

    runtime = request.app[RUNTIME_KEY]
    service = runtime.gmail
    operation = {"gmail_drafts_create": "gmail_drafts", "gmail_draft_update": "gmail_draft", "gmail_connector_oauth_start": "gmail_oauth_start", "gmail_connector_status": "gmail_status", "gmail_connector_disconnect": "gmail_disconnect"}.get(request.match_info.route.name, request.match_info.route.name)
    try:
        if operation == "gmail_status":
            return web.json_response(_public_status(service))
        if operation == "gmail_config":
            _require_local(request)
            payload = await body_object(request)
            async with runtime.gmail_config_lock:
                current = runtime.gmail
                settings = _validate_settings(payload, current.config)
                fields = {"client_id": "gmail_client_id", "client_secret": "gmail_client_secret", "redirect_uri": "gmail_redirect_uri", "send_enabled": "gmail_send_enabled", "proxy_url": "gmail_proxy_url"}
                supplied = {fields[key] for key, value in payload.items() if key in fields and value not in ("", None)}
                supplied.update(fields[key] for key in payload.get("clear_fields", []) if key in fields)
                if any(runtime.config.get("_config_sources", {}).get(key) == "environment" for key in supplied):
                    raise ValueError("该 Gmail 字段由环境变量管理，不能从后台覆盖")
                settings_path = Path(runtime.gmail_runtime_dir) / "gmail_settings.dpapi"
                vault = DpapiTokenVault(settings_path)
                previous = await asyncio.to_thread(vault.load)
                await asyncio.to_thread(vault.save, {key: value for key, value in settings.items() if runtime.config.get("_config_sources", {}).get(key) != "environment"})
                if settings != current.config:
                    try:
                        await current.reconfigure(settings)
                    except Exception:
                        LOGGER.exception("gmail_configuration_apply_failed")
                        if previous is None:
                            await asyncio.to_thread(vault.clear)
                        else:
                            await asyncio.to_thread(vault.save, previous)
                        raise
                runtime.config.update({key: settings[key] for key in SETTING_KEYS if key in settings})
                runtime.config.setdefault("_config_sources", {}).update({key: "secure_override" for key in SETTING_KEYS if key in settings and runtime.config.get("_config_sources", {}).get(key) != "environment"})
            return web.json_response({"saved": True, "configured": current.configured, "authorized": current.status()["authorized"], "effective": "hot_reload", "message": "配置已保存；变更客户端配置会清除旧授权，请重新完成 OAuth"})
        if operation == "gmail_oauth_start":
            return web.json_response(service.start_authorization())
        if operation == "gmail_disconnect":
            await service.disconnect()
            return web.json_response(_public_status(service))
        if operation == "gmail_messages":
            return web.json_response(await service.search(request.query.get("q", ""), request.query.get("page_token"), int(request.query.get("limit", "20"))))
        if operation == "gmail_message":
            return web.json_response(await service.read_message(request.match_info["message_id"]))
        if operation == "gmail_drafts" and request.method == "GET":
            return web.json_response(await service.list_drafts(request.query.get("page_token"), int(request.query.get("limit", "20"))))
        if operation == "gmail_draft" and request.method == "GET":
            return web.json_response(await service.get_draft(request.match_info["draft_id"]))
        payload = await body_object(request)
        if operation in ("gmail_drafts", "gmail_draft"):
            arguments = {key: payload.get(key) for key in ("to", "subject", "body", "cc", "bcc", "reply_to_message_id")}
            if operation == "gmail_drafts":
                result = await service.create_draft(**arguments)
            else:
                result = await service.update_draft(request.match_info["draft_id"], **arguments, revision=payload.get("revision"))
            return web.json_response(result)
        if operation == "gmail_prepare_send":
            return web.json_response(await service.prepare_send(request.match_info["draft_id"]))
        if operation == "gmail_send":
            return web.json_response(await service.confirm_send(request.match_info["draft_id"], payload.get("confirmation_id")))
        raise web.HTTPNotFound()
    except GmailError as error:
        LOGGER.exception("gmail_panel_operation_failed operation=%s", operation)
        return web.json_response({"error": {"code": "gmail_unavailable", "message": str(error)}}, status=409)


async def gmail_callback(request):
    service = request.app[RUNTIME_KEY].gmail
    try:
        if request.query.get("error"):
            raise GmailError("Google授权未完成，请回到面板重新发起")
        await service.complete_authorization(request.query.get("state", ""), request.query.get("code", ""))
        message = "Gmail 授权已完成。请关闭此页，回到助手面板点击刷新。"
        status = 200
    except GmailError as error:
        LOGGER.exception("gmail_oauth_callback_failed")
        message = "Gmail 授权未完成：" + str(error) + "。请回到助手面板重新发起授权，不要刷新旧回调页面。"
        status = 400
    except ValueError:
        LOGGER.exception("gmail_oauth_callback_failed")
        message = "Gmail 授权回调格式无效。请回到助手面板重新发起授权。"
        status = 400
    return web.Response(text="<!doctype html><html lang='zh-CN'><meta charset='utf-8'><title>Gmail授权</title><p>" + html.escape(message) + "</p></html>", content_type="text/html", status=status, headers={"Cache-Control": "no-store", "Referrer-Policy": "no-referrer", "Content-Security-Policy": "default-src 'none'; frame-ancestors 'none'"})


def register_gmail_routes(app):
    for method, path, name in (
        ("GET", "/api/gmail/status", "gmail_status"),
        ("GET", "/api/connectors/gmail/status", "gmail_connector_status"),
        ("POST", "/api/gmail/config", "gmail_config"),
        ("POST", "/api/gmail/oauth/start", "gmail_oauth_start"),
        ("POST", "/api/connectors/gmail/oauth/start", "gmail_connector_oauth_start"),
        ("POST", "/api/gmail/disconnect", "gmail_disconnect"),
        ("POST", "/api/connectors/gmail/disconnect", "gmail_connector_disconnect"),
        ("GET", "/api/gmail/messages", "gmail_messages"),
        ("GET", "/api/gmail/messages/{message_id}", "gmail_message"),
        ("GET", "/api/gmail/drafts", "gmail_drafts"),
        ("POST", "/api/gmail/drafts", "gmail_drafts_create"),
        ("GET", "/api/gmail/drafts/{draft_id}", "gmail_draft"),
        ("PUT", "/api/gmail/drafts/{draft_id}", "gmail_draft_update"),
        ("POST", "/api/gmail/drafts/{draft_id}/prepare-send", "gmail_prepare_send"),
        ("POST", "/api/gmail/drafts/{draft_id}/send", "gmail_send"),
    ):
        app.router.add_route(method, path, gmail_panel, name=name)
    app.router.add_get("/gmail/oauth/callback", gmail_callback)
