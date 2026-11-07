"""由用户主动运行：发出一次文字问候，验证阿里音频回复，可能产生费用。"""

import asyncio
import base64
import json
from pathlib import Path
import sys
import wave

import aiohttp

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from gateway import load_config, session_settings


async def main():
    config = load_config(ROOT / "config.local.json")
    if any("REPLACE" in config[key] for key in ("aliyun_api_key", "aliyun_workspace_id")):
        raise ValueError("请先在本地填写阿里配置")
    url = f"wss://{config['aliyun_workspace_id']}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime"
    pcm = bytearray()
    usage = {}
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20, sock_connect=10)) as client:
        async with client.ws_connect(url, params={"model": config["aliyun_model"]}, headers={"Authorization": "Bearer " + config["aliyun_api_key"]}, max_msg_size=4 * 1024 * 1024) as socket:
            async with asyncio.timeout(60):
                async for message in socket:
                    if message.type != aiohttp.WSMsgType.TEXT:
                        raise RuntimeError("阿里连接在回复完成前结束")
                    event = json.loads(message.data)
                    kind = event.get("type")
                    if kind == "session.created":
                        settings = session_settings(config, "{}")
                        settings["session"].update({"turn_detection": None, "tools": [], "enable_search": False})
                        settings["session"].pop("tool_choice", None)
                        settings["session"].pop("search_options", None)
                        await socket.send_json(settings)
                    elif kind == "session.updated":
                        await socket.send_json({"type": "conversation.item.create", "item": {"type": "message", "role": "user", "content": [{"type": "input_text", "text": "请用中文说：你好牛牛，语音测试开始。"}]}})
                        await socket.send_json({"type": "response.create"})
                    elif kind == "response.audio.delta":
                        pcm.extend(base64.b64decode(event["delta"], validate=True))
                        if len(pcm) > 24000 * 2 * 30:
                            raise ValueError("回复超过30秒，已停止探测")
                    elif kind == "error":
                        raise RuntimeError("阿里接口错误代码=" + str(event.get("error", {}).get("code", "unknown")))
                    elif kind == "response.done":
                        if event.get("response", {}).get("status") != "completed":
                            raise RuntimeError("阿里回复未完成")
                        usage = event.get("response", {}).get("usage", {})
                        break
    if not pcm:
        raise RuntimeError("没有收到音频，本次探测不通过")
    output = ROOT / "runtime" / "probe"
    output.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output / "reply.wav"), "wb") as target:
        target.setnchannels(1)
        target.setsampwidth(2)
        target.setframerate(24000)
        target.writeframes(pcm)
    (output / "usage.json").write_text(json.dumps(usage, ensure_ascii=False, indent=2), encoding="utf-8")
    print("已收到阿里音频，保存在 runtime/probe/reply.wav；请人工播放确认。用量见 usage.json。")


if __name__ == "__main__":
    asyncio.run(main())
