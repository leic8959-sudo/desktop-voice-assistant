"""伊迪斯完整唤醒词的真实 ASR 同音字回归。"""

import unittest

from gateway import VoiceSession
from wake_phrase import wake_phrase_variants


class WakeNameTests(unittest.TestCase):
    def test_exact_and_observed_homophone_require_complete_prefix(self):
        for text in ("你好，伊迪斯。", "你好，伊迪丝。", "你好伊迪丝打开计算器"):
            with self.subTest(text=text):
                self.assertEqual(VoiceSession._wake_transcript_state(text, "")[1], "verified")

    def test_split_homophone_remains_pending_until_complete(self):
        prefix, state = VoiceSession._wake_transcript_state("你好，伊迪", "")
        self.assertEqual(state, "pending")
        self.assertEqual(VoiceSession._wake_transcript_state("丝。", prefix)[1], "verified")

    def test_unrelated_or_embedded_phrases_do_not_pass(self):
        for text in ("你好伊迪", "你好伊迪思", "不要你好伊迪丝", "请说你好伊迪斯", "伊迪丝", "你好小燕子", "打开计算器"):
            with self.subTest(text=text):
                self.assertNotEqual(VoiceSession._wake_transcript_state(text, "")[1], "verified")

    def test_cached_ack_only_for_complete_wakes(self):
        for text in ("你好伊迪丝", "你好伊迪斯你好伊迪丝"):
            self.assertTrue(VoiceSession._is_wake_only(text))
        for text in ("你好伊迪丝打开计算器", "你好伊迪", "不要你好伊迪丝"):
            self.assertFalse(VoiceSession._is_wake_only(text))

    def test_alias_does_not_expand_other_devices_wake_words(self):
        self.assertEqual(wake_phrase_variants("你好小智"), ("你好小智",))
        self.assertEqual(wake_phrase_variants("你好小燕子"), ("你好小燕子",))


class WakeNameAudioTests(unittest.IsolatedAsyncioTestCase):
    async def submit(self, text):
        from tests.test_wake_clip import make_voice

        voice, backend = make_voice(text)
        await backend.send_user_audio(b"\x00\x00" * 3200)
        await backend.end_wake_clip({"wake_id": voice.wake_id, "audio_bytes": 6400, "reason": "silence"})
        with self.assertRaisesRegex(RuntimeError, "云端连接已结束"):
            await voice.cloud_output()
        for task in voice.tasks:
            await task
        return voice

    async def test_observed_alias_reaches_cached_audio_reply(self):
        voice = await self.submit("你好，伊迪丝。")
        self.assertTrue(voice.wake_phrase_verified)
        self.assertEqual([item["text"] for item in voice.socket.sent if item.get("state") == "sentence_start"], ["我在。"])

    async def test_command_after_alias_is_preserved_without_cached_greeting(self):
        voice = await self.submit("你好伊迪丝，打开计算器")
        self.assertTrue(voice.wake_phrase_verified)
        voice.observe_user_text.assert_awaited_once_with("你好伊迪丝，打开计算器", refresh=False, source="verified_audio")
        self.assertFalse(any(item.get("state") == "sentence_start" for item in voice.socket.sent))
