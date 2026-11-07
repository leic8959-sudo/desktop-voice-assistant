"""知识与会议路由，依赖 gateway 的 api_boundary 统一认证与审计。"""
from functools import wraps
import asyncio
import json
import logging

from aiohttp import web

from src.assistant_app.application.knowledge import KnowledgeService, KnowledgeUpstreamError, MAX_AUDIO_BYTES, MAX_FILE_BYTES

LOGGER = logging.getLogger("assistant.knowledge.api")
KNOWLEDGE_KEY = web.AppKey("knowledge_service", KnowledgeService)


def _service(request):
    return request.app[KNOWLEDGE_KEY]


def _upstream_errors(handler):
    @wraps(handler)
    async def wrapped(request):
        try:
            return await handler(request)
        except KnowledgeUpstreamError as exc:
            LOGGER.exception("knowledge_upstream_failed request_id=%s", request.get("request_id", ""))
            return web.json_response({"error": {"code": "knowledge_upstream_failed", "message": str(exc)}}, status=502)
        except asyncio.TimeoutError:
            LOGGER.exception("knowledge_upload_timeout request_id=%s", request.get("request_id", ""))
            return web.json_response({"error": {"code": "upload_timeout", "message": "文件上传超过30秒，未开始云端转写"}}, status=408)
    return wrapped


async def _json(request):
    try:
        value = await request.json()
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ValueError("请求必须为 JSON") from exc
    if not isinstance(value, dict):
        raise ValueError("请求内容必须为对象")
    return value


async def _file(request, maximum):
    if not request.content_type.startswith("multipart/"):
        raise ValueError("请使用 multipart file 字段上传")
    if request.content_length is not None and request.content_length > maximum + 64 * 1024:
        raise ValueError("上传内容超过大小上限")
    reader = await request.multipart()
    name, content, authorized = None, None, False
    total = 0
    authorization_seen = False
    async with asyncio.timeout(30):
        while True:
            field = await reader.next()
            if field is None:
                break
            if field.name == "authorized":
                if authorization_seen:
                    raise ValueError("授权字段不能重复")
                authorization_seen = True
                value = await field.read_chunk(8192)
                if len(value) > 16 or not field.at_eof():
                    raise ValueError("授权字段无效")
                authorized = value == b"true"
            elif field.name == "file" and content is None:
                name = field.filename or "upload.txt"
                chunks = []
                while True:
                    chunk = await field.read_chunk(64 * 1024)
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > maximum:
                        raise ValueError("文件超过大小上限")
                    chunks.append(chunk)
                content = b"".join(chunks)
            else:
                raise ValueError("上传仅允许一个 file 字段和一个 authorized 字段")
    if content is None:
        raise ValueError("未收到上传文件")
    return name, content, authorized


async def status(request):
    return web.json_response(await _service(request).status())


async def documents(request):
    return web.json_response(await _service(request).list_documents(request.query.get("cursor", ""), request.query.get("limit", 20)))


async def document(request):
    service = _service(request)
    identifier = request.match_info["document_id"]
    if request.method == "DELETE":
        return web.json_response(await service.delete(identifier))
    return web.json_response(await service.document(identifier))


@_upstream_errors
async def document_import(request):
    if request.content_type.startswith("multipart/"):
        filename, data, _ = await _file(request, MAX_FILE_BYTES)
    else:
        payload = await _json(request)
        filename, data = payload.get("filename", "note.txt"), payload.get("content")
    return web.json_response(await _service(request).import_document(filename, data, document_id=request.match_info.get("document_id")))


async def search(request):
    return web.json_response(await _service(request).search(request.query.get("q", ""), limit=request.query.get("limit", 8)))


@_upstream_errors
async def minutes(request):
    payload = await _json(request)
    return web.json_response(await _service(request).meeting_minutes(payload.get("transcript"), payload.get("title", "会议")))


async def meetings(request):
    return web.json_response(await _service(request).meetings(request.query.get("cursor", ""), request.query.get("limit", 20)))


async def meeting(request):
    return web.json_response(await _service(request).meeting(request.match_info["meeting_id"], delete=request.method == "DELETE"))


@_upstream_errors
async def audio(request):
    _, data, authorized = await _file(request, MAX_AUDIO_BYTES)
    return web.json_response(await _service(request).transcribe_audio(data, authorized=authorized))


@_upstream_errors
async def translate(request):
    payload = await _json(request)
    return web.json_response(await _service(request).translate(payload.get("text"), payload.get("source", "auto"), payload.get("target", "ru")))


@_upstream_errors
async def practice(request):
    payload = await _json(request)
    return web.json_response(await _service(request).practice(payload.get("text"), payload.get("language", "ru"), payload.get("level", "A2"), payload.get("answer", "")))


def register_routes(app):
    app.router.add_get("/api/knowledge/status", status)
    app.router.add_get("/api/knowledge/documents", documents)
    app.router.add_post("/api/knowledge/import", document_import)
    app.router.add_get("/api/knowledge/search", search)
    app.router.add_get("/api/knowledge/documents/{document_id}", document)
    app.router.add_put("/api/knowledge/documents/{document_id}", document_import)
    app.router.add_delete("/api/knowledge/documents/{document_id}", document)
    app.router.add_delete("/api/knowledge/{document_id}", document)
    app.router.add_get("/api/meetings", meetings)
    app.router.add_get("/api/meetings/{meeting_id}", meeting)
    app.router.add_delete("/api/meetings/{meeting_id}", meeting)
    app.router.add_post("/api/meetings/minutes", minutes)
    app.router.add_post("/api/meetings/audio", audio)
    app.router.add_post("/api/language/translate", translate)
    app.router.add_post("/api/language/practice", practice)
