"""语义控制的授权、顺序、回执与去重；不调用真实模型、Windows或家电。"""

import json
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from assistant_runtime import RUNTIME_KEY
from gateway import CONFIG_KEY, HTTP_KEY, VoiceSession
from home_control import HomeController
from src.assistant_app.application.desktop_actions import DesktopActionsService
from src.assistant_app.application.voice_control import plan_voice_control


def media_service():
    return SimpleNamespace(
        supported=True,
        tool_schemas=DesktopActionsService.tool_schemas.fget(None),
        normalize=lambda name, arguments: DesktopActionsService.normalize(None, name, arguments),
        invoke=AsyncMock(return_value={"status": "requested", "message": "Windows接受请求，未核查窗口"}),
        cancel_voice_request=AsyncMock(),
    )


def session():
    runtime = SimpleNamespace(media_service=media_service(), memory_service=None, home=SimpleNamespace(configured=False), send_home_screen_card=AsyncMock())
    app = {CONFIG_KEY: {"session_seconds": 600, "idle_seconds": 30, "enable_search": False}, RUNTIME_KEY: runtime, HTTP_KEY: object()}
    result = VoiceSession(app, SimpleNamespace(send_json=AsyncMock()), browser=True)
    result.state = AsyncMock()
    result.refresh_memory_context = AsyncMock()
    result.backend = SimpleNamespace(name="qwen_realtime", request_response=AsyncMock())
    return result, runtime


def climate_steps():
    return [
        {"tool": "execute_home_action", "arguments": {"device_id": "actual-ac", "action": "climate.actual:turn_on", "parameters": {}}},
        {"tool": "execute_home_action", "arguments": {"device_id": "actual-ac", "action": "climate.actual:set_temperature", "parameters": {"temperature": 30}}},
    ]


class SpeechAuthorizationTests(unittest.IsolatedAsyncioTestCase):
    async def test_explicit_model_control_does_not_reparse_and_is_idempotent_per_user_input(self):
        voice, runtime = session()
        await voice.observe_user_text("打开那个算数的", refresh=False, source="text")
        with patch("gateway.plan_voice_control", new=AsyncMock(side_effect=AssertionError("不能重复理解同一工具意图"))):
            for _ in range(2):
                await voice._execute_tool("desktop_open_app", {"app": "calculator"})
            runtime.media_service.invoke.assert_awaited_once()
            await voice.observe_user_text("再打开一次", refresh=False, source="text")
            await voice._execute_tool("desktop_open_app", {"app": "calculator"})
        self.assertEqual(runtime.media_service.invoke.await_count, 2)

    async def test_explicit_home_unobserved_result_blocks_following_steps(self):
        voice, runtime = session()
        runtime.home.get_home_state = AsyncMock(return_value={"device_id": "actual", "actions": [{"id": "climate.actual:turn_on"}, {"id": "climate.actual:set_temperature"}]})
        runtime.home._validate_parameters = Mock()
        runtime.home.execute_home_action = AsyncMock(return_value={"target_observed": False, "reported_state": "off"})
        await voice.observe_user_text("打开再调温", refresh=False, source="text")
        await voice._execute_tool("execute_home_action", {"device_id": "actual", "action": "turn_on", "parameters": {}})
        self.assertTrue(voice.tools_paused)
        await voice._execute_tool("execute_home_action", {"device_id": "actual", "action": "set_temperature", "parameters": {"temperature": 30}})
        runtime.home.execute_home_action.assert_awaited_once()

    async def test_model_home_alias_and_suffix_resolve_to_one_canonical_effect(self):
        voice, runtime = session()
        runtime.home.get_home_state = AsyncMock(return_value={"device_id": "stable-device", "actions": [{"id": "climate.actual:turn_on"}]})
        runtime.home._validate_parameters = Mock()
        runtime.home.execute_home_action = AsyncMock(return_value={"target_observed": True})
        await voice.observe_user_text("把空调开一下", refresh=False, source="text")
        await voice._execute_tool("execute_home_action", {"device_id": "房间空调", "action": "turn_on", "parameters": {}})
        await voice._execute_tool("execute_home_action", {"device_id": "stable-device", "action": "climate.actual:turn_on", "parameters": {}})
        runtime.home.execute_home_action.assert_awaited_once_with("stable-device", "climate.actual:turn_on", {})

    async def test_real_followup_near_playback_authorizes_control_without_expanding_memory(self):
        voice, runtime = session()
        memory = SimpleNamespace(submit=Mock())
        runtime.memory_service = memory
        voice.memory_audio_allowed = False
        voice.playback_ended_at = 100.0
        voice._note_speech_started(100.8, {"audio_start_ms": 1000})
        self.assertFalse(voice._note_speech_stopped(102.2, {"audio_end_ms": 2100}))
        self.assertTrue(voice.command_audio_allowed)
        await voice.observe_user_text("电脑打开计算器，计算一加一等于几。", refresh=False, source="verified_audio")
        self.assertIn("打开计算器", voice.last_user_text)
        memory.submit.assert_not_called()
        self.assertFalse(voice.memory_audio_allowed)

    async def test_short_tail_and_playback_overlap_never_authorize_control(self):
        voice, _ = session()
        voice.memory_audio_allowed = False
        voice.playback_ended_at = 100.0
        voice._note_speech_started(100.5, {"audio_start_ms": 1000})
        self.assertTrue(voice._note_speech_stopped(101.2, {"audio_end_ms": 1200}))
        self.assertFalse(voice.command_audio_allowed)
        await voice.observe_user_text("打开计算器", refresh=False, source="verified_audio")
        self.assertEqual(voice.last_user_text, "")
        voice.speaking = True
        voice._note_speech_started(103.0, {"audio_start_ms": 2000})
        voice._note_speech_stopped(105.0, {"audio_end_ms": 3500})
        self.assertFalse(voice.command_audio_allowed)


class ControlExecutionTests(unittest.IsolatedAsyncioTestCase):
    async def test_semantic_pc_action_is_executed_once_for_repeated_model_calls(self):
        voice, runtime = session()
        await voice.observe_user_text("把电脑那个算账的小工具叫出来", refresh=False, source="text")
        step = {"tool": "desktop_open_app", "arguments": {"app": "calculator"}}
        with patch("gateway.plan_voice_control", new=AsyncMock(return_value={"status": "execute", "actions": [step]})) as planner:
            for _ in range(2):
                result = await voice.execute_control_plan(requested=(step["tool"], step["arguments"]))
        planner.assert_awaited_once()
        runtime.media_service.invoke.assert_awaited_once()
        self.assertEqual(result["status"], "requested")

    async def test_climate_open_then_temperature_and_duplicate_calls(self):
        voice, runtime = session()
        runtime.home.execute_home_action = AsyncMock(side_effect=[{"target_observed": True, "reported_state": "heat"}, {"target_observed": True, "reported_state": "heat", "temperature": 30}])
        await voice.observe_user_text("打开空调设置30°", refresh=False, source="text")
        steps = climate_steps()
        with patch("gateway.plan_voice_control", new=AsyncMock(return_value={"status": "execute", "actions": steps})):
            for _ in range(2):
                result = await voice.execute_control_plan(requested=("execute_home_action", steps[1]["arguments"]))
        self.assertEqual(runtime.home.execute_home_action.await_args_list[0].args[1], "climate.actual:turn_on")
        self.assertEqual(runtime.home.execute_home_action.await_args_list[1].args[1], "climate.actual:set_temperature")
        self.assertEqual(runtime.home.execute_home_action.await_count, 2)
        self.assertEqual(result["status"], "verified")

    async def test_unobserved_power_on_stops_temperature_and_never_retries(self):
        voice, runtime = session()
        runtime.home.execute_home_action = AsyncMock(return_value={"target_observed": False, "reported_state": "off"})
        await voice.observe_user_text("打开空调设置30°", refresh=False, source="text")
        with patch("gateway.plan_voice_control", new=AsyncMock(return_value={"status": "execute", "actions": climate_steps()})):
            await voice.execute_control_plan()
            result = await voice.execute_control_plan()
        runtime.home.execute_home_action.assert_awaited_once()
        self.assertEqual(result["status"], "unconfirmed")

    async def test_mismatched_target_and_negated_intent_do_not_execute(self):
        voice, runtime = session()
        await voice.observe_user_text("不要打开计算器", refresh=False, source="text")
        with patch("gateway.plan_voice_control", new=AsyncMock(return_value={"status": "none", "actions": []})):
            with self.assertRaises(PermissionError):
                await voice.execute_control_plan(requested=("desktop_open_app", {"app": "calculator"}))
        runtime.media_service.invoke.assert_not_awaited()
        voice.control_plan = {"status": "execute", "actions": [{"tool": "desktop_open_app", "arguments": {"app": "notepad"}}]}
        with self.assertRaises(PermissionError):
            await voice.execute_control_plan(requested=("desktop_open_app", {"app": "calculator"}))
        runtime.media_service.invoke.assert_not_awaited()

    async def test_new_turn_during_planning_rejects_old_plan(self):
        voice, runtime = session()
        await voice.observe_user_text("打开计算器", refresh=False, source="text")

        async def changed(*_args):
            voice.control_turn += 1
            return {"status": "execute", "actions": [{"tool": "desktop_open_app", "arguments": {"app": "calculator"}}]}

        with patch("gateway.plan_voice_control", new=changed):
            with self.assertRaises(PermissionError):
                await voice.execute_control_plan()
        runtime.media_service.invoke.assert_not_awaited()

    async def test_prose_only_response_executes_and_requests_truthful_followup_once(self):
        voice, runtime = session()
        await voice.observe_user_text("请把计算器打开", refresh=False, source="text")
        plan = {"status": "execute", "actions": [{"tool": "desktop_open_app", "arguments": {"app": "calculator"}}]}
        with patch("gateway.plan_voice_control", new=AsyncMock(return_value=plan)):
            await voice.finish_unhandled_control(voice.control_turn, voice.generation)
            await voice.finish_unhandled_control(voice.control_turn, voice.generation)
        runtime.media_service.invoke.assert_awaited_once()
        voice.backend.request_response.assert_awaited_once()


class PlanValidationTests(unittest.IsolatedAsyncioTestCase):
    async def test_home_catalog_is_paginated_with_supported_limits(self):
        media = media_service()
        home = SimpleNamespace(configured=True, list_home_devices=AsyncMock(side_effect=[
            {"devices": [], "next_cursor": "actual-next"},
            {"devices": [], "next_cursor": None},
        ]))
        with patch("src.assistant_app.application.voice_control.complete_text", new=AsyncMock(return_value='{"status":"none","actions":[]}')):
            await plan_voice_control({}, object(), "你好", [], media, home)
        self.assertEqual(home.list_home_devices.await_count, 2)
        self.assertEqual(home.list_home_devices.await_args_list[1].kwargs, {"limit": 30, "cursor": "actual-next"})

    async def test_home_unavailable_does_not_block_pc_planning(self):
        home = SimpleNamespace(configured=True, list_home_devices=AsyncMock(side_effect=ConnectionError("offline")))
        step = {"tool": "desktop_open_app", "arguments": {"app": "calculator"}}
        with self.assertLogs("assistant.voice_control", level="ERROR"):
            with patch("src.assistant_app.application.voice_control.complete_text", new=AsyncMock(return_value=json.dumps({"status": "execute", "actions": [step]}))):
                result = await plan_voice_control({}, object(), "打开计算器", [], media_service(), home)
        self.assertEqual(result["actions"], [step])

    async def test_model_parameters_are_validated_before_any_execution(self):
        media = media_service()
        plan = {"status": "execute", "actions": [{"tool": "desktop_open_app", "arguments": {"app": "arbitrary.exe"}}]}
        with patch("src.assistant_app.application.voice_control.complete_text", new=AsyncMock(return_value=json.dumps(plan))):
            with self.assertRaises(ValueError):
                await plan_voice_control({}, object(), "打开计算器", [], media, None)
        media.invoke.assert_not_awaited()

    async def test_unknown_tool_and_excessive_plan_are_rejected(self):
        for actions in ([{"tool": "run_shell", "arguments": {}}], [{"tool": "desktop_lock", "arguments": {}}] * 5):
            with patch("src.assistant_app.application.voice_control.complete_text", new=AsyncMock(return_value=json.dumps({"status": "execute", "actions": actions}))):
                with self.assertRaises(ValueError):
                    await plan_voice_control({}, object(), "执行命令", [], media_service(), None)


class ClimateCapabilityTests(unittest.TestCase):
    def test_power_actions_require_reported_features_and_services(self):
        controller = object.__new__(HomeController)
        state = {"state": "off", "attributes": {"supported_features": 429, "hvac_modes": ["off", "heat"], "min_temp": 16, "max_temp": 31}}
        services = {"climate": {"turn_on", "turn_off", "set_temperature", "set_hvac_mode"}}
        actions = controller._actions_for("climate.actual", state, services)
        self.assertIn("climate.actual:turn_on", {item["id"] for item in actions})
        self.assertIn("climate.actual:turn_off", {item["id"] for item in actions})
        state["attributes"]["supported_features"] = 1
        actions = controller._actions_for("climate.actual", state, services)
        self.assertNotIn("climate.actual:turn_on", {item["id"] for item in actions})


if __name__ == "__main__":
    unittest.main()
