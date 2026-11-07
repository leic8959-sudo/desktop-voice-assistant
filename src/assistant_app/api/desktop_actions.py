"""电脑动作API：复用api_boundary认证、ConfirmationStore与审计。"""

from __future__ import annotations

import asyncio
import hashlib
import logging
import uuid
from pathlib import Path

from aiohttp import web

from control_api import AUDIT_KEY, CONFIRMATIONS_KEY, _require_config_transport, body_object
from ..application.desktop_actions import DesktopActionsService, DIRECT_PC_ACTIONS, MAX_MEDIA_BYTES
from ..infrastructure.audit import record

LOGGER = logging.getLogger("assistant.desktop_api")
DESKTOP_ACTIONS_KEY = web.AppKey("desktop_actions_service", DesktopActionsService)


def _service(request):
    return request.app[DESKTOP_ACTIONS_KEY]


def _confirm(request, token, action, arguments):
    subject = request["principal"].subject
    return request.app[CONFIRMATIONS_KEY].begin(token, subject, "computer." + action, arguments)


def _finish(request, token, state, result):
    request.app[CONFIRMATIONS_KEY].finish(token, state, result)
    record(request.app[AUDIT_KEY], action="computer_action", outcome=state, action_name=request.match_info.get("action", "media_upload"), subject=request["principal"].subject, request_id=request.get("request_id"))


def _replay(mode, item):
    if mode == "in_progress":
        return web.json_response({"status": "executing", "message": "相同确认正在执行，请刷新状态核对"}, status=202)
    if mode == "replay":
        return web.json_response(item.result or {"status": item.state})
    return None


async def status(request):
    return web.json_response(_service(request).status())


async def prepare(request):
    payload = await body_object(request)
    action = request.match_info["action"]
    service = _service(request)
    try:
        arguments = service.normalize(action, payload.get("arguments", {}))
        preview = service.preview(action, arguments)
    except RuntimeError as error:
        LOGGER.exception("computer_preview_unavailable action=%s", action)
        return web.json_response({"error": {"code": "computer_unavailable", "message": str(error)}}, status=503)
    token, item = request.app[CONFIRMATIONS_KEY].prepare(request["principal"].subject, "computer." + action, arguments)
    record(request.app[AUDIT_KEY], action="computer_prepare", outcome="pending_confirmation", action_name=action, subject=request["principal"].subject, request_id=request.get("request_id"))
    return web.json_response({"status": "pending_confirmation", "confirmation_token": token, "expires_at": item.expires_at_iso, "preview": preview, "arguments": arguments, "instance_scope": "单实例，确认凭证重启后失效"})


async def invoke(request):
    payload = await body_object(request)
    action = request.match_info["action"]
    if action == "media_upload":
        raise ValueError("音源上传请使用专用上传接口")
    service = _service(request)
    arguments = service.normalize(action, payload.get("arguments", {}))
    if action in DIRECT_PC_ACTIONS:
        result = await service.execute_direct(action, arguments)
        record(request.app[AUDIT_KEY], action="computer_action", outcome=result.get("status", "unknown"), action_name=action, subject=request["principal"].subject, request_id=request.get("request_id"))
        return web.json_response(result)
    token = str(payload.get("confirmation_token", ""))
    mode, item = _confirm(request, token, action, arguments)
    replay = _replay(mode, item)
    if replay is not None:
        return replay
    try:
        result = await service.execute_confirmed(action, arguments, confirmation_verified=True)
    except Exception:
        LOGGER.exception("confirmed_computer_action_failed action=%s", action)
        _finish(request, token, "failed", {"status": "failed", "message": "动作失败；请刷新核对后重新生成预览，不自动重试"})
        raise
    state = "blocked" if result.get("status") in {"blocked", "unsupported"} else "completed"
    _finish(request, token, state, result)
    return web.json_response(result)


async def oauth_config(request):
    service, provider = _service(request), request.match_info["provider"]
    if request.method == "GET":
        return web.json_response(service.tokens.status(provider))
    _require_config_transport(request)
    payload = await body_object(request)
    try:
        result = await service.tokens.configure(provider, payload.get("values", {}))
    except Exception:
        LOGGER.exception("oauth_configuration_failed provider=%s", provider)
        raise
    request.app[CONFIRMATIONS_KEY].invalidate_pending()
    record(request.app[AUDIT_KEY], action="oauth_refresh_configuration", outcome="saved", connector_id=provider, subject=request["principal"].subject, request_id=request.get("request_id"))
    return web.json_response(result)


async def media_upload(request):
    # 只消费用户在当前页面选定并确认过的文件；路径与文件名由服务生成。
    service = _service(request)
    if request.content_type != "multipart/form-data":
        raise ValueError("请使用multipart/form-data上传用户自备音源")
    reader = await request.multipart()
    metadata = await reader.next()
    if metadata is None or metadata.name != "metadata":
        raise ValueError("上传缺少metadata字段")
    raw = bytearray()
    while chunk := await metadata.read_chunk(4096):
        raw.extend(chunk)
        if len(raw) > 4096:
            raise ValueError("上传metadata超限")
    import json
    payload = json.loads(raw)
    if not isinstance(payload, dict):
        raise ValueError("上传metadata必须为对象")
    arguments = service.normalize("media_upload", payload.get("arguments", {}))
    token = str(payload.get("confirmation_token", ""))
    mode, item = _confirm(request, token, "media_upload", arguments)
    replay = _replay(mode, item)
    if replay is not None:
        return replay
    path = None
    converted_path = None
    try:
        async with service.upload_lock:
            service.media_root.mkdir(parents=True, exist_ok=True)
            files = list(service.media_root.glob("*.wav"))
            if len(files) >= 32 or sum(file.stat().st_size for file in files) + arguments["size"] > 200 * 1024 * 1024:
                raise ValueError("音源限32项、总容量200MiB，请由管理员维护媒体目录")
            field = await reader.next()
            if field is None or field.name != "file":
                raise ValueError("上传缺少file字段")
            source_id = uuid.uuid4().hex
            path = service.media_root / (source_id + ".upload")
            digest, count = hashlib.sha256(), 0
            with path.open("xb") as stream:
                while chunk := await field.read_chunk(65536):
                    count += len(chunk)
                    if count > MAX_MEDIA_BYTES or count > arguments["size"]:
                        raise ValueError("上传大小超过已确认限额")
                    digest.update(chunk)
                    await asyncio.to_thread(stream.write, chunk)
            if count != arguments["size"] or digest.hexdigest() != arguments["sha256"]:
                raise PermissionError("上传内容与预览确认的文件不匹配")
            if await reader.next() is not None:
                raise ValueError("一次仅允许上传一个文件")
            extension = Path(arguments["filename"]).suffix.lower()
            if extension == ".wav":
                await asyncio.to_thread(service.validate_wav, path)
                ready_path = path
            else:
                converted_path = service.media_root / (source_id + ".pcm-temp")
                await service.convert_upload_async(path, converted_path, extension)
                ready_path = converted_path
            if sum(file.stat().st_size for file in files) + ready_path.stat().st_size > 200 * 1024 * 1024:
                raise ValueError("转换后音源超过媒体库200MiB总容量限制")
            destination = service.media_root / (source_id + ".wav")
            await asyncio.to_thread(ready_path.replace, destination)
            if extension == ".wav":
                path = None
            else:
                converted_path = None
            try:
                await service.register_upload(source_id, arguments)
            except BaseException:
                await asyncio.to_thread(destination.unlink, missing_ok=True)
                raise
        result = {"status": "uploaded", "source": source_id, "title": arguments["title"], "kind": arguments["kind"], "message": "用户自备音源及名称分类已保存在本机；上传不会自动播放，也不表示音源版权已核验"}
        _finish(request, token, "completed", result)
        return web.json_response(result)
    except RuntimeError as error:
        LOGGER.exception("confirmed_media_upload_unavailable")
        _finish(request, token, "failed", {"status": "failed", "message": "音源转换不可用，未进入播放列表"})
        return web.json_response({"error": {"code": "media_decode_unavailable", "message": str(error)}}, status=503)
    except Exception:
        LOGGER.exception("confirmed_media_upload_failed")
        _finish(request, token, "failed", {"status": "failed", "message": "音源上传失败，未进入播放列表"})
        raise
    finally:
        if path is not None:
            await asyncio.to_thread(path.unlink, missing_ok=True)
        if converted_path is not None:
            await asyncio.to_thread(converted_path.unlink, missing_ok=True)


def register_routes(app):
    # 调用者必须先注册现有api_boundary与control_api路由，初始化认证/确认/审计依赖。
    app.router.add_get("/api/computer/status", status)
    app.router.add_post("/api/computer/actions/{action}/prepare", prepare)
    app.router.add_post("/api/computer/actions/{action}", invoke)
    app.router.add_post("/api/computer/media/upload", media_upload)
    app.router.add_route("GET", "/api/computer/oauth/{provider}/config", oauth_config)
    app.router.add_route("POST", "/api/computer/oauth/{provider}/config", oauth_config)
