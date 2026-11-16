"""同源助手面板 API。所有数据接口绑定服务端配置的设备身份。"""

import asyncio
import hashlib
import ipaddress
import json
import logging
import secrets
import time
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import urlsplit
from collections import defaultdict, deque
from pathlib import Path

import aiohttp
from aiohttp import web

from assistant_core.store import IdempotencyConflict, LeaseConflict, NotFoundError
from assistant_runtime import RUNTIME_KEY
from src.assistant_app.adapters.auth import authenticate, extract_bearer
from src.assistant_app.infrastructure.audit import read_recent, record
from src.assistant_app.infrastructure.confirmations import ConfirmationStore
from src.assistant_app.infrastructure.settings import connector_configuration
from src.assistant_app.application.screen_feedback import TOOLS, api_operation
from src.assistant_app.infrastructure.config_store import ConfigStoreError, CONNECTOR_CONFIG_FIELDS, SecureConfigStore, config_field_metadata, save_runtime_preferences, validate_connector_values
from src.assistant_app.integrations import ConnectorContext, PermissionScope
from src.assistant_app.integrations.contracts import action_for
from src.assistant_app.integrations.action_schemas import normalize_arguments, schema_for
from src.assistant_app.integrations.mcp_http import MCPConnector, connection_fingerprint, fingerprint
from src.assistant_app.application.meeting_travel import plan_meeting_travel
from src.assistant_app.application.meeting_notes import save_meeting_notes
from src.assistant_app.application.github_issue import create_action_item
from src.assistant_app.application.todoist_task import create_todoist_task
from src.assistant_app.api.auto_memory import MEMORY_KEY
from src.assistant_app.infrastructure.admin_sessions import PersistentAdminSessions, AdminSessionCapacityError, AdminSessionStoreError, SESSION_TTL_SECONDS

LOGGER = logging.getLogger("assistant")
RATE_LIMIT_KEY = web.AppKey("api_rate_limit", dict)
AUDIT_KEY = web.AppKey("audit_logger", object)
PAIRING_KEY = web.AppKey("pairing_manager", object)
ADMIN_SESSIONS_KEY = web.AppKey("admin_sessions", PersistentAdminSessions)
CONFIRMATIONS_KEY = web.AppKey("action_confirmations", ConfirmationStore)
CONFIG_STORE_KEY = web.AppKey("connector_config_store", SecureConfigStore)
CONFIG_LOCK_KEY = web.AppKey("connector_config_lock", asyncio.Lock)
RATE_LIMIT_MAX = 120
RATE_LIMIT_WINDOW = 60.0


def _rate_limited(request) -> bool:
    buckets = request.app[RATE_LIMIT_KEY]
    key = request.remote or "unknown"
    now = time.monotonic()
    bucket = buckets.setdefault(key, deque())
    while bucket and now - bucket[0] > RATE_LIMIT_WINDOW:
        bucket.popleft()
    if len(bucket) >= RATE_LIMIT_MAX:
        return True
    bucket.append(now)
    if len(buckets) > 1024:
        oldest = min(buckets, key=lambda item: buckets[item][0] if buckets[item] else now)
        buckets.pop(oldest, None)
    return False


def task_item(item):
    # 租约、幂等键和内部设备身份不需要暴露给面板。
    fields = ("title", "status", "due_at", "created_at", "updated_at", "reminder_state")
    return {"id": item.get("id", item.get("task_id")), **{key: item.get(key) for key in fields}}


def memory_item(item):
    return {"id": item.get("id", item.get("memory_id")), **{key: item.get(key) for key in ("key", "value", "updated_at", "expires_at", "source", "category", "confidence")}}


async def body_object(request):
    try:
        payload = await request.json()
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError("请求必须为 JSON") from error
    if not isinstance(payload, dict):
        raise ValueError("请求内容必须为对象")
    return payload


@web.middleware
async def api_boundary(request, handler):
    if not request.path.startswith("/api/"):
        return await handler(request)
    if request.match_info.route.name == "local_admin_login":
        return await handler(request)
    if request.match_info.route.name in {"pairing_device_start", "pairing_device_result"}:
        if _rate_limited(request):
            return web.json_response({"error": {"code": "rate_limited", "message": "配对请求过于频繁，请稍后重试"}}, status=429, headers={"Retry-After": "60"})
        try:
            return await handler(request)
        except (ValueError, TypeError):
            return web.json_response({"error": {"code": "invalid_request", "message": "配对请求格式无效"}}, status=400)
    request_id = request.headers.get("X-Request-ID", "")[:128] or uuid.uuid4().hex
    if _rate_limited(request):
        record(request.app[AUDIT_KEY], action="api_rate_limit", outcome="blocked", request_id=request_id, remote=request.remote)
        return web.json_response({"error": {"code": "rate_limited", "message": "请求过于频繁，请稍后重试"}}, status=429, headers={"Retry-After": "60", "X-Request-ID": request_id})
    runtime = request.app[RUNTIME_KEY]
    origin = request.headers.get("Origin")
    if origin and origin not in (f"http://{request.host}", f"https://{request.host}"):
        return web.json_response({"error": {"code": "origin_rejected", "message": "请从同一服务页面访问"}}, status=403)
    authorization = request.headers.get("Authorization", "")
    session = request.cookies.get("assistant_admin", "")
    if not extract_bearer(authorization) and session and await asyncio.to_thread(request.app[ADMIN_SESSIONS_KEY].valid, session):
        authorization = "Bearer " + runtime.config["device_token"]
    principal = authenticate(authorization, runtime.config["device_token"])
    device_ota = request.match_info.route.name == "device_ota_firmware" and request.app[PAIRING_KEY].authenticate_device(extract_bearer(authorization) or "")
    if (principal is None or not principal.has_role("admin")) and not device_ota:
        record(request.app[AUDIT_KEY], action="api_authentication", outcome="failed", request_id=request_id, remote=request.remote)
        return web.json_response({"error": {"code": "unauthorized", "message": "请先完成管理员认证；当前使用服务配置中的 device_token，六位配对码不能用于登录"}}, status=401, headers={"X-Request-ID": request_id})
    request["principal"] = principal
    request["request_id"] = request_id
    disabled = runtime.config.get("disabled_connectors") or []
    gmail_data = request.path.startswith("/api/gmail/") and request.path not in {"/api/gmail/config", "/api/gmail/status", "/api/gmail/disconnect"}
    if "home_assistant" in disabled and (request.path == "/api/home" or request.path.startswith("/api/home/")) or "gmail" in disabled and gmail_data:
        return web.json_response({"error": {"code": "permission_denied", "message": "该连接器已撤销小燕子访问，请先在连接中心重新允许"}}, status=403)
    screen_operation = api_operation(request.method, request.path, runtime.connector_registry)
    if request.method == "GET" and request.path == "/api/home" and request.query.get("refresh") == "true":
        screen_operation = TOOLS["list_home_devices"]
    screen_feedback_id = uuid.uuid4().hex
    await runtime.operation_screen_feedback(screen_operation, phase="start", feedback_id=screen_feedback_id)
    try:
        response = await handler(request)
    except NotFoundError:
        response = web.json_response({"error": {"code": "not_found", "message": "记录不存在"}}, status=404)
    except (IdempotencyConflict, LeaseConflict) as error:
        response = web.json_response({"error": {"code": "conflict", "message": str(error)}}, status=409)
    except KeyError:
        response = web.json_response({"error": {"code": "not_found", "message": "记录不存在或请求缺少字段"}}, status=404)
    except ConfigStoreError as error:
        LOGGER.exception("configuration_operation_failed request_id=%s", request_id)
        response = web.json_response({"error": {"code": "configuration_failed", "message": str(error)}}, status=400)
    except (ValueError, TypeError) as error:
        response = web.json_response({"error": {"code": "invalid_request", "message": str(error)}}, status=400)
    except PermissionError as error:
        response = web.json_response({"error": {"code": "permission_denied", "message": str(error)}}, status=403)
    except RuntimeError as error:
        LOGGER.warning("connector_unavailable request_id=%s error=%s", request_id, type(error).__name__)
        response = web.json_response({"error": {"code": "upstream_unavailable", "message": "第三方服务当前不可用或尚未配置"}}, status=503)
    except (aiohttp.ClientError, asyncio.TimeoutError):
        LOGGER.exception("external_tool_request_failed request_id=%s route=%s", request_id, request.match_info.route.name)
        response = web.json_response({"error": {"code": "upstream_unavailable", "message": "家居平台不可用，执行结果尚未确认；请先查询设备状态"}}, status=502)
    except web.HTTPException as error:
        response = web.json_response({"error": {"code": "request_rejected", "message": "请求被拒绝；秘密配置仅允许直连本机 loopback 或 HTTPS，Gmail 配置限本机" if error.status == 403 else "请求未被接受，请核对接口及操作条件"}}, status=error.status)
    except Exception:
        LOGGER.exception("control_api_failed request_id=%s route=%s", request_id, request.match_info.route.name)
        response = web.json_response({"error": {"code": "internal_error", "message": "处理失败，请查看本地日志"}}, status=500)
    if screen_operation is not None:
        try:
            data = json.loads(response.body) if isinstance(response, web.Response) and response.content_type == "application/json" and response.body else {}
        except (ValueError, TypeError, UnicodeDecodeError):
            LOGGER.exception("screen_feedback_response_parse_failed request_id=%s", request_id)
            data = {}
        await runtime.operation_screen_feedback(screen_operation, data, http_status=response.status, feedback_id=screen_feedback_id)
    response.headers["Cache-Control"] = "no-store"
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Referrer-Policy"] = "no-referrer"
    response.headers["X-Request-ID"] = request_id
    LOGGER.info("api_request request_id=%s method=%s route=%s status=%s remote=%s", request_id, request.method, request.match_info.route.name, response.status, request.remote)
    record(request.app[AUDIT_KEY], action="api_request", outcome=str(response.status), request_id=request_id, method=request.method, route=request.match_info.route.name, remote=request.remote)
    return response


async def status(request):
    return web.json_response(request.app[RUNTIME_KEY].status())


async def voices(request):
    return web.json_response(request.app[RUNTIME_KEY].voice_status())


async def set_voice(request):
    payload = await body_object(request)
    return web.json_response(await request.app[RUNTIME_KEY].set_voice(payload.get("voice")))


async def llm(request):
    return web.json_response(request.app[RUNTIME_KEY].llm_status())


async def set_llm(request):
    payload = await body_object(request)
    return web.json_response(await request.app[RUNTIME_KEY].set_llm(payload.get("provider")))


async def tasks(request):
    runtime = request.app[RUNTIME_KEY]
    if request.method == "GET":
        result = await asyncio.to_thread(runtime.store.list_tasks, request.query.get("status"), limit=int(request.query.get("limit", "30")), cursor=request.query.get("cursor"), due_from=request.query.get("due_from"), due_before=request.query.get("due_before"))
        return web.json_response({"items": [task_item(item) for item in result["tasks"]], "next_cursor": result.get("next_cursor")})
    payload = await body_object(request)
    item = await asyncio.to_thread(runtime.store.create_task, payload.get("title"), payload.get("due_at"), payload.get("idempotency_key"))
    return web.json_response({"item": task_item(item)}, status=201)


async def task_action(request):
    runtime = request.app[RUNTIME_KEY]
    method = runtime.store.complete_task if request.match_info["action"] == "complete" else runtime.store.cancel_task
    item = await asyncio.to_thread(method, request.match_info["task_id"])
    runtime.notifications.pop(request.match_info["task_id"], None)
    return web.json_response({"item": task_item(item)})


async def screen_state(request):
    runtime = request.app[RUNTIME_KEY]
    devices = [{"device_id": getattr(device, "paired_device_id", ""), "supports_cards": getattr(device, "screen_cards_supported", False), "last_receipt": getattr(device, "last_screen_receipt", None), "preferences_receipt": getattr(device, "last_device_preferences_receipt", None)} for device in tuple(runtime.native_devices) if not device.socket.closed]
    return web.json_response({"timer": await runtime.timer_snapshot(), "devices": devices, "timezone": "Asia/Shanghai", "device_preferences": await runtime.device_preferences(), "completions": await runtime.completion_snapshot()})


async def device_preferences(request):
    runtime = request.app[RUNTIME_KEY]
    if request.method == "GET":
        return web.json_response({"preferences": await runtime.device_preferences(), "timezone": "Asia/Shanghai"})
    payload = await body_object(request)
    return web.json_response(await runtime.save_device_preferences(payload))


async def screen_completions(request):
    return web.json_response({"items": await request.app[RUNTIME_KEY].completion_snapshot(), "limit": 32})


async def screen_completion_ack(request):
    return web.json_response({"acknowledged": True, "item": await request.app[RUNTIME_KEY].acknowledge_completion(request.match_info["completion_id"])})


async def screen_reminders(request):
    items = await asyncio.to_thread(request.app[RUNTIME_KEY].store.due_reminders, limit=10)
    return web.json_response({"items": [task_item(item) for item in items], "limit": 10})


async def screen_timer(request):
    runtime = request.app[RUNTIME_KEY]
    payload = await body_object(request)
    action = request.match_info.get("action")
    if action is not None:
        if set(payload) - {"timer_id", "revision"}:
            raise ValueError("计时操作包含未允许的字段")
        result = await runtime.timer_action(action, payload.get("timer_id"), payload.get("revision"))
    else:
        if set(payload) - {"title", "seconds", "request_id"}:
            raise ValueError("计时参数包含未允许的字段")
        async with runtime.timer_lock:
            result = await asyncio.to_thread(runtime.desktop_timer.create, payload.get("title"), payload.get("seconds"), payload.get("request_id"))
    delivery = await runtime.send_timer_screen_card(result)
    return web.json_response({"timer": result, "delivery": delivery})


async def screen_today(request):
    runtime = request.app[RUNTIME_KEY]
    payload = await body_object(request)
    source = payload.get("calendar_connector", "")
    if set(payload) - {"calendar_connector"} or not isinstance(source, str) or source not in {"", "google_calendar", "microsoft_calendar"}:
        raise ValueError("请选择本地待办、Google 或 Microsoft 日历")
    if source:
        if not connector_configuration(runtime.config).get(source):
            raise ValueError("所选日历尚未配置 Access Token，请先到连接器中心保存")
        now = datetime.now(timezone(timedelta(hours=8)))
        end = (now + timedelta(days=1)).replace(hour=0, minute=0, second=0, microsecond=0)
        context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset({PermissionScope.READ}), metadata={})
        result = await runtime.connector_registry.invoke(source, "list_events", {"time_min": now.isoformat(), "time_max": end.isoformat(), "limit": 5}, context)
        events = result.get("items", [])[:5]
        lines = []
        for item in events[:3]:
            start = str(item.get("start") or "")
            label = "全天" if len(start) == 10 else "时间待核对"
            try:
                parsed = datetime.fromisoformat(start.replace("Z", "+00:00"))
                if parsed.tzinfo is not None:
                    label = parsed.astimezone(timezone(timedelta(hours=8))).strftime("%H:%M")
            except ValueError:
                label = "时间格式待核对"
            lines.append(label + " " + str(item.get("summary") or "无标题日程")[:30])
        detail = "\n".join(lines) or "今日剩余时间无日程返回"
        detail += "\n仅本次读取快照 · 最多5条"
        delivery = await runtime.send_screen_card({"kind": "today", "title": "今日日程 · 只读快照", "detail": detail})
    else:
        delivery = await runtime.send_today_screen_card()
    return web.json_response({"delivery": delivery, "source": source or "local_tasks", "read_at": datetime.now(timezone.utc).isoformat()})


async def screen_weather(request):
    payload = await body_object(request)
    city = payload.get("city")
    cities = {"110000": "北京", "310000": "上海", "440100": "广州"}
    if set(payload) - {"city"} or not isinstance(city, str) or city not in cities:
        raise ValueError("请选择北京、上海或广州的城市代码")
    runtime = request.app[RUNTIME_KEY]
    context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset({PermissionScope.READ}), metadata={})
    try:
        data = await runtime.connector_registry.invoke("amap", "weather", {"city": city}, context)
    except (RuntimeError, aiohttp.ClientError, asyncio.TimeoutError):
        LOGGER.exception("screen_weather_read_failed request_id=%s", request.get("request_id"))
        raise
    forecasts = data.get("forecasts")
    if not isinstance(forecasts, list) or not forecasts:
        raise ValueError("高德未返回天气预报，未发送成功卡片")
    forecast = forecasts[0]
    casts = forecast.get("casts", []) if isinstance(forecast, dict) else []
    if not casts or not isinstance(casts[0], dict):
        raise ValueError("高德天气内容不完整，请稍后重新读取")
    item = casts[0]
    detail = f"{item.get('dayweather', '天气未提供')}\n{item.get('nighttemp', '?')}～{item.get('daytemp', '?')} ℃\n预报日期 {item.get('date', '未知')}\n更新时间 {forecast.get('reporttime', '未知')}"
    delivery = await runtime.send_screen_card({"kind": "weather", "title": cities[city] + " · 天气预报", "detail": detail})
    return web.json_response({"delivery": delivery, "read_at": datetime.now(timezone.utc).isoformat()})


async def reminder_snooze(request):
    payload = await body_object(request)
    if set(payload) - {"updated_at"}:
        raise ValueError("稍后提醒只接受当前记录的 updated_at")
    runtime = request.app[RUNTIME_KEY]
    result = await asyncio.to_thread(runtime.store.snooze_reminder, request.match_info["task_id"], payload.get("updated_at"), minutes=5)
    runtime.notifications.pop(request.match_info["task_id"], None)
    return web.json_response({"item": task_item(result)})


async def task_reschedule(request):
    payload = await body_object(request)
    if set(payload) != {"due_at", "expected_updated_at"}:
        raise ValueError("修改提醒时间只接受 due_at 和当前记录的 expected_updated_at")
    runtime = request.app[RUNTIME_KEY]
    result = await asyncio.to_thread(runtime.store.reschedule_task, request.match_info["task_id"], payload["expected_updated_at"], payload["due_at"])
    runtime.notifications.pop(request.match_info["task_id"], None)
    return web.json_response({"item": task_item(result)})


async def task_rename(request):
    payload = await body_object(request)
    if set(payload) != {"title", "expected_updated_at"}:
        raise ValueError("修改待办标题只接受 title 和当前记录的 expected_updated_at")
    runtime = request.app[RUNTIME_KEY]
    result = await asyncio.to_thread(runtime.store.rename_task, request.match_info["task_id"], payload["expected_updated_at"], payload["title"])
    return web.json_response({"item": task_item(result)})


async def memories(request):
    runtime = request.app[RUNTIME_KEY]
    memory_service = request.app.get(MEMORY_KEY)
    result = await memory_service.listing(limit=int(request.query.get("limit", "30")), cursor=request.query.get("cursor")) if memory_service else await asyncio.to_thread(runtime.store.recall_memory, limit=int(request.query.get("limit", "30")), cursor=request.query.get("cursor"))
    return web.json_response({"items": [memory_item(item) for item in result["memories"]], "next_cursor": result.get("next_cursor")})


async def memory_action(request):
    runtime = request.app[RUNTIME_KEY]
    key = request.match_info["key"]
    if request.method == "DELETE":
        if request.app.get(MEMORY_KEY):
            await request.app[MEMORY_KEY].manual_tool("delete_memory", {"key": key})
        else:
            await asyncio.to_thread(runtime.store.delete_memory, key=key)
            await runtime.refresh_memory_context()
        return web.json_response({"deleted": True})
    payload = await body_object(request)
    if request.app.get(MEMORY_KEY):
        await request.app[MEMORY_KEY].manual_tool("remember_fact", {"key": key, "value": payload.get("value")})
    else:
        await asyncio.to_thread(runtime.store.remember_fact, key, payload.get("value"), authorized=True)
        await runtime.refresh_memory_context()
    result = await asyncio.to_thread(runtime.store.recall_memory, key)
    return web.json_response({"item": memory_item(result["memories"][0])})


async def home(request):
    runtime = request.app[RUNTIME_KEY]
    if not runtime.home.configured:
        return web.json_response({"configured": False, "items": [], "next_cursor": None})
    enabled_only = request.query.get("enabled_only", "false").lower() == "true"
    limit = int(request.query.get("limit", "30"))
    result = await runtime.home.list_home_devices(enabled_only=enabled_only, force=request.query.get("refresh") == "true", query=request.query.get("query", ""), cursor=request.query.get("cursor", ""), limit=limit)
    return web.json_response({"configured": True, "items": result["devices"], "next_cursor": result.get("next_cursor"), "discovery_error": result.get("discovery_error"), "stale": result.get("stale", False)})


async def home_action(request):
    payload = await body_object(request)
    controller = request.app[RUNTIME_KEY].home
    identifier = request.match_info["identifier"]
    if request.path.endswith("/authorization"):
        return web.json_response(await controller.set_authorization(identifier, payload.get("enabled")))
    parameters = payload.get("parameters")
    if parameters is None:
        parameters = {key: payload[key] for key in ("state", "hvac_mode", "temperature") if key in payload}
    try:
        result = await controller.execute_home_action(identifier, payload.get("action"), parameters)
    except (ValueError, RuntimeError, aiohttp.ClientError, asyncio.TimeoutError):
        LOGGER.exception("screen_home_action_unconfirmed request_id=%s", request.get("request_id"))
        raise
    await request.app[RUNTIME_KEY].send_home_screen_card(result)
    return web.json_response(result)


async def notification_ack(request):
    result = await request.app[RUNTIME_KEY].acknowledge(request.match_info["notification_id"])
    return web.json_response({"acknowledged": True, "item": task_item(result)})


async def connectors(request):
    registry = request.app[RUNTIME_KEY].connector_registry
    config = request.app[RUNTIME_KEY].config
    registry.configure(config)
    items = []
    for item in registry.manifests():
        connector = {"id": item.connector_id, "name": item.display_name, "version": item.version, **registry.status(item.connector_id), "oauth_scopes": list(item.oauth_scopes), "authorization": "oauth" if item.oauth_scopes else "webhook_or_token", "action_count": len(item.actions), "config_fields": config_field_metadata(item.connector_id, config), "actions": [{"name": action.name, "description": action.description, "scopes": [scope.value for scope in action.scopes], "requires_confirmation": action.needs_confirmation, "input_schema": schema_for(item.connector_id, action)} for action in item.actions]}
        if item.connector_id == "mcp":
            connector["tools"] = registry.get("mcp").public_tools()
            connector["tool_revision"] = registry.get("mcp").discovery_revision
        if item.connector_id == "gmail" and request.app[RUNTIME_KEY].gmail is not None:
            gmail = request.app[RUNTIME_KEY].gmail
            connector["configured"] = gmail.configured
            connector["config_fields"] = config_field_metadata("gmail", {**config, **gmail.config})
        connector["config_available"] = request.app[CONFIG_STORE_KEY].available
        connector["config_note"] = "Windows DPAPI；留空保留，勾选清除才删除覆盖值" if connector["config_available"] else "当前环境未配置安全秘密存储，后台仅可读取状态；请使用部署环境变量，不能降级明文保存"
        items.append(connector)
    if not any(item["id"] == "mcp" for item in items):
        items.append({"id": "mcp", "name": "外部工具服务", **registry.status("mcp"), "config_fields": config_field_metadata("mcp", config), "config_available": request.app[CONFIG_STORE_KEY].available, "config_note": "首版支持一个 Streamable HTTP 服务；保存后主动检查，再逐项允许能力。需要交互式 OAuth 的服务暂不支持。", "actions": [], "tools": []})
    return web.json_response({"items": items})


def _require_config_transport(request):
    peer = False
    try:
        peer = ipaddress.ip_address(request.remote or "").is_loopback
        host = urlsplit("http://" + request.host).hostname
        local_host = host == "localhost" or ipaddress.ip_address(host or "").is_loopback
    except ValueError:
        local_host = False
    forwarded = any(key in request.headers for key in ("Forwarded", "X-Forwarded-For", "X-Forwarded-Host", "X-Forwarded-Proto"))
    if not request.secure and (not peer or not local_host or forwarded):
        raise web.HTTPForbidden(text="远程配置秘密必须通过 HTTPS；本机仅允许 loopback")


async def connector_config(request):
    connector_id = request.match_info["connector_id"]
    config = request.app[RUNTIME_KEY].config
    if connector_id not in CONNECTOR_CONFIG_FIELDS:
        raise web.HTTPNotFound(text="连接器没有可配置字段")
    if request.method == "GET":
        effective_config = {**config, **request.app[RUNTIME_KEY].gmail.config} if connector_id == "gmail" else config
        return web.json_response({"connector_id": connector_id, "fields": config_field_metadata(connector_id, effective_config), "storage": "gmail_dpapi" if connector_id == "gmail" else "windows_dpapi", "available": request.app[CONFIG_STORE_KEY].available, "effective": "next_session" if connector_id == "models" else "next_request"})
    _require_config_transport(request)
    if connector_id == "gmail":
        raise ValueError("Gmail 配置请使用既有 /api/gmail/config；该接口沿用 Gmail DPAPI 与 OAuth 流程")
    payload = await body_object(request)
    if set(payload) - {"values", "clear_fields"}:
        raise ValueError("只允许 values 与 clear_fields")
    values = payload.get("values", {})
    clear = payload.get("clear_fields", [])
    if not isinstance(values, dict) or not isinstance(clear, list) or any(not isinstance(item, str) for item in clear):
        raise ValueError("values 必须为对象，clear_fields 必须为字符串数组")
    fields = {field["key"]: field for field in CONNECTOR_CONFIG_FIELDS[connector_id]}
    if (set(values) | set(clear)) - set(fields) or any(fields[key].get("managed_by") for key in set(values) | set(clear)):
        raise ValueError("该字段由既有专用流程管理，或不在当前白名单中")
    if any(not fields[key]["secret"] and key not in {"home_assistant_url", "mcp_url"} for key in clear):
        raise ValueError("必需的模型 / 音色 ID 不支持清除，请填写受支持的值")
    runtime = request.app[RUNTIME_KEY]
    async with request.app[CONFIG_LOCK_KEY]:
        values = {key: value for key, value in values.items() if value != ""}
        if set(values) & set(clear):
            raise ValueError("不能同时填写并清除同一字段")
        sources = config.get("_config_sources", {})
        if any(sources.get(key) == "environment" for key in set(values) | set(clear)):
            raise ValueError("该字段由环境变量管理，不能从后台覆盖；请修改环境后重启服务")
        candidate = dict(config)
        candidate.update({key: {} if key == "home_entities" else "" for key in clear})
        validated = validate_connector_values(connector_id, values, candidate)
        if connector_id == "mcp":
            validated["mcp_approved_tools"] = {}
        candidate.update(validated)
        if connector_id == "models":
            from llm_providers import provider_available
            provider = config.get("llm_provider", "qwen_realtime")
            if provider_available(provider, config) and not provider_available(provider, candidate):
                raise ValueError("当前模型依赖该凭据；请先选择可用的其他模型，再清除旧凭据，或在维护窗口处理")
        store = request.app[CONFIG_STORE_KEY]
        if connector_id == "home_assistant":
            if runtime.home.base and runtime.home.base != str(candidate.get("home_assistant_url", "")).rstrip("/") and runtime.home._authorizations:
                raise ValueError("已有家居授权时，更换或清除 Home Assistant 地址需维护窗口；本次未保存。相同地址可更新令牌")
            runtime.home.validate_configuration(candidate)
            async with runtime.home.configuration_lock:
                await asyncio.to_thread(store.save, validated, clear)
                runtime.home.reconfigure(candidate)
        else:
            await asyncio.to_thread(store.save, validated, clear)
        config.update({key: candidate.get(key) for key in set(validated) | set(clear)})
        config.setdefault("_config_sources", {}).update({key: "secure_override" for key in set(validated) | set(clear)})
        registry = runtime.connector_registry
        registry.configure(config)
        registry.invalidate(connector_id)
        if connector_id == "mcp":
            try:
                mcp = registry.get("mcp")
                mcp.discovered = []
                mcp.discovery_revision = ""
                mcp.apply_approvals()
            except KeyError:
                registry.register(MCPConnector(registry.get("todoist").client, config))
        request.app[CONFIRMATIONS_KEY].invalidate_pending()
    record(request.app[AUDIT_KEY], action="configuration_save", outcome="saved", connector_id=connector_id, request_id=request.get("request_id"))
    return web.json_response({"saved": True, "connector_id": connector_id, "fields": config_field_metadata(connector_id, config), "effective": "next_session" if connector_id == "models" else "next_request", "message": "已安全保存；下次语音会话生效" if connector_id == "models" else "已安全保存并在下次请求使用；请单独检查第三方连接"})


async def connector_access(request):
    _require_config_transport(request)
    connector_id = request.match_info["connector_id"]
    registry = request.app[RUNTIME_KEY].connector_registry
    registry.get(connector_id)
    payload = await body_object(request)
    if set(payload) != {"enabled"} or type(payload["enabled"]) is not bool:
        raise ValueError("enabled 必须为布尔值")
    async with request.app[CONFIG_LOCK_KEY]:
        config = request.app[RUNTIME_KEY].config
        disabled = set(config.get("disabled_connectors") or [])
        if payload["enabled"]:
            disabled.discard(connector_id)
        else:
            disabled.add(connector_id)
        values = {"disabled_connectors": sorted(disabled)}
        if connector_id == "mcp":
            values["mcp_approved_tools"] = {}
        await asyncio.to_thread(request.app[CONFIG_STORE_KEY].save, values)
        config.update(values)
        registry.configure(config)
        registry.invalidate(connector_id)
        if connector_id == "mcp":
            registry.get("mcp").apply_approvals()
        request.app[CONFIRMATIONS_KEY].invalidate_pending()
    record(request.app[AUDIT_KEY], action="connector_access", outcome="enabled" if payload["enabled"] else "revoked", connector_id=connector_id, subject=request["principal"].subject)
    return web.json_response({**registry.status(connector_id), "message": "已允许访问，请重新检查连接" if payload["enabled"] else "已撤销小燕子访问并作废未执行确认；提供商账号授权与令牌仍须在提供商设置中另行撤销"})


async def mcp_permissions(request):
    _require_config_transport(request)
    registry = request.app[RUNTIME_KEY].connector_registry
    connector = registry.get("mcp")
    payload = await body_object(request)
    modes = payload.get("tools")
    if set(payload) != {"tools", "revision"} or not isinstance(modes, dict) or len(modes) > 64 or any(not isinstance(mode, str) or mode not in {"read", "confirm", "blocked"} for mode in modes.values()):
        raise ValueError("tools 必须逐项选择 read / confirm / blocked")
    async with request.app[CONFIG_LOCK_KEY]:
        if registry.status("mcp")["status"] != "available":
            raise ValueError("请先主动检查当前服务连接，再允许能力")
        if not connector.discovery_revision or payload["revision"] != connector.discovery_revision:
            raise ValueError("能力列表已变化，请刷新后重新核对允许的能力")
        known = {tool["name"]: tool for tool in connector.discovered}
        if set(modes) - set(known):
            raise ValueError("工具不在本次检查列表中")
        config = request.app[RUNTIME_KEY].config
        approved = {name: {"mode": mode, "fingerprint": fingerprint(known[name]), "connection": connection_fingerprint(config)} for name, mode in modes.items() if mode != "blocked"}
        await asyncio.to_thread(request.app[CONFIG_STORE_KEY].save, {"mcp_approved_tools": approved})
        config["mcp_approved_tools"] = approved
        connector.apply_approvals()
        registry.record_check("mcp")
        request.app[CONFIRMATIONS_KEY].invalidate_pending()
    record(request.app[AUDIT_KEY], action="mcp_permissions", outcome="saved", connector_id="mcp", subject=request["principal"].subject)
    return web.json_response({"saved": True, "tools": connector.public_tools()})


async def settings(request):
    runtime = request.app[RUNTIME_KEY]
    if request.method == "GET":
        config = runtime.config
        read_only = ("host", "port", "device_id", "public_base_url", "max_connections", "session_seconds", "idle_seconds", "memory_backend", "chat_asr_model", "chat_tts_model", "chat_max_tokens", "chat_turn_silence_ms")
        items = [{"key": key, "label": key, "value": config.get(key, "未配置"), "editable": False, "effective": "maintenance_required"} for key in read_only]
        items.extend({"key": key, "label": key, "configured": bool(config.get(key)), "editable": False, "effective": "maintenance_required"} for key in ("device_token", "provisioning_token", "qwen_chat_base_url", "deepseek_base_url", "dashscope_ws_url"))
        items.append({"key": "enable_search", "label": "联网搜索", "value": bool(config.get("enable_search")), "editable": config.get("_config_sources", {}).get("enable_search") != "environment", "effective": "next_session", "source": config.get("_config_sources", {}).get("enable_search", "config_file")})
        return web.json_response({"items": items, "maintenance_note": "监听、权限令牌、设备绑定、固件、OTA 路径与运行参数需维护窗口，由服务管理员管理；本页不修改。", "model_config": config_field_metadata("models", config), "secure_storage_available": request.app[CONFIG_STORE_KEY].available})
    _require_config_transport(request)
    payload = await body_object(request)
    if set(payload) != {"enable_search"} or type(payload["enable_search"]) is not bool:
        raise ValueError("当前仅允许保存 enable_search 布尔值")
    async with request.app[CONFIG_LOCK_KEY]:
        if runtime.config.get("_config_sources", {}).get("enable_search") == "environment":
            raise ValueError("联网搜索由环境变量管理，请修改环境后重启服务")
        await asyncio.to_thread(save_runtime_preferences, runtime.config.get("runtime_dir", Path(__file__).resolve().parent / "runtime"), payload)
        runtime.config.update(payload)
        runtime.config.setdefault("_config_sources", {})["enable_search"] = "saved_preference"
    return web.json_response({"saved": True, "key": "enable_search", "value": payload["enable_search"], "effective": "next_session"})


SAFE_CHECKS = {
    "github": ("repository_status", {"repository": ""}),
    "notion": ("search", {"query": "", "limit": 1}),
    "todoist": ("list_tasks", {"limit": 1}),
    "google_calendar": ("list_events", {"limit": 1}),
    "microsoft_calendar": ("list_events", {"limit": 1}),
    "microsoft_mail": ("mail_folders_read", {}),
    "amap": ("weather", {"city": ""}),
    "home_assistant": ("list_devices", {"limit": 1}),
}


async def connector_check(request):
    # 同一个配置版本完成检查后才登记；避免检查旧凭据期间保存新凭据而误标可用。
    async with request.app[CONFIG_LOCK_KEY]:
        return await _connector_check(request)


async def _connector_check(request):
    connector_id = request.match_info["connector_id"]
    registry = request.app[RUNTIME_KEY].connector_registry
    registry.configure(request.app[RUNTIME_KEY].config)
    registry.require_enabled(connector_id)
    if connector_id == "mcp":
        action_name, defaults = "tools/list", {}
    else:
        action_name, defaults = SAFE_CHECKS.get(connector_id, (None, None))
    if action_name is None:
        raise ValueError("该连接器没有安全的只读连接检查；Webhook 不通过发送消息测试")
    payload = await body_object(request)
    arguments = dict(defaults)
    supplied = payload.get("arguments", {})
    if not isinstance(supplied, dict):
        raise ValueError("arguments 必须为对象")
    allowed = {"github": {"repository"}, "notion": {"query"}, "amap": {"city"}}.get(connector_id, set())
    if set(supplied) - allowed:
        raise ValueError("连接检查包含未允许的参数")
    arguments.update(supplied)
    if connector_id in {"google_calendar", "microsoft_calendar"}:
        now = datetime.now(timezone.utc)
        arguments.update({"time_min": now.isoformat(), "time_max": (now + timedelta(days=1)).isoformat()})
    if connector_id == "amap" and arguments.get("city") not in {"110000", "310000", "440100"}:
        raise ValueError("连接检查城市代码只支持 110000（北京）、310000（上海）、440100（广州）")
    if connector_id == "github" and not str(arguments.get("repository", "")).strip():
        raise ValueError("GitHub 只读检查需要 repository（owner/name）")
    if connector_id in {"notion", "amap"} and not str(arguments.get("query", arguments.get("city", ""))).strip():
        raise ValueError("该只读检查需要 query 或 city")
    context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset({PermissionScope.READ}), metadata={})
    try:
        if connector_id == "mcp":
            await registry.get("mcp").check_connection()
        elif connector_id == "home_assistant":
            await request.app[RUNTIME_KEY].home.check_connection()
        elif connector_id == "microsoft_mail":
            await registry.get(connector_id).check_connection()
        else:
            await registry.invoke(connector_id, action_name, arguments, context)
    except (aiohttp.ClientError, asyncio.TimeoutError):
        LOGGER.exception("connector_check_network_failed connector_id=%s", connector_id)
        registry.record_check(connector_id, "connection_error", "连接错误或超时，请检查网络和服务地址")
        return web.json_response(registry.status(connector_id), status=502)
    except RuntimeError as error:
        LOGGER.exception("connector_check_failed connector_id=%s", connector_id)
        denied = getattr(error, "status", None) in {401, 403} or any(code in str(error) for code in (": 401", ": 403"))
        registry.record_check(connector_id, "expired_or_denied" if denied else "connection_error", "授权已过期或权限不足，请重新授权" if denied else str(error))
        return web.json_response(registry.status(connector_id), status=502)
    registry.record_check(connector_id)
    record(request.app[AUDIT_KEY], action="connector_check", outcome="verified", connector_id=connector_id, subject=request["principal"].subject)
    return web.json_response({"status": "verified", "connector_id": connector_id, "action": action_name, "checked_at": datetime.now(timezone.utc).isoformat(), "request_id": request.get("request_id")})


def _workflow_parameters(workflow_id: str, payload: dict) -> tuple[str, dict]:
    if workflow_id == "meeting-notes":
        parameters = {"parent_id": str(payload.get("parent_id", "")).strip(), "parent_type": str(payload.get("parent_type", "page")).strip(), "title": str(payload.get("title", "")).strip(), "content": str(payload.get("content", "")).strip()}
        if not parameters["parent_id"] or not parameters["title"] or not parameters["content"]:
            raise ValueError("parent_id、title 和 content 不能为空")
        if parameters["parent_type"] not in {"page", "database"}:
            raise ValueError("parent_type 必须是 page 或 database")
        return "notion.create_page", parameters
    if workflow_id == "github-issue":
        parameters = {"repository": str(payload.get("repository", "")).strip(), "title": str(payload.get("title", "")).strip(), "body": str(payload.get("body", ""))}
        if not parameters["repository"] or not parameters["title"]:
            raise ValueError("repository 和 title 不能为空")
        return "github.create_issue", parameters
    if workflow_id == "todoist-task":
        try:
            priority = int(payload.get("priority", 1))
        except (TypeError, ValueError) as error:
            raise ValueError("priority 必须是1到4之间的整数") from error
        parameters = {"content": str(payload.get("content", "")).strip(), "due_string": str(payload.get("due_string", "")), "priority": priority}
        if not parameters["content"] or not 1 <= priority <= 4:
            raise ValueError("content 不能为空，priority 必须在1到4之间")
        return "todoist.create_task", parameters
    raise ValueError("未知或不支持确认的工作流")


async def workflow_prepare(request):
    payload = await body_object(request)
    workflow_id = request.match_info["workflow_id"]
    action, parameters = _workflow_parameters(workflow_id, payload)
    token, item = request.app[CONFIRMATIONS_KEY].prepare(request["principal"].subject, action, parameters)
    record(request.app[AUDIT_KEY], action="confirmation_prepare", outcome="pending_confirmation", workflow_id=workflow_id, action_name=action, subject=request["principal"].subject)
    return web.json_response({"status": "pending_confirmation", "confirmation_id": token, "expires_at": item.expires_at_iso, "workflow_id": workflow_id, "action": action, "preview": parameters})


async def connector_prepare(request):
    payload = await body_object(request)
    connector_id, action_name = request.match_info["connector_id"], request.match_info["action"]
    connector = request.app[RUNTIME_KEY].connector_registry.get(connector_id)
    descriptor = action_for(connector.manifest, action_name)
    if connector_id == "gmail" and action_name == "send":
        raise ValueError("Gmail 发送请使用已有 prepare-send / confirm-send 流程")
    if not descriptor.needs_confirmation:
        raise ValueError("只读动作不需要确认，请直接执行")
    arguments = payload.get("arguments", {})
    if not isinstance(arguments, dict):
        raise ValueError("arguments 必须为对象")
    request.app[RUNTIME_KEY].connector_registry.require_enabled(connector_id)
    arguments = normalize_arguments(connector_id, descriptor, arguments)
    action = f"{connector_id}.{action_name}"
    token, item = request.app[CONFIRMATIONS_KEY].prepare(request["principal"].subject, action, arguments)
    record(request.app[AUDIT_KEY], action="confirmation_prepare", outcome="pending_confirmation", connector_id=connector_id, action_name=action_name, subject=request["principal"].subject)
    return web.json_response({"status": "pending_confirmation", "confirmation_id": token, "expires_at": item.expires_at_iso, "action": action, "preview": arguments})


def _replay_response(mode: str, item) -> web.Response | None:
    if mode == "in_progress":
        return web.json_response({"status": "executing", "message": "相同请求正在执行，请稍后读取审计或提供商结果"}, status=202)
    if mode == "replay":
        return web.json_response(item.result or {"status": item.state}, status=200)
    return None


async def _run_confirmed(request, token: str, action: str, parameters: dict, operation):
    # 配置保存不能在已确认外部写入过程中更换凭据或目标。
    async with request.app[CONFIG_LOCK_KEY]:
        return await _execute_confirmed(request, token, action, parameters, operation)


async def _execute_confirmed(request, token: str, action: str, parameters: dict, operation):
    mode, item = request.app[CONFIRMATIONS_KEY].begin(token, request["principal"].subject, action, parameters)
    replay = _replay_response(mode, item)
    if replay is not None:
        return replay
    try:
        result = await operation()
    except (aiohttp.ClientError, asyncio.TimeoutError):
        unknown = {"status": "unknown", "message": "结果待核实，请到提供商核查，已停止自动重试"}
        request.app[CONFIRMATIONS_KEY].finish(token, "unknown", unknown)
        record(request.app[AUDIT_KEY], action="confirmed_action", outcome="unknown", action_name=action, subject=request["principal"].subject)
        return web.json_response(unknown, status=502)
    except RuntimeError as error:
        failed = {"status": "failed", "message": str(error)}
        request.app[CONFIRMATIONS_KEY].finish(token, "failed", failed)
        record(request.app[AUDIT_KEY], action="confirmed_action", outcome="failed", action_name=action, subject=request["principal"].subject)
        return web.json_response(failed, status=502)
    except Exception:
        request.app[CONFIRMATIONS_KEY].finish(token, "failed", {"status": "failed", "message": "执行失败，请查看服务端错误提示后重新生成确认"})
        record(request.app[AUDIT_KEY], action="confirmed_action", outcome="failed", action_name=action, subject=request["principal"].subject)
        raise
    completed = {"status": "completed", "result": result, "confirmation_id": token}
    request.app[CONFIRMATIONS_KEY].finish(token, "completed", completed)
    record(request.app[AUDIT_KEY], action="confirmed_action", outcome="completed", action_name=action, subject=request["principal"].subject)
    return web.json_response(completed)


async def audit_recent(request):
    runtime_dir = Path(request.app[RUNTIME_KEY].config.get("runtime_dir", Path(__file__).resolve().parent / "runtime"))
    path = runtime_dir / "audit.log"
    return web.json_response(await asyncio.to_thread(read_recent, path))


async def pairing_start(request):
    payload = await body_object(request)
    nonce = str(payload.get("device_nonce", "")).strip()
    if not nonce:
        raise ValueError("device_nonce 不能为空")
    session = request.app[PAIRING_KEY].create(nonce)
    return web.json_response(session.public(), status=201)


async def pairing_device_start(request):
    payload = await body_object(request)
    nonce = str(payload.get("device_nonce", "")).strip()
    if len(nonce) < 16:
        raise ValueError("device_nonce 至少需要16个字符")
    session = request.app[PAIRING_KEY].create(nonce)
    return web.json_response({"pairing_id": session.pairing_id, "pairing_code": session.code, "expires_at": session.expires_at.isoformat()}, status=201, headers={"Cache-Control": "no-store"})


async def pairing_status(request):
    session = request.app[PAIRING_KEY].get(request.match_info["pairing_id"])
    if session is None:
        raise web.HTTPNotFound()
    return web.json_response(session.public())


async def pairing_confirm(request):
    payload = await body_object(request)
    manager = request.app[PAIRING_KEY]
    session = manager.claim(request.match_info["pairing_id"], str(payload.get("code", "")), str(payload.get("workspace_id", "local")), str(payload.get("device_id", "")))
    return web.json_response(session.public())


async def pairing_claim_code(request):
    payload = await body_object(request)
    code = payload.get("code")
    if not isinstance(code, str):
        raise ValueError("配对码必须为字符串")
    session = request.app[PAIRING_KEY].claim_code(code, "local")
    record(request.app[AUDIT_KEY], action="device_pairing_claim", outcome="success", device_id=session.device_id)
    return web.json_response({"device_id": session.device_id, "status": session.status.value})


async def pairing_revoke(request):
    manager = request.app[PAIRING_KEY]
    pairing_id = request.match_info.get("pairing_id")
    if pairing_id is None:
        pairing_id = next((item["pairing_id"] for item in manager.paired_devices() if item["id"] == request.match_info["device_id"]), None)
    session = manager.get(pairing_id)
    if session is None:
        raise web.HTTPNotFound()
    manager.revoke(session.pairing_id)
    runtime = request.app[RUNTIME_KEY]
    async with runtime.native_device_lock:
        for device in tuple(runtime.native_devices):
            if session.device_id and getattr(device, "paired_device_id", None) == session.device_id:
                await device.end_conversation()
                await device.socket.close(code=1008, message=b"Device pairing revoked")
                runtime.native_devices.discard(device)
    record(request.app[AUDIT_KEY], action="device_pairing_revoke", outcome="success", device_id=session.device_id)
    return web.json_response({"revoked": True})


async def pairing_device_result(request):
    payload = await body_object(request)
    credential = request.app[PAIRING_KEY].device_result(request.match_info["pairing_id"], str(payload.get("code", "")), str(payload.get("device_nonce", "")))
    if not credential:
        raise web.HTTPNotFound()
    return web.json_response({"device_id": request.app[PAIRING_KEY].get(request.match_info["pairing_id"]).device_id, "device_credential": credential}, headers={"Cache-Control": "no-store"})


async def devices(request):
    runtime = request.app[RUNTIME_KEY]
    live = {getattr(device, "paired_device_id", runtime.config["device_id"]): device for device in tuple(runtime.native_devices) if not device.socket.closed}
    records = request.app[PAIRING_KEY].paired_devices()
    # 配置中的默认 ID 不是设备注册记录；旧版设备只在实际连接时显示。
    legacy_id = runtime.config["device_id"]
    if legacy_id in live and not any(item["id"] == legacy_id for item in records):
        records.append({"id": legacy_id, "workspace": "local"})
    items = [{**item, "name": item["id"], "model": "小燕子桌面助手", "state": "online" if item["id"] in live else "offline", "firmware": getattr(live.get(item["id"]), "firmware_version", "unknown"), "last_seen": None} for item in records]
    return web.json_response({"items": items})


async def connector_invoke(request):
    payload = await body_object(request)
    arguments = payload.get("arguments", {})
    if not isinstance(arguments, dict):
        raise ValueError("arguments 必须为对象")
    connector_id, action_name = request.match_info["connector_id"], request.match_info["action"]
    connector = request.app[RUNTIME_KEY].connector_registry.get(connector_id)
    descriptor = action_for(connector.manifest, action_name)
    arguments = normalize_arguments(connector_id, descriptor, arguments)
    request.app[RUNTIME_KEY].connector_registry.require_enabled(connector_id)
    if not descriptor.needs_confirmation:
        context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset(PermissionScope), metadata={})
        return web.json_response(await request.app[RUNTIME_KEY].connector_registry.invoke(connector_id, action_name, arguments, context))
    if connector_id == "gmail" and action_name == "send":
        raise ValueError("Gmail 发送请使用已有 prepare-send / confirm-send 流程")
    token = str(payload.get("confirmation_id", ""))
    context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset(PermissionScope), metadata={"confirmation_id": token, "confirmation_verified": True})
    return await _run_confirmed(request, token, f"{connector_id}.{action_name}", arguments, lambda: request.app[RUNTIME_KEY].connector_registry.invoke(connector_id, action_name, arguments, context))


async def meeting_travel(request):
    payload = await body_object(request)
    required = ("calendar_connector", "time_min", "time_max", "origin")
    if any(not str(payload.get(key, "")).strip() for key in required):
        raise ValueError("calendar_connector、time_min、time_max 和 origin 不能为空")
    context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset({PermissionScope.READ}), metadata={})
    result = await plan_meeting_travel(request.app[RUNTIME_KEY].connector_registry, calendar_connector=payload["calendar_connector"], time_min=payload["time_min"], time_max=payload["time_max"], origin=payload["origin"], context=context)
    first = next((item.get("route") for item in result.get("items", []) if (item.get("route") or {}).get("found")), None)
    if first:
        step = (first.get("steps") or [{}])[0]
        instruction = str(step.get("instruction", ""))
        direction = "uturn" if "掉头" in instruction else "left" if "左转" in instruction else "right" if "右转" in instruction else "straight" if "直行" in instruction else ""
        await request.app[RUNTIME_KEY].send_screen_card({"kind": "route", "title": "会议出行", "detail": "路线预览 · 非实时导航\n" + (instruction[:80] or "路线已规划"), "direction": direction, "distance_m": first.get("distance_m"), "duration_s": first.get("duration_s")})
    else:
        await request.app[RUNTIME_KEY].send_screen_card({"kind": "task", "status": "empty", "title": "会议出行", "detail": "没有可显示路线；请核对日程地点、起点和所选时间范围"})
    return web.json_response(result)


async def navigation_demo(request):
    result = await request.app[RUNTIME_KEY].navigation_demo.run()
    status = 409 if result["status"] == "blocked" else 503 if result["status"] == "unavailable" else 200
    return web.json_response(result, status=status)


async def meeting_notes(request):
    payload = await body_object(request)
    action, parameters = _workflow_parameters("meeting-notes", payload)
    token = str(payload.get("confirmation_id", ""))
    context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset(PermissionScope), metadata={"confirmation_id": token, "confirmation_verified": True})
    return await _run_confirmed(request, token, action, parameters, lambda: save_meeting_notes(request.app[RUNTIME_KEY].connector_registry, context=context, **parameters))


async def github_issue(request):
    payload = await body_object(request)
    action, parameters = _workflow_parameters("github-issue", payload)
    token = str(payload.get("confirmation_id", ""))
    context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset(PermissionScope), metadata={"confirmation_id": token, "confirmation_verified": True})
    return await _run_confirmed(request, token, action, parameters, lambda: create_action_item(request.app[RUNTIME_KEY].connector_registry, context=context, **parameters))


async def todoist_task(request):
    payload = await body_object(request)
    action, parameters = _workflow_parameters("todoist-task", payload)
    token = str(payload.get("confirmation_id", ""))
    context = ConnectorContext(subject=request["principal"].subject, approved_scopes=frozenset(PermissionScope), metadata={"confirmation_id": token, "confirmation_verified": True})
    return await _run_confirmed(request, token, action, parameters, lambda: create_todoist_task(request.app[RUNTIME_KEY].connector_registry, context=context, **parameters))


def ota_firmware_manifest():
    """解析本地固件产物的版本与散列；产物缺失或 app_desc 魔数不符返回 None。"""
    bin_path = Path(__file__).resolve().parent / "firmware/assistant_espidf/build/assistant_espidf.bin"
    if not bin_path.is_file():
        return None
    data = bin_path.read_bytes()
    if len(data) < 0x50 or data[0x20:0x24] != bytes((0x32, 0x54, 0xCD, 0xAB)):
        return None
    version = data[0x30:0x50].split(b"\x00")[0].decode("ascii", "replace")
    return {"version": version, "sha256": hashlib.sha256(data).hexdigest(), "path": str(bin_path)}


async def ota_firmware(request):
    runtime = request.app[RUNTIME_KEY]
    authorization = request.headers.get("Authorization", "")
    if authenticate(authorization, runtime.config["device_token"]) is None and not request.app[PAIRING_KEY].authenticate_device(extract_bearer(authorization) or ""):
        raise web.HTTPUnauthorized()
    manifest = ota_firmware_manifest()
    if not manifest:
        raise web.HTTPNotFound(text="firmware artifact missing")
    response = web.FileResponse(manifest["path"])
    response.headers["Cache-Control"] = "no-store"
    return response


async def local_admin_login(request):
    # 仅信任直连本机，不信任代理头；拒绝外站发起的浏览器登录请求。
    if request.remote not in {"127.0.0.1", "::1"} or request.host not in {"localhost:8765", "127.0.0.1:8765", "[::1]:8765"}:
        raise web.HTTPForbidden(text="本机登录仅允许在服务所在电脑打开 localhost:8765")
    if request.headers.get("Origin") != f"{request.scheme}://{request.host}" or request.headers.get("Sec-Fetch-Site", "same-origin") != "same-origin":
        raise web.HTTPForbidden(text="请从控制台页面登录")
    sessions = request.app[ADMIN_SESSIONS_KEY]
    existing = request.cookies.get("assistant_admin", "")
    if existing and await asyncio.to_thread(sessions.valid, existing):
        return web.json_response({"authenticated": True, "restored": True}, headers={"Cache-Control": "no-store"})
    try:
        token, _ = await asyncio.to_thread(sessions.issue)
    except AdminSessionCapacityError as error:
        raise web.HTTPTooManyRequests(text=str(error)) from error
    except AdminSessionStoreError as error:
        raise web.HTTPServiceUnavailable(text="管理员登录状态无法持久保存，请查看本地日志") from error
    response = web.json_response({"authenticated": True}, headers={"Cache-Control": "no-store"})
    response.del_cookie("assistant_admin", path="/api/")
    response.set_cookie("assistant_admin", token, httponly=True, samesite="Strict", secure=request.secure, max_age=SESSION_TTL_SECONDS, path="/")
    return response


async def admin_logout(request):
    token = request.cookies.get("assistant_admin", "")
    if token:
        await asyncio.to_thread(request.app[ADMIN_SESSIONS_KEY].revoke, token)
    response = web.json_response({"authenticated": False}, headers={"Cache-Control": "no-store"})
    response.del_cookie("assistant_admin", path="/")
    response.del_cookie("assistant_admin", path="/api/")
    return response


def register_routes(app, config):
    app[ADMIN_SESSIONS_KEY] = PersistentAdminSessions(Path(config.get("runtime_dir", Path(__file__).resolve().parent / "runtime")) / "admin-sessions.sqlite3", config["device_token"])
    app[CONFIRMATIONS_KEY] = ConfirmationStore()
    app.router.add_post("/api/auth/local-login", local_admin_login, name="local_admin_login")
    app.router.add_post("/api/auth/logout", admin_logout, name="admin_logout")
    app.router.add_get("/api/connectors", connectors)
    app.router.add_route("GET", "/api/connectors/{connector_id}/config", connector_config, name="connector_config_get")
    app.router.add_route("POST", "/api/connectors/{connector_id}/config", connector_config, name="connector_config_save")
    app.router.add_post("/api/connectors/{connector_id}/check", connector_check, name="connector_check")
    app.router.add_post("/api/connectors/{connector_id}/access", connector_access, name="connector_access")
    app.router.add_post("/api/connectors/mcp/permissions", mcp_permissions, name="mcp_permissions")
    app.router.add_post("/api/connectors/{connector_id}/{action}/prepare", connector_prepare, name="connector_prepare")
    app.router.add_get("/api/audit/recent", audit_recent)
    app.router.add_post("/api/devices/pairing/start", pairing_start)
    app.router.add_post("/api/devices/pairing/claim", pairing_claim_code)
    app.router.add_post("/api/devices/pairing/device-start", pairing_device_start, name="pairing_device_start")
    app.router.add_get("/api/devices/pairing/{pairing_id}", pairing_status)
    app.router.add_post("/api/devices/pairing/{pairing_id}/confirm", pairing_confirm)
    app.router.add_post("/api/devices/pairing/{pairing_id}/revoke", pairing_revoke)
    app.router.add_post("/api/devices/pairing/{pairing_id}/device-result", pairing_device_result, name="pairing_device_result")
    app.router.add_get("/api/devices", devices)
    app.router.add_post("/api/devices/{device_id}/unpair", pairing_revoke)
    app.router.add_post("/api/connectors/{connector_id}/{action}", connector_invoke)
    app.router.add_post("/api/workflows/meeting-travel", meeting_travel)
    app.router.add_post("/api/workflows/navigation-demo", navigation_demo, name="navigation_demo")
    app.router.add_post("/api/workflows/{workflow_id}/prepare", workflow_prepare, name="workflow_prepare")
    app.router.add_post("/api/workflows/meeting-notes", meeting_notes)
    app.router.add_post("/api/workflows/github-issue", github_issue)
    app.router.add_post("/api/workflows/todoist-task", todoist_task)
    app.router.add_get("/api/voices", voices)
    app.router.add_put("/api/voice", set_voice)
    app.router.add_get("/api/llm", llm)
    app.router.add_put("/api/llm", set_llm)
    app.router.add_get("/api/status", status)
    app.router.add_route("GET", "/api/settings", settings, name="settings_get")
    app.router.add_route("PUT", "/api/settings", settings, name="settings_save")
    app.router.add_route("GET", "/api/settings/{connector_id:models}", connector_config, name="model_config_get")
    app.router.add_route("POST", "/api/settings/{connector_id:models}", connector_config, name="model_config_save")
    app.router.add_get("/api/ota/firmware", ota_firmware, name="device_ota_firmware")
    app.router.add_get("/api/tasks", tasks)
    app.router.add_post("/api/tasks", tasks)
    app.router.add_get("/api/screen/state", screen_state)
    app.router.add_get("/api/device/preferences", device_preferences)
    app.router.add_put("/api/device/preferences", device_preferences)
    app.router.add_get("/api/screen/completions", screen_completions)
    app.router.add_post("/api/screen/completions/{completion_id}/ack", screen_completion_ack)
    app.router.add_get("/api/screen/reminders", screen_reminders)
    app.router.add_post("/api/screen/today", screen_today)
    app.router.add_post("/api/screen/timer", screen_timer)
    app.router.add_post("/api/screen/timer/{action:pause|resume|cancel}", screen_timer)
    app.router.add_post("/api/tasks/{task_id}/snooze", reminder_snooze)
    app.router.add_post("/api/tasks/{task_id}/reschedule", task_reschedule)
    app.router.add_post("/api/tasks/{task_id}/rename", task_rename)
    app.router.add_post("/api/screen/weather", screen_weather)
    app.router.add_post("/api/tasks/{task_id}/{action:complete|cancel}", task_action)
    app.router.add_get("/api/memories", memories)
    app.router.add_put("/api/memories/{key}", memory_action)
    app.router.add_delete("/api/memories/{key}", memory_action)
    app.router.add_get("/api/home", home)
    app.router.add_post("/api/home/{identifier}/authorization", home_action, name="home_authorization")
    app.router.add_post("/api/home/{identifier}/actions", home_action, name="home_action_generic")
    app.router.add_post("/api/home/{identifier}", home_action, name="home_action_legacy")
    app.router.add_post("/api/notifications/{notification_id}/ack", notification_ack)
