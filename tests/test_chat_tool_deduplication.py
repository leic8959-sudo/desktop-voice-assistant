"""真实Chat回合/响应编排的工具身份回归；模型流和动作全部受控。"""

import asyncio
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock

from chat_pipeline import ChatBackend, _Chunk
from assistant_core import AssistantStore
from assistant_core.tools import execute_tool


def call(index, identity='call-1', name='music_queue', arguments='{"selection_id":"server-track"}'):
    return {'index':index, 'id':identity, 'function':{'name':name, 'arguments':arguments}}


class BlockingResponse:
    status = 200

    def __init__(self, message):
        self.message = message

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_args):
        return False

    async def json(self, **_kwargs):
        return {'choices':[{'message':self.message}]}


class ChatToolDeduplicationTests(unittest.IsolatedAsyncioTestCase):
    async def test_next_user_choice_keeps_real_search_selection_and_paired_history(self):
        self.session.execute_tool.side_effect = lambda name, arguments: {"items": [{"selection_id": "real-original", "title": "晴天", "album": "叶惠美"}]} if name == "music_search" else {"status": "scheduled"}
        self.scripts = [[call(0, name="music_search", arguments='{"query":"晴天"}')], [], [call(0, identity="play-original", name="music_play", arguments='{"selection_id":"real-original"}')], []]
        await self.backend._run_turn("播放晴天")
        await self.backend._run_turn("原版，随便来一个放就行了")
        second_turn = self.requests[2]["messages"]
        self.assertTrue(any(item.get("role") == "tool" and "real-original" in item["content"] for item in second_turn))
        self.assertEqual(self.session.execute_tool.await_args.args, ("music_play", {"selection_id": "real-original"}))
        for index, item in enumerate(self.backend._history):
            if item.get("tool_calls"):
                outputs = self.backend._history[index + 1:index + 1 + len(item["tool_calls"])]
                self.assertEqual({call["id"] for call in item["tool_calls"]}, {value["tool_call_id"] for value in outputs})

    async def test_history_eviction_keeps_complete_tool_groups(self):
        for index in range(8):
            self.scripts = [[call(0, identity=f"queued-{index}")], []]
            await self.backend._run_turn(f"选择第{index}个候选")
        self.assertLessEqual(len(self.backend._history), 20)
        self.assertEqual(self.backend._history[0]["role"], "user")
        for index, item in enumerate(self.backend._history):
            if item.get("role") == "tool":
                previous = self.backend._history[index - 1]
                self.assertEqual(previous["role"], "assistant")
                self.assertIn(item["tool_call_id"], [call["id"] for call in previous["tool_calls"]])

    async def test_interrupted_or_failed_second_turn_keeps_prior_and_current_tool_pairs(self):
        for cancelled in (False, True):
            with self.subTest(cancelled=cancelled):
                self.backend._history = []
                self.scripts = [[call(0, identity="prior-search", name="music_search", arguments='{"query":"晴天"}')], []]
                await self.backend._run_turn("先找版本")
                previous = copy.deepcopy(self.backend._history)
                round_number = 0

                async def interrupted(messages):
                    nonlocal round_number
                    round_number += 1
                    if round_number == 1:
                        yield _Chunk(tool_delta=[call(0, identity="current-play", name="music_play", arguments='{"selection_id":"server-track"}')])
                    elif cancelled:
                        raise asyncio.CancelledError()
                    else:
                        raise ConnectionError("fixed-offline-disconnect")

                old_request = self.backend._request
                self.backend._request = interrupted
                try:
                    if cancelled:
                        with self.assertRaises(asyncio.CancelledError):
                            await self.backend._run_turn("原版")
                    else:
                        with self.assertLogs("assistant.chat_pipeline", level="ERROR"):
                            await self.backend._run_turn("原版")
                finally:
                    self.backend._request = old_request
                self.assertEqual(self.backend._history[:len(previous)], previous)
                self.assertTrue(any(item.get("role") == "tool" and item["tool_call_id"] == "current-play" for item in self.backend._history))
                pending = set()
                for item in self.backend._history:
                    if item.get("tool_calls"):
                        pending.update(call["id"] for call in item["tool_calls"])
                    elif item.get("role") == "tool":
                        self.assertIn(item["tool_call_id"], pending)
                        pending.remove(item["tool_call_id"])
                self.assertEqual(pending, set())

    def setUp(self):
        self.effects = []
        self.requests = []
        self.scripts = []

        async def execute(name, arguments):
            self.effects.append((name, copy.deepcopy(arguments)))
            return {'status':'queued'}

        self.session = SimpleNamespace(config={}, session_id='offline-tool-identity', state=AsyncMock(), execute_tool=AsyncMock(side_effect=execute), executing_tools=0)
        self.backend = ChatBackend(self.session, 'qwen_chat')
        self.backend._history = []
        self.backend._instructions = 'offline'
        self.backend._pending_history = None
        self.backend._active_response_id = None
        self.backend._events = asyncio.Queue()
        self.backend._cancelled = set()
        self.backend._response_sequence = 0

        async def model(messages):
            self.requests.append({'response_id':self.backend._active_response_id, 'messages':copy.deepcopy(messages)})
            script = self.scripts.pop(0)
            if script:
                yield _Chunk(tool_delta=script)
            else:
                yield _Chunk()

        self.backend._request = model

    async def asyncTearDown(self):
        await self.backend.close()

    def followup_tools(self, index=1):
        messages = self.requests[index]['messages']
        assistant = next(message for message in reversed(messages) if message['role'] == 'assistant')
        outputs = [message for message in messages if message['role'] == 'tool']
        return assistant, outputs

    def blocking_model(self, tool_calls):
        responses = [BlockingResponse({'tool_calls':tool_calls}), BlockingResponse({'content':''})]

        def post(_url, **options):
            self.requests.append({'response_id':self.backend._active_response_id, 'messages':copy.deepcopy(options['json']['messages'])})
            return responses.pop(0)

        self.backend.config.update({'chat_max_tokens':128, 'qwen_chat_thinking':None})
        self.backend._tools = []
        self.backend._chat_model = 'offline-model'
        self.backend._chat_key = 'offline-placeholder'
        self.backend._chat_url = 'https://offline.invalid/chat/completions'
        self.backend._streaming = False
        self.backend.client = SimpleNamespace(post=post)
        self.backend._request = ChatBackend._request.__get__(self.backend, ChatBackend)

    async def test_duplicate_id_with_equivalent_json_executes_once_and_has_one_tool_output(self):
        self.scripts = [[call(0, arguments='{"selection_id":"server-track", "options":{"a":1,"b":2}}'), call(1, arguments=' { "options": {"b":2,"a":1}, "selection_id":"server-track" } ')], []]
        await self.backend._run_turn('把这首加入队列')
        self.assertEqual(len(self.effects), 1, '同一响应同一身份不能将队列增加两次')
        assistant, outputs = self.followup_tools()
        self.assertEqual(len(assistant['tool_calls']), 1)
        self.assertEqual(len(outputs), 1)
        self.assertEqual(outputs[0]['tool_call_id'], assistant['tool_calls'][0]['id'])

    async def test_conflicting_duplicate_rejects_entire_response_before_any_tool(self):
        for conflicting in (call(2, identity='duplicate', arguments='{"selection_id":"different-track"}'), call(2, identity='duplicate', name='music_stop')):
            with self.subTest(conflicting=conflicting):
                self.effects.clear()
                self.requests.clear()
                self.scripts = [[call(0, identity='unrelated-valid'), call(1, identity='duplicate'), conflicting], []]
                await self.backend._run_turn('本轮模拟复合请求')
                self.assertEqual(self.effects, [], '冲突位于后面也必须在第一项副作用前拒绝整批')

    async def test_independent_responses_may_reuse_id_without_old_result_or_idempotency_key(self):
        with tempfile.TemporaryDirectory() as directory, AssistantStore(Path(directory) / 'identity.sqlite3', 'offline-identity') as store:
            async def create(name, arguments):
                self.effects.append((name, copy.deepcopy(arguments)))
                return execute_tool(store, name, arguments)

            self.session.execute_tool.side_effect = create
            identity = 'vendor-' + 'x' * 200
            self.scripts = [[call(0, identity=identity, name='create_task', arguments='{"title":"第一条"}')], [], [call(0, identity=identity, name='create_task', arguments='{"title":"第二条"}')], []]
            await self.backend._run_turn('创建第一条提醒')
            await self.backend._run_turn('创建第二条提醒')
            self.assertEqual(len(self.effects), 2)
            first, second = [arguments for _name, arguments in self.effects]
            self.assertNotEqual(self.requests[0]['response_id'], self.requests[2]['response_id'])
            self.assertNotEqual(first['idempotency_key'], second['idempotency_key'], '不同响应复用call-id必须取得不同真实幂等身份')
            self.assertEqual((first['title'], second['title']), ('第一条', '第二条'))
            rows = store.list_tasks()['tasks']
            self.assertEqual({row['title'] for row in rows}, {'第一条', '第二条'})
            self.assertTrue(all(len(row['idempotency_key'].encode('utf-8')) <= 128 for row in rows))

    async def test_idempotent_tool_without_response_identity_never_writes(self):
        with tempfile.TemporaryDirectory() as directory, AssistantStore(Path(directory) / 'missing-identity.sqlite3', 'offline-identity') as store:
            async def create(name, arguments):
                return execute_tool(store, name, arguments)

            self.session.execute_tool.side_effect = create
            result = await self.backend._execute_tool({'id':'call-1', 'name':'create_task', 'arguments':'{"title":"不应写入"}'})
            self.assertEqual(result['status'], 'invalid_arguments')
            self.session.execute_tool.assert_not_awaited()
            self.assertEqual(store.list_tasks()['tasks'], [])

    async def test_unknown_result_is_not_executed_again_for_duplicate_in_same_response(self):
        async def partially_acted(name, arguments):
            self.effects.append((name, copy.deepcopy(arguments)))
            raise TimeoutError('private-provider-detail')

        self.session.execute_tool.side_effect = partially_acted
        self.scripts = [[call(0), call(1)], []]
        with self.assertLogs('assistant.chat_pipeline', level='ERROR'):
            await self.backend._run_turn('加入这首歌曲')
        self.assertEqual(len(self.effects), 1, '未知结果不能因同响应重复身份自动再执行')
        assistant, outputs = self.followup_tools()
        self.assertEqual(len(assistant['tool_calls']), 1)
        self.assertEqual(len(outputs), 1)
        result = json.loads(outputs[0]['content'])
        self.assertEqual(result['status'], 'unknown')
        self.assertNotIn('private-provider-detail', str(result))

    async def test_more_than_eight_tools_rejects_whole_response_before_side_effect(self):
        self.scripts = [[call(index, identity=f'call-{index}') for index in range(9)], []]
        await self.backend._run_turn('模拟超限批次')
        self.assertEqual(self.effects, [])

    async def test_invalid_bounds_and_nonfinite_json_reject_before_any_tool(self):
        invalid = [call(1, identity='x' * 257), call(1, name='x' * 129), call(1, arguments='{"value":"' + 'x' * 16384 + '"}'), call(1, arguments='{"value":NaN}'), call(1, arguments='{"value":Infinity}'), call(1, arguments='[]')]
        for bad_call in invalid:
            with self.subTest(bad_call=bad_call):
                self.effects.clear()
                self.scripts = [[call(0, identity='first-valid'), bad_call], []]
                with self.assertLogs('assistant.chat_pipeline', level='ERROR'):
                    await self.backend._run_turn('模拟非法批次')
                self.assertEqual(self.effects, [])

    async def test_session_budget_stops_the_101st_new_call_even_when_ids_are_reused(self):
        for index in range(101):
            self.scripts = [[call(0)], []]
            await self.backend._run_turn(f'模拟独立输入{index}')
        self.assertEqual(len(self.effects), 100)

    async def test_cancel_after_possible_action_does_not_execute_duplicate_or_claim_success(self):
        entered = asyncio.Event()

        async def awaiting_receipt(name, arguments):
            self.effects.append((name, copy.deepcopy(arguments)))
            entered.set()
            await asyncio.Event().wait()

        self.session.execute_tool.side_effect = awaiting_receipt
        self.scripts = [[call(0), call(1)], []]
        task = asyncio.create_task(self.backend._run_turn('加入这首歌曲'))
        try:
            await asyncio.wait_for(entered.wait(), 1)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.assertEqual(len(self.effects), 1)
            self.assertEqual(self.session.executing_tools, 0)
            outputs = [message for message in self.backend._history if message.get('role') == 'tool']
            self.assertEqual(len(outputs), 1)
            self.assertEqual(json.loads(outputs[0]['content'])['status'], 'unknown')
        finally:
            if not task.done():
                task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_blocking_response_without_indexes_executes_two_distinct_tools_once(self):
        tools = [{key:value for key, value in item.items() if key != 'index'} for item in (call(0, identity='first'), call(1, identity='second', arguments='{"selection_id":"other-track"}'))]
        self.blocking_model(tools)
        await self.backend._run_turn('非流式模拟两首入队')
        self.assertEqual(len(self.effects), 2)
        assistant, outputs = self.followup_tools()
        self.assertEqual([item['id'] for item in assistant['tool_calls']], ['first', 'second'])
        self.assertEqual([item['tool_call_id'] for item in outputs], ['first', 'second'])

    async def test_blocking_duplicate_ids_are_deduplicated_or_rejected_before_actions(self):
        for conflict in (False, True):
            with self.subTest(conflict=conflict):
                self.effects.clear()
                self.requests.clear()
                tools = [{key:value for key, value in item.items() if key != 'index'} for item in (call(0), call(1, arguments='{"selection_id":"different"}' if conflict else '{ "selection_id": "server-track" }'))]
                self.blocking_model(tools)
                await self.backend._run_turn('非流式重复身份')
                self.assertEqual(len(self.effects), 0 if conflict else 1)
                if not conflict:
                    assistant, outputs = self.followup_tools()
                    self.assertEqual(len(assistant['tool_calls']), 1)
                    self.assertEqual(len(outputs), 1)


if __name__ == '__main__':
    unittest.main()
