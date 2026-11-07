"""自动记忆管理，统一沿用主应用管理员认证。"""

from aiohttp import web

from src.assistant_app.application.auto_memory import AutoMemory

MEMORY_KEY = web.AppKey("automatic_memory", AutoMemory)


async def settings(request):
    service = request.app[MEMORY_KEY]
    if request.method == "PUT":
        return web.json_response(await service.configure(await request.json()))
    return web.json_response(service.status())


async def search(request):
    return web.json_response(await request.app[MEMORY_KEY].context(request.query.get("query", ""), limit=12))


async def approve(request):
    body = await request.json()
    if not isinstance(body, dict):
        raise ValueError("请求必须为对象")
    return web.json_response(await request.app[MEMORY_KEY].approve_candidate(body.get("candidate_id")))


def register_routes(app):
    app.router.add_get("/api/memory/settings", settings)
    app.router.add_put("/api/memory/settings", settings)
    app.router.add_get("/api/memory/search", search)
    app.router.add_post("/api/memory/approve", approve)
