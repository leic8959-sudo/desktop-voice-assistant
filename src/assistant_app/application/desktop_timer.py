"""一个有界本地计时器；只保存当前状态，服务器重启后按 UTC 截止时间恢复。"""

import json
import math
import os
import tempfile
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

from assistant_core.store import IdempotencyConflict


class DesktopTimer:
    def __init__(self, directory):
        self.path = Path(directory) / "desktop-timer.json"
        self.state = None
        try:
            if self.path.stat().st_size > 4096:
                raise ValueError("本地计时器状态超过限制")
            value = json.loads(self.path.read_text(encoding="utf-8"))
            if not isinstance(value, dict) or value.get("status") not in {"running", "paused", "finished", "cancelled"}:
                raise ValueError("本地计时器状态损坏")
            if not isinstance(value.get("id"), str) or not isinstance(value.get("title"), str) or len(value["title"]) > 60 or type(value.get("revision")) is not int:
                raise ValueError("本地计时器字段无效")
            for key in ("remaining_s", "deadline", "duration_s"):
                if type(value.get(key)) not in {int, float} or not math.isfinite(value[key]) or value[key] < 0:
                    raise ValueError("本地计时器时间无效")
            self.state = value
        except FileNotFoundError:
            pass

    def _save(self, state):
        state["updated_at"] = datetime.now(timezone.utc).isoformat()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, name = tempfile.mkstemp(prefix="desktop-timer.", suffix=".tmp", dir=self.path.parent)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(state, stream, ensure_ascii=False, separators=(",", ":"))
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(name, self.path)
        finally:
            if os.path.exists(name):
                os.unlink(name)
        self.state = state

    def snapshot(self):
        if self.state is None:
            return None
        result = dict(self.state)
        if result["status"] == "running":
            result["remaining_s"] = max(0, math.ceil(result["deadline"] - time.time()))
            if result["remaining_s"] == 0:
                result.update(status="finished", revision=result["revision"] + 1)
                self._save(result)
        return result

    def create(self, title, seconds, request_id):
        if not isinstance(title, str) or not 1 <= len(title.strip()) <= 60:
            raise ValueError("计时名称需为1～60个字符")
        if type(seconds) is not int or not 1 <= seconds <= 86400:
            raise ValueError("计时时长需为1～86400秒")
        if not isinstance(request_id, str) or not 8 <= len(request_id) <= 128:
            raise ValueError("计时请求需要有效 request_id")
        current = self.snapshot()
        if current and current.get("request_id") == request_id:
            if current["title"] != title.strip() or current["duration_s"] != seconds:
                raise IdempotencyConflict("同一请求 ID 已用于其他计时参数")
            return current
        if current and current["status"] in {"running", "paused"}:
            raise IdempotencyConflict("已有计时器，请先暂停或取消，不会直接覆盖")
        state = {"id": uuid.uuid4().hex, "title": title.strip(), "duration_s": seconds, "remaining_s": seconds, "deadline": time.time() + seconds, "status": "running", "revision": 1, "request_id": request_id, "created_at": datetime.now(timezone.utc).isoformat()}
        self._save(state)
        return dict(state)

    def action(self, action, timer_id, revision):
        current = self.snapshot()
        if not current or current["id"] != timer_id:
            raise ValueError("计时器不存在或已被替换")
        if type(revision) is not int or revision != current["revision"]:
            raise IdempotencyConflict("计时器状态已更新，请刷新后重试")
        if action == "pause" and current["status"] == "running":
            current.update(status="paused", deadline=0)
        elif action == "resume" and current["status"] == "paused":
            current.update(status="running", deadline=time.time() + current["remaining_s"])
        elif action == "cancel":
            current.update(status="cancelled", remaining_s=0, deadline=0)
        else:
            raise ValueError("当前状态不支持此操作")
        current["revision"] += 1
        self._save(current)
        return dict(current)
