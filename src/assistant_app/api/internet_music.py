"""实体点歌路由，全部复用父应用 api_boundary 认证、同源和审计。"""

from functools import wraps
import logging

from aiohttp import web

from control_api import AUDIT_KEY, _require_config_transport, body_object
from ..infrastructure.audit import record
from ..integrations.music_assistant import MusicAssistantError

LOGGER = logging.getLogger("assistant.music_api")
INTERNET_MUSIC_KEY = web.AppKey("internet_music_service", object)


def _service(request):
    return request.app[INTERNET_MUSIC_KEY]


def _errors(handler):
    @wraps(handler)
    async def wrapped(request):
        try:
            return await handler(request)
        except MusicAssistantError as error:
            LOGGER.exception("internet_music_upstream_failed request_id=%s", request.get("request_id", ""))
            return web.json_response({"error": {"code": "music_assistant_unavailable", "message": str(error)}}, status=503)
        except RuntimeError as error:
            LOGGER.exception("internet_music_unavailable request_id=%s", request.get("request_id", ""))
            return web.json_response({"error": {"code": "music_unavailable", "message": str(error)}}, status=503)
    return wrapped


def _audit(request, action, outcome):
    record(request.app[AUDIT_KEY], action="internet_music_" + action, outcome=outcome, subject=request["principal"].subject, request_id=request.get("request_id"))


async def status(request):
    return web.json_response(_service(request).status())


@_errors
async def config(request):
    if request.method == "GET":
        return web.json_response(_service(request).status()["configuration"])
    _require_config_transport(request)
    result = await _service(request).configure(await body_object(request))
    _audit(request, "configuration", "saved")
    return web.json_response(result)


@_errors
async def search(request):
    raw_limit = request.query.get("limit", "5")
    if not raw_limit.isdecimal():
        raise ValueError("候选数量必须为整数")
    return web.json_response(await _service(request).search(request.query.get("q", ""), limit=int(raw_limit)))


@_errors
async def play(request):
    payload = await body_object(request)
    if set(payload) - {"selection_id"}:
        raise ValueError("播放只接受当前搜索返回的 selection_id")
    result = await _service(request).play(payload.get("selection_id"))
    _audit(request, "play", result.get("status", "unknown"))
    return web.json_response(result)


@_errors
async def action(request):
    name = request.match_info["action"]
    if name not in {"pause", "resume", "stop", "queue", "noise"}:
        raise ValueError("不支持的音乐操作")
    payload = await body_object(request)
    allowed = {"selection_id"} if name == "queue" else {"source", "minutes"} if name == "noise" else set()
    if set(payload) - allowed:
        raise ValueError("音乐操作包含不支持的参数")
    result = await _service(request).action(name, payload)
    _audit(request, name, result.get("status", "unknown"))
    return web.json_response(result)


def register_routes(app):
    app.router.add_get("/api/music/status", status)
    app.router.add_get("/api/music/config", config)
    app.router.add_post("/api/music/config", config)
    app.router.add_get("/api/music/search", search)
    app.router.add_post("/api/music/play", play)
    app.router.add_post("/api/music/actions/{action}", action)
