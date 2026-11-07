"""由用户主动运行：真实调用 CosyVoice TTS → Paraformer ASR → 千问兼容接口各一次，验证 chat 管道协议；产生少量费用。

闭环设计：TTS 合成 16k PCM 直喂 ASR，得到转写文本；chat 用流式请求分别验证
纯文本与流式+工具两种形态。DeepSeek 无密钥时跳过。
"""

import asyncio
import base64
import json
from pathlib import Path
import sys
import uuid

import aiohttp

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from gateway import load_config
from llm_providers import apply_defaults, dashscope_ws_url


def header_of(event):
    return event.get("header", {})


async def smoke_tts(client, config, text):
    url = dashscope_ws_url(config)
    headers = {"Authorization": "Bearer " + config["aliyun_api_key"], "X-DashScope-DataInspection": "enable"}
    workspace = config.get("aliyun_workspace_id", "")
    if workspace and "REPLACE" not in workspace:
        headers["X-DashScope-WorkSpace"] = workspace
    task_id = str(uuid.uuid4())
    audio = bytearray()
    async with client.ws_connect(url, headers=headers, timeout=aiohttp.ClientWSTimeout(ws_close=10)) as socket:
        await socket.send_json({"header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"task_group": "audio", "task": "tts", "function": "SpeechSynthesizer", "model": config["chat_tts_model"], "parameters": {"text_type": "PlainText", "voice": config["chat_tts_voice"], "format": "pcm", "sample_rate": 16000, "volume": 50, "rate": 1.0, "pitch": 1.0}, "input": {}}})
        started = False
        async for message in socket:
            if message.type != aiohttp.WSMsgType.TEXT:
                continue
            name = header_of(json.loads(message.data)).get("event")
            if name == "task-started":
                started = True
                break
            if name == "task-failed":
                raise RuntimeError("TTS task-failed：" + message.data[:200])
        if not started:
            raise RuntimeError("TTS 未收到 task-started")
        await socket.send_json({"header": {"action": "continue-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"input": {"text": text}}})
        await socket.send_json({"header": {"action": "finish-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"input": {}}})
        async for message in socket:
            if message.type == aiohttp.WSMsgType.BINARY:
                audio.extend(message.data)
                continue
            if message.type != aiohttp.WSMsgType.TEXT:
                continue
            name = header_of(json.loads(message.data)).get("event")
            if name == "task-finished":
                break
            if name == "task-failed":
                raise RuntimeError("TTS task-failed：" + message.data[:200])
    return bytes(audio)


async def smoke_asr(client, config, pcm):
    url = dashscope_ws_url(config)
    headers = {"Authorization": "Bearer " + config["aliyun_api_key"], "X-DashScope-DataInspection": "enable"}
    workspace = config.get("aliyun_workspace_id", "")
    if workspace and "REPLACE" not in workspace:
        headers["X-DashScope-WorkSpace"] = workspace
    task_id = str(uuid.uuid4())
    finals = []
    async with client.ws_connect(url, headers=headers, timeout=aiohttp.ClientWSTimeout(ws_close=10)) as socket:
        await socket.send_json({"header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"task_group": "audio", "task": "asr", "function": "recognition", "model": config["chat_asr_model"], "parameters": {"format": "pcm", "sample_rate": 16000}, "input": {}}})
        started = False
        async for message in socket:
            if message.type != aiohttp.WSMsgType.TEXT:
                continue
            name = header_of(json.loads(message.data)).get("event")
            if name == "task-started":
                started = True
                break
            if name == "task-failed":
                raise RuntimeError("ASR task-failed：" + message.data[:200])
        if not started:
            raise RuntimeError("ASR 未收到 task-started")
        for offset in range(0, len(pcm), 3200):
            await socket.send_bytes(pcm[offset:offset + 3200])
            await asyncio.sleep(0.03)
        await socket.send_json({"header": {"action": "finish-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"input": {}}})
        async for message in socket:
            if message.type != aiohttp.WSMsgType.TEXT:
                continue
            event = json.loads(message.data)
            name = header_of(event).get("event")
            if name == "result-generated":
                sentence = event.get("payload", {}).get("output", {}).get("sentence", {})
                if sentence.get("sentence_end") and sentence.get("text"):
                    finals.append(sentence["text"])
            elif name == "task-finished":
                break
            elif name == "task-failed":
                raise RuntimeError("ASR task-failed：" + message.data[:200])
    return "".join(finals)


async def smoke_chat(client, config, with_tools):
    url = config["qwen_chat_base_url"].rstrip("/") + "/chat/completions"
    payload = {"model": config["qwen_chat_model"], "messages": [{"role": "user", "content": "北京今天天气怎么样？如果需要天气数据请调用工具。"}], "max_tokens": 256, "stream": True, "stream_options": {"include_usage": True}}
    if config.get("qwen_chat_thinking") is not None:
        payload["enable_thinking"] = bool(config["qwen_chat_thinking"])
    if with_tools:
        payload["tools"] = [{"type": "function", "function": {"name": "get_weather", "description": "查询城市天气", "parameters": {"type": "object", "properties": {"city": {"type": "string"}}, "required": ["city"]}}}]
        payload["tool_choice"] = "auto"
    chunks = 0
    content = ""
    tool_calls = {}
    usage = None
    async with client.post(url, json=payload, headers={"Authorization": "Bearer " + config["aliyun_api_key"], "Content-Type": "application/json"}, timeout=aiohttp.ClientTimeout(total=None, connect=10, sock_read=60)) as response:
        status = response.status
        if status != 200:
            return {"status": status, "body": (await response.text())[:300]}
        while True:
            line = await response.content.readline()
            if not line:
                break
            line = line.strip()
            if not line.startswith(b"data:"):
                continue
            data = line[5:].strip()
            if data == b"[DONE]":
                break
            event = json.loads(data)
            choices = event.get("choices") or []
            delta = (choices[0].get("delta") or {}) if choices else {}
            content += delta.get("content") or ""
            for item in delta.get("tool_calls") or []:
                slot = tool_calls.setdefault(item.get("index", 0), {"id": "", "name": "", "arguments": ""})
                if item.get("id"):
                    slot["id"] = item["id"]
                function = item.get("function") or {}
                slot["name"] += function.get("name") or ""
                slot["arguments"] += function.get("arguments") or ""
            if event.get("usage"):
                usage = event["usage"]
            chunks += 1
    return {"status": status, "chunks": chunks, "content": content[:120], "tool_calls": list(tool_calls.values()), "usage": usage}


async def main():
    config = load_config(ROOT / "config.local.json")
    apply_defaults(config)
    print("== 1. CosyVoice TTS ==")
    pcm = await smoke_tts(config=config, client=CLIENT["http"], text="你好牛牛，小燕子的模型冒烟测试开始。")
    print(f"TTS_OK bytes={len(pcm)} seconds={len(pcm) / 16000:.2f}")
    print("== 2. Paraformer ASR ==")
    transcript = await smoke_asr(client=CLIENT["http"], config=config, pcm=pcm)
    print(f"ASR_OK transcript={transcript!r}")
    print("== 3. 千问兼容接口 流式纯文本 ==")
    plain = await smoke_chat(client=CLIENT["http"], config=config, with_tools=False)
    print("CHAT_PLAIN " + json.dumps(plain, ensure_ascii=False)[:400])
    print("== 4. 千问兼容接口 流式+工具 ==")
    tools = await smoke_chat(client=CLIENT["http"], config=config, with_tools=True)
    print("CHAT_TOOLS " + json.dumps(tools, ensure_ascii=False)[:400])
    key = config.get("deepseek_api_key")
    print("== 5. DeepSeek ==")
    if key and "REPLACE" not in key and len(key) >= 8:
        deepseek_config = {**config, "qwen_chat_base_url": config["deepseek_base_url"], "qwen_chat_model": config["deepseek_model"]}
        result = await smoke_chat(client=CLIENT["http"], config=deepseek_config, with_tools=True)
        print("DEEPSEEK " + json.dumps(result, ensure_ascii=False)[:400])
    else:
        print("DEEPSEEK_SKIPPED 未配置 deepseek_api_key")


async def amain():
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=90, sock_connect=10)) as client:
        CLIENT["http"] = client
        await main()


CLIENT = {}
if __name__ == "__main__":
    asyncio.run(amain())
