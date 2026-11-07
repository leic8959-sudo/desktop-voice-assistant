"""后台和语音共用的模拟导航；单实例、单任务，语音退出后再交接音频通道。"""

import asyncio
import logging
import re
import time
import uuid

LOGGER = logging.getLogger("assistant.navigation_demo")
NAVIGATION_DEMO_STEPS = (
    ("straight", "开始模拟导航，全程12公里，预计25分钟。沿当前道路直行。"),
    ("right", "直行2.4公里后，前方300米请右转，进入文一路。"),
    ("straight", "已右转。沿文一路直行2公里，途经文一新村路段。"),
    ("left", "前方150米请左转，进入学院路，注意路口行人。"),
    ("uturn", "前方80米请掉头，注意对向来车。"),
    ("straight", "前方300米到达目的地，目的地在道路右侧。"),
    ("flag", "已到达目的地附近，本次导航演示结束。"),
)


def is_navigation_demo_request(text):
    return bool(
        re.search(r"(?:演示|模拟|示范).{0,8}导航|导航.{0,8}(?:演示|模拟|示范)", text)
        and not re.search(r"不要|不用|别|不想|取消|停止|为什么|为何|怎么|如何|假如|假设|例如|比如|他说|她说|翻译", text)
    )


class NavigationDemoService:
    def __init__(self, runtime):
        self.runtime = runtime
        self.pending = None
        self.task = None
        self.closed = False
        self.last_result = None

    def _available(self):
        speech = getattr(self.runtime, "proactive_speech", None)
        if self.closed or speech is None or speech.closed:
            return "语音播报服务不可用"
        _, reason = speech._device()
        if reason:
            return "请连接唯一一台支持导航播报的小燕子"
        if any(not self.runtime.config.get(key) for key in ("aliyun_api_key", "chat_tts_model", "chat_tts_voice")):
            return "导航演示语音尚未配置"
        return None

    def _busy(self):
        return self.pending is not None or self.task is not None and not self.task.done()

    async def schedule(self, session):
        reason = self._available()
        if reason:
            return {"status": "unavailable", "message": reason}
        if self.pending is not None and self.pending["session"] is session:
            return {"status": "scheduled", "message": "准备开始模拟导航。"}
        if self._busy():
            return {"status": "blocked", "message": "导航演示进行中"}
        self.pending = {"session": session, "ready": False, "expires": time.monotonic() + 120}
        LOGGER.info("navigation_demo_scheduled session=%s", session.session_id)
        return {"status": "scheduled", "message": "准备开始模拟导航。", "simulation": True}

    async def cancel_voice_request(self, session):
        if self.pending is not None and self.pending["session"] is session:
            await self.stop()

    async def voice_response_finished(self, session):
        pending = self.pending
        if pending is None or pending["session"] is not session:
            return False
        if pending["expires"] <= time.monotonic():
            self.pending = None
            return False
        if not pending["ready"]:
            pending["ready"] = True
            self.task = asyncio.create_task(self._handoff(pending), name="navigation-demo")
        return True

    async def _handoff(self, pending):
        try:
            async with asyncio.timeout(15):
                while not pending["session"].closed or pending["session"] in self.runtime.voice_sessions or any(device.task is not None and not device.task.done() for device in tuple(self.runtime.native_devices)):
                    await asyncio.sleep(0.05)
            if self.pending is pending:
                self.last_result = await self._run()
        except asyncio.CancelledError:
            raise
        except Exception:
            LOGGER.exception("navigation_demo_handoff_failed")
            self.last_result = {"status": "failed", "message": "导航演示未启动"}
        finally:
            if self.pending is pending:
                self.pending = None

    async def run(self):
        reason = self._available()
        if reason:
            return {"status": "unavailable", "message": reason}
        if self._busy():
            return {"status": "blocked", "message": "导航演示进行中"}
        task = self.task = asyncio.create_task(self._run(), name="navigation-demo")
        try:
            self.last_result = await task
            return self.last_result
        finally:
            if self.task is task:
                self.task = None

    async def _run(self):
        deliveries = []
        identity = uuid.uuid4().hex
        LOGGER.info("navigation_demo_started id=%s", identity)
        try:
            for index, (icon, text) in enumerate(NAVIGATION_DEMO_STEPS):
                display = await self.runtime.proactive_speech.navigation_step(identity, icon)
                if display.get("status") != "sent":
                    return {"status": "aborted", "completed_steps": index, "failed_step": index + 1, "reason": display.get("reason", "indicator_unavailable"), "deliveries": deliveries}
                result = await self.runtime.proactive_speech.speak(text, kind="summary", event_id=f"navigation-demo-{identity}-{index + 1}", settings_gated=False, payload_extra={"nav_icon": icon})
                deliveries.append({"step": index + 1, "icon": icon, "text": text, **result})
                LOGGER.info("navigation_demo_step id=%s step=%s status=%s", identity, index + 1, result.get("status"))
                if result.get("status") != "sent":
                    return {"status": "aborted", "completed_steps": index, "failed_step": index + 1, "reason": str(result.get("reason") or result.get("message") or "unknown")[:120], "deliveries": deliveries}
                await asyncio.sleep(1.5)
            return {"status": "completed", "steps": len(deliveries), "deliveries": deliveries, "simulation": True}
        finally:
            await self.runtime.proactive_speech.navigation_end(identity)

    async def stop(self):
        self.pending = None
        task = self.task
        if task is not None and not task.done():
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        if self.task is task:
            self.task = None

    async def close(self):
        self.closed = True
        await self.stop()
