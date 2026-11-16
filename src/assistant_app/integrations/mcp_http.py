"""有界 Streamable HTTP MCP 适配；不是完整 MCP SDK，不实现交互式 OAuth。"""

from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import re
import secrets
from contextlib import asynccontextmanager

import aiohttp

from .contracts import ConnectorAction, ConnectorManifest, PermissionScope
from .action_schemas import validate_schema

LOGGER = logging.getLogger("assistant.mcp")
VERSIONS = {"2025-06-18", "2025-03-26"}
MAX_BYTES = 262144
MAX_TOOLS = 64
MAX_PAGES = 8


class MCPError(RuntimeError):
    def __init__(self, message, status=None):
        super().__init__(message)
        self.status = status


def fingerprint(tool):
    value = {key: tool.get(key) for key in ("name", "description", "inputSchema")}
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def action_name(name):
    return "tool_" + hashlib.sha256(name.encode()).hexdigest()[:24]


def connection_fingerprint(config):
    return hashlib.sha256(json.dumps([config.get("mcp_url", ""), config.get("mcp_token", "")]).encode()).hexdigest()


class MCPSession:
    def __init__(self, http, url, token):
        self.http, self.url, self.token = http, url, token
        self.session_id = None
        self.version = None
        self.request_id = 0

    def headers(self):
        result = {"Accept": "application/json, text/event-stream", "Content-Type": "application/json"}
        if self.token:
            result["Authorization"] = f"Bearer {self.token}"
        if self.session_id:
            result["Mcp-Session-Id"] = self.session_id
        if self.version:
            result["MCP-Protocol-Version"] = self.version
        return result

    async def rpc(self, method, params=None, *, notification=False):
        self.request_id += 1
        request_id = self.request_id
        payload = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        if not notification:
            payload["id"] = request_id
        async with self.http.post(self.url, headers=self.headers(), json=payload, timeout=aiohttp.ClientTimeout(total=25, sock_connect=8), allow_redirects=False) as response:
            if response.status >= 300:
                message = "服务要求授权或权限不足；交互式 OAuth 暂不支持，请配置服务端令牌" if response.status in {401, 403} else "服务会话已失效，请重新检查；不会自动重放动作" if response.status == 404 else "外部工具服务拒绝请求，请检查固定地址和服务配置"
                raise MCPError(message, response.status)
            if method == "initialize":
                session_id = response.headers.get("Mcp-Session-Id")
                if session_id and (len(session_id) > 1024 or not re.fullmatch(r"[\x21-\x7e]+", session_id)):
                    raise MCPError("服务返回的会话标识无效")
                self.session_id = session_id
            if notification:
                if response.status not in {200, 202, 204}:
                    raise MCPError("服务未接受初始化通知")
                return None
            raw = bytearray()
            sse = response.content_type == "text/event-stream"
            async for chunk in response.content.iter_chunked(8192):
                raw.extend(chunk)
                if len(raw) > MAX_BYTES:
                    raise MCPError("外部工具响应超过256 KiB")
                if sse:
                    normalized = bytes(raw).replace(b"\r\n", b"\n")
                    frames = normalized.split(b"\n\n")
                    for frame in frames[:-1]:
                        lines = [line[5:].lstrip() for line in frame.split(b"\n") if line.startswith(b"data:")]
                        if lines:
                            data = self._decode(b"\n".join(lines))
                            if data.get("id") == request_id and "method" not in data and ("result" in data or "error" in data):
                                return self._result(data, request_id)
                    # 保留所有字节以执行总响应边界；不跟随重连或重放事件。
            if sse:
                raise MCPError("服务事件流未返回本次请求结果")
            if response.content_type != "application/json":
                raise MCPError("外部工具响应必须是 JSON 或事件流")
            return self._result(self._decode(raw), request_id)

    @staticmethod
    def _decode(raw):
        try:
            data = json.loads(raw)
        except (ValueError, UnicodeDecodeError) as error:
            raise MCPError("外部工具返回了无效 JSON") from error
        if not isinstance(data, dict) or data.get("jsonrpc") != "2.0":
            raise MCPError("外部工具返回了无效协议消息")
        return data

    @staticmethod
    def _result(data, request_id):
        if data.get("id") != request_id or "method" in data:
            raise MCPError("外部工具响应与请求不匹配")
        if "error" in data:
            # 服务端消息可能包含令牌/内部地址，不直接转发。
            raise MCPError("外部工具返回执行错误，请在服务端核查；不会自动重试")
        result = data.get("result")
        if not isinstance(result, dict):
            raise MCPError("外部工具结果格式无效")
        return result

    async def initialize(self):
        result = await self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "xiaoyanzi", "version": "1.0"}})
        self.version = result.get("protocolVersion")
        if not isinstance(self.version, str) or self.version not in VERSIONS:
            raise MCPError("服务协商的协议版本暂不支持")
        if not isinstance(result.get("capabilities"), dict) or not isinstance(result["capabilities"].get("tools"), dict):
            raise MCPError("该服务未声明工具能力")
        await self.rpc("notifications/initialized", notification=True)

    async def tools(self):
        items, cursor, seen = [], None, set()
        for _ in range(MAX_PAGES):
            result = await self.rpc("tools/list", {"cursor": cursor} if cursor else {})
            page = result.get("tools")
            if not isinstance(page, list) or len(items) + len(page) > MAX_TOOLS:
                raise MCPError("工具列表无效或超过64项；请在服务端缩小范围")
            for tool in page:
                if not isinstance(tool, dict) or not isinstance(tool.get("name"), str) or not 1 <= len(tool["name"]) <= 128 or tool["name"] in {item["name"] for item in items}:
                    raise MCPError("工具名称无效或重复")
                schema = tool.get("inputSchema")
                if not isinstance(schema, dict) or schema.get("type") != "object" or len(json.dumps(schema).encode()) > 32768:
                    raise MCPError("工具参数定义无效或超过32 KiB")
                try:
                    validate_schema(schema)
                except ValueError as error:
                    raise MCPError(f"工具参数定义暂不支持：{error}") from error
                if not isinstance(tool.get("description", ""), str) or len(tool.get("description", "")) > 4000:
                    raise MCPError("工具说明无效或过长")
                items.append(tool)
            cursor = result.get("nextCursor")
            if cursor is None:
                return items
            if not isinstance(cursor, str) or not cursor or len(cursor) > 4096 or cursor in seen:
                raise MCPError("工具列表分页游标无效或重复")
            seen.add(cursor)
        raise MCPError("工具列表分页超过8页；未授权任何截断列表")

    async def close(self):
        if self.session_id:
            try:
                async with self.http.delete(self.url, headers=self.headers(), timeout=aiohttp.ClientTimeout(total=5), allow_redirects=False) as response:
                    if response.status not in {200, 202, 204, 404, 405}:
                        LOGGER.warning("mcp_session_cleanup_rejected status=%s", response.status)
            except (aiohttp.ClientError, asyncio.TimeoutError):
                LOGGER.exception("mcp_session_cleanup_failed")


class MCPConnector:
    def __init__(self, http, config):
        self.http, self.config = http, config
        self.discovered = []
        self.discovery_revision = ""
        self._lock = asyncio.Lock()
        self.manifest = ConnectorManifest("mcp", "外部工具服务", "1.0")

    @asynccontextmanager
    async def session(self):
        url = self.config.get("mcp_url", "")
        if not url:
            raise MCPError("请先配置外部工具服务地址")
        # 启动配置与控制台保存走相同 URL 限制，模型永远不能指定地址或请求头。
        from ..infrastructure.config_store import validate_connector_values
        validate_connector_values("mcp", {"mcp_url": url}, self.config)
        session = MCPSession(self.http, url, self.config.get("mcp_token", ""))
        try:
            await session.initialize()
            yield session
        finally:
            await session.close()

    def apply_approvals(self):
        approved = self.config.get("mcp_approved_tools") or {}
        actions = []
        for tool in self.discovered:
            grant = approved.get(tool["name"], {})
            if grant.get("connection") != connection_fingerprint(self.config) or grant.get("fingerprint") != fingerprint(tool) or grant.get("mode") not in {"read", "confirm"}:
                continue
            read = grant["mode"] == "read"
            actions.append(ConnectorAction(action_name(tool["name"]), f"{tool['name']}：{tool.get('description', '')}", frozenset({PermissionScope.READ if read else PermissionScope.WRITE}), not read, tool["inputSchema"]))
        self.manifest = ConnectorManifest("mcp", "外部工具服务", "1.0", tuple(actions))

    async def check_connection(self):
        async with self._lock:
            async with self.session() as session:
                discovered = await session.tools()
            self.discovered = discovered
            self.discovery_revision = secrets.token_urlsafe(18)
            self.apply_approvals()

    def public_tools(self):
        approved = self.config.get("mcp_approved_tools") or {}
        return [{"name": tool["name"], "description": tool.get("description", ""), "action": action_name(tool["name"]), "input_schema": tool["inputSchema"], "mode": approved.get(tool["name"], {}).get("mode", "blocked") if approved.get(tool["name"], {}).get("fingerprint") == fingerprint(tool) and approved.get(tool["name"], {}).get("connection") == connection_fingerprint(self.config) else "blocked"} for tool in self.discovered]

    async def invoke(self, action, arguments, context):
        async with self._lock:
            tool = next((item for item in self.discovered if action_name(item["name"]) == action), None)
            grant = (self.config.get("mcp_approved_tools") or {}).get(tool["name"], {}) if tool else {}
            if not tool or grant.get("connection") != connection_fingerprint(self.config) or grant.get("fingerprint") != fingerprint(tool) or grant.get("mode") not in {"read", "confirm"}:
                raise PermissionError("该外部能力未被管理员允许，请重新检查并逐项批准")
            if grant["mode"] != "read" and context.metadata.get("confirmation_verified") is not True:
                raise PermissionError("该外部能力需要控制台预览确认")
            async with self.session() as session:
                current = next((item for item in await session.tools() if item["name"] == tool["name"]), None)
                if current is None or fingerprint(current) != fingerprint(tool):
                    raise PermissionError("外部能力定义已改变，请重新检查和批准；本次没有执行")
                result = await session.rpc("tools/call", {"name": tool["name"], "arguments": arguments})
                if result.get("isError") is True:
                    raise MCPError("外部工具报告执行失败，请到提供商核查结果；不会自动重试")
                return {"provider": "mcp", "untrusted_external_content": True, "result": result}
