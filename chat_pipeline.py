"""DeepSeek / 千问文本模型语音管道：DashScope 实时 ASR → chat.completions → CosyVoice TTS。

设备侧协议与 qwen_realtime 完全一致；差异在云端编排：
- 上行 PCM 直喂 Paraformer 实时识别（推理端点一连接一任务，断线重连并缓存音频）；
- 识别文本按静音计时合成用户轮，流式请求 chat completions（流式失败自动降级非流式重试）；
- 回复按句送 CosyVoice（pcm 24k，与设备下行采样率一致），音频帧以 response.audio.delta
  归一化事件回传，播放节拍、打断和状态机由 VoiceSession 统一处理。

供应商说明：ASR/TTS 固定走 DashScope（阿里凭据），chat 模型按供应商选择——
deepseek 只替换推理接口，仍依赖 aliyun_api_key 完成语音两端。

DashScope 推理协议要点（2026-11 官方文档核实）：鉴权只走 Authorization 头，任务 ID 在
消息体 header.task_id；ASR 结果 payload.output.sentence.sentence_end 区分中间/最终结果；
TTS 必须先收 task-started 再 continue-task，文本发完必须 finish-task 才能拿到完整音频，
task-failed 的错误码在 header 内。
"""

import asyncio
import base64
import hashlib
import json
import logging
import uuid

import aiohttp

from llm_providers import build_instructions, chat_tool_schemas, dashscope_ws_url
from src.assistant_app.application.errors import LocalToolArgumentsError, tool_error_result, tool_result_unconfirmed, paused_tool_result
from src.assistant_app.integrations.music_assistant import MusicAssistantError

LOGGER = logging.getLogger("assistant.chat_pipeline")

MAX_TOOL_ROUNDS = 4
MAX_RESPONSE_TOOLS = 8
MAX_SESSION_TOOL_CALLS = 100
MAX_TOOL_ARGUMENT_BYTES = 16384
MAX_HISTORY_MESSAGES = 20
MAX_REPLY_CHARS = 4000
MAX_EVENT_QUEUE = 2000
MAX_ASR_AUDIO_QUEUE = 800  # 约16秒@20ms帧，限制重连窗口内的缓存上限
ASR_RECONNECT_LIMIT = 3
ASR_HANDSHAKE_TIMEOUT = 15
ASR_SEND_TIMEOUT = 10
ASR_STARTUP_TIMEOUT = 20
ASR_CLOSE_TIMEOUT = 10
STREAM_READ_BUFSIZE = 1024 * 1024
REQUEST_TIMEOUT = aiohttp.ClientTimeout(total=None, connect=10, sock_read=90)
BLOCKING_TOTAL_TIMEOUT = 120
TTS_HANDSHAKE_TIMEOUT = 15
TTS_SEND_TIMEOUT = 15
TTS_FINISH_TIMEOUT = 30
HARD_BOUNDARIES = "。！？!?；;\n"
SOFT_BOUNDARIES = "，,、：:"
SOFT_FLUSH_CHARS = 60


class _ChatHTTPError(Exception):
    def __init__(self, status, body):
        super().__init__(f"chat接口HTTP {status}：{body}")
        self.status = status


class _Chunk:
    __slots__ = ("content", "reasoning", "tool_delta", "usage")

    def __init__(self, content="", reasoning="", tool_delta=None, usage=None):
        self.content, self.reasoning, self.tool_delta, self.usage = content, reasoning, tool_delta, usage


class _SentenceSplitter:
    """流式回复按句切分：硬边界立即出句，长缓冲在软边界补切，尾句由调用方 flush。"""

    def __init__(self):
        self.buffer = ""

    def push(self, text):
        self.buffer += text
        pieces = []
        while True:
            cuts = [index for index in (self.buffer.find(ch) for ch in HARD_BOUNDARIES) if index >= 0]
            if not cuts:
                break
            index = min(cuts)
            pieces.append(self.buffer[: index + 1])
            self.buffer = self.buffer[index + 1 :]
        if len(self.buffer) >= SOFT_FLUSH_CHARS:
            soft = [index for index in (self.buffer.rfind(ch) for ch in SOFT_BOUNDARIES) if index > 0]
            if soft:
                index = max(soft)
                pieces.append(self.buffer[: index + 1])
                self.buffer = self.buffer[index + 1 :]
        return pieces


class _AsrCleanupError(RuntimeError):
    pass


class _AsrPump:
    """Paraformer 实时识别连接管理：一连接一任务，断线重连，缓存重连窗口内的音频。"""

    def __init__(self, backend):
        self.backend = backend
        self.client = backend.client
        self.task = None
        self.ws = None
        self.ready = asyncio.Event()
        self.restart_requested = asyncio.Event()

    def start(self):
        if self.task is None or self.task.done():
            self.task = asyncio.create_task(self.run())

    def restart(self):
        # 唤醒词后的 detect：丢弃引擎内半截状态，立即重建任务；期间音频入队不丢。
        self.restart_requested.set()

    async def wait_ready(self):
        async with asyncio.timeout(ASR_STARTUP_TIMEOUT):
            while not self.backend._closed:
                waiter = asyncio.create_task(self.ready.wait())
                try:
                    done, _ = await asyncio.wait((waiter, self.task), return_when=asyncio.FIRST_COMPLETED)
                    if self.task in done:
                        await self.task
                        raise RuntimeError("ASR启动失败，未收到可用的识别任务")
                    if self.ready.is_set():
                        return
                finally:
                    if not waiter.done():
                        waiter.cancel()
                    await asyncio.gather(waiter, return_exceptions=True)
        raise RuntimeError("ASR启动期间会话已关闭")

    async def _close_quietly(self, ws):
        try:
            async with asyncio.timeout(ASR_CLOSE_TIMEOUT):
                await ws.close()
            if not ws.closed:
                raise _AsrCleanupError("ASR连接尚未关闭")
        except Exception:
            LOGGER.exception("asr_ws_close_failed")
            return False
        return True

    async def run(self):
        retry = 0
        while not self.backend._closed:
            self.restart_requested.clear()
            try:
                await self._cycle()
            except asyncio.CancelledError:
                raise
            except _AsrCleanupError:
                LOGGER.exception("asr_cleanup_unconfirmed")
                self.backend._emit({"type": "error", "error": {"code": "asr_unavailable"}})
                return
            except Exception:
                if self.backend._closed:
                    return
                retry += 1
                LOGGER.exception("asr_connection_failed retry=%d", retry)
                if retry > ASR_RECONNECT_LIMIT:
                    self.backend._emit({"type": "error", "error": {"code": "asr_unavailable"}})
                    return
                try:
                    await asyncio.wait_for(self.restart_requested.wait(), timeout=min(0.5 * retry, 2))
                except TimeoutError:
                    pass

    async def _cycle(self):
        connection = asyncio.create_task(self._serve())
        restart = asyncio.create_task(self.restart_requested.wait())
        try:
            done, _ = await asyncio.wait((connection, restart), return_when=asyncio.FIRST_COMPLETED)
            if restart not in done and not self.restart_requested.is_set():
                await connection
        finally:
            for task in (connection, restart):
                if not task.done() and not task.cancelling():
                    task.cancel()
            results = await asyncio.gather(connection, restart, return_exceptions=True)
            if isinstance(results[0], Exception):
                raise results[0]

    async def _serve(self):
        ws = None
        tasks = []
        task_id = str(uuid.uuid4())
        try:
            async with asyncio.timeout(ASR_HANDSHAKE_TIMEOUT):
                ws = await self.client.ws_connect(dashscope_ws_url(self.backend.config), headers=self.backend.dashscope_headers(), heartbeat=15, max_msg_size=1024 * 1024, timeout=aiohttp.ClientWSTimeout(ws_close=10))
                self.ws = ws
                parameters = {"format": "pcm", "sample_rate": 16000, "max_sentence_silence": 600}
                await ws.send_json({"header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"task_group": "audio", "task": "asr", "function": "recognition", "model": self.backend.config["chat_asr_model"], "parameters": parameters, "input": {}}})
                async for message in ws:
                    if message.type == aiohttp.WSMsgType.TEXT:
                        header = json.loads(message.data).get("header", {})
                        if header.get("event") == "task-started":
                            break
                        if header.get("event") == "task-failed":
                            raise RuntimeError("ASR任务被拒绝：" + str(header.get("error_code", "")))
                    elif message.type == aiohttp.WSMsgType.ERROR:
                        raise RuntimeError("ASR握手连接错误")
                    elif message.type in (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.CLOSING):
                        raise RuntimeError("ASR连接在握手完成前关闭")
                else:
                    raise RuntimeError("ASR连接在握手完成前结束")
            self.ready.set()
            tasks = [asyncio.create_task(self._feed(ws)), asyncio.create_task(self._read(ws))]
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                await task
            raise RuntimeError("ASR双向传输提前结束")
        finally:
            # 先废弃旧的待定句和静音计时，慢关闭期间也不能触发旧命令。
            self.ready.clear()
            self.backend._reset_accumulators()
            for task in tasks:
                if not task.done() and not task.cancelling():
                    task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            if ws is not None:
                if not await self._close_quietly(ws):
                    raise _AsrCleanupError("ASR连接清理未确认，停止重连")
                if self.ws is ws:
                    self.ws = None

    async def _feed(self, ws):
        while True:
            frame = await self.backend._asr_audio.get()
            async with asyncio.timeout(ASR_SEND_TIMEOUT):
                await ws.send_bytes(frame)

    async def _read(self, ws):
        async for message in ws:
            if message.type == aiohttp.WSMsgType.TEXT:
                event = json.loads(message.data)
                header = event.get("header", {})
                name = header.get("event")
                if name == "result-generated":
                    self.backend.on_asr_result(event)
                elif name == "task-failed":
                    raise RuntimeError("ASR任务失败：" + str(header.get("error_code", "")) + " " + str(header.get("error_message", ""))[:120])
            elif message.type == aiohttp.WSMsgType.ERROR:
                raise RuntimeError("ASR连接错误")
            elif message.type in (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.CLOSING):
                raise RuntimeError("ASR连接已结束")
        raise RuntimeError("ASR连接被服务端关闭")


class ChatBackend:
    """deepseek / qwen_chat 共用的云端编排，事件形状与 Realtime 事件对齐。"""

    def __init__(self, session, provider_id):
        self.session = session
        self.name = provider_id
        self.config = session.config
        self.client = None
        self._closed = False
        self.asr = None
        self._input_generation = 0
        self._pending_input = None
        self._turn_drain_task = None
        self._active_user_turn_id = None
        self._pending_wake = {}
        self._tool_call_count = 0
        self._tool_chain_paused = False
        self._event_overflowed = False
        self._forced_music_tools = None
        self._turn_task = None
        self._timer_task = None
        self._tts_task = None

    async def connect(self):
        config = self.config
        if self.name == "deepseek":
            if not provider_key(config, "deepseek"):
                raise ValueError("DeepSeek 语音管道需要 deepseek_api_key 与阿里 ASR/TTS 凭据同时配置")
            self._chat_url = config["deepseek_base_url"].rstrip("/") + "/chat/completions"
            self._chat_key = config["deepseek_api_key"]
            self._chat_model = config["deepseek_model"]
        else:
            if "REPLACE" in config["aliyun_api_key"] or "REPLACE" in config["aliyun_workspace_id"]:
                raise ValueError("阿里 API Key 和北京业务空间ID尚未配置")
            self._chat_url = config["qwen_chat_base_url"].rstrip("/") + "/chat/completions"
            self._chat_key = config["aliyun_api_key"]
            self._chat_model = config["qwen_chat_model"]
        self.client = self.session.http
        memory_service = getattr(self.session, "memory_service", None)
        memories = await memory_service.context() if memory_service else await asyncio.to_thread(self.session.store.recall_memory, limit=10)
        self._instructions = build_instructions(config, json.dumps(memories, ensure_ascii=False), wake_phrase=self.session.wake_phrase)
        self._tools = chat_tool_schemas(self.session.active_tool_schemas())
        self._history = []
        self._events = asyncio.Queue(maxsize=MAX_EVENT_QUEUE)
        self._asr_audio = asyncio.Queue(maxsize=MAX_ASR_AUDIO_QUEUE)
        self._streaming = True
        self._finals = []
        self._last_partial = ""
        self._turn_generation = 0
        self._response_sequence = 0
        self._active_response_id = None
        self._cancelled = set()
        self._tts_text = None
        self._pending_history = None
        self.asr = _AsrPump(self)
        self.asr.start()
        try:
            await self.asr.wait_ready()
        except BaseException:
            await self.close()
            raise
        self.session.selected_voice = config["chat_tts_voice"]
        LOGGER.info("chat_backend_ready session=%s provider=%s model=%s", self.session.session_id, self.name, self._chat_model)

    @property
    def connected(self):
        return not self._closed

    async def close(self):
        if self._closed:
            return
        self._closed = True
        self._pending_wake.clear()
        self._input_generation += 1
        self._pending_input = None
        current = asyncio.current_task()
        tasks = [self._turn_task, self._timer_task, self._tts_task]
        if current is not self._turn_task:
            tasks.append(self._turn_drain_task)
        if self.asr is not None and self.asr.task is not None:
            tasks.append(self.asr.task)
        tasks = [task for task in tasks if task is not None and task is not current]
        for task in tasks:
            if task is not self._turn_drain_task and not task.done() and not task.cancelling():
                task.cancel()
        cleanup = asyncio.gather(*tasks, return_exceptions=True)
        try:
            await asyncio.shield(cleanup)
        except asyncio.CancelledError:
            await cleanup
            raise
        if self.asr is not None and self.asr.ws is not None:
            if not await self.asr._close_quietly(self.asr.ws):
                raise _AsrCleanupError("ASR连接关闭未确认，不能继续建立其他会话")
            self.asr.ws = None
        LOGGER.info("chat_backend_closed session=%s", self.session.session_id)

    # ---- VoiceSession 调用的接口 ----

    async def events(self):
        while not self._closed:
            event = await self._events.get()
            yield event

    async def send_user_audio(self, pcm):
        if self._closed:
            return
        try:
            self._asr_audio.put_nowait(pcm)
        except asyncio.QueueFull:
            # 丢掉旧帧可能把完整指令改成另一条；结束本轮，不能继续执行残缺语音。
            raise RuntimeError("ASR音频积压超过上限，本次收音已结束，请重新说完整指令")

    async def clear_audio(self):
        self._reset_accumulators()
        while not self._asr_audio.empty():
            self._asr_audio.get_nowait()
        if self.asr is not None:
            self.asr.restart()

    async def commit_audio(self):
        text = "".join(self._finals) or self._last_partial
        self._reset_accumulators()
        if text.strip():
            await self._start_user_turn(text.strip())

    async def send_user_text(self, text):
        await self._start_user_turn(text, typed=True)

    async def set_turn_detection(self, manual):
        # chat 管道的轮次由 ASR 静音计时决定，无服务端 VAD 可切换。
        LOGGER.debug("turn_detection_ignored provider=%s manual=%s", self.name, manual)

    async def update_instructions(self, instructions):
        self._instructions = instructions

    async def cancel_response(self):
        self._pending_wake.clear()
        self._input_generation += 1
        self._pending_input = None
        current = asyncio.current_task()
        response_id = self._active_response_id
        if response_id is not None:
            self._cancelled.add(response_id)
            if len(self._cancelled) > 100:
                self._cancelled.clear()
        self._turn_generation += 1
        timer = self._timer_task
        self._timer_task = None
        task = self._turn_task
        tasks = [item for item in (timer, task) if item is not None and item is not current]
        for item in tasks:
            if not item.done() and not item.cancelling():
                item.cancel()
        # 只等待取消时的旧任务；后到的新输入可由drain继续处理。
        cleanup = asyncio.gather(*tasks, return_exceptions=True)
        try:
            await asyncio.shield(cleanup)
        except asyncio.CancelledError:
            await cleanup
            raise
        if self._turn_task is task and task is not current:
            self._turn_task = None

    # ---- ASR 结果与轮次检测 ----

    def dashscope_headers(self):
        headers = {"Authorization": "Bearer " + self.config["aliyun_api_key"], "X-DashScope-DataInspection": "enable"}
        workspace = self.config.get("aliyun_workspace_id", "")
        if isinstance(workspace, str) and workspace and "REPLACE" not in workspace:
            headers["X-DashScope-WorkSpace"] = workspace
        return headers

    def on_asr_result(self, event):
        if self._closed:
            return
        sentence = event.get("payload", {}).get("output", {}).get("sentence", {})
        if sentence.get("heartbeat"):
            return
        text = sentence.get("text") or ""
        if not text:
            return
        if sentence.get("sentence_end"):
            self._finals.append(text)
            self._last_partial = ""
        else:
            self._last_partial = text
        self._arm_turn_timer()

    def _reset_accumulators(self):
        self._pending_wake.clear()
        self._finals.clear()
        self._last_partial = ""
        if self._timer_task is not None:
            self._timer_task.cancel()
            self._timer_task = None
        self._turn_generation += 1

    def _arm_turn_timer(self):
        delay = self.config["chat_turn_silence_ms"] / 1000
        self._turn_generation += 1
        generation = self._turn_generation

        async def fire():
            try:
                await asyncio.sleep(delay)
            except asyncio.CancelledError:
                return
            if generation != self._turn_generation or self._closed:
                return
            self._timer_task = None
            text = "".join(self._finals) or self._last_partial
            self._finals.clear()
            self._last_partial = ""
            if text.strip():
                await self._start_user_turn(text.strip())

        if self._timer_task is not None:
            self._timer_task.cancel()
        self._timer_task = asyncio.create_task(fire())

    async def _start_user_turn(self, text, *, typed=False):
        if self._closed:
            return
        self._input_generation += 1
        self._pending_input = (self._input_generation, text, typed)
        # 输入只占一个槽；旧任务收尾期间的新输入覆盖前一份，不累积任务。
        response_id = self._active_response_id
        task = self._turn_task
        if task is not None and task is not asyncio.current_task() and not task.done():
            if response_id is not None:
                self._cancelled.add(response_id)
                if len(self._cancelled) > 100:
                    self._cancelled.clear()
            if not task.cancelling():
                task.cancel()
        if self._turn_drain_task is None or self._turn_drain_task.done():
            self._turn_drain_task = asyncio.create_task(self._drain_turns())

    async def _drain_turns(self):
        try:
            while True:
                task = self._turn_task
                if task is not None:
                    # 取消等待者不能再次取消正在清理的旧回合。
                    await asyncio.shield(asyncio.gather(task, return_exceptions=True))
                    if self._turn_task is task:
                        self._turn_task = None
                if self._closed or self._pending_input is None:
                    return
                generation, text, typed = self._pending_input
                self._pending_input = None
                if generation == self._input_generation:
                    self._turn_task = asyncio.create_task(self._observe_and_run(text, typed, generation))
        except asyncio.CancelledError:
            self._input_generation += 1
            self._pending_input = None
            raise
        finally:
            task = self._turn_task
            if task is not None and not task.done():
                if not task.cancelling():
                    task.cancel()
                await asyncio.gather(task, return_exceptions=True)
            if self._turn_task is task:
                self._turn_task = None
            if self._turn_drain_task is asyncio.current_task():
                self._turn_drain_task = None

    async def _observe_and_run(self, text, typed, generation):
        try:
            if self._closed or generation != self._input_generation:
                return
            accepted = self.session.prepare_chat_user_text(text, self._pending_wake)
            if accepted is None:
                self._emit({"type": "conversation.item.input_audio_transcription.completed", "transcript": text[:4000], "wake_verification": self._pending_wake.get("rejection", "pending")})
                return
            text = accepted
            self._emit({"type": "conversation.item.input_audio_transcription.completed", "transcript": text[:4000]})
            await self.session.observe_user_text(text, source="text" if typed else "audio")
        except asyncio.CancelledError:
            raise
        except Exception:
            LOGGER.exception("chat_turn_prepare_failed session=%s", self.session.session_id)
            self._emit({"type": "error", "error": {"code": "chat_turn_failed"}})
            return
        if not self._closed and generation == self._input_generation:
            self._active_user_turn_id = f"chat-turn-{self.session.session_id[:8]}-{generation}"
            self._emit({"type": "conversation.user_turn.started", "user_turn_id": self._active_user_turn_id})
            await self._run_turn(text)

    # ---- LLM 回合 ----

    async def _run_turn(self, user_text):
        self._tool_chain_paused = False
        previous_history = list(self._history)
        self._pending_history = previous_history
        status = "completed"
        try:
            self._history.append({"role": "user", "content": user_text})
            messages = [{"role": "system", "content": self._instructions}] + self._history
            for _ in range(MAX_TOOL_ROUNDS):
                choice_needed = getattr(self.session, "needs_music_choice", lambda: False)()
                message, tool_calls = await self._chat_round(messages)
                if choice_needed and not tool_calls:
                    if self.session.music_choice_attempted_turn == self.session.input_turn_serial:
                        raise RuntimeError("模型未完成结构化选曲决策，本次没有播放歌曲")
                    self.session.music_choice_attempted_turn = self.session.input_turn_serial
                    self._forced_music_tools = chat_tool_schemas(self.session.music_choice_tools())
                    try:
                        message, tool_calls = await self._chat_round(messages + [{"role": "system", "content": self.session.music_choice_instructions()}])
                    finally:
                        self._forced_music_tools = None
                    if not tool_calls:
                        await self.session.finish_music_no_action()
                        messages.append({"role": "assistant", "content": "服务器事实：未收到可执行选择，本轮没有播放歌曲，也没有执行其他操作。"})
                        break
                    if len(tool_calls) != 1 or any(call["name"] not in {"music_play", "music_queue", "music_selection_decision"} for call in tool_calls.values()):
                        raise RuntimeError("模型未返回允许的结构化选曲决策，本次没有播放歌曲")
                if choice_needed and tool_calls:
                    self.session.settle_music_choice()
                if not tool_calls:
                    messages.append(message)
                    break
                # 带工具调用的 assistant 消息必须紧跟对应 tool 结果，单独入历史会悬空。
                messages.append(message)
                for _, call in sorted(tool_calls.items()):
                    try:
                        result = paused_tool_result() if self._tool_chain_paused else await self._execute_tool(call)
                    except asyncio.CancelledError:
                        messages.append({"role": "tool", "tool_call_id": call["id"], "content": json.dumps({"status": "unknown", "message": "工具执行期间中断，结果需先核查，不能自动重试"}, ensure_ascii=False)})
                        raise
                    messages.append({"role": "tool", "tool_call_id": call["id"], "content": json.dumps(result, ensure_ascii=False)})
                    if tool_result_unconfirmed(result):
                        self._tool_chain_paused = True
                if self._tool_chain_paused:
                    messages.append({"role": "system", "content": "本轮操作结果待核实。只说明请先核查实际状态；不得调用工具、重试操作或宣称成功。"})
                    final_message, final_calls = await self._chat_round(messages)
                    if final_calls:
                        status = "paused"
                    else:
                        messages.append(final_message)
                    break
            else:
                status = "limited"
                LOGGER.warning("tool_round_limit session=%s rounds=%d", self.session.session_id, MAX_TOOL_ROUNDS)
            # 工具调用与结果同属完整用户轮；下一轮选曲必须能看到真实selection_id。
            self._history = messages[1:]
            while len(self._history) > MAX_HISTORY_MESSAGES:
                next_user = next((index for index, item in enumerate(self._history[1:], 1) if item["role"] == "user"), None)
                if next_user is None:
                    break  # 单轮仍受4轮×8工具/4000回复等既有上限约束。
                del self._history[:next_user]
        except asyncio.CancelledError:
            status = "cancelled"
            # 已返回的工具事实不能回滚成从未执行；仅移除未得到结果的调用。
            self._history = self._settled_history(previous_history, messages)
            raise
        except Exception:
            status = "failed"
            self._history = self._settled_history(previous_history, messages)
            LOGGER.exception("chat_turn_failed session=%s", self.session.session_id)
        finally:
            if self._active_user_turn_id is not None:
                self._emit({"type": "conversation.user_turn.done", "user_turn_id": self._active_user_turn_id, "status": status, "tools_paused": self._tool_chain_paused})
            if status == "failed":
                self._emit({"type": "error", "error": {"code": "chat_turn_failed"}})
            self._active_user_turn_id = None
            self._pending_history = None
            self._active_response_id = None

    @staticmethod
    def _settled_history(previous_history, messages):
        current = messages[len(previous_history) + 1:]
        completed = {item["tool_call_id"] for item in current if item.get("role") == "tool"}
        if not completed:
            return previous_history
        retained = []
        for item in current:
            if item.get("role") == "assistant" and item.get("tool_calls"):
                calls = [call for call in item["tool_calls"] if call["id"] in completed]
                if calls:
                    retained.append({**item, "tool_calls": calls})
            else:
                retained.append(item)
        return list(previous_history) + retained + [{"role": "assistant", "content": "上一轮未完成。已返回的操作结果保留；不能自动重做，结果不明的操作应先核查。"}]

    async def _chat_round(self, messages):
        """一次请求的完整回合：response.created → 句级 TTS 音频 → response.done。"""
        self._response_sequence += 1
        response_id = f"chat-{self.session.session_id[:8]}-{self._response_sequence}"
        user_turn_id = self._active_user_turn_id
        self._active_response_id = response_id
        self._cancelled.discard(response_id)
        splitter = _SentenceSplitter()
        text_queue = asyncio.Queue()
        self._tts_text = text_queue
        defer_selection = getattr(self.session, "needs_music_choice", lambda: False)()
        tts_task = asyncio.create_task(asyncio.sleep(0) if defer_selection else self._tts_turn(response_id, text_queue))
        self._tts_task = tts_task
        content_parts = []
        reasoning = ""
        tool_calls = {}
        usage = None
        created = True
        last_progress = 0.0
        self._emit({"type": "response.created", "user_turn_id": user_turn_id, "response": {"id": response_id}})
        try:
            try:
                async for chunk in self._request(messages):
                    if response_id in self._cancelled or self._closed:
                        break
                    now = asyncio.get_running_loop().time()
                    if now - last_progress >= 1:
                        self._emit({"type": "response.progress", "response_id": response_id, "user_turn_id": user_turn_id})
                        last_progress = now
                    if chunk.usage:
                        usage = chunk.usage
                    if chunk.tool_delta:
                        self._accumulate_tool_calls(tool_calls, chunk.tool_delta)
                        continue
                    text = chunk.content or ""
                    reasoning += chunk.reasoning or ""
                    if not text:
                        continue
                    content_parts.append(text)
                    if sum(len(part) for part in content_parts) > MAX_REPLY_CHARS:
                        LOGGER.warning("reply_truncated session=%s", self.session.session_id)
                        break
                    if not defer_selection:
                        for piece in splitter.push(text):
                            text_queue.put_nowait(piece)
            finally:
                if created and splitter.buffer and not defer_selection:
                    text_queue.put_nowait(splitter.buffer)
                    splitter.buffer = ""
                text_queue.put_nowait(None)
            tool_calls = self._validate_tool_calls(tool_calls, response_id)
            if response_id in self._cancelled or not created:
                message = {"role": "assistant", "content": "".join(content_parts) or None}
                if tool_calls:
                    message["tool_calls"] = self._tool_call_list(tool_calls)
                    if reasoning:
                        message["reasoning_content"] = reasoning
                return message, tool_calls
            await tts_task
            normalized_usage = {}
            if usage:
                normalized_usage = {"input_tokens": usage.get("prompt_tokens", 0), "output_tokens": usage.get("completion_tokens", 0), "total_tokens": usage.get("total_tokens", 0)}
            assistant_text = "".join(content_parts)
            if assistant_text and not defer_selection:
                self._emit({"type": "response.audio_transcript.done", "response_id": response_id, "transcript": assistant_text[:4000]})
            self._emit({"type": "response.done", "user_turn_id": user_turn_id, "response": {"id": response_id, "status": "completed", "usage": normalized_usage, "handoff_final": not bool(tool_calls) and not self._tool_chain_paused and not defer_selection}})
            message = {"role": "assistant", "content": assistant_text or None}
            if tool_calls:
                message["tool_calls"] = self._tool_call_list(tool_calls)
                if reasoning:
                    message["reasoning_content"] = reasoning
            return message, tool_calls
        finally:
            # 请求失败和取消也必须等本轮TTS收尾，下一轮不能继承旧任务或队列。
            if not tts_task.done() and not tts_task.cancelling():
                tts_task.cancel()
            await asyncio.gather(tts_task, return_exceptions=True)
            if self._tts_task is tts_task:
                self._tts_task = None

    @staticmethod
    def _tool_call_list(tool_calls):
        return [{"id": call["id"], "type": "function", "function": {"name": call["name"], "arguments": call["arguments"]}} for _, call in sorted(tool_calls.items())]

    def _validate_tool_calls(self, tool_calls, response_id):
        unique = {}
        fingerprints = {}
        for index, call in sorted(tool_calls.items()):
            identity, name = call.get("id"), call.get("name")
            if not isinstance(identity, str) or not 1 <= len(identity) <= 256 or not isinstance(name, str) or not 1 <= len(name) <= 128:
                raise LocalToolArgumentsError()
            try:
                arguments = json.loads(call["arguments"])
                if not isinstance(arguments, dict):
                    raise ValueError("工具参数必须是对象")
                canonical = json.dumps(arguments, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)
            except (ValueError, TypeError) as error:
                raise LocalToolArgumentsError() from error
            fingerprint = (name, canonical)
            if identity in fingerprints:
                if fingerprints[identity] != fingerprint:
                    raise LocalToolArgumentsError()
                continue
            fingerprints[identity] = fingerprint
            unique[index] = {**call, "arguments": canonical, "response_id": response_id}
        if self._tool_call_count + len(unique) > MAX_SESSION_TOOL_CALLS:
            raise LocalToolArgumentsError()
        self._tool_call_count += len(unique)
        return unique

    @staticmethod
    def _accumulate_tool_calls(tool_calls, deltas):
        for item in deltas:
            if not isinstance(item, dict):
                raise LocalToolArgumentsError()
            index = item.get("index", 0)
            if type(index) is not int or not 0 <= index < MAX_RESPONSE_TOOLS:
                raise LocalToolArgumentsError()
            if index not in tool_calls and len(tool_calls) >= MAX_RESPONSE_TOOLS:
                raise LocalToolArgumentsError()
            slot = tool_calls.setdefault(index, {"id": "", "name": "", "arguments": ""})
            if item.get("id"):
                if not isinstance(item["id"], str) or len(item["id"]) > 256 or slot["id"] and slot["id"] != item["id"]:
                    raise LocalToolArgumentsError()
                slot["id"] = item["id"]
            function = item.get("function") or {}
            if not isinstance(function, dict):
                raise LocalToolArgumentsError()
            if function.get("name"):
                if not isinstance(function["name"], str) or len(function["name"]) > 128 or slot["name"] and slot["name"] != function["name"]:
                    raise LocalToolArgumentsError()
                slot["name"] = function["name"]
            if function.get("arguments"):
                if not isinstance(function["arguments"], str) or len((slot["arguments"] + function["arguments"]).encode("utf-8")) > MAX_TOOL_ARGUMENT_BYTES:
                    raise LocalToolArgumentsError()
                slot["arguments"] += function["arguments"]

    async def _execute_tool(self, call):
        name = call.get("name", "")
        try:
            arguments = json.loads(call.get("arguments", "{}"))
            if not isinstance(arguments, dict):
                raise ValueError("工具参数必须是对象")
        except (ValueError, TypeError):
            LOGGER.exception("tool_arguments_invalid session=%s tool=%s", self.session.session_id, name)
            return tool_error_result(LocalToolArgumentsError())
        if name in {"create_task", "create_project_tasks", "subscribe", "subscription_action", "create_routine", "routine_action", "start_desktop_timer"}:
            response_id, call_id = call.get("response_id"), call.get("id")
            if not isinstance(response_id, str) or not response_id or not isinstance(call_id, str) or not call_id:
                return tool_error_result(LocalToolArgumentsError())
            identity = json.dumps([self.session.session_id, response_id, call_id], ensure_ascii=False, separators=(",", ":"))
            key = "chat-tool-" + hashlib.sha256(identity.encode("utf-8")).hexdigest()
            arguments["request_id" if name == "start_desktop_timer" else "idempotency_key"] = key
        try:
            await self.session.state("executing", name)
            self.session.executing_tools += 1
            try:
                return await self.session.execute_tool(name, arguments)
            finally:
                self.session.executing_tools -= 1
        except MusicAssistantError as error:
            LOGGER.exception("music_tool_unavailable session=%s tool=%s", self.session.session_id, name)
            return {"ok": False, "status": "unavailable", "error": str(error)}
        except Exception as error:
            LOGGER.exception("tool_failed session=%s tool=%s", self.session.session_id, name)
            return tool_error_result(error)

    # ---- chat completions 请求 ----

    def _chat_headers(self):
        return {"Authorization": "Bearer " + self._chat_key, "Content-Type": "application/json"}

    def _chat_payload(self, messages, stream):
        payload = {"model": self._chat_model, "messages": messages, "tool_choice": "none" if self._tool_chain_paused else "auto", "max_tokens": self.config["chat_max_tokens"], "stream": stream}
        if not self._tool_chain_paused:
            payload["tools"] = self._tools
        if self._forced_music_tools is not None:
            payload["tools"] = self._forced_music_tools
            payload["tool_choice"] = "required"
        if stream:
            payload["stream_options"] = {"include_usage": True}
        if self.name == "qwen_chat" and self.config["qwen_chat_thinking"] is not None:
            # qwen3 混合推理模型默认思考；语音链路要低首字延迟，默认关闭。
            payload["enable_thinking"] = bool(self.config["qwen_chat_thinking"])
        if self.name == "deepseek" and self.config["deepseek_thinking"] != "unset":
            payload["thinking"] = {"type": self.config["deepseek_thinking"]}
        return payload

    async def _request(self, messages):
        if self._streaming:
            try:
                async for chunk in self._stream_request(messages):
                    yield chunk
                return
            except _ChatHTTPError as error:
                # 兼容接口对流式+工具的支持有反复：HTTP 层失败即降级非流式重试。
                LOGGER.warning("stream_request_failed provider=%s error=%s", self.name, str(error)[:200])
                self._streaming = False
        chunk = await self._blocking_request(messages)
        yield chunk

    async def _stream_request(self, messages):
        async with self.client.post(self._chat_url, json=self._chat_payload(messages, stream=True), headers=self._chat_headers(), timeout=REQUEST_TIMEOUT, read_bufsize=STREAM_READ_BUFSIZE) as response:
            if response.status != 200:
                body = (await response.text())[:300]
                raise _ChatHTTPError(response.status, body)
            while True:
                line = await response.content.readline()
                if not line:
                    return
                line = line.strip()
                if not line or line.startswith(b":") or not line.startswith(b"data:"):
                    continue
                data = line[5:].strip()
                if data == b"[DONE]":
                    return
                payload = json.loads(data)
                choices = payload.get("choices") or []
                delta = (choices[0].get("delta") or {}) if choices else {}
                yield _Chunk(content=delta.get("content") or "", reasoning=delta.get("reasoning_content") or "", tool_delta=delta.get("tool_calls"), usage=payload.get("usage"))

    async def _blocking_request(self, messages):
        async with asyncio.timeout(BLOCKING_TOTAL_TIMEOUT):
            async with self.client.post(self._chat_url, json=self._chat_payload(messages, stream=False), headers=self._chat_headers(), timeout=REQUEST_TIMEOUT) as response:
                if response.status != 200:
                    body = (await response.text())[:300]
                    raise _ChatHTTPError(response.status, body)
                data = await response.json(content_type=None)
        choice = (data.get("choices") or [{}])[0]
        message = choice.get("message") or {}
        tool_calls = message.get("tool_calls")
        if tool_calls is not None:
            if not isinstance(tool_calls, list) or len(tool_calls) > MAX_RESPONSE_TOOLS or any(not isinstance(call, dict) for call in tool_calls):
                raise LocalToolArgumentsError()
            tool_calls = [{**call, "index": index} for index, call in enumerate(tool_calls)]
        return _Chunk(content=message.get("content") or "", reasoning=message.get("reasoning_content") or "", tool_delta=tool_calls, usage=data.get("usage"))

    # ---- TTS ----

    def on_tts_audio(self, response_id, pcm):
        if self._closed or response_id in self._cancelled:
            return
        self._emit({"type": "response.audio.delta", "response_id": response_id, "delta": base64.b64encode(pcm).decode("ascii")})

    async def _tts_turn(self, response_id, text_queue=None):
        text_queue = self._tts_text if text_queue is None else text_queue
        # 握手与LLM流式并行预热：首句到达时连接通常已就绪，首音不再串行等待建连。
        task_id = str(uuid.uuid4())
        open_task = asyncio.create_task(self._open_tts(task_id))
        ws = None
        feeder = None
        reader = None
        try:
            first_text = await text_queue.get()
            if first_text is None or self._closed or response_id in self._cancelled:
                return
            ws = await open_task
            feeder = asyncio.create_task(self._tts_feed(ws, task_id, first_text, text_queue))
            reader = asyncio.create_task(self._tts_read(ws, response_id))
            done, _ = await asyncio.wait((feeder, reader), return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                await task
            if reader in done and not feeder.done():
                raise RuntimeError("TTS在文本发送完成前结束")
            await feeder
            # 只限制文本发完后的合成收尾，不把等待LLM下一句计入网络超时。
            async with asyncio.timeout(TTS_FINISH_TIMEOUT):
                await reader
        finally:
            # 没等到文本就退出时，预热连接可能已经建好，必须一并关闭。
            if not open_task.done() and not open_task.cancelling():
                open_task.cancel()
            opened = await asyncio.gather(open_task, return_exceptions=True)
            orphan = opened[0] if ws is None and hasattr(opened[0], "close") else None
            if orphan is not None and not orphan.closed:
                try:
                    await orphan.close()
                except Exception:
                    LOGGER.debug("tts_ws_close_ignored", exc_info=True)
            tasks = [task for task in (feeder, reader) if task is not None]
            for task in tasks:
                if not task.done() and not task.cancelling():
                    task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            if ws is not None:
                try:
                    await ws.close()
                except Exception:
                    LOGGER.debug("tts_ws_close_ignored", exc_info=True)

    async def _open_tts(self, task_id):
        url = dashscope_ws_url(self.config)
        ws = None
        try:
            async with asyncio.timeout(TTS_HANDSHAKE_TIMEOUT):
                ws = await self.client.ws_connect(url, headers=self.dashscope_headers(), heartbeat=15, max_msg_size=4 * 1024 * 1024, timeout=aiohttp.ClientWSTimeout(ws_close=10))
                await ws.send_json({"header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"task_group": "audio", "task": "tts", "function": "SpeechSynthesizer", "model": self.config["chat_tts_model"], "parameters": {"text_type": "PlainText", "voice": self.config["chat_tts_voice"], "format": "pcm", "sample_rate": 24000, "volume": 50, "rate": 1.0, "pitch": 1.0}, "input": {}}})
                async for message in ws:
                    if message.type != aiohttp.WSMsgType.TEXT:
                        continue
                    header = json.loads(message.data).get("header", {})
                    name = header.get("event")
                    if name == "task-started":
                        return ws
                    if name == "task-failed":
                        raise RuntimeError("TTS任务被拒绝：" + str(header.get("error_code", "")) + " " + str(header.get("error_message", ""))[:120])
            raise RuntimeError("TTS连接在握手完成前结束")
        except BaseException:
            # 交给调用方前，本方法持有socket；拒绝、超时和取消都要在此关闭。
            if ws is not None:
                try:
                    await ws.close()
                except Exception:
                    LOGGER.debug("tts_handshake_close_ignored", exc_info=True)
            raise

    async def _tts_feed(self, ws, task_id, first_text, text_queue=None):
        text_queue = self._tts_text if text_queue is None else text_queue
        # 首句在 _tts_turn 取出用于判断，这里必须先补发，否则首句丢失。
        async with asyncio.timeout(TTS_SEND_TIMEOUT):
            await ws.send_json({"header": {"action": "continue-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"input": {"text": first_text}}})
        while True:
            text = await text_queue.get()
            if text is None:
                break
            async with asyncio.timeout(TTS_SEND_TIMEOUT):
                await ws.send_json({"header": {"action": "continue-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"input": {"text": text}}})
        async with asyncio.timeout(TTS_SEND_TIMEOUT):
            await ws.send_json({"header": {"action": "finish-task", "task_id": task_id, "streaming": "duplex"}, "payload": {"input": {}}})

    async def _tts_read(self, ws, response_id):
        async for message in ws:
            if message.type == aiohttp.WSMsgType.BINARY:
                self.on_tts_audio(response_id, message.data)
            elif message.type == aiohttp.WSMsgType.TEXT:
                header = json.loads(message.data).get("header", {})
                name = header.get("event")
                if name == "task-finished":
                    return
                if name == "task-failed":
                    raise RuntimeError("TTS合成失败：" + str(header.get("error_code", "")) + " " + str(header.get("error_message", ""))[:120])
            elif message.type == aiohttp.WSMsgType.ERROR:
                raise RuntimeError("TTS连接错误")
        raise RuntimeError("TTS连接在合成完成前结束")

    # ---- 事件与杂项 ----

    def _emit(self, event):
        if self._closed or self._event_overflowed:
            return
        try:
            self._events.put_nowait(event)
        except asyncio.QueueFull:
            self._event_overflowed = True
            while not self._events.empty():
                self._events.get_nowait()
            self._events.put_nowait({"type": "error", "error": {"code": "chat_event_overflow"}})
            raise RuntimeError("对话输出积压超过上限，本轮已停止，未丢帧后继续执行")


def provider_key(config, name):
    """chat 供应商建连前的凭据齐套检查；ASR/TTS 恒走阿里，故都要求阿里凭据。"""
    aliyun_ok = all(isinstance(config.get(key), str) and config.get(key) and "REPLACE" not in config.get(key) for key in ("aliyun_api_key", "aliyun_workspace_id"))
    if name == "deepseek":
        return aliyun_ok and bool(config.get("deepseek_api_key"))
    return aliyun_ok
