"""独立UI验收预览：真实平台只读，隔离本地授权，所有家电动作与云模型连接禁用。"""

import argparse
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

from aiohttp import web

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from gateway import create_app
from home_control import HomeController


async def block_appliance_action(self, identifier, action, parameters=None):
    raise ValueError("独立验收预览只读，真实家电动作已禁用")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8766)
    arguments = parser.parse_args()
    config = json.loads((ROOT / "config.local.json").read_text(encoding="utf-8-sig"))
    with tempfile.TemporaryDirectory(prefix="home-ui-review-") as folder:
        config.update({"runtime_dir": folder, "device_id": "isolated-home-ui-review", "device_token": "isolated-ui-review-token-20261029", "max_connections": 1})
        with patch.object(HomeController, "execute_home_action", new=block_appliance_action):
            app = create_app(config, realtime_url="ws://127.0.0.1:1/disabled-for-ui-review")
            web.run_app(app, host="127.0.0.1", port=arguments.port, access_log=None)


if __name__ == "__main__":
    main()
