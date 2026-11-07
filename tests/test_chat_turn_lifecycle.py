"""可控事件覆盖并发输入、取消与关闭；不依赖真实云端或固定sleep顺序。"""

import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock

from chat_pipeline import ChatBackend


class ChatTurnLifecycleTests(unittest.IsolatedAsyncioTestCase):
    async def test_asr_overflow_fails_instead_of_dropping_old_audio(self):
        backend = self.make_backend()
        backend._asr_audio = asyncio.Queue(maxsize=1)
        await backend.send_user_audio(b"original-frame")
        with self.assertRaisesRegex(RuntimeError, "积压超过上限"):
            await backend.send_user_audio(b"new-frame")
        self.assertEqual(backend._asr_audio.get_nowait(), b"original-frame")

    async def test_event_overflow_reports_one_error_instead_of_losing_response_identity(self):
        backend = self.make_backend()
        backend._events = asyncio.Queue(maxsize=1)
        backend._emit({"type": "response.created", "response": {"id": "old"}})
        with self.assertRaisesRegex(RuntimeError, "输出积压"):
            backend._emit({"type": "response.audio.delta", "delta": "invalid-old-frame"})
        backend._emit({"type": "response.done"})
        self.assertEqual(backend._events.qsize(), 1)
        self.assertEqual(backend._events.get_nowait(), {"type": "error", "error": {"code": "chat_event_overflow"}})

    def setUp(self):
        self.backends = []

    async def asyncTearDown(self):
        for backend in self.backends:
            await asyncio.wait_for(backend.close(), 1)

    def make_backend(self):
        session = SimpleNamespace(config={"chat_turn_silence_ms": 0}, session_id="offline-lifecycle", observe_user_text=AsyncMock(), prepare_chat_user_text=lambda text, _pending: text)
        backend = ChatBackend(session, "qwen_chat")
        backend._turn_task = backend._timer_task = backend._tts_task = None
        backend._active_response_id = None
        backend._turn_generation = 0
        backend._cancelled = set()
        backend._events = asyncio.Queue()
        backend._finals = []
        backend._last_partial = ""
        backend._run_turn = AsyncMock()
        self.backends.append(backend)
        return backend

    async def wait_event(self, event):
        await asyncio.wait_for(event.wait(), 1)

    async def wait_drain(self, backend):
        task = backend._turn_drain_task
        if task is not None:
            await asyncio.wait_for(asyncio.shield(task), 1)

    async def test_repeated_inputs_keep_one_owner_one_drain_and_only_latest_pending(self):
        backend = self.make_backend()
        observing, cleaning, release_cleanup, answering, release_answer = (asyncio.Event() for _ in range(5))
        observed = []

        async def observe(text, **_kwargs):
            observed.append(text)
            if text == "旧输入":
                observing.set()
                try:
                    await asyncio.Event().wait()
                finally:
                    cleaning.set()
                    await release_cleanup.wait()

        async def answer(text):
            self.assertTrue(release_cleanup.is_set())
            self.assertEqual(text, "最新输入49")
            answering.set()
            await release_answer.wait()

        backend.session.observe_user_text = observe
        backend._run_turn = answer
        try:
            await backend.send_user_text("旧输入")
            await self.wait_event(observing)
            owner, drain = backend._turn_task, backend._turn_drain_task
            await backend._start_user_turn("被覆盖的语音输入")
            await self.wait_event(cleaning)
            for index in range(50):
                await backend.send_user_text(f"最新输入{index}")
                self.assertIs(backend._turn_task, owner)
                self.assertIs(backend._turn_drain_task, drain)
            lifecycle_tasks = [task for task in asyncio.all_tasks() if task.get_coro().__name__ in {"_observe_and_run", "_drain_turns"}]
            self.assertEqual(len(lifecycle_tasks), 2)
            self.assertEqual(backend._pending_input[1], "最新输入49")
            self.assertEqual(owner.cancelling(), 1)
            release_cleanup.set()
            await self.wait_event(answering)
            self.assertTrue(owner.done())
            self.assertEqual(observed, ["旧输入", "最新输入49"])
            release_answer.set()
            await self.wait_drain(backend)
        finally:
            release_cleanup.set()
            release_answer.set()

    async def test_cancel_waits_old_cleanup_but_does_not_wait_or_cancel_new_input(self):
        backend = self.make_backend()
        observing, cleaning, release_cleanup, answering, release_answer = (asyncio.Event() for _ in range(5))

        async def observe(text, **_kwargs):
            if text == "旧输入":
                observing.set()
                try:
                    await asyncio.Event().wait()
                finally:
                    cleaning.set()
                    await release_cleanup.wait()

        async def answer(text):
            self.assertEqual(text, "取消之后的新输入")
            answering.set()
            await release_answer.wait()

        backend.session.observe_user_text = observe
        backend._run_turn = answer
        cancellation = None
        try:
            await backend.send_user_text("旧输入")
            await self.wait_event(observing)
            cancellation = asyncio.create_task(backend.cancel_response())
            await self.wait_event(cleaning)
            self.assertFalse(cancellation.done())
            await backend.send_user_text("取消之后的新输入")
            release_cleanup.set()
            await asyncio.wait_for(cancellation, 1)
            await self.wait_event(answering)
            self.assertFalse(backend._turn_task.done())
            release_answer.set()
            await self.wait_drain(backend)
        finally:
            release_cleanup.set()
            release_answer.set()
            if cancellation is not None:
                await asyncio.gather(cancellation, return_exceptions=True)

    async def test_cancelled_observer_cannot_start_after_suppressing_cancellation(self):
        backend = self.make_backend()
        observing, cancelled, release = (asyncio.Event() for _ in range(3))

        async def observe(_text, **_kwargs):
            observing.set()
            try:
                await asyncio.Event().wait()
            except asyncio.CancelledError:
                cancelled.set()
                await release.wait()

        backend.session.observe_user_text = observe
        try:
            await backend.send_user_text("不应复活")
            await self.wait_event(observing)
            cancellation = asyncio.create_task(backend.cancel_response())
            await self.wait_event(cancelled)
            self.assertFalse(cancellation.done())
            release.set()
            await asyncio.wait_for(cancellation, 1)
            await self.wait_drain(backend)
            backend._run_turn.assert_not_awaited()
        finally:
            release.set()

    async def test_close_waits_observation_and_rejects_later_input(self):
        backend = self.make_backend()
        observing, cancelled, release = (asyncio.Event() for _ in range(3))

        async def observe(_text, **_kwargs):
            observing.set()
            try:
                await asyncio.Event().wait()
            except asyncio.CancelledError:
                cancelled.set()
                await release.wait()

        backend.session.observe_user_text = observe
        closing = None
        try:
            await backend.send_user_text("关闭前输入")
            await self.wait_event(observing)
            closing = asyncio.create_task(backend.close())
            await self.wait_event(cancelled)
            self.assertFalse(closing.done())
            await backend.send_user_text("关闭后输入")
            self.assertIsNone(backend._pending_input)
            release.set()
            await asyncio.wait_for(closing, 1)
            backend._run_turn.assert_not_awaited()
            self.assertIsNone(backend._turn_task)
            self.assertIsNone(backend._turn_drain_task)
        finally:
            release.set()
            if closing is not None:
                await asyncio.gather(closing, return_exceptions=True)

    async def test_close_reaches_asr_turn_after_timer_reference_was_cleared(self):
        backend = self.make_backend()
        observing, observed_cleanup = asyncio.Event(), asyncio.Event()

        async def observe(_text, **_kwargs):
            observing.set()
            try:
                await asyncio.Event().wait()
            finally:
                observed_cleanup.set()

        backend.session.observe_user_text = observe
        backend._finals = ["语音输入"]
        backend._arm_turn_timer()
        timer = backend._timer_task
        await self.wait_event(observing)
        self.assertIsNone(backend._timer_task)
        self.assertTrue(timer.done())
        await asyncio.wait_for(backend.close(), 1)
        self.assertTrue(observed_cleanup.is_set())
        backend._run_turn.assert_not_awaited()
        self.assertIsNone(backend._turn_drain_task)

    async def test_internal_cancel_and_close_do_not_wait_on_their_own_drain(self):
        for operation in ("cancel_response", "close"):
            with self.subTest(operation=operation):
                backend = self.make_backend()
                returned = asyncio.Event()

                async def observe(_text, **_kwargs):
                    await getattr(backend, operation)()
                    returned.set()

                backend.session.observe_user_text = observe
                await backend.send_user_text("内部取消")
                await self.wait_event(returned)
                await self.wait_drain(backend)
                backend._run_turn.assert_not_awaited()

    async def test_observation_failure_emits_one_error_and_releases_owner(self):
        backend = self.make_backend()
        backend.session.observe_user_text = AsyncMock(side_effect=RuntimeError("offline observation failure"))
        with self.assertLogs("assistant.chat_pipeline", level="ERROR"):
            await backend.send_user_text("失败输入")
            await self.wait_drain(backend)
        errors = [backend._events.get_nowait() for _ in range(backend._events.qsize())]
        self.assertEqual(errors, [{"type": "conversation.item.input_audio_transcription.completed", "transcript": "失败输入"}, {"type": "error", "error": {"code": "chat_turn_failed"}}])
        backend._run_turn.assert_not_awaited()
        self.assertIsNone(backend._turn_task)

    async def test_cancelled_drain_waits_for_child_cleanup(self):
        backend = self.make_backend()
        observing, cleaning, release = (asyncio.Event() for _ in range(3))

        async def observe(_text, **_kwargs):
            observing.set()
            try:
                await asyncio.Event().wait()
            finally:
                cleaning.set()
                await release.wait()

        backend.session.observe_user_text = observe
        try:
            await backend.send_user_text("退出清理")
            await self.wait_event(observing)
            owner, drain = backend._turn_task, backend._turn_drain_task
            drain.cancel()
            await self.wait_event(cleaning)
            self.assertFalse(drain.done())
            release.set()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(drain, 1)
            self.assertTrue(owner.done())
            self.assertIsNone(backend._turn_task)
            self.assertIsNone(backend._turn_drain_task)
        finally:
            release.set()

    async def assert_cancelled_caller_waits_cleanup(self, operation):
        backend = self.make_backend()
        observing, cleaning, release, cleaned = (asyncio.Event() for _ in range(4))

        async def observe(_text, **_kwargs):
            observing.set()
            try:
                await asyncio.Event().wait()
            finally:
                cleaning.set()
                await release.wait()
                cleaned.set()

        backend.session.observe_user_text = observe
        caller = None
        try:
            await backend.send_user_text("调用者取消")
            await self.wait_event(observing)
            owner = backend._turn_task
            caller = asyncio.create_task(getattr(backend, operation)())
            await self.wait_event(cleaning)
            caller.cancel()
            cancellation_dispatched = asyncio.Event()
            asyncio.get_running_loop().call_soon(cancellation_dispatched.set)
            await self.wait_event(cancellation_dispatched)
            self.assertFalse(caller.done())
            self.assertEqual(owner.cancelling(), 1)
            release.set()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(caller, 1)
            self.assertTrue(cleaned.is_set())
            self.assertTrue(owner.done())
            backend._run_turn.assert_not_awaited()
        finally:
            release.set()
            if caller is not None:
                await asyncio.gather(caller, return_exceptions=True)

    async def test_cancel_response_caller_cancellation_waits_existing_cleanup(self):
        await self.assert_cancelled_caller_waits_cleanup("cancel_response")

    async def test_close_caller_cancellation_waits_existing_cleanup(self):
        await self.assert_cancelled_caller_waits_cleanup("close")


if __name__ == "__main__":
    unittest.main()
