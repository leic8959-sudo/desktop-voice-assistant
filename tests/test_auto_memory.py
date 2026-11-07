"""自动记忆闭环回归：临时数据库与模拟模型，不访问真实账号或设备。"""

import asyncio
import json
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from assistant_core.store import AssistantStore
from assistant_runtime import RUNTIME_KEY
from gateway import CONFIG_KEY, VoiceSession
from src.assistant_app.application.auto_memory import AutoMemory
from src.assistant_app.api.auto_memory import MEMORY_KEY
from tests import test_gateway_v1 as gateway_fixture


class AutomaticMemoryTests(unittest.IsolatedAsyncioTestCase):
    async def test_manual_voice_request_accepts_polite_question_without_new_confirmation(self):
        result = await self.memory.voice_tool("remember_fact", {"key": "饮食", "value": "清淡"}, "把这个偏好保存一下，可以吗？我喜欢清淡。")
        self.assertIn("memory_id", result)
        self.assertEqual((await self.memory.context("饮食"))["memories"][0]["value"], "清淡")
        await self.memory.voice_tool("update_memory", {"memory_id": result["memory_id"], "value": "辣一点"}, "帮我换成辣一点，好吗？")
        await self.memory.voice_tool("delete_memory", {"memory_id": result["memory_id"]}, "这个删掉就好，可以吗？")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_manual_fact_still_requires_source_and_real_targets(self):
        with self.assertRaises(PermissionError):
            await self.memory.voice_tool("remember_fact", {"key": "姓名", "value": "模型推测"}, "我的名字可以保存吗？")
        with self.assertRaises(PermissionError):
            await self.memory.voice_tool("remember_fact", {"key": "密码", "value": "abcdef"}, "记住密码abcdef")
        with self.assertRaises(ValueError):
            await self.memory.voice_tool("delete_memory", {"memory_id": "invented"}, "删这个")
        result = await self.memory.voice_tool("remember_fact", {"key": "姓名", "value": "林林"}, "可以把我的名字林林记下来吗？")
        self.assertIn("memory_id", result)
        with self.assertRaises(ValueError):
            await self.memory.voice_tool("delete_memory", {"memory_id": result["memory_id"], "key": "其他主题"}, "删掉那个")

    async def test_privacy_toggle_uses_boolean_contract_even_when_reason_mentions_secrets(self):
        await self.memory.voice_tool("memory_privacy", {"enabled": False}, "因为不想泄露密码，可以把自动记忆停一下吗？")
        self.assertFalse(self.memory.settings["enabled"])
        with self.assertRaises(ValueError):
            await self.memory.voice_tool("memory_privacy", {"enabled": "false"}, "停一下")

    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "memory.sqlite3"
        self.runtime = SimpleNamespace(refresh_memory_context=AsyncMock())
        self.store = AssistantStore(self.path, device_id="memory-regression")
        self.memory = AutoMemory({}, self.store, self.runtime, None)
        await self.memory.start()

    async def asyncTearDown(self):
        await self.memory.close()
        self.store.close()
        self.directory.cleanup()

    async def submit(self, text, **options):
        result = self.memory.submit(text, source_id="synthetic-regression", **options)
        await asyncio.wait_for(self.memory.queue.join(), timeout=3)
        return result

    @staticmethod
    def candidate(text, value, key="回答风格", category="preference"):
        return {"key": key, "value": value, "category": category, "evidence": text, "confidence": 0.99}

    async def test_polite_question_and_first_person_free_preferences_save(self):
        with patch("src.assistant_app.application.knowledge.complete_text", new_callable=AsyncMock) as model:
            await self.submit("我希望以后回答简洁一点，可以吗？")
            self.assertEqual(self.memory.last_status["status"], "saved")
            self.assertEqual((await self.memory.context())["memories"][0]["value"], "简洁一点")
            await self.submit("以后回答详细一点")
            self.assertEqual((await self.memory.context())["memories"][0]["value"], "详细一点")
            model.assert_not_awaited()

    async def test_mixed_temporary_and_long_term_statement_only_saves_stable_part(self):
        text = "今天很累，今后解释步骤清晰一些。"
        candidate = self.candidate("今后解释步骤清晰一些", "步骤清晰一些")
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value=json.dumps({"memories": [candidate]}))):
            await self.submit(text)
        self.assertEqual(self.memory.last_status["status"], "saved")
        self.assertEqual((await self.memory.context())["memories"][0]["value"], "步骤清晰一些")

    async def test_model_cannot_strip_temporary_limits_or_save_questions_and_personal_facts(self):
        cases = [
            ("我喜欢吃面，但只限今天", "我喜欢吃面", "面", "饮食偏好"),
            ("今天我喜欢吃面", "我喜欢吃面", "面", "饮食偏好"),
            ("我喜欢吃面吗？", "我喜欢吃面", "面", "饮食偏好"),
            ("如果我喜欢吃面会怎样", "我喜欢吃面", "面", "饮食偏好"),
            ("他说我喜欢吃面", "我喜欢吃面", "面", "饮食偏好"),
            ("引用：我喜欢吃面", "我喜欢吃面", "面", "饮食偏好"),
            ("我叫小明", "我叫小明", "小明", "长期称谓"),
            ("我的职业是设计师", "我的职业是设计师", "设计师", "长期项目"),
            ("我是设计师", "我是设计师", "设计师", "长期项目"),
            ("他喜欢吃面。", "他喜欢吃面。", "面", "饮食偏好"),
            ("医生都喜欢清淡饮食", "医生都喜欢清淡饮食", "清淡饮食", "饮食偏好"),
            ("我觉得他喜欢吃面", "我觉得他喜欢吃面", "面", "饮食偏好"),
            ("以后都这样", "以后都这样", "这样", "回答风格"),
        ]
        for text, evidence, value, key in cases:
            with self.subTest(text=text):
                candidate = self.candidate(evidence, value, key)
                with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value=json.dumps({"memories": [candidate]}))):
                    await self.submit(text)
                self.assertEqual(self.memory.last_status["status"], "candidate_rejected")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_negative_preference_keeps_negation_in_saved_value(self):
        text = "我不喜欢吃辣"
        candidates = [self.candidate(text, "辣", "饮食偏好"), self.candidate(text, "不喜欢吃辣", "饮食偏好")]
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(side_effect=[json.dumps({"memories": [item]}) for item in candidates])):
            await self.submit(text)
            self.assertEqual(self.memory.last_status["status"], "candidate_rejected")
            await self.submit(text)
            self.assertEqual(self.memory.last_status["status"], "saved")
        self.assertEqual((await self.memory.context())["memories"][0]["value"], "不喜欢吃辣")

    async def test_preference_identity_wording_is_not_misclassified_as_personal_fact(self):
        for text, value in (("我是更喜欢清淡饮食", "清淡饮食"), ("I am fond of quiet conversation", "quiet conversation")):
            with self.subTest(text=text):
                self.assertTrue(self.memory._valid_candidate(self.candidate(text, value), text))

    async def test_queue_processing_capacity_and_expired_observation_are_explained(self):
        started, release = asyncio.Event(), asyncio.Event()
        async def model(*_args, **_kwargs):
            started.set()
            await release.wait()
            return '{"memories": []}'
        with patch("src.assistant_app.application.knowledge.complete_text", model):
            queued = self.memory.submit("普通聊天不包含稳定偏好", source_id="offline")
            self.assertEqual(queued["status"], "queued")
            await asyncio.wait_for(started.wait(), 2)
            self.assertEqual(self.memory.last_status["status"], "processing")
            for _ in range(32):
                self.memory.submit("我喜欢吃面。", source_id="offline")
            with self.assertLogs("assistant.memory", level="WARNING"):
                overflow = self.memory.submit("我喜欢吃面。", source_id="offline")
            self.assertEqual(overflow["status"], "queue_full")
            await self.memory.configure({"enabled": False})
            self.assertEqual(self.memory.queue.qsize(), 0)
            release.set()
            await asyncio.wait_for(self.memory.queue.join(), 2)
        await self.memory.configure({"enabled": True})
        self.memory.observation_serial += 1
        self.memory.queue.put_nowait(("我喜欢吃面。", "offline", self.memory.epoch, self.memory.observation_serial, None, time.monotonic() - 121))
        await asyncio.wait_for(self.memory.queue.join(), 2)
        self.assertEqual(self.memory.last_status["reason"], "expired")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_modes_and_invalid_model_category_have_distinct_results(self):
        await self.memory.configure({"mode": "preferences_only"})
        await self.submit("我一直在开发小燕子。")
        self.assertEqual(self.memory.last_status["status"], "mode_excluded")
        await self.memory.configure({"mode": "review"})
        await self.submit("我喜欢吃面。")
        self.assertEqual(self.memory.last_status["status"], "review_required")
        await self.memory.configure({"mode": "preferences_and_long_term"})
        self.assertEqual(self.memory.pending_candidates, [])
        text = "我通常比较偏好清淡饮食"
        candidate = self.candidate(text, "清淡饮食", "饮食偏好", "fact")
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value=json.dumps({"memories": [candidate]}))):
            await self.submit(text)
        self.assertEqual(self.memory.last_status["status"], "candidate_rejected")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_storage_error_does_not_erase_already_committed_candidate_count(self):
        text = "我偏好简洁回答；我平时偏好清淡饮食"
        candidates = [self.candidate("我偏好简洁回答", "简洁回答"), self.candidate("我平时偏好清淡饮食", "清淡饮食", "饮食偏好")]
        original = self.memory._save_candidate
        def save(candidate, source_id):
            if candidate["key"] == "饮食偏好":
                raise RuntimeError("PRIVATE_STORAGE_DIAGNOSTIC")
            return original(candidate, source_id)
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value=json.dumps({"memories": candidates}))), patch.object(self.memory, "_save_candidate", save):
            with self.assertLogs("assistant.memory", level="ERROR"):
                await self.submit(text)
        self.assertEqual(self.memory.last_status["status"], "partial_saved")
        self.assertEqual(self.memory.last_status["saved"], 1)
        self.assertEqual(self.memory.last_status["outcomes"], {"saved": 1, "storage_failed": 1})
        self.assertEqual(len((await self.memory.context())["memories"]), 1)

    async def test_same_content_manual_protection_and_deleted_protection_have_distinct_results(self):
        await self.submit("我喜欢吃面。")
        await self.submit("我喜欢吃面。")
        self.assertEqual(self.memory.last_status["status"], "unchanged")
        await self.memory.manual_tool("remember_fact", {"key": "饮食偏好", "value": "清淡"})
        await self.submit("我喜欢吃辣。")
        self.assertEqual(self.memory.last_status["status"], "manual_protected")
        await self.memory.manual_tool("delete_memory", {"key": "饮食偏好"})
        await self.submit("我喜欢吃辣。")
        self.assertEqual(self.memory.last_status["status"], "deleted_protected")
        self.assertEqual(self.memory.last_status["saved"], 0)

    async def test_model_failure_invalid_json_and_empty_result_are_explained_without_raw_text(self):
        model = AsyncMock(side_effect=[RuntimeError("PRIVATE_DIAGNOSTIC_PAYLOAD"), "not-json", '{"other": []}', '{"memories": []}'])
        with patch("src.assistant_app.application.knowledge.complete_text", model):
            for _ in range(3):
                with self.assertLogs("assistant.memory", level="ERROR") as logs:
                    await self.submit("我偏好平静的交流语气")
                self.assertEqual(self.memory.last_status["status"], "model_failed")
                self.assertNotIn("PRIVATE_DIAGNOSTIC_PAYLOAD", "\n".join(logs.output))
                self.assertIn("Traceback", "\n".join(logs.output))
            await self.submit("今天是什么天气")
        self.assertEqual(self.memory.last_status["status"], "no_candidate")
        self.assertNotIn("utterance", self.memory.status()["last_extraction"])

    async def test_committed_save_remains_reported_if_context_refresh_fails(self):
        self.runtime.refresh_memory_context.side_effect = RuntimeError("PRIVATE_REFRESH_DIAGNOSTIC")
        with self.assertLogs("assistant.memory", level="ERROR"):
            await self.submit("我喜欢吃面。")
        self.assertEqual(self.memory.last_status["status"], "saved")
        self.assertEqual(self.memory.last_status["saved"], 1)
        self.assertEqual(self.memory.last_status["context_refresh"], "failed")
        self.assertEqual((await self.memory.context())["memories"][0]["value"], "面")

    async def test_context_is_bounded_to_actual_session_and_never_becomes_evidence(self):
        captured = []
        async def model(_config, _http, messages, **_options):
            captured.append(json.loads(messages[-1]["content"]))
            return '{"memories": []}'
        with patch("src.assistant_app.application.knowledge.complete_text", model):
            for index in range(6):
                await self.submit(f"第一会话普通聊天第{index}轮", session_id="one")
            await self.submit("第二会话普通聊天", session_id="two")
            await self.submit("没有会话标识的普通聊天")
        self.assertEqual(len(captured[5]["recent_user_utterances"]), 3)
        self.assertEqual(captured[-2]["recent_user_utterances"], [])
        self.assertEqual(captured[-1]["recent_user_utterances"], [])
        candidate = self.candidate("我长期在开发旧项目", "开发旧项目", "长期项目", "long_term")
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value=json.dumps({"memories": [candidate]}))):
            await self.submit("还是继续那个事情", session_id="one")
        self.assertEqual(self.memory.last_status["status"], "candidate_rejected")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_idle_context_expiry_capacity_and_privacy_clearing(self):
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value='{"memories": []}')):
            for index in range(18):
                await self.submit("本轮只是普通聊天", session_id=f"session-{index}")
        self.assertEqual(len(self.memory.recent_turns), 16)
        self.memory.recent_turns = {"expired": [(time.monotonic() - 121, "不应驻留的旧发言", 0)]}
        await asyncio.sleep(1.1)
        self.assertEqual(self.memory.recent_turns, {})
        await self.submit("我喜欢吃面。", session_id="privacy")
        result = await self.submit("这句话不要记住", session_id="privacy")
        self.assertEqual(result["reason"], "do_not_store")
        self.assertEqual(self.memory.recent_turns, {})
        await self.submit("我喜欢吃面。", session_id="privacy")
        await self.memory.configure({"enabled": False})
        self.assertEqual(self.memory.recent_turns, {})
        self.assertEqual((await self.submit("我喜欢吃面。"))["reason"], "disabled")

    async def test_inflight_extraction_cannot_save_after_privacy_change(self):
        started, release = asyncio.Event(), asyncio.Event()
        text = "我长期专注研究无障碍设计"
        async def model(*_args, **_kwargs):
            started.set()
            await release.wait()
            return json.dumps({"memories": [self.candidate(text, "研究无障碍设计", "长期项目", "long_term")]})
        with patch("src.assistant_app.application.knowledge.complete_text", model):
            self.memory.submit(text, source_id="offline", session_id="one")
            await asyncio.wait_for(started.wait(), 2)
            await self.memory.configure({"enabled": False})
            release.set()
            await asyncio.wait_for(self.memory.queue.join(), 2)
        self.assertEqual((await self.memory.context())["memories"], [])
        self.assertEqual(self.memory.last_status["reason"], "disabled")

    async def test_sensitive_and_non_user_sources_do_not_enter_model_queue(self):
        with patch("src.assistant_app.application.knowledge.complete_text", new_callable=AsyncMock) as model:
            sensitive = await self.submit("密码是保密内容")
            untrusted = await self.submit("我喜欢吃面。", source="assistant")
            model.assert_not_awaited()
        self.assertEqual(sensitive["reason"], "sensitive")
        self.assertEqual(untrusted["reason"], "untrusted_source")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_voice_session_a_b_a_updates_and_same_input_duplicate_is_ignored(self):
        session = VoiceSession({CONFIG_KEY: {"enable_search": False}, RUNTIME_KEY: SimpleNamespace(memory_service=self.memory)}, None, browser=True)
        session.memory_audio_allowed = True
        for index, value in enumerate(("面", "米饭", "面"), 1):
            session.input_turn_serial = index
            await session.observe_user_text(f"我喜欢吃{value}。", refresh=False, source="verified_audio")
            await asyncio.wait_for(self.memory.queue.join(), 2)
            self.assertEqual(self.memory.last_status["status"], "saved")
            self.assertEqual((await self.memory.context())["memories"][0]["value"], value)
        last_observation = self.memory.observation_serial
        await session.observe_user_text("我喜欢吃面。", refresh=False, source="verified_audio")
        self.assertEqual(self.memory.observation_serial, last_observation)
        with self.store._transaction() as connection:
            row = connection.execute("SELECT source_ref FROM memory_annotations").fetchone()
        self.assertTrue(row[0].startswith(session.session_id + ":3:"))

    async def test_normal_preference_saves_without_remember_command_or_model(self):
        with patch("src.assistant_app.application.knowledge.complete_text", new_callable=AsyncMock) as model:
            await self.submit("我喜欢吃面。")
            model.assert_not_awaited()
        result = await self.memory.context("饮食")
        self.assertEqual(result["memories"][0]["value"], "面")
        self.assertEqual(result["memories"][0]["source"], "automatic")

    async def test_long_term_and_preferences_survive_database_reopen(self):
        await self.submit("我一直在开发小燕子。")
        await self.memory.close()
        self.store.close()
        self.store = AssistantStore(self.path, device_id="memory-regression")
        self.memory = AutoMemory({}, self.store, self.runtime, None)
        await self.memory.start()
        self.assertEqual((await self.memory.context("小燕子"))["memories"][0]["value"], "开发小燕子")

    async def test_semantic_candidate_saves_automatically(self):
        text = "我平时比较偏爱清淡的饮食"
        reply = json.dumps({"memories": [{"key": "饮食偏好", "value": "清淡的饮食", "category": "preference", "evidence": text, "confidence": 0.99}]})
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value=reply)):
            await self.submit(text)
        self.assertEqual((await self.memory.context("饮食"))["memories"][0]["value"], "清淡的饮食")

    async def test_personal_fact_and_temporary_statement_are_not_auto_saved(self):
        reply = json.dumps({"memories": [{"key": "姓名", "value": "小明", "category": "fact", "evidence": "我叫小明", "confidence": 1}]})
        with patch("src.assistant_app.application.knowledge.complete_text", AsyncMock(return_value=reply)):
            await self.submit("我叫小明")
            await self.submit("我喜欢吃面，但只限今天")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_deleted_value_cannot_return_under_another_topic(self):
        await self.submit("我喜欢吃面。")
        await self.memory.manual_tool("delete_memory", {"key": "饮食偏好"})
        text = "我喜欢吃面。"
        candidate = {"key": "最爱食物", "value": "面", "category": "preference", "evidence": text, "confidence": 1}
        self.assertEqual(await self.memory._write(self.memory._save_candidate, candidate, "synthetic-regression"), "deleted_protected")
        self.assertEqual((await self.memory.context())["memories"], [])

    async def test_manual_correction_and_disabled_setting_are_respected(self):
        await self.submit("我喜欢吃面。")
        await self.memory.manual_tool("remember_fact", {"key": " 饮食偏好 ", "value": "清淡"})
        await self.submit("我喜欢吃辣。")
        self.assertEqual((await self.memory.context("饮食"))["memories"][0]["value"], "清淡")
        await self.memory.configure({"enabled": False})
        await self.submit("我一直在开发项目。")
        self.assertEqual(len((await self.memory.context())["memories"]), 1)

    async def test_late_verified_transcript_is_not_lost_when_reply_has_started(self):
        session = VoiceSession({CONFIG_KEY: {"enable_search": False}, RUNTIME_KEY: SimpleNamespace(memory_service=self.memory)}, None, browser=True)
        session.self_turn_pending = False
        session.require_wake_phrase = False
        session.speaking = True
        session.playback_ended_at = time.monotonic()
        session.memory_audio_allowed = True
        session.last_user_text = ""
        session.memory_observations = set()
        session.session_id = "synthetic-session"
        await session.observe_user_text("我喜欢吃面。", refresh=False, source="verified_audio")
        await asyncio.wait_for(self.memory.queue.join(), timeout=3)
        self.assertEqual((await self.memory.context())["memories"][0]["value"], "面")
        session.memory_audio_allowed = False
        await session.observe_user_text("我一直在开发回声。", refresh=False, source="verified_audio")
        self.assertEqual(self.memory.queue.qsize(), 0)


class AutomaticMemoryGatewayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.fixture = gateway_fixture.GatewayTests()
        self.addCleanup(self.fixture.doCleanups)
        await self.fixture.asyncSetUp()

    async def asyncTearDown(self):
        await self.fixture.asyncTearDown()

    async def saved_memories(self):
        async with asyncio.timeout(3):
            while True:
                response = await self.fixture.client.get("/api/memories", headers=self.fixture.headers)
                result = await response.json()
                if result["items"]:
                    return result["items"]
                await asyncio.sleep(0.02)

    async def test_browser_text_to_authenticated_memory_api_without_save_tool(self):
        # 模拟云端同一输入的回送转写，避免夹入另一句固定问候覆盖“最近一轮”状态。
        self.fixture.mock_first_transcript = "我喜欢吃面。"
        socket = await self.fixture.client.ws_connect("/debug/ws")
        try:
            await socket.send_json({"token": gateway_fixture.TOKEN})
            await self.fixture.event(socket, "custom", state="listening")
            await socket.send_json({"type": "text", "text": "我喜欢吃面。"})
            memories = await self.saved_memories()
            self.assertEqual(memories[0]["value"], "面")
            self.assertEqual(memories[0]["source"], "automatic")
            await asyncio.wait_for(self.fixture.app[MEMORY_KEY].queue.join(), 3)
            response = await self.fixture.client.get("/api/memory/settings", headers=self.fixture.headers)
            status = await response.json()
            self.assertEqual(status["last_extraction"]["status"], "saved")
            self.assertEqual(status["last_extraction"]["saved"], 1)
            self.assertNotIn("我喜欢吃面", json.dumps(status["last_extraction"], ensure_ascii=False))
            self.assertFalse(any(event.get("type") == "conversation.item.create" and event.get("item", {}).get("type") == "function_call_output" for event in self.fixture.cloud_events))
        finally:
            await socket.close()

    async def test_native_late_transcript_to_persistent_memory_api(self):
        self.fixture.mock_first_transcript = "我喜欢吃面。"
        self.fixture.mock_transcript_before_response = False
        socket = await self.fixture.native()
        try:
            await socket.send_json({"type": "listen", "state": "start", "mode": "manual"})
            await self.fixture.event(socket, "custom", state="listening")
            # 本例验证回答开始后的迟到转写；使用300ms有效录音，短按拒绝由独立边界用例覆盖。
            await socket.send_bytes(b"\x00\x00" * 4800)
            await socket.send_json({"type": "listen", "state": "stop"})
            await self.fixture.event(socket, "stt")
            memories = await self.saved_memories()
            self.assertEqual(memories[0]["value"], "面")
            self.assertEqual(self.fixture.app[MEMORY_KEY].last_status["saved"], 1)
        finally:
            await socket.close()


if __name__ == "__main__":
    unittest.main()
