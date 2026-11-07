"""统一组装可选个人助理能力，设备协议和既有业务入口保持集中。"""

import logging

from src.assistant_app.api.auto_memory import MEMORY_KEY, register_routes as register_memory_routes
from src.assistant_app.api.desktop_actions import DESKTOP_ACTIONS_KEY, register_routes as register_computer_routes
from src.assistant_app.api.knowledge import KNOWLEDGE_KEY, register_routes as register_knowledge_routes
from src.assistant_app.api.productivity import register_routes as register_productivity_routes
from src.assistant_app.api.internet_music import INTERNET_MUSIC_KEY, register_routes as register_music_routes
from src.assistant_app.application.auto_memory import AutoMemory
from src.assistant_app.application.desktop_actions import DesktopActionsService
from src.assistant_app.application.knowledge import KnowledgeService
from src.assistant_app.application.productivity import PRODUCTIVITY_KEY, ProductivityService
from src.assistant_app.application.internet_music import InternetMusicService
from src.assistant_app.application.navigation_demo import NavigationDemoService

LOGGER = logging.getLogger("assistant.extensions")


async def initialize_extensions(app, runtime, store, http, runtime_path):
    runtime.config.setdefault("runtime_dir", str(runtime_path))
    services = (
        (KNOWLEDGE_KEY, KnowledgeService),
        (MEMORY_KEY, AutoMemory),
        (PRODUCTIVITY_KEY, ProductivityService),
        (DESKTOP_ACTIONS_KEY, DesktopActionsService),
        (INTERNET_MUSIC_KEY, InternetMusicService),
    )
    runtime.capability_services = []
    try:
        for key, service_class in services:
            service = service_class(runtime.config, store, runtime, http)
            app[key] = service
            runtime.capability_services.append(service)
            await service.start()
    except BaseException:
        await close_extensions(runtime)
        raise
    runtime.memory_service = app[MEMORY_KEY]
    runtime.navigation_demo = NavigationDemoService(runtime)
    for connector_id in ("google_calendar", "microsoft_calendar", "microsoft_mail"):
        runtime.connector_registry.get(connector_id).token_service = app[DESKTOP_ACTIONS_KEY].token_refresh


async def close_extensions(runtime):
    navigation = getattr(runtime, "navigation_demo", None)
    if navigation is not None:
        await navigation.close()
    for service in reversed(getattr(runtime, "capability_services", ())):
        try:
            await service.close()
        except Exception:
            LOGGER.exception("extension_cleanup_failed service=%s", type(service).__name__)


def register_extension_routes(app):
    register_memory_routes(app)
    register_productivity_routes(app)
    register_knowledge_routes(app)
    register_computer_routes(app)
    register_music_routes(app)
