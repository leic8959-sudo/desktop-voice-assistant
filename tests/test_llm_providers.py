"""LLM 供应商注册表、偏好持久化与句子切分的本地单元测试。"""

import json
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from llm_providers import LlmSettings, apply_defaults, build_instructions, chat_tool_schemas, dashscope_ws_url, provider_available, provider_chain, validate_config
from chat_pipeline import _SentenceSplitter


def make_config(**overrides):
    config = {"aliyun_api_key": "mock-aliyun-key", "aliyun_workspace_id": "mock-space", "deepseek_api_key": ""}
    apply_defaults(config)
    config.update(overrides)
    return config


class ProviderRegistryTests(unittest.TestCase):
    def test_apply_defaults_fill_all_llm_keys(self):
        config = {}
        apply_defaults(config)
        self.assertEqual(config["llm_provider"], "qwen_realtime")
        self.assertEqual(config["chat_tts_voice"], "longxiaochun_v2")
        self.assertIn("qwen_chat_base_url", config)

    def test_validate_config_rejects_bad_values(self):
        with self.assertRaises(ValueError):
            validate_config(make_config(llm_provider="gpt"))
        with self.assertRaises(ValueError):
            validate_config(make_config(chat_tts_voice="longxiaochun"))
        with self.assertRaises(ValueError):
            validate_config(make_config(deepseek_thinking="maybe"))
        with self.assertRaises(ValueError):
            validate_config(make_config(chat_turn_silence_ms=50))
        validate_config(make_config())

    def test_provider_available_requires_credentials(self):
        self.assertTrue(provider_available("qwen_chat", make_config()))
        self.assertFalse(provider_available("deepseek", make_config()))
        self.assertTrue(provider_available("deepseek", make_config(deepseek_api_key="sk-deepseek-key-0001")))
        self.assertFalse(provider_available("qwen_chat", make_config(aliyun_api_key="REPLACE_WITH_BEIJING_API_KEY")))

    def test_provider_chain_keeps_selected_first_then_available(self):
        config = make_config(llm_provider="deepseek", deepseek_api_key="sk-deepseek-key-0001")
        self.assertEqual(provider_chain(config), ["deepseek", "qwen_realtime", "qwen_chat"])
        config = make_config(llm_provider="qwen_realtime")
        self.assertEqual(provider_chain(config), ["qwen_realtime", "qwen_chat"])
        empty = {"llm_provider": "qwen_realtime", "aliyun_api_key": "REPLACE", "aliyun_workspace_id": "REPLACE"}
        apply_defaults(empty)
        self.assertEqual(provider_chain(empty), ["qwen_realtime"])

    def test_dashscope_ws_url_prefers_workspace_domain_and_override(self):
        config = make_config()
        self.assertEqual(dashscope_ws_url(config), "wss://mock-space.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference")
        config["dashscope_ws_url"] = "ws://127.0.0.1:9/inference"
        self.assertEqual(dashscope_ws_url(config), "ws://127.0.0.1:9/inference")


class LlmSettingsTests(unittest.TestCase):
    def test_save_and_reload_persists_provider(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "llm-preference.json"
            config = make_config(deepseek_api_key="sk-deepseek-key-0001")
            settings = LlmSettings(path, config)
            settings.save("deepseek")
            self.assertEqual(config["llm_provider"], "deepseek")
            reloaded = make_config(deepseek_api_key="sk-deepseek-key-0001")
            LlmSettings(path, reloaded)
            self.assertEqual(reloaded["llm_provider"], "deepseek")

    def test_save_rejects_provider_without_key(self):
        with tempfile.TemporaryDirectory() as folder:
            settings = LlmSettings(Path(folder) / "llm-preference.json", make_config())
            with self.assertRaises(ValueError):
                settings.save("deepseek")

    def test_corrupt_preference_file_keeps_default(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "llm-preference.json"
            path.write_text(json.dumps({"provider": "gpt"}), encoding="utf-8")
            config = make_config()
            with self.assertRaises(ValueError):
                LlmSettings(path, config)


class SchemaAndPromptTests(unittest.TestCase):
    def test_chat_tool_schemas_nests_function(self):
        converted = chat_tool_schemas([{"type": "function", "name": "remember_fact", "description": "保存", "parameters": {"type": "object"}}])
        self.assertEqual(converted[0]["type"], "function")
        self.assertEqual(converted[0]["function"]["name"], "remember_fact")
        self.assertEqual(converted[0]["function"]["parameters"], {"type": "object"})

    def test_build_instructions_contains_memory_and_search(self):
        text = build_instructions(make_config(enable_search=True), "[]")
        self.assertIn("你叫小燕子", text)
        self.assertIn("search_web", text)
        self.assertIn("[]", text)
        closed = build_instructions(make_config(enable_search=False), "[]")
        self.assertIn("联网搜索当前关闭", closed)


class SentenceSplitterTests(unittest.TestCase):
    def test_hard_boundary_flushes_sentence(self):
        splitter = _SentenceSplitter()
        self.assertEqual(splitter.push("好的，已"), [])
        self.assertEqual(splitter.push("创建提醒。"), ["好的，已创建提醒。"])
        self.assertEqual(splitter.buffer, "")

    def test_soft_boundary_flushes_long_buffer(self):
        splitter = _SentenceSplitter()
        pieces = splitter.push("今天需要完成很多事情，比如写报告和开会，还要整理文档" * 4)
        self.assertTrue(pieces)
        self.assertTrue(all(len(piece) > 0 for piece in pieces))
        self.assertLess(len(splitter.buffer), len("今天需要完成很多事情，比如写报告和开会，还要整理文档" * 4))

    def test_english_and_newline_boundaries(self):
        splitter = _SentenceSplitter()
        self.assertEqual(splitter.push("ok!\n"), ["ok!", "\n"])
        self.assertEqual(splitter.push("\n"), ["\n"])
        self.assertEqual(splitter.buffer, "")


if __name__ == "__main__":
    unittest.main()
