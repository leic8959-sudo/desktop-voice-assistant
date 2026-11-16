"""持续控制连接、独立提醒投递及会话运行状态。"""

import asyncio
import json
import logging
import sys
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path
from src.assistant_app.application.desktop_timer import DesktopTimer
from src.assistant_app.application.screen_feedback import feedback_card
from src.assistant_app.application.device_preferences import DevicePreferences, is_night_now, is_quiet_now
from assistant_core.store import IdempotencyConflict, LeaseConflict, NotFoundError
from assistant_core.project_tasks import reminder_title

from voice_settings import VoiceSettings, VOICES, VOICE_SOURCE
from llm_providers import LlmSettings, LLM_SOURCE, apply_defaults, provider_items, provider_model
from src.assistant_app.integrations import ConnectorRegistry
from src.assistant_app.infrastructure.settings import connector_configuration

from aiohttp import WSMsgType, web

from wake_phrase import DEFAULT_WAKE_PHRASE, device_wake_phrase, wake_phrase_display

LOGGER = logging.getLogger("assistant")
RUNTIME_KEY = web.AppKey("assistant_runtime", object)
NOTIFICATION_SEND_TIMEOUT = 3
SCREEN_CARD_KINDS = {"task", "home", "timer", "reminder", "weather", "today", "route", "power"}
SCREEN_CARD_STATUSES = {"", "clear", "completed", "processing", "error", "unknown", "blocked", "saved", "finished", "observed", "unconfirmed", "verified", "empty", "pending_confirmation", "pending_authorization", "authorized", "unconfigured", "running", "paused", "cancelled"}
SCREEN_CARD_SOURCES = {"send_task_screen_card", "send_today_screen_card", "send_timer_screen_card", "handle", "screen_today", "screen_weather", "meeting_travel", "_publish"}


def screen_card_log_id(value):
    text = str(value or "")[:80]
    return text if text and all(character.isascii() and (character.isalnum() or character in "_.:-") for character in text) else "unknown"


class AssistantRuntime:
    def __init__(self, config, store, home):
        apply_defaults(config)
        self.config, self.store, self.home = config, store, home
        self.native_devices = set()
        self.native_device_lock = asyncio.Lock()
        self.voice_sessions = set()
        self.notifications = {}
        self.last_usage = None
        self.last_latency = None
        self.last_cloud_ready = None
        self.last_provider = None
        self.last_device_state = "idle"
        self.latest_device_power = {"status": "unknown", "source": "AXP2101_readback_unavailable"}
        self.voice_lock = asyncio.Lock()
        self.llm_lock = asyncio.Lock()
        self.gmail = None
        self.gmail_config_lock = asyncio.Lock()
        self.gmail_runtime_dir = None
        self.connector_registry = ConnectorRegistry()
        runtime_path = Path(config.get("runtime_dir", Path(__file__).resolve().parent / "runtime"))
        self.voice_settings = VoiceSettings(runtime_path / "voice-preference.json", config)
        self.llm_settings = LlmSettings(runtime_path / "llm-preference.json", config)
        self.screen_card_lock = asyncio.Lock()
        self.timer_lock = asyncio.Lock()
        self.desktop_timer = DesktopTimer(runtime_path)
        self.device_preferences_store = DevicePreferences(runtime_path)
        self.device_preferences_lock = asyncio.Lock()
        self.completion_events = {}
        self.completion_events_lock = asyncio.Lock()

    async def send_screen_card(self, card, *, target=None):
        # 一个卡片只携带屏幕需要的摘要，不透传任务/提供商原始 JSON。
        payload = {key: str(card.get(key, "")).encode("utf-8")[:limit].decode("utf-8", errors="ignore") for key, limit in (("kind", 24), ("title", 180), ("detail", 540), ("status", 24), ("reference_id", 80), ("reference_revision", 64), ("direction", 16))}
        payload.update({"type": "screen_card", "card_id": str(card.get("card_id") or uuid.uuid4().hex)[:80], "ttl_s": min(86400, max(1, int(card.get("ttl_s", 8))))})
        caller = sys._getframe(1).f_code.co_name
        source = caller if caller in SCREEN_CARD_SOURCES else "unknown"
        metadata = {"card_id": payload["card_id"], "kind": payload["kind"] if payload["kind"] in SCREEN_CARD_KINDS else "unknown", "status": payload["status"] if payload["status"] in SCREEN_CARD_STATUSES else "unknown", "source": source}
        # 延续既有人工决策：通用操作结果不能绕过专用反馈入口重新发卡；clear 只用于清理。
        if payload["kind"] not in {"timer", "reminder"} and payload["status"] not in {"", "clear"}:
            LOGGER.info("screen_card_blocked card_id=%s kind=%s status=%s source=%s device=%s", screen_card_log_id(payload["card_id"]), metadata["kind"], metadata["status"], source, screen_card_log_id(getattr(target, "paired_device_id", "broadcast")))
            return {"status": "disabled", "card_id": payload["card_id"], "sent_devices": 0, "message": "操作结果卡片已停用"}
        for key in ("distance_m", "duration_s", "remaining_s"):
            payload[key] = min(86400 if key != "distance_m" else 10000000, max(0, int(card.get(key) or 0)))
        payload["updated_at"] = datetime.now(timezone.utc).isoformat()
        sent = 0
        supported = 0
        devices = tuple(device for device in ((target,) if target is not None else tuple(self.native_devices)) if not device.socket.closed)
        async with self.screen_card_lock:
            for device in devices:
                if device.socket.closed or not getattr(device, "screen_cards_supported", False):
                    continue
                supported += 1
                try:
                    # 每设备仅保存最近一次下发的标识，不缓存正文，也不猜测旧回执的来源。
                    device.last_screen_card = dict(metadata)
                    await asyncio.wait_for(device.send_json(payload), timeout=3)
                    sent += 1
                    LOGGER.info("screen_card_send card_id=%s kind=%s status=%s source=%s device=%s", screen_card_log_id(payload["card_id"]), metadata["kind"], metadata["status"], source, screen_card_log_id(getattr(device, "paired_device_id", "")))
                except Exception:
                    LOGGER.exception("screen_card_send_failed card_id=%s kind=%s status=%s source=%s device=%s", screen_card_log_id(payload["card_id"]), metadata["kind"], metadata["status"], source, screen_card_log_id(getattr(device, "paired_device_id", "")))
        return {"status": "sent" if sent else "unsupported_firmware" if devices and not supported else "no_device" if not devices else "send_failed", "card_id": payload["card_id"], "sent_devices": sent, "message": "已发送，待设备确认显示" if sent else "设备固件未声明卡片能力，请在维护窗口更新" if devices and not supported else "没有可用设备显示"}

    async def device_preferences(self):
        async with self.device_preferences_lock:
            values = await asyncio.to_thread(self.device_preferences_store.snapshot)
        values["quiet_active"] = bool(values["dnd_enabled"] and is_quiet_now(values))
        values["night_active"] = bool(values["night_mode_enabled"] and is_night_now(values))
        values["effective_brightness"] = values["night_brightness"] if values["night_active"] else values["brightness"]
        values["battery"] = dict(self.latest_device_power)
        return values

    async def save_device_preferences(self, values):
        async with self.device_preferences_lock:
            preferences = await asyncio.to_thread(self.device_preferences_store.save, values)
        preferences["quiet_active"] = bool(preferences["dnd_enabled"] and is_quiet_now(preferences))
        preferences["night_active"] = bool(preferences["night_mode_enabled"] and is_night_now(preferences))
        preferences["effective_brightness"] = preferences["night_brightness"] if preferences["night_active"] else preferences["brightness"]
        preferences["battery"] = dict(self.latest_device_power)
        payload = {"type": "device_preferences", **preferences}
        sent = 0
        for device in tuple(self.native_devices):
            if device.socket.closed:
                continue
            try:
                await asyncio.wait_for(device.send_json(payload), timeout=3)
                sent += 1
            except Exception:
                LOGGER.exception("device_preferences_send_failed")
        return {"saved": True, "applies": "device_now_or_next_connection", "sent_devices": sent, "preferences": preferences}

    async def completion_snapshot(self):
        now = datetime.now(timezone.utc)
        async with self.completion_events_lock:
            self.completion_events = {key: item for key, item in self.completion_events.items() if datetime.fromisoformat(item["expires_at"]) > now}
            return [dict(item) for item in self.completion_events.values()]

    async def acknowledge_completion(self, completion_id):
        async with self.completion_events_lock:
            item = self.completion_events.get(completion_id)
            if item is None:
                raise KeyError("完成提醒不存在或已过期")
            item["unread"] = False
            return dict(item)

    async def send_task_screen_card(self, item):
        due = item.get("due_at")
        detail = "待办已保存"
        if due:
            local_due = datetime.fromisoformat(due.replace("Z", "+00:00")).astimezone(timezone(timedelta(hours=8)))
            detail = "提醒已保存\n" + local_due.strftime("%m-%d %H:%M") + "（北京时间）"
        return await self.send_screen_card({"kind": "task", "title": item.get("title", "待办"), "detail": detail})

    async def operation_screen_feedback(self, operation, result=None, *, phase="result", http_status=200, error=None, feedback_id=""):
        # [人工决策-2026-11-06 07:38:21] 牛牛要求彻底移除通用操作反馈卡片；后台完成记录保留，专用提醒/计时/导航独立处理。
        if operation is None or phase != "result" or operation.specialized:
            return
        try:
            card = feedback_card(operation, result, phase=phase, http_status=http_status, error=error)
            if phase == "result" and card["status"] in {"completed", "observed", "unknown", "error", "blocked", "saved", "finished"}:
                completion_id = feedback_id or uuid.uuid4().hex
                event = {"id": completion_id, "title": card["title"], "status": card["status"], "detail": card["detail"], "unread": True, "created_at": datetime.now(timezone.utc).isoformat(), "expires_at": (datetime.now(timezone.utc) + timedelta(hours=24)).isoformat()}
                async with self.completion_events_lock:
                    self.completion_events[completion_id] = event
                    while len(self.completion_events) > 32:
                        self.completion_events.pop(next(iter(self.completion_events)))
        except Exception:
            # 辅助记录失败不能改变已执行的业务结果。
            LOGGER.exception("operation_screen_feedback_failed phase=%s", phase)

    async def send_today_screen_card(self, *, target=None):
        now = datetime.now(timezone(timedelta(hours=8)))
        end = (now + timedelta(days=1)).replace(hour=0, minute=0, second=0, microsecond=0)
        result = await asyncio.to_thread(self.store.screen_tasks, end, limit=5)
        items = result["tasks"]
        detail = "\n".join(item["title"][:30] for item in items[:3]) if items else "今天及逾期暂无待办"
        if result["truncated"] or len(items) > 3:
            detail += "\n更多待办请到后台查看"
        return await self.send_screen_card({"kind": "today", "title": "待办摘要 · 含逾期/未定时", "detail": detail}, target=target)

    async def timer_snapshot(self):
        async with self.timer_lock:
            return await asyncio.to_thread(self.desktop_timer.snapshot)

    async def send_timer_screen_card(self, timer, *, target=None):
        if timer is None:
            return {"status": "no_timer"}
        labels = {"running": "计时中", "paused": "已暂停", "finished": "计时结束", "cancelled": "已取消"}
        return await self.send_screen_card({"card_id": "timer-" + timer["id"] + "-" + str(timer["revision"]), "kind": "timer", "title": timer["title"], "status": timer["status"], "detail": labels[timer["status"]], "reference_id": timer["id"], "reference_revision": str(timer["revision"]), "remaining_s": timer["remaining_s"], "ttl_s": 30 if timer["status"] == "finished" else 8 if timer["status"] == "cancelled" else 86400}, target=target)

    async def timer_action(self, action, timer_id, revision):
        async with self.timer_lock:
            result = await asyncio.to_thread(self.desktop_timer.action, action, timer_id, revision)
        return result

    async def send_home_screen_card(self, result):
        # [人工决策-2026-11-06 11:04:01] 牛牛确认移除操作结果卡片；家居专用路径也不能再下发。
        return

    def voice_status(self):
        active = next((getattr(session, "selected_voice", None) for session in self.voice_sessions if session.backend_connected), None)
        return {"current": self.config["aliyun_voice"], "active_voice": active, "items": VOICES, "applies": "next_session", "source": VOICE_SOURCE}

    async def set_voice(self, value):
        async with self.voice_lock:
            await asyncio.to_thread(self.voice_settings.save, value)
        return self.voice_status()

    def llm_status(self):
        return {"current": self.config["llm_provider"], "active_provider": self.last_provider, "model": provider_model(self.config), "items": provider_items(self.config), "applies": "next_session", "source": LLM_SOURCE}

    async def set_llm(self, value):
        async with self.llm_lock:
            await asyncio.to_thread(self.llm_settings.save, value)
        return self.llm_status()

    def status(self):
        configured_connectors = connector_configuration(self.config)
        return {
            "bridge": {"running": True, "model": provider_model(self.config), "llm_provider": self.config["llm_provider"], "active_provider": self.last_provider, "memory_backend": "pc_prototype", "timezone": "Asia/Shanghai"},
            "device": {"connected": bool(self.native_devices) or any(not session.browser and not session.closed for session in self.voice_sessions), "state": self.last_device_state},
            "cloud": {"connected": any(session.backend_connected for session in self.voice_sessions), "last_ready_at": self.last_cloud_ready},
            "llm": self.llm_status(),
            "notifications": [{key: value for key, value in item.items() if key != "lease_token"} for item in self.notifications.values() if item.get("status") == "pending_delivery"],
            "device_preferences": self.device_preferences_store.snapshot(),
            "usage": self.last_usage, "first_audio_ms": self.last_latency,
            "capabilities": {"live_voice": True, "tasks": True, "task_reschedule": True, "task_add_reminder": True, "task_date_filter": True, "task_rename": True, "reminders": True, "memories": True, "automatic_memory": getattr(self, "memory_service", None) is not None, "home": self.home.configured, "web_search": self.config.get("enable_search", False), "native_protocol": True, "device_firmware_verified": False, "agent": False, "sd_memory_authoritative": False, "multi_llm": True},
            "connectors": [{"id": item.connector_id, "name": item.display_name, "version": item.version, "configured": configured_connectors.get(item.connector_id, False), "actions": [action.name for action in item.actions]} for item in self.connector_registry.manifests()],
            "workflows": [
                {"id": "meeting-travel", "name": "会议出行规划", "required_connectors": ["google_calendar 或 microsoft_calendar", "amap"], "ready": (configured_connectors.get("amap", False) and (configured_connectors.get("google_calendar", False) or configured_connectors.get("microsoft_calendar", False))), "read_only": True},
                {"id": "meeting-notes", "name": "会议纪要沉淀", "required_connectors": ["notion"], "ready": configured_connectors.get("notion", False), "read_only": False},
                {"id": "github-issue", "name": "待办转 GitHub Issue", "required_connectors": ["github"], "ready": configured_connectors.get("github", False), "read_only": False},
                {"id": "todoist-task", "name": "待办转 Todoist 任务", "required_connectors": ["todoist"], "ready": configured_connectors.get("todoist", False), "read_only": False},
            ],
        }

    async def acknowledge(self, notification_id, lease_token=None):
        item = self.notifications.get(notification_id)
        if item is None or item.get("status") != "pending_delivery":
            raise KeyError("提醒不存在或投递租约已经过期")
        if lease_token is not None and item["lease_token"] != lease_token:
            raise ValueError("提醒确认令牌不匹配")
        result = await asyncio.to_thread(self.store.ack_reminder, notification_id, item["lease_token"])
        if self.notifications.get(notification_id) is item:
            self.notifications.pop(notification_id, None)
        return result

    async def _register_claimed_notifications(self, rows):
        if not rows:
            return []
        staged = []
        for row in rows:
            item = {**row, "status": "validating"}
            self.notifications[item["id"]] = item
            staged.append(item)
        try:
            valid = await asyncio.to_thread(self.store.valid_reminder_leases, staged)
            snapshots = {(row["id"], row["lease_token"], row["updated_at"]) for row in valid}
            ready = []
            for item in staged:
                if self.notifications.get(item["id"]) is item and (item["id"], item["lease_token"], item["updated_at"]) in snapshots:
                    item["status"] = "pending_delivery"
                    ready.append(item)
            return ready
        finally:
            for item in staged:
                if self.notifications.get(item["id"]) is item and item["status"] == "validating":
                    self.notifications.pop(item["id"], None)

    async def _send_notification_batch(self, device, candidates):
        if not candidates:
            return
        try:
            valid = await asyncio.to_thread(self.store.valid_reminder_leases, candidates)
            snapshots = {(row["id"], row["lease_token"], row["updated_at"]) for row in valid}
            # 完整等待存储复核；网络预算不提前丢下仍在执行的数据库线程。
            # 一个慢目标的整批发送受同一期限限制，之后继续其他设备与计时卡。
            async with asyncio.timeout(NOTIFICATION_SEND_TIMEOUT):
                for item in candidates:
                    if item.get("status") == "pending_delivery" and (item["id"], item["lease_token"], item["updated_at"]) in snapshots:
                        await device.send_notification(item)
        except Exception:
            LOGGER.exception("notification_send_failed")

    async def refresh_memory_context(self):
        for session in tuple(self.voice_sessions):
            if session.backend_connected:
                try:
                    await session.refresh_memory_context()
                except Exception:
                    LOGGER.exception("memory_context_refresh_failed session=%s", session.session_id)
                    # 不能继续使用已删除的旧记忆；刷新失败结束这一语音回合。
                    await session.socket.close()

    async def notifications_loop(self):
        last_pruned = 0
        last_screen_sync = 0.0
        last_timer_signature = None
        while True:
            try:
                now = asyncio.get_running_loop().time()
                if now - last_pruned >= 3600:
                    await asyncio.to_thread(self.store.prune_tasks, limit=100)
                    last_pruned = now
                utc_now = datetime.now(timezone.utc)
                for identity, item in tuple(self.notifications.items()):
                    expiry = datetime.fromisoformat(item["lease_until"].replace("Z", "+00:00"))
                    if expiry <= utc_now:
                        self.notifications.pop(identity, None)
                available = 100 - len(self.notifications)
                rows = await asyncio.to_thread(self.store.claim_due_reminders, lease_seconds=60, limit=min(20, available)) if available > 0 else []
                ready = await self._register_claimed_notifications(rows)
                for device in tuple(self.native_devices):
                    await self._send_notification_batch(device, ready)
                if now - last_screen_sync >= 5:
                    timer = await self.timer_snapshot()
                    signature = (timer["id"], timer["revision"]) if timer else None
                    if timer and (timer["status"] in {"running", "paused"} or signature != last_timer_signature) and self.native_devices:
                        await self.send_timer_screen_card(timer)
                    last_timer_signature = signature
                    last_screen_sync = now
                # 没有设备确认的提醒保持待交付；租约到期后由存储重新抢占。
            except asyncio.CancelledError:
                raise
            except Exception:
                LOGGER.exception("reminder_dispatch_failed")
            await asyncio.sleep(2)


class ConversationSocket:
    """把一个语音回合与常驻控制连接隔离，回合结束不关闭设备连接。"""

    def __init__(self, device):
        self.device = device
        self.queue = asyncio.Queue(maxsize=150)
        self.closed = False
        self.early_wake_input = False
        self.pending_audio_bytes = 0
        self.created_at = asyncio.get_running_loop().time()
        self.input_started = False

    def __aiter__(self):
        return self

    async def __anext__(self):
        message = await self.queue.get()
        if message is None:
            raise StopAsyncIteration
        self.input_started = True
        if self.early_wake_input and message.type == WSMsgType.BINARY:
            self.pending_audio_bytes -= len(message.data)
        return message

    def feed(self, message):
        audio_bytes = len(message.data) if message.type == WSMsgType.BINARY else 0
        if self.early_wake_input and (audio_bytes > 3840 or self.pending_audio_bytes + audio_bytes > 256000 or not self.input_started and asyncio.get_running_loop().time() - self.created_at > 4):
            raise ValueError("提前唤醒输入超过单包、积压字节或4秒等待上限")
        if self.queue.full():
            raise ValueError("语音输入消息队列已满，停止本次候选")
        self.queue.put_nowait(message)
        if self.early_wake_input:
            self.pending_audio_bytes += audio_bytes

    async def send_json(self, payload):
        if not self.closed:
            await self.device.send_json(payload)

    async def send_bytes(self, payload):
        if not self.closed:
            await self.device.send_bytes(payload)

    def exception(self):
        return self.device.socket.exception()

    async def close(self, **kwargs):
        self.closed = True
        while not self.queue.empty():
            self.queue.get_nowait()
        self.pending_audio_bytes = 0
        self.queue.put_nowait(None)


class NativeDevice:
    def __init__(self, socket, runtime, session_factory):
        self.socket, self.runtime, self.session_factory = socket, runtime, session_factory
        self.conversation = None
        self.task = None
        self._session = None
        self.send_lock = asyncio.Lock()
        self.paired_device_id = runtime.config.get("device_id", "")
        self.firmware_version = "unknown"
        self.wake_phrase = DEFAULT_WAKE_PHRASE
        self.screen_cards_supported = False
        self.media_stream_supported = False
        self.media_wake_word_supported = False
        self.last_screen_receipt = None
        self.last_screen_card = None
        self.last_wake_detected_at = None
        self.last_wake_id = None
        self.wake_clip_supported = False
        self.wake_input_ready_supported = False
        self.manual_input_ready_supported = False
        self.natural_barge_in_supported = False

    async def send_json(self, payload):
        async with self.send_lock:
            await self.socket.send_json(payload)

    async def send_bytes(self, payload):
        async with self.send_lock:
            await self.socket.send_bytes(payload)

    async def send_notification(self, item):
        identity, token, revision = item["id"], item["lease_token"], item["updated_at"]
        async with asyncio.timeout(NOTIFICATION_SEND_TIMEOUT):
            async with self.send_lock:
                current = self.runtime.notifications.get(identity)
                if self.socket.closed or current is not item or current.get("status") != "pending_delivery" or current["lease_token"] != token or current["updated_at"] != revision:
                    return
                if datetime.fromisoformat(current["lease_until"].replace("Z", "+00:00")) <= datetime.now(timezone.utc):
                    return
                await self.socket.send_json({"type": "notification", "notification": {**item, "title": reminder_title(item)}})

    async def start(self, mode="auto"):
        async with self.runtime.voice_lock:
            if self.task is not None and not self.task.done():
                return
            navigation = getattr(self.runtime, "navigation_demo", None)
            if navigation is not None:
                await navigation.stop()
            speech = getattr(self.runtime, "proactive_speech", None)
            if speech is not None:
                await speech.stop()
            media = getattr(self.runtime, "media_service", None)
            if media is not None:
                await media.before_voice_session()
            music = getattr(self.runtime, "device_music", None)
            if music is not None:
                await music.before_voice_session()
            self.conversation = ConversationSocket(self)
            session = self.session_factory(self.conversation)
            session.manual = mode == "manual"
            session.require_wake_phrase = mode != "manual"
            session.wake_phrase = self.wake_phrase
            session.manual_input_ready_supported = self.manual_input_ready_supported
            session.natural_barge_in_supported = self.natural_barge_in_supported
            session.wake_clip_supported = self.wake_clip_supported and session.require_wake_phrase
            self.conversation.early_wake_input = self.wake_input_ready_supported and session.wake_clip_supported and session.config.get("llm_provider", "qwen_realtime") == "qwen_realtime"
            if session.require_wake_phrase and self.last_wake_detected_at is not None and asyncio.get_running_loop().time() - self.last_wake_detected_at < 10:
                session.wake_detected_at = self.last_wake_detected_at
                session.wake_id = self.last_wake_id
            self.last_wake_detected_at = None
            self.last_wake_id = None
            LOGGER.info("native_listen_start session=%s mode=%s wake_verification=%s", session.session_id, mode, session.require_wake_phrase)
            if len(self.runtime.voice_sessions) >= self.runtime.config.get("max_connections", 1):
                await self.send_json({"type": "alert", "status": "正在使用", "message": "其他语音会话正在使用，请稍后唤醒。", "emotion": "neutral"})
                self.conversation.closed = True
                return
            self.runtime.voice_sessions.add(session)
            self._session = session
            self.task = asyncio.create_task(self.run_conversation(session))
            if self.conversation.early_wake_input and session.wake_id:
                # 只表示有界传输队列已建立；云连接和口令验证仍由VoiceSession完成。
                await self.send_json({"type": "wake_input_ready", "wake_id": session.wake_id})

    async def run_conversation(self, session):
        try:
            await session.run()
        finally:
            await self._finish_conversation(session, asyncio.current_task())

    async def _finish_conversation(self, session, owner_task):
        self.runtime.voice_sessions.discard(session)
        async with self.send_lock:
            if self._session is not session or self.task is not owner_task:
                return
            try:
                if not any(not getattr(other, "browser", False) and not getattr(other, "closed", False) for other in self.runtime.voice_sessions):
                    self.runtime.last_device_state = "idle"
                if not self.socket.closed:
                    await self.socket.send_json({"type": "custom", "payload": {"state": "idle", "detail": f"说“{wake_phrase_display(self.wake_phrase)}”可继续"}})
            finally:
                if self._session is session and self.task is owner_task:
                    self._session = None

    async def handle(self, message):
        from aiohttp import WSMsgType
        if message.type == WSMsgType.TEXT:
            event = json.loads(message.data)
            if not isinstance(event, dict):
                raise ValueError("设备消息必须为对象")
            kind = event.get("type")
            if kind == "hello":
                wake_phrase = device_wake_phrase(event)
                self.firmware_version = str(event.get("firmware_version") or "unknown")[:64]
                self.wake_phrase = wake_phrase
                features = event.get("features") if isinstance(event.get("features"), dict) else {}
                self.screen_cards_supported = features.get("screen_cards") is True
                self.media_stream_supported = features.get("media_stream") is True
                self.media_wake_word_supported = features.get("media_wake_word") is True
                self.wake_clip_supported = features.get("wake_clip_v1") is True
                self.wake_input_ready_supported = features.get("wake_input_ready_v1") is True
                self.manual_input_ready_supported = features.get("manual_input_ready_v1") is True
                self.natural_barge_in_supported = features.get("natural_barge_in_v1") is True
                if isinstance(event.get("power"), dict):
                    self.latest_device_power = {"status": str(event["power"].get("status") or "unknown")[:16], "source": str(event["power"].get("source") or "device")[:80]}
                return
            if kind == "device_preferences_applied":
                self.last_device_preferences_receipt = {key: event.get(key) for key in ("audio", "display", "battery_known")}
                return
            if kind in {"media_ack", "media_progress"}:
                music = getattr(self.runtime, "device_music", None)
                if music is not None:
                    await music.on_device_event(self, event)
                return
            if kind != "screen_action":
                self.last_screen_action_reference = None
            if kind == "screen_card_ack":
                card_id = event.get("card_id")
                if not isinstance(card_id, str) or not 1 <= len(card_id) <= 80 or event.get("status") not in {"shown", "queued", "expired"}:
                    raise ValueError("屏幕回执无效")
                metadata = self.last_screen_card if self.last_screen_card and self.last_screen_card["card_id"] == card_id else {}
                self.last_screen_receipt = {"card_id": card_id, "status": event["status"], "timestamp": datetime.now(timezone.utc).isoformat(), "kind": metadata.get("kind", "unknown"), "card_status": metadata.get("status", "unknown"), "source": metadata.get("source", "unknown")}
                LOGGER.info("screen_card_receipt card_id=%s kind=%s status=%s source=%s device=%s", screen_card_log_id(card_id), self.last_screen_receipt["kind"], event["status"], self.last_screen_receipt["source"], screen_card_log_id(self.paired_device_id))
                return
            if kind == "screen_action":
                action = event.get("action")
                identifier = event.get("reference_id")
                revision = event.get("revision")
                if not isinstance(identifier, str) or not 1 <= len(identifier) <= 80:
                    raise ValueError("屏幕操作标识无效")
                self.last_screen_action_reference = identifier
                if action in {"pause", "resume", "cancel"}:
                    if not isinstance(revision, str) or not revision.isdigit() or len(revision) > 9:
                        raise ValueError("计时状态版本无效")
                    timer = await self.runtime.timer_action(action, identifier, int(revision))
                    await self.runtime.send_timer_screen_card(timer)
                elif action == "snooze":
                    item = await asyncio.to_thread(self.runtime.store.snooze_reminder, identifier, revision, minutes=5)
                    self.runtime.notifications.pop(identifier, None)
                    await self.runtime.send_screen_card({"kind": "task", "title": item["title"], "detail": "已稍后5分钟提醒"})
                else:
                    raise ValueError("屏幕操作不受支持")
                await self.send_json({"type": "screen_action_result", "reference_id": identifier, "status": "accepted"})
                self.last_screen_action_reference = None
                return
            if kind == "device_ping":
                # 设备侧断线自愈心跳：有下行即证明链路双向可用。
                await self.send_json({"type": "device_pong"})
                return
            if kind == "audio_stats":
                fields = ("frames", "underruns", "overflows", "elapsed_ms")
                if not all(type(event.get(key)) in (int, float) and 0 <= event[key] <= 10_000_000 for key in fields):
                    raise ValueError("音频统计字段无效")
                optional = ("first_write_after_tts_ms", "first_write_after_receive_ms", "first_display_ms")
                timing = {key: event[key] for key in optional if type(event.get(key)) is int and 0 <= event[key] <= 60000}
                LOGGER.info("device_audio_stats %s", json.dumps({**{key: event[key] for key in fields}, **timing}))
                return
            if kind == "notification_ack":
                if not isinstance(event.get("id"), str) or not 1 <= len(event["id"]) <= 64 or not isinstance(event.get("lease_token"), str):
                    raise ValueError("设备提醒确认必须携带租约令牌")
                item = await self.runtime.acknowledge(event["id"], event["lease_token"])
                await self.send_json({"type": "reminder_state", "reference_id": event["id"], "revision": item["updated_at"]})
                return
            if kind == "device_state":
                if event.get("state") in ("idle", "listening", "speaking", "connecting", "thinking", "executing"):
                    self.runtime.last_device_state = event["state"]
                return
            if kind == "barge_in":
                phase = event.get("phase")
                candidate_id = event.get("candidate_id")
                playback_id = event.get("playback_id")
                played_samples = event.get("played_samples")
                if phase not in ("candidate", "confirmed", "resume") or type(candidate_id) is not int or not 0 <= candidate_id <= 0xFFFFFFFF or not isinstance(playback_id, str) or not 1 <= len(playback_id) <= 128 or type(played_samples) is not int or not 0 <= played_samples <= 0xFFFFFF:
                    raise ValueError("barge_in字段无效")
            if kind == "memory_request":
                request_id = event.get("request_id")
                if not isinstance(request_id, str) or len(request_id) > 64:
                    raise ValueError("memory_request 需要不超过64字符的 request_id")
                snapshot = await asyncio.to_thread(self.runtime.store.recall_memory, limit=30)
                items = []
                budget = 0
                for item in snapshot.get("memories", []):
                    size = len(json.dumps(item, ensure_ascii=False).encode("utf-8"))
                    if budget + size > 5000:
                        break
                    items.append(item)
                    budget += size
                await self.send_json({"type": "memory_snapshot", "request_id": request_id, "items": items, "partial": len(items) < len(snapshot.get("memories", [])) or bool(snapshot.get("next_cursor")), "storage_role": "cache"})
                return
            if kind == "listen" and event.get("state") == "start":
                await self.start(event.get("mode", "auto"))
            elif kind == "listen" and event.get("state") == "detect":
                wake_id = event.get("wake_id")
                if isinstance(wake_id, str) and 1 <= len(wake_id) <= 64:
                    self.last_wake_detected_at = asyncio.get_running_loop().time()
                    self.last_wake_id = wake_id
                    LOGGER.info("native_wake_detect wake_id=%s", wake_id)
                return
            elif kind == "session_end":
                await self.end_conversation()
                return
        if self.conversation is not None and not self.conversation.closed:
            try:
                self.conversation.feed(message)
            except ValueError:
                LOGGER.exception("native_wake_input_buffer_rejected")
                await self.conversation.close()
                await self.end_conversation()

    async def end_conversation(self):
        if self.task is not None:
            task = self.task
            session = self._session
            if not task.cancelling():
                task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            try:
                # create_task尚未首次调度就被取消时，run的finally不会执行。
                if session is not None:
                    await self._finish_conversation(session, task)
            finally:
                if self.task is task:
                    self.task = None

    async def run(self):
        self.runtime.native_devices.add(self)
        try:
            await self.send_json({"type": "hello", "version": 1, "transport": "websocket", "capabilities": {"two_stage_barge_in": True, "wake_clip_v1": True, "wake_input_ready_v1": True, "manual_input_ready_v1": True}, "audio_params": {"format": "pcm_s16le", "sample_rate": 24000, "channels": 1, "frame_duration": 20}})
            preferences = await self.runtime.device_preferences()
            await self.send_json({"type": "device_preferences", **preferences})
            await self.runtime.send_screen_card({"kind": "power", "title": "电量状态", "detail": "电量未知 · 未获得可验证的 AXP2101 读数", "ttl_s": 12}, target=self)
            await self.send_json({"type": "custom", "payload": {"state": "idle", "detail": f"说“{wake_phrase_display(self.wake_phrase)}”唤醒我"}})
            timer = await self.runtime.timer_snapshot()
            if timer and timer["status"] in {"running", "paused", "finished"}:
                await self.runtime.send_timer_screen_card(timer, target=self)
            else:
                await self.runtime.send_today_screen_card(target=self)
            candidates = [item for item in tuple(self.runtime.notifications.values()) if item.get("status") == "pending_delivery"]
            await self.runtime._send_notification_batch(self, candidates)
            async for message in self.socket:
                try:
                    await self.handle(message)
                except (ValueError, KeyError, IdempotencyConflict, LeaseConflict, NotFoundError, OSError) as error:
                    LOGGER.exception("native_message_rejected type=%s", type(error).__name__)
                    reference = getattr(self, "last_screen_action_reference", None)
                    if reference:
                        await self.send_json({"type": "screen_action_result", "reference_id": reference, "status": "rejected"})
                        self.last_screen_action_reference = None
                    await self.send_json({"type": "alert", "status": "请求未完成", "message": "消息格式无效或提醒已经过期，请刷新后重试。", "emotion": "neutral"})
        finally:
            await self.end_conversation()
            self.runtime.native_devices.discard(self)
