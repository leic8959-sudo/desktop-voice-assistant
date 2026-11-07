"""失败或取消的语音回合退出上下文，已执行动作不被补偿或重试。"""

import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock

from chat_pipeline import ChatBackend


class ChatTurnHistoryTests(unittest.IsolatedAsyncioTestCase):
    def make_backend(self):
        session = SimpleNamespace(config={}, session_id="offline-history", observe_user_text=AsyncMock())
        backend = ChatBackend(session, "qwen_chat")
        backend._history = [{"role": "user", "content": "之前的问题"}, {"role": "assistant", "content": "之前的回答"}]
        backend._instructions = "offline"
        backend._events = asyncio.Queue()
        backend._pending_history = None
        backend._active_response_id = None
        backend._cancelled = set()
        backend._turn_generation = 0
        backend._turn_task = backend._timer_task = None
        return backend

    async def test_failed_turn_restores_history_and_emits_error(self):
        backend = self.make_backend()
        original = list(backend._history)
        backend._chat_round = AsyncMock(side_effect=RuntimeError("synthetic round failure"))
        with self.assertLogs("assistant.chat_pipeline", level="ERROR"):
            await backend._run_turn("失败的问题")
        self.assertEqual(backend._history, original)
        events = [backend._events.get_nowait() for _ in range(backend._events.qsize())]
        self.assertTrue(any(event.get("error", {}).get("code") == "chat_turn_failed" for event in events))

    async def test_cancel_then_new_turn_excludes_interrupted_user_message(self):
        backend = self.make_backend()
        original = list(backend._history)
        started = asyncio.Event()

        async def interrupted(_messages):
            started.set()
            await asyncio.Event().wait()

        backend._chat_round = interrupted
        backend._turn_task = asyncio.create_task(backend._run_turn("被打断的问题"))
        await asyncio.wait_for(started.wait(), 1)
        await backend.cancel_response()
        self.assertEqual(backend._history, original)
        backend._chat_round = AsyncMock(return_value=({"role": "assistant", "content": "新回答"}, {}))
        await backend._run_turn("新问题")
        messages = backend._chat_round.call_args.args[0]
        self.assertNotIn({"role": "user", "content": "被打断的问题"}, messages)
        self.assertEqual(backend._history[-2:], [{"role": "user", "content": "新问题"}, {"role": "assistant", "content": "新回答"}])

    async def test_cancellation_after_tool_does_not_repeat_or_compensate_action(self):
        backend = self.make_backend()
        original = list(backend._history)
        waiting_for_answer = asyncio.Event()
        call = {"id": "offline-call", "name": "create_task", "arguments": "{}"}

        async def round_with_tool(messages):
            if messages[-1]["role"] == "user":
                return {"role": "assistant", "content": None, "tool_calls": [{"id": call["id"], "type": "function", "function": {"name": call["name"], "arguments": call["arguments"]}}]}, {0: call}
            waiting_for_answer.set()
            await asyncio.Event().wait()

        backend._chat_round = round_with_tool
        backend._execute_tool = AsyncMock(return_value={"status": "pending"})
        backend._turn_task = asyncio.create_task(backend._run_turn("创建提醒"))
        await asyncio.wait_for(waiting_for_answer.wait(), 1)
        await backend.cancel_response()
        self.assertEqual(backend._history[:len(original)], original)
        assistant = next(item for item in backend._history[len(original):] if item.get("tool_calls"))
        outputs = [item for item in backend._history[len(original):] if item["role"] == "tool"]
        self.assertEqual({item["id"] for item in assistant["tool_calls"]}, {item["tool_call_id"] for item in outputs})
        self.assertIn('"status": "pending"', outputs[0]["content"])
        backend._execute_tool.assert_awaited_once_with(call)
        backend._chat_round = AsyncMock(return_value=({"role": "assistant", "content": "收到"}, {}))
        await backend._run_turn("换个问题")
        backend._execute_tool.assert_awaited_once_with(call)


if __name__ == "__main__":
    unittest.main()
