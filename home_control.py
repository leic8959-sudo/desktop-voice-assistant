"""Home Assistant discovery and capability boundary."""
import asyncio
import json
import logging
import math
import os
import re
import tempfile
import time
from pathlib import Path
from functools import wraps
from urllib.parse import quote, urlsplit
import aiohttp

LOGGER = logging.getLogger("assistant.home")
DISCOVERY_TTL = 20
MAX_ENTITIES = 500
MAX_DEVICES = 100
VACUUM_FEATURES = {"start": (8192, ("cleaning",), "清扫"), "pause": (4, ("paused",), "暂停"), "return_to_base": (16, ("returning", "docked"), "回充")}
SAFE_DOMAINS = {"light", "switch", "fan", "climate", "vacuum"}
VACUUM_QUERY_TERMS = ("扫地机器人", "扫拖机器人", "扫托机器人", "扫地机", "扫拖机", "扫托机", "扫地", "扫拖", "扫托", "清扫机器人", "清洁机器人", "拖地机器人", "拖地", "吸尘机器人", "吸尘器", "机器人")
VACUUM_QUERY_ENGLISH_TERMS = ("vacuum", "robotvacuum", "robot")


def _normalize_home_query(value):
    return re.sub(r"[\W_]+", "", str(value).casefold(), flags=re.UNICODE)


def _split_vacuum_query(value):
    normalized = _normalize_home_query(value)
    for term in sorted(VACUUM_QUERY_TERMS + VACUUM_QUERY_ENGLISH_TERMS, key=len, reverse=True):
        if term in normalized:
            return normalized.replace(term, "", 1), True
    return normalized, False


def valid_number(value):
    return type(value) in (int, float) and math.isfinite(value)


def configuration_guard(operation):
    @wraps(operation)
    async def guarded(self, *args, **kwargs):
        async with self.configuration_lock:
            return await operation(self, *args, **kwargs)
    return guarded

class HomeController:
    def __init__(self, config, client):
        self.client = client
        self.base = config.get("home_assistant_url", "").rstrip("/")
        self.token = config.get("home_assistant_token", "")
        self.entities = dict(config.get("home_entities", {}))
        self.runtime_dir = Path(config.get("runtime_dir", Path(__file__).resolve().parent / "runtime"))
        self.authorization_path = self.runtime_dir / "home-authorizations.json"
        if self.base:
            parsed = urlsplit(self.base)
            if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password:
                raise ValueError("Home Assistant 地址需为不含凭据的 HTTP(S) 地址")
        if any(not isinstance(alias, str) or not isinstance(entity, str) or "." not in entity for alias, entity in self.entities.items()):
            raise ValueError("家居配置需为别名到实体ID的映射")
        self.locks = {}
        self.configuration_lock = asyncio.Lock()
        self._catalog = None
        self._catalog_at = 0
        self._service_map = {}
        self._seed_entities = {}
        self._authorizations = self._load_authorizations()

    @property
    def configured(self):
        return bool(self.base and self.token)

    @staticmethod
    def validate_configuration(config):
        base = str(config.get("home_assistant_url", "")).rstrip("/")
        token = config.get("home_assistant_token", "")
        entities = config.get("home_entities", {})
        if bool(base) != bool(token):
            raise ValueError("Home Assistant 地址和令牌必须同时配置或同时清除")
        if base:
            parsed = urlsplit(base)
            if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password:
                raise ValueError("Home Assistant 地址需为不含凭据的 HTTP(S) 地址")
        if not isinstance(entities, dict) or len(entities) > 500 or any(not isinstance(alias, str) or not isinstance(entity, str) or "." not in entity for alias, entity in entities.items()):
            raise ValueError("家居配置需为别名到实体ID的映射")

    def reconfigure(self, config):
        self.validate_configuration(config)
        self.base = str(config.get("home_assistant_url", "")).rstrip("/")
        self.token = config.get("home_assistant_token", "")
        self.entities = dict(config.get("home_entities", {}))
        self._catalog = None
        self._catalog_at = 0
        self._service_map = {}

    def _load_authorizations(self):
        try:
            if self.authorization_path.stat().st_size > 65536:
                raise ValueError("家居授权文件超出限制")
            value = json.loads(self.authorization_path.read_text(encoding="utf-8"))
            if not isinstance(value, dict) or type(value.get("version")) is not int or value["version"] != 1 or "devices" not in value or "seeds" not in value:
                raise ValueError("家居授权文件类型、版本或必要字段无效")
            devices = value["devices"]
            seeds = value["seeds"]
            if not isinstance(devices, dict) or len(devices) > MAX_DEVICES:
                raise ValueError("家居授权文件格式或数量无效")
            if not isinstance(seeds, dict) or len(seeds) > MAX_DEVICES:
                raise ValueError("家居迁移种子格式或数量无效")
            if any(not isinstance(key, str) or not isinstance(entity, str) for key, entity in seeds.items()):
                raise ValueError("家居迁移种子格式无效")
            self._seed_entities = dict(seeds)
            result = {}
            for key, item in devices.items():
                if not isinstance(key, str) or not isinstance(item, dict) or type(item.get("enabled")) is not bool:
                    raise ValueError("家居授权文件格式无效")
                result[key] = item["enabled"]
            return result
        except FileNotFoundError:
            return {}
        except (OSError, ValueError, TypeError) as error:
            LOGGER.exception("home_authorization_load_failed path=%s", self.authorization_path)
            raise ValueError("家居授权文件不可用，已阻止自动授权") from error

    def _save_authorizations(self):
        self.runtime_dir.mkdir(parents=True, exist_ok=True)
        fd, path = tempfile.mkstemp(prefix="home-authorizations.", suffix=".tmp", dir=self.runtime_dir)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                json.dump({"version": 1, "updated_at": time.time(), "seeds": self._seed_entities, "devices": {key: {"enabled": value} for key, value in self._authorizations.items()}}, stream, ensure_ascii=False, separators=(",", ":"))
                stream.flush(); os.fsync(stream.fileno())
            os.replace(path, self.authorization_path)
        finally:
            if os.path.exists(path): os.unlink(path)

    def _headers(self):
        return {"Authorization": "Bearer " + self.token}

    async def _get_json(self, path, timeout=8, limit=2_000_000):
        async with self.client.get(self.base + path, headers=self._headers(), timeout=aiohttp.ClientTimeout(total=timeout)) as response:
            response.raise_for_status()
            if response.content_length and response.content_length > limit: raise ValueError("家居平台响应超出限制")
            chunks = []
            total = 0
            async for chunk in response.content.iter_chunked(65536):
                total += len(chunk)
                if total > limit:
                    raise ValueError("家居平台响应超出限制")
                chunks.append(chunk)
            data = b"".join(chunks)
        return json.loads(data)

    async def _registry(self, request_type):
        websocket_url = self.base.replace("https://", "wss://", 1).replace("http://", "ws://", 1) + "/api/websocket"
        async with self.client.ws_connect(websocket_url, timeout=8, max_msg_size=1_000_000) as socket:
            async with asyncio.timeout(8):
                if (await socket.receive_json()).get("type") != "auth_required": raise ValueError("Home Assistant WebSocket 未进入认证阶段")
                await socket.send_json({"type": "auth", "access_token": self.token})
                if (await socket.receive_json()).get("type") != "auth_ok": raise ValueError("Home Assistant WebSocket 认证失败")
                await socket.send_json({"id": 1, "type": request_type})
                message = await socket.receive_json()
                if message.get("id") != 1 or not message.get("success"): raise ValueError("Home Assistant 注册表请求失败")
                return message.get("result") or []

    async def _registry_maps(self):
        try:
            devices, entities, areas = await asyncio.gather(*(self._registry(item) for item in ("config/device_registry/list", "config/entity_registry/list", "config/area_registry/list")))
            return ({item["id"]: item for item in devices if isinstance(item, dict) and isinstance(item.get("id"), str)}, {item["entity_id"]: item for item in entities if isinstance(item, dict) and isinstance(item.get("entity_id"), str)}, {item["area_id"]: item for item in areas if isinstance(item, dict) and isinstance(item.get("area_id"), str)}, None)
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError):
            LOGGER.exception("home_registry_discovery_failed")
            return {}, {}, {}, "registry_unavailable"

    @staticmethod
    def _action(action_id, label, properties, required=()):
        return {"id": action_id, "label": label, "parameters": {"type": "object", "properties": properties, "required": list(required), "additionalProperties": False}, "supported": True, "permission": "authorized", "confirmation": "platform_reported_state; physical_completion_not_confirmed"}

    def _actions_for(self, entity_id, payload, service_map=None, display_name=None):
        domain = entity_id.split(".", 1)[0]; attrs = payload.get("attributes", {}) if isinstance(payload, dict) else {}; attrs = attrs if isinstance(attrs, dict) else {}
        def available(service):
            return service_map is None or service in service_map.get(domain, set())
        if domain in ("light", "switch", "fan"):
            label_prefix = display_name or entity_id
            actions = [self._action("turn_on", f"{label_prefix} · 打开", {"state": {"type": "string", "title": "状态", "enum": ["on"], "default": "on"}}, ("state",))] if available("turn_on") else []
            if available("turn_off"): actions.append(self._action("turn_off", f"{label_prefix} · 关闭", {"state": {"type": "string", "title": "状态", "enum": ["off"], "default": "off"}}, ("state",)))
            return self._namespace_actions(entity_id, actions)
        if domain == "vacuum":
            features = attrs.get("supported_features", 0)
            label_prefix = display_name or "未命名功能"
            return self._namespace_actions(entity_id, [self._action(key, f"{label_prefix} · {label}", {}) for key, (feature, _, label) in VACUUM_FEATURES.items() if type(features) is int and features & feature and available(key)])
        if domain == "climate":
            actions = []; modes = [mode for mode in attrs.get("hvac_modes", []) if isinstance(mode, str)]
            if modes and available("set_hvac_mode"):
                mode_labels = {"off": "关闭", "cool": "制冷", "heat": "制热", "dry": "除湿", "fan_only": "送风", "auto": "自动", "heat_cool": "自动冷热"}
                actions.append(self._action("set_hvac_mode", f"{display_name or '未命名功能'} · 设置模式", {"hvac_mode": {"type": "string", "title": "模式", "enum": modes, "default": payload.get("state"), "enumLabels": {mode: mode_labels.get(mode, mode) for mode in modes}}}, ("hvac_mode",)))
            features = attrs.get("supported_features", 0)
            if type(features) is int:
                for service, feature, state in (("turn_on", 128, "on"), ("turn_off", 256, "off")):
                    if features & feature and available(service):
                        actions.append(self._action(service, "开机" if state == "on" else "关机", {}))
            if type(features) is int and features & 1 and available("set_temperature"):
                schema = {"type": "number"}
                for source, target in (("min_temp", "minimum"), ("max_temp", "maximum"), ("target_temp_step", "multipleOf")):
                    if valid_number(attrs.get(source)): schema[target] = attrs[source]
                if valid_number(attrs.get("temperature")): schema["default"] = attrs["temperature"]
                schema["unit"] = "°C"
                schema["title"] = "设定温度"
                actions.append(self._action("set_temperature", f"{display_name or '未命名功能'} · 设置温度", {"temperature": schema}, ("temperature",)))
            return self._namespace_actions(entity_id, actions)
        return []

    @staticmethod
    def _namespace_actions(entity_id, actions):
        return [{**action, "id": entity_id + ":" + action["id"], "entity_id": entity_id} for action in actions]

    @staticmethod
    def _matches_device_query(device, query):
        normalized_query = _normalize_home_query(query)
        if not normalized_query:
            return True
        entity_id = (device.get("state") or {}).get("entity_id", "")
        domain = entity_id.split(".", 1)[0]
        vacuum_query, has_vacuum_term = _split_vacuum_query(normalized_query)
        has_vacuum_entity = domain == "vacuum" or any((action.get("entity_id") or "").split(".", 1)[0] == "vacuum" for action in device.get("actions") or ())
        searchable = (device.get("name"), device.get("alias"), device.get("area"), device.get("device_id"), entity_id)
        # 品类同义词只扩大检索命中；可执行动作仍来自设备实际报告的 actions。
        if has_vacuum_term:
            if not has_vacuum_entity:
                return False
            if not vacuum_query:
                return True
            normalized_query = vacuum_query
        return any(normalized_query in _normalize_home_query(value) for value in searchable if value)

    async def _discover(self, force=False):
        if not self.configured: return {"devices": [], "next_cursor": None, "discovery_error": "not_configured"}
        if not force and self._catalog is not None and time.monotonic() - self._catalog_at < DISCOVERY_TTL: return self._catalog
        try:
            states = await self._get_json("/api/states")
            if not isinstance(states, list) or len(states) > MAX_ENTITIES: raise ValueError("Home Assistant 状态目录格式无效或超出限制")
            services = await self._get_json("/api/services")
            if not isinstance(services, list): raise ValueError("Home Assistant 服务目录格式无效")
            service_map = {item.get("domain"): set((item.get("services") or {}).keys()) for item in services if isinstance(item, dict) and isinstance(item.get("domain"), str)}
            self._service_map = service_map
            device_map, entity_map, area_map, registry_error = await self._registry_maps()
            if registry_error:
                raise ValueError("Home Assistant 注册表不可用，无法安全聚合物理设备")
            grouped = {}
            for state in states:
                entity_id = state.get("entity_id") if isinstance(state, dict) else None
                if not isinstance(entity_id, str): continue
                registry = entity_map.get(entity_id, {}); device_id = registry.get("device_id")
                if not device_id: continue
                if registry.get("entity_category") in ("config", "diagnostic"): continue
                grouped.setdefault(device_id, []).append((entity_id, state, registry))
            if len(grouped) > MAX_DEVICES: raise ValueError("发现的物理设备数量超出限制")
            devices = []
            for device_id, members in grouped.items():
                safe_members = [item for item in members if item[0].split(".", 1)[0] in SAFE_DOMAINS]
                if not safe_members: continue
                registry_device = device_map.get(device_id, {})
                configured_entity = next((value for value in self.entities.values() if any(value == member[0] for member in safe_members)), None)
                primary = next((item for item in safe_members if item[0] == configured_entity), None)
                if primary is None:
                    priority = {"vacuum": 0, "climate": 1, "fan": 2, "light": 3, "switch": 4}
                    primary = min(safe_members, key=lambda item: priority.get(item[0].split(".", 1)[0], 9))
                entity_id, state, registry = primary
                alias = next((key for key, value in self.entities.items() if any(value == member[0] for member in members)), None)
                # Existing config aliases are one-time migration seeds; later revocation wins.
                # [人工决策-2026-10-29 22:21:34] 配置别名只迁移一次；新发现设备默认关闭，页面授权后才可执行。
                if alias and alias not in self._seed_entities:
                    if device_id not in self._authorizations: self._authorizations[device_id] = True
                    self._seed_entities[alias] = self.entities[alias]
                area_id = registry.get("area_id") or registry_device.get("area_id"); area = area_map.get(area_id, {}) if area_id else {}
                device_name = registry_device.get("name_by_user") or registry_device.get("name") or registry.get("name") or alias or "设备"
                actions = []
                for member_entity, member_state, member_registry in safe_members:
                    raw_name = member_registry.get("original_name") or member_registry.get("name") or (member_state.get("attributes") or {}).get("friendly_name")
                    member_name = str(raw_name or "").strip()
                    if device_name and member_name.startswith(device_name): member_name = member_name[len(device_name):].strip(" -_·")
                    actions.extend(self._actions_for(member_entity, member_state, service_map, member_name or "未命名功能"))
                if not actions: continue
                for action in actions:
                    action["group"] = "primary" if action.get("entity_id") == entity_id else "secondary"
                    if action["group"] == "primary" and " · " in action.get("label", ""):
                        action["label"] = action["label"].split(" · ", 1)[1]
                actions.sort(key=lambda action: 0 if action["group"] == "primary" else 1)
                attributes = state.get("attributes", {}) if isinstance(state.get("attributes"), dict) else {}
                mode_labels = next((action["parameters"]["properties"]["hvac_mode"].get("enumLabels", {}) for action in actions if "hvac_mode" in action["parameters"].get("properties", {})), {})
                state_labels = {"cleaning": "清扫中", "paused": "已暂停", "returning": "回充中", "docked": "在充电座", "idle": "待机", "error": "故障", "unknown": "未知", "unavailable": "不可用"}
                raw_state = state.get("state", "unknown")
                display_fields = [{"label": "状态", "value": mode_labels.get(raw_state, state_labels.get(raw_state, raw_state)), "unit": ""}]
                for source, label, unit in (("current_temperature", "室温", "°C"), ("temperature", "设定温度", "°C"), ("battery_level", "电量", "%")):
                    if isinstance(attributes.get(source), (int, float)):
                        display_fields.append({"label": label, "value": attributes[source], "unit": unit})
                if not self._authorizations.get(device_id, False):
                    actions = [{**action, "permission": "not_authorized"} for action in actions]
                devices.append({"device_id": device_id, "name": device_name, "alias": alias, "area": area.get("name") or area_id, "enabled": bool(self._authorizations.get(device_id, False)), "available": any(member[1].get("state") not in ("unknown", "unavailable") for member in members), "entity_ids": [member[0] for member in members], "state": {"entity_id": entity_id, "value": state.get("state", "unknown"), "attributes": attributes}, "display_fields": display_fields, "states": [{"entity_id": member[0], "value": member[1].get("state", "unknown"), "attributes": member[1].get("attributes", {}) if isinstance(member[1].get("attributes"), dict) else {}} for member in members[:50]], "actions": actions})
            catalog = {"devices": devices, "next_cursor": None, "refreshed_at": time.time()}
            self._catalog = catalog; self._catalog_at = time.monotonic()
            if any(alias for alias in self.entities): self._save_authorizations()
            return catalog
        except Exception:
            LOGGER.exception("home_discovery_failed")
            if self._catalog is not None:
                self._catalog = {**self._catalog, "discovery_error": "refresh_failed", "stale": True}
                return self._catalog
            raise

    @configuration_guard
    async def check_connection(self):
        if not self.configured:
            raise ValueError("请先保存 Home Assistant 地址和 Access Token")
        data = await self._get_json("/api/", limit=65536)
        if not isinstance(data, dict) or data.get("message") != "API running.":
            raise ValueError("Home Assistant 未返回预期的 API 状态")
        return {"verified": True}

    @configuration_guard
    async def list_home_devices(self, enabled_only=True, force=False, query="", cursor="", limit=30, refresh=None, include_disabled=None):
        if not isinstance(limit, int) or not 1 <= limit <= 30: raise ValueError("limit 需为1～30")
        if refresh is not None: force = bool(refresh)
        if include_disabled is not None: enabled_only = not bool(include_disabled)
        catalog = await self._discover(force=force)
        devices = [device for device in catalog["devices"] if (not enabled_only or device["enabled"]) and self._matches_device_query(device, query)]
        start = next((index + 1 for index, device in enumerate(devices) if device["device_id"] == cursor), 0) if cursor else 0
        page = devices[start:start + limit]
        return {"devices": page, "next_cursor": page[-1]["device_id"] if start + limit < len(devices) else None, "refreshed_at": catalog.get("refreshed_at"), "discovery_error": catalog.get("discovery_error"), "stale": catalog.get("stale", False)}

    async def _find_device(self, identifier, force=False):
        catalog = await self._discover(force=force)
        if force and catalog.get("stale"): raise ValueError("家居发现失败，拒绝使用旧目录执行")
        matches = [device for device in catalog["devices"] if identifier in (device["device_id"], device.get("alias"), device.get("name"))]
        if len(matches) == 1: return matches[0]
        if len(matches) > 1: raise ValueError("设备名称重复，请使用稳定 device_id")
        raise ValueError("家居设备不在发现范围")

    @configuration_guard
    async def get_home_state(self, identifier, require_enabled=True):
        device = await self._find_device(identifier)
        if require_enabled and not device["enabled"]: raise ValueError("设备尚未授权")
        return device

    @configuration_guard
    async def set_authorization(self, identifier, enabled):
        if not isinstance(enabled, bool): raise ValueError("enabled 必须为布尔值")
        device = await self._find_device(identifier)
        lock = self.locks.setdefault(device["device_id"], asyncio.Lock())
        async with lock:
            device = await self._find_device(identifier, force=True)
            self._authorizations[device["device_id"]] = enabled
            self._save_authorizations()
            device["enabled"] = enabled
            for action in device["actions"]: action["permission"] = "authorized" if enabled else "not_authorized"
            return device

    @configuration_guard
    async def execute_home_action(self, identifier, action, parameters=None):
        if parameters is None: parameters = {}
        if not isinstance(parameters, dict): raise ValueError("动作参数必须为对象")
        requested_action = action
        device = await self._find_device(identifier)
        if not device["enabled"]: raise ValueError("设备尚未授权")
        if not device["available"]: raise ValueError("设备当前不可用")
        exact = [item for item in device["actions"] if item["id"] == action]
        suffix = [item for item in device["actions"] if item["id"].endswith(":" + str(action))]
        matches = exact or suffix
        if len(matches) > 1: raise ValueError("动作标识不明确，请使用完整 action id")
        command = matches[0] if matches else None
        if command is None: raise ValueError("设备未报告支持此动作")
        self._validate_parameters(command, parameters)
        entity = device["state"]["entity_id"]; domain = entity.split(".", 1)[0]; payload = {"entity_id": entity}
        service_name = command["id"].rsplit(":", 1)[-1]
        entity = command.get("entity_id", entity); domain = entity.split(".", 1)[0]; payload = {"entity_id": entity}
        if service_name in ("turn_on", "turn_off"):
            expected = "on" if service_name == "turn_on" else "off"
            if parameters.get("state", expected) != expected: raise ValueError("动作状态参数无效")
            service = service_name
        elif service_name in VACUUM_FEATURES: service = service_name
        elif service_name == "set_hvac_mode":
            mode = parameters.get("hvac_mode"); enum = command["parameters"]["properties"]["hvac_mode"]["enum"]
            if mode not in enum:
                raise ValueError("空调模式不在设备支持范围")
            payload["hvac_mode"] = mode; service = service_name
        elif service_name == "set_temperature":
            temperature = parameters.get("temperature"); schema = command["parameters"]["properties"]["temperature"]
            if not valid_number(temperature) or ("minimum" in schema and temperature < schema["minimum"]) or ("maximum" in schema and temperature > schema["maximum"]): raise ValueError("温度不在设备允许范围")
            step = schema.get("multipleOf")
            if step and abs((temperature - schema.get("minimum", 0)) / step - round((temperature - schema.get("minimum", 0)) / step)) > 1e-6: raise ValueError("温度不符合设备步长")
            payload["temperature"] = temperature; service = service_name
        else: raise ValueError("动作不在安全能力白名单")
        lock = self.locks.setdefault(device["device_id"], asyncio.Lock())
        async with lock:
            if self._catalog and self._catalog.get("stale"): raise ValueError("家居发现失败，拒绝使用旧目录执行")
            if not self._authorizations.get(device["device_id"], False): raise ValueError("设备尚未授权")
            fresh_exact = [item for item in device["actions"] if item["id"] == requested_action]
            fresh_suffix = [item for item in device["actions"] if item["id"].endswith(":" + service_name)]
            fresh_matches = fresh_exact or fresh_suffix
            if len(fresh_matches) > 1: raise ValueError("动作标识不明确，请使用完整 action id")
            command = fresh_matches[0] if fresh_matches else None
            if command is None: raise ValueError("设备未报告支持此动作")
            entity = command.get("entity_id", device["state"]["entity_id"])
            current_payload = await self._get_json("/api/states/" + quote(entity, safe=""), timeout=5, limit=65536)
            if current_payload.get("state") in ("unknown", "unavailable"): raise ValueError("设备当前不可用")
            if domain == "climate" and service_name == "set_temperature" and current_payload.get("state") == "off":
                raise ValueError("空调当前关机，尚未调温；用户已要求开机时先执行真实turn_on能力并回读，不能重复关机调温请求")
            current_actions = self._actions_for(entity, current_payload, self._service_map)
            command = next((item for item in current_actions if item["id"] == command["id"]), None)
            if command is None: raise ValueError("设备当前未报告支持此动作")
            self._validate_parameters(command, parameters)
            service_name = command["id"].rsplit(":", 1)[-1]
            domain = entity.split(".", 1)[0]; payload = {"entity_id": entity}
            if service_name == "set_hvac_mode": payload["hvac_mode"] = parameters["hvac_mode"]
            elif service_name == "set_temperature": payload["temperature"] = parameters["temperature"]
            async with self.client.post(f"{self.base}/api/services/{domain}/{service}", json=payload, headers=self._headers(), timeout=aiohttp.ClientTimeout(total=8)) as response:
                response.raise_for_status(); await response.content.read(65536)
            expected = VACUUM_FEATURES[service_name][1] if service_name in VACUUM_FEATURES else (("on",) if service_name == "turn_on" else ("off",) if service_name == "turn_off" else ((parameters.get("hvac_mode"),) if service_name == "set_hvac_mode" else None))
            if domain == "climate" and service_name == "turn_on":
                expected = tuple(mode for mode in current_payload.get("attributes", {}).get("hvac_modes", []) if mode != "off")
            state = await self._wait_device(identifier, expected, parameters.get("temperature") if service_name == "set_temperature" else None, entity)
            result = {"device_id": device["device_id"], "action": requested_action, "service": service_name, "request_accepted": True, "reported_state": state["state"]["value"], "target_observed": state["target_observed"], "confirmation": command["confirmation"], "evidence": "home_assistant_reported_state", "device": state}
            for key in ("current_temperature", "temperature", "min_temp", "max_temp", "target_temp_step", "hvac_modes", "battery_level"):
                if key in state["state"].get("attributes", {}): result[key] = state["state"]["attributes"][key]
            return result

    @staticmethod
    def _validate_parameters(command, parameters):
        schema = command["parameters"]
        properties = schema.get("properties", {})
        if set(parameters) - set(properties): raise ValueError("动作参数包含未声明字段")
        missing = [key for key in schema.get("required", []) if key not in parameters]
        if missing: raise ValueError("动作缺少必填参数")
        for key, value in parameters.items():
            definition = properties[key]
            if definition.get("type") == "number" and (not valid_number(value) or type(value) is bool): raise ValueError("动作数值参数无效")
            if definition.get("type") == "string" and not isinstance(value, str): raise ValueError("动作字符串参数无效")
            if definition.get("enum") and value not in definition["enum"]: raise ValueError("动作枚举参数无效")
            if "minimum" in definition and value < definition["minimum"]: raise ValueError("动作参数低于允许范围")
            if "maximum" in definition and value > definition["maximum"]: raise ValueError("动作参数高于允许范围")
            if "multipleOf" in definition and abs((value - definition.get("minimum", 0)) / definition["multipleOf"] - round((value - definition.get("minimum", 0)) / definition["multipleOf"])) > 1e-6: raise ValueError("动作参数不符合步长")

    async def _wait_device(self, identifier, states=None, temperature=None, target_entity=None):
        deadline = time.monotonic() + 3
        while True:
            result = await self._find_device(identifier)
            entity = target_entity or result["state"]["entity_id"]
            payload = await self._get_json("/api/states/" + quote(entity, safe=""), timeout=5, limit=65536)
            attrs = payload.get("attributes", {}) if isinstance(payload.get("attributes"), dict) else {}
            result = {**result, "state": {"entity_id": entity, "value": payload.get("state", "unknown"), "attributes": attrs}}
            primary = result["state"]
            observed = (states is None or primary["value"] in states) and (temperature is None or primary["attributes"].get("temperature") == temperature)
            if observed or time.monotonic() >= deadline:
                return {**result, "target_observed": observed, "note": "这是平台报告状态；未接独立反馈传感器时，不能代表物理动作确认。"}
            await asyncio.sleep(.25)

    def resolve(self, alias):
        if not isinstance(alias, str) or alias not in self.entities: raise ValueError("家居设备不在允许列表")
        if not self.configured: raise ValueError("尚未配置 Home Assistant 和允许的设备")
        return self.entities[alias]

    @configuration_guard
    async def read(self, alias):
        entity = self.resolve(alias)
        async with self.client.get(f"{self.base}/api/states/{quote(entity, safe='')}", headers=self._headers(), timeout=aiohttp.ClientTimeout(total=5)) as response:
            response.raise_for_status(); payload = await response.json()
        attrs = payload.get("attributes", {}) if isinstance(payload.get("attributes"), dict) else {}; result = {"alias": alias, "entity_id": entity, "domain": entity.split(".", 1)[0], "state": payload.get("state", "unknown"), "available": payload.get("state") not in ("unknown", "unavailable"), "evidence": "home_assistant_reported_state"}
        result["supported_actions"] = [item["id"].rsplit(":", 1)[-1] for item in self._actions_for(entity, payload)]; result.update({key: attrs.get(key) for key in ("current_temperature", "temperature", "min_temp", "max_temp", "target_temp_step", "hvac_modes", "battery_level", "supported_features") if key in attrs}); return result

    async def wait_for_state(self, alias, states=None, temperature=None):
        deadline = time.monotonic() + 3
        while True:
            result = await self.read(alias); observed = (states is None or result["state"] in states) and (temperature is None or result.get("temperature") == temperature)
            if observed or time.monotonic() >= deadline: return {**result, "reported_state": result["state"], "target_observed": observed, "note": "这是平台报告状态；未接独立反馈传感器时，不能代表物理动作确认。"}
            await asyncio.sleep(.25)

    async def set(self, alias, state):
        if state not in ("on", "off"): raise ValueError("仅支持明确设置开或关")
        return await self.execute_home_action(alias, "turn_on" if state == "on" else "turn_off", {"state": state})
    async def command_vacuum(self, alias, action): return await self.execute_home_action(alias, action, {})
    async def command_climate(self, alias, action, hvac_mode=None, temperature=None): return await self.execute_home_action(alias, action, {"hvac_mode": hvac_mode} if action == "set_hvac_mode" else {"temperature": temperature})
