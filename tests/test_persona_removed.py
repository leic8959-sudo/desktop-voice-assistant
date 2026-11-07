"""移除人格后的兼容、接口、提示词检查；只用临时数据。"""

import tempfile
import unittest
from pathlib import Path

from aiohttp.test_utils import TestClient, TestServer

from assistant_runtime import RUNTIME_KEY
from gateway import create_app
from llm_providers import apply_defaults, build_instructions, validate_config


class NeutralPromptTests(unittest.TestCase):
    def test_old_persona_config_does_not_apply(self):
        for previous in ("roast", "northeast", "unknown-legacy-persona"):
            config = {"enable_search": False, "persona_id": previous}
            apply_defaults(config)
            validate_config(config)
            self.assertNotIn("persona_id", config)
            prompt = build_instructions(config, "{}")
            for removed in ("毒舌损友", "切换人格", "皇阿玛", "双击屏幕", "东北话", "PERSONA"):
                self.assertNotIn(removed, prompt)
            self.assertIn("工具结果未确认前不得宣称设备执行成功", prompt)
            self.assertIn("泄露隐私与密钥", prompt)


class RemovedApiTests(unittest.IsolatedAsyncioTestCase):
    async def test_old_preference_is_not_loaded_and_endpoints_are_gone(self):
        with tempfile.TemporaryDirectory() as directory:
            # 即使旧偏好是损坏文件，启动也不再读取或应用它。
            Path(directory, "persona-preference.json").write_text("invalid-old-file", encoding="utf-8")
            app = create_app({"runtime_dir": directory, "device_id": "offline", "device_token": "offline-persona-removal-token", "aliyun_model": "offline", "aliyun_voice": "Tina", "enable_search": False, "home_entities": {}})
            async with TestClient(TestServer(app)) as client:
                headers = {"Authorization": "Bearer offline-persona-removal-token"}
                for method, path in (("get", "/api/personas"), ("put", "/api/persona")):
                    response = await getattr(client, method)(path, headers=headers, json={"persona": "roast"})
                    self.assertEqual(response.status, 404)
                response = await client.get("/api/status", headers=headers)
                status = await response.json()
                self.assertNotIn("persona", status)
                self.assertNotIn("personas", status["capabilities"])
                self.assertFalse(hasattr(app[RUNTIME_KEY], "persona_settings"))


if __name__ == "__main__": unittest.main()
