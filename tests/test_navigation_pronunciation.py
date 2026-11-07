"""导航合成文字与屏幕原文分离，不修改普通播报。"""

import asyncio
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

from src.assistant_app.application.proactive_speech import ProactiveSpeech


class NavigationPronunciationTests(unittest.IsolatedAsyncioTestCase):
    async def test_navigation_uses_unambiguous_speech_and_original_screen_text(self):
        text = "沿当前道路直行，直行2.4公里后右转。"
        collector = SimpleNamespace(synthesize=AsyncMock(return_value=b"\0" * 960))
        device = SimpleNamespace(send_json=AsyncMock(), send_bytes=AsyncMock())
        speaker = SimpleNamespace(_blocked=lambda *args, **kwargs: None)
        with patch("src.assistant_app.application.proactive_speech.asyncio.sleep", new_callable=AsyncMock):
            await ProactiveSpeech._produce(speaker, collector, device, text, "nav-test", asyncio.Event(), False, {"nav_icon": "straight"})
        collector.synthesize.assert_awaited_once_with("沿当前道路直走，直走2.4公里后右转。")
        self.assertEqual(device.send_json.await_args_list[0].args[0]["text"], text)
        self.assertEqual(device.send_json.await_args_list[0].args[0]["nav_icon"], "straight")

    async def test_indicator_messages_have_identity_and_never_send_tts_stop(self):
        device = SimpleNamespace(send_json=AsyncMock())
        speaker = ProactiveSpeech({}, SimpleNamespace(voice_lock=asyncio.Lock()), None)
        speaker._blocked = lambda **kwargs: None
        speaker._device = lambda: (device, None)
        await speaker.navigation_step("new-navigation", "right")
        await speaker.navigation_end("old-navigation")
        self.assertEqual(device.send_json.await_args_list[0].args[0], {"type": "navigation", "state": "step", "navigation_id": "new-navigation", "nav_icon": "right"})
        self.assertEqual(device.send_json.await_args_list[1].args[0], {"type": "navigation", "state": "end", "navigation_id": "old-navigation"})

    async def test_busy_session_blocks_indicator_before_any_device_message(self):
        device = SimpleNamespace(send_json=AsyncMock())
        speaker = ProactiveSpeech({}, SimpleNamespace(voice_lock=asyncio.Lock()), None)
        speaker._blocked = lambda **kwargs: "voice_session_active"
        speaker._device = lambda: (device, None)
        result = await speaker.navigation_step("test", "straight")
        self.assertEqual(result["reason"], "voice_session_active")
        device.send_json.assert_not_awaited()

    async def test_ordinary_speech_is_unchanged(self):
        text = "直行的行是多音字。"
        collector = SimpleNamespace(synthesize=AsyncMock(return_value=b"\0" * 960))
        device = SimpleNamespace(send_json=AsyncMock(), send_bytes=AsyncMock())
        speaker = SimpleNamespace(_blocked=lambda *args, **kwargs: None)
        with patch("src.assistant_app.application.proactive_speech.asyncio.sleep", new_callable=AsyncMock):
            await ProactiveSpeech._produce(speaker, collector, device, text, "speech-test", asyncio.Event())
        collector.synthesize.assert_awaited_once_with(text)


if __name__ == "__main__":
    unittest.main()
