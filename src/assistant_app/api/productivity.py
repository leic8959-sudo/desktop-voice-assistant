"""同源管理员主动助理API；沿用父应用api_boundary认证。"""
import json
from aiohttp import web
from ..application.productivity import PRODUCTIVITY_KEY


async def _body(request):
    try:
        payload = await request.json()
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ValueError("请求必须是JSON") from exc
    if not isinstance(payload, dict) or len(json.dumps(payload, ensure_ascii=False).encode()) > 16384:
        raise ValueError("请求必须是不超过16KB的对象")
    return payload


async def collection(request):
    service, category = request.app[PRODUCTIVITY_KEY], request.match_info["category"]
    if category == "home-catalog":
        return web.json_response(await service.home_catalog(request.query.get("cursor", "")))
    readers = {"routines": service.list_routines, "summaries": service.summary_settings, "subscriptions": service.list_subscriptions, "scenes": service.list_scenes, "events": service.events, "home-catalog": service.home_catalog}
    if request.method == "GET":
        return web.json_response(await readers[category]())
    writers = {"routines": service.create_routine, "summaries": service.save_summary, "subscriptions": service.subscribe, "scenes": service.save_scene}
    return web.json_response(await writers[category](await _body(request)))


async def record_action(request):
    service = request.app[PRODUCTIVITY_KEY]
    category, identity, action = request.match_info["category"], request.match_info["identity"], request.match_info["action"]
    payload = {**await _body(request), "id": identity, "action": action}
    if category == "routines":
        result = await service.routine_action(payload)
    elif category == "subscriptions":
        result = await service.poll_subscription(identity) if action == "check" else await service.subscription_action(payload)
    elif category == "scenes":
        result = await service.scene_preview(payload) if action == "preview" else await service.delete_scene(payload)
    else:
        result = await service.acknowledge_event(payload)
    return web.json_response(result)


async def summary(request):
    return web.json_response(await request.app[PRODUCTIVITY_KEY].daily_summary(await _body(request)))


async def confirm_scene(request):
    principal = request.get("principal")
    if principal is None or not principal.has_role("admin"):
        raise PermissionError("需管理员认证后确认家居场景")
    payload = await _body(request)
    if payload.get("confirmed") is not True:
        raise PermissionError("必须明确确认本次场景")
    return web.json_response(await request.app[PRODUCTIVITY_KEY].confirm_scene(payload))


async def speech_settings(request):
    service = request.app[PRODUCTIVITY_KEY]
    if request.method == "GET":
        return web.json_response(await service.speech_settings())
    principal = request.get("principal")
    if principal is None or not principal.has_role("admin"):
        raise PermissionError("主动语音设置需要管理员认证")
    return web.json_response(await service.save_speech_settings(await _body(request)))


def register_routes(app):
    app.router.add_get("/api/productivity/speech", speech_settings, name="productivity_speech_get")
    app.router.add_post("/api/productivity/speech", speech_settings, name="productivity_speech_save")
    app.router.add_get("/api/productivity/{category:routines|summaries|subscriptions|scenes|events|home-catalog}", collection, name="productivity_collection_get")
    app.router.add_post("/api/productivity/{category:routines|summaries|subscriptions|scenes}", collection, name="productivity_collection_write")
    app.router.add_post("/api/productivity/{category:routines}/{identity}/{action:pause|resume|cancel|complete|skip|delete}", record_action, name="productivity_routine_action")
    app.router.add_post("/api/productivity/{category:subscriptions}/{identity}/{action:pause|resume|delete|check}", record_action, name="productivity_subscription_action")
    app.router.add_post("/api/productivity/{category:scenes}/{identity}/{action:preview|delete}", record_action, name="productivity_scene_action")
    app.router.add_post("/api/productivity/{category:events}/{identity}/{action:ack}", record_action, name="productivity_event_ack")
    app.router.add_post("/api/productivity/summary", summary, name="productivity_summary")
    app.router.add_post("/api/productivity/scenes/confirm", confirm_scene, name="productivity_scene_confirm")
