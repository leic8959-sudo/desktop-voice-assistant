"""本机单用户的重要事件读取、持久水位与通知合并；外部文字始终作为数据。"""
from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import re
import uuid
from datetime import timedelta
from email.utils import parseaddr

import aiohttp

LOGGER = logging.getLogger("assistant.important_events")
FAILURES = {"failure", "timed_out", "cancelled", "action_required", "startup_failure"}


class ImportantEvents:
    def __init__(self, service, now, iso, parse_time, zone):
        self.service, self.db = service, service.db
        self.now, self.iso, self.time, self.zone = now, iso, parse_time, zone
        self.db.executescript("""
        CREATE TABLE IF NOT EXISTS p_subscription_settings(subscription_id TEXT PRIMARY KEY,options TEXT NOT NULL,state TEXT NOT NULL,failures INTEGER NOT NULL DEFAULT 0,last_notice_at TEXT,last_error TEXT NOT NULL DEFAULT '');
        CREATE TABLE IF NOT EXISTS p_notification_seen(subscription_id TEXT NOT NULL,event_key TEXT NOT NULL,expires_at TEXT NOT NULL,PRIMARY KEY(subscription_id,event_key));
        CREATE INDEX IF NOT EXISTS p_notification_seen_expiry ON p_notification_seen(expires_at);
        CREATE TABLE IF NOT EXISTS p_notification_queue(subscription_id TEXT NOT NULL,event_key TEXT NOT NULL,payload TEXT NOT NULL,ready_at TEXT NOT NULL,expires_at TEXT NOT NULL,batch_id TEXT NOT NULL DEFAULT '',PRIMARY KEY(subscription_id,event_key));
        CREATE INDEX IF NOT EXISTS p_notification_queue_due ON p_notification_queue(ready_at,subscription_id);
        CREATE INDEX IF NOT EXISTS p_notification_queue_expiry ON p_notification_queue(expires_at);
        CREATE TABLE IF NOT EXISTS p_notification_dispatch(id INTEGER PRIMARY KEY,next_at TEXT NOT NULL);
        """)

    def options(self, args):
        kind = args["kind"]
        provider = args.get("provider") or {"meeting": "google_calendar", "mail": "gmail", "github_ci": "github"}.get(kind, "ha")
        if provider not in {"meeting": {"google_calendar", "microsoft_calendar"}, "mail": {"gmail", "microsoft_mail"}, "github_ci": {"github"}, "ha_state": {"ha"}}[kind]:
            raise ValueError("订阅来源与类型不匹配")
        def number(name, default, low, high):
            value = args.get(name, default)
            if type(value) is not int or not low <= value <= high:
                raise ValueError(f"{name}需为{low}～{high}的整数")
            return value
        def text(name, limit):
            value = args.get(name, "")
            if not isinstance(value, str) or len(value) > limit or any(ord(char) < 32 for char in value):
                raise ValueError(f"{name}格式无效")
            return value.strip()
        sender, subject, branch = text("sender", 254), text("subject_contains", 120), text("branch", 120)
        if kind == "mail" and not (sender or subject):
            raise ValueError("指定邮件订阅至少填写发件人或主题关键词")
        if sender and (parseaddr(sender)[1] != sender or not re.fullmatch(r"[^\s<>\"(){}:]+@[^\s<>\"(){}:]+\.[^\s<>\"(){}:]+", sender)):
            raise ValueError("发件人需为完整邮箱地址")
        conclusions = args.get("conclusions", ["failure", "timed_out"])
        if not isinstance(conclusions, list) or not 1 <= len(conclusions) <= 5 or any(item not in FAILURES for item in conclusions):
            raise ValueError("CI通知仅允许失败、超时、取消、需处理或启动失败")
        timezone = str(self.zone(args.get("timezone", str(self.service.timezone))))
        return {"provider": provider, "sender": sender.casefold(), "subject_contains": subject, "branch": branch, "conclusions": sorted(set(conclusions)), "lead_minutes": number("lead_minutes", 10, 1, 1440), "merge_window_s": number("merge_window_s", 30, 0, 300), "min_notify_interval_s": number("min_notify_interval_s", 120, 30, 3600), "timezone": timezone}

    def setting(self, identity, kind):
        row = self.db.execute("SELECT * FROM p_subscription_settings WHERE subscription_id=?", (identity,)).fetchone()
        if row is None:
            options = self.options({"kind": kind})
            self.db.execute("INSERT INTO p_subscription_settings(subscription_id,options,state) VALUES(?,?,'{}')", (identity, json.dumps(options)))
            row = self.db.execute("SELECT * FROM p_subscription_settings WHERE subscription_id=?", (identity,)).fetchone()
        return dict(row), json.loads(row["options"]), json.loads(row["state"])

    def describe(self, item):
        row, options, state = self.setting(item["id"], item["kind"])
        item["filters"] = options
        item["last_error"] = row["last_error"]
        item["watermark"] = state.get("watermark")
        item["catching_up"] = bool(state.get("cursor") or state.get("page", 1) > 1)
        item["pending_notifications"] = self.db.execute("SELECT count(*) FROM p_notification_queue WHERE subscription_id=?", (item["id"],)).fetchone()[0]
        return item

    def cleanup(self):
        now = self.iso(self.now())
        for table in ("p_notification_queue", "p_notification_seen"):
            self.db.execute(f"DELETE FROM {table} WHERE expires_at<=?", (now,))

    def remove(self, identity):
        for table in ("p_subscription_settings", "p_notification_queue", "p_notification_seen"):
            self.db.execute(f"DELETE FROM {table} WHERE subscription_id=?", (identity,))

    def enqueue(self, row, options, key, payload, ready=None, expires=None):
        if self.db.execute("SELECT 1 FROM p_notification_seen WHERE subscription_id=? AND event_key=?", (row["id"], key)).fetchone():
            return
        if self.db.execute("SELECT count(*) FROM p_notification_queue").fetchone()[0] >= 512:
            raise RuntimeError("通知暂存容量已满；保留水位并稍后重试")
        now = self.now()
        # 已有通知的合并截止时间不因后续事件延长。
        ready = ready or now + timedelta(seconds=options["merge_window_s"])
        self.db.execute("INSERT INTO p_notification_queue(subscription_id,event_key,payload,ready_at,expires_at) VALUES(?,?,?,?,?) ON CONFLICT(subscription_id,event_key) DO UPDATE SET payload=excluded.payload,expires_at=excluded.expires_at", (row["id"], key, json.dumps(payload, ensure_ascii=False), self.iso(ready), self.iso(expires or now + timedelta(days=1))))

    async def _mail(self, row, options, state, now):
        provider = options["provider"]
        if provider == "gmail":
            gmail = getattr(self.service.runtime, "gmail", None)
            if gmail is None or not gmail.status().get("configured") or not gmail.status().get("authorized"):
                raise RuntimeError("Gmail缺少配置或现有OAuth授权，请到邮件配置页完成授权")
        if not state.get("watermark"):
            # 创建后的第一轮只从当前时刻开始，既有邮件不补发。
            if provider != "gmail":
                await self.service._connector(provider, "notification_messages", {"received_after": self.iso(now), "received_before": self.iso(now + timedelta(seconds=1)), "limit": 1})
            state.update(watermark=self.iso(now), baseline_at=self.iso(now))
            return "baseline"
        lower = state.get("window_start") or self.iso(self.time(state["watermark"]) - timedelta(seconds=120))
        upper = state.get("window_end") or self.iso(now)
        cursor = state.get("cursor")
        if provider == "gmail":
            def quoted(value):
                return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
            query = f"in:inbox after:{int(self.time(lower).timestamp())} before:{int(self.time(upper).timestamp()) + 1}"
            if options["sender"]:
                query += " from:" + quoted(options["sender"])
            # Gmail按词搜索不是子串搜索；主题只在本页元数据中做子串匹配，避免漏掉rebuild/build一类包含关系。
            data = await asyncio.wait_for(gmail.search(query, page_token=cursor, limit=20), timeout=35)
        else:
            data = await self.service._connector(provider, "notification_messages", {"received_after": lower, "received_before": upper, "page_token": cursor, "sender": options["sender"], "limit": 20})
        items = data.get("items")
        if not isinstance(items, list) or len(items) > 20:
            raise ValueError("邮件元数据响应无效")
        for item in items:
            if not item.get("id") or not item.get("received_at"):
                raise RuntimeError("邮件元数据不完整，未推进水位")
            received = self.time(item["received_at"])
            if not self.time(lower) <= received <= self.time(upper) or received < self.time(state["baseline_at"]):
                continue
            sender = parseaddr(str(item.get("sender") or item.get("from", "")))[1].casefold()
            subject = str(item.get("subject", ""))[:200]
            if options["sender"] and sender != options["sender"]:
                continue
            if options["subject_contains"].casefold() not in subject.casefold():
                continue
            self.enqueue(row, options, provider + ":" + str(item["id"]), {"title": subject or "无主题邮件", "detail": f"发件人：{sender}；收到时间：{self.iso(received)}", "received_at": self.iso(received), "source_id": str(item["id"]), "source": provider})
        cursor = data.get("next_page_token")
        if cursor:
            if not isinstance(cursor, str) or len(cursor) > 4096:
                raise ValueError("邮件分页游标无效")
            state.update(cursor=cursor, window_start=lower, window_end=upper)
            return "catching_up"
        state.update(watermark=upper)
        for key in ("cursor", "window_start", "window_end"):
            state.pop(key, None)
        return "checked"

    async def _ci(self, row, options, state, now):
        arguments = {"repository": row["target"], "limit": 50, "branch": options["branch"], "page": state.get("page", 1)}
        window_end = state.get("window_end") or self.iso(now)
        if state.get("watermark"):
            # GitHub只支持按创建时间过滤；固定35天覆盖其最长运行窗口。
            # 超过1000条则明确unknown，不悄悄跳过运行；用户可按分支缩小范围。
            arguments["created_after"] = self.iso(self.time(window_end) - timedelta(days=35))
            arguments["created_before"] = window_end
        data = await self.service._connector("github", "workflow_runs", arguments)
        items = data.get("items")
        if not isinstance(items, list) or len(items) > 50:
            raise ValueError("CI响应无效")
        baseline = not state.get("watermark")
        for item in items:
            if not item.get("id"):
                raise ValueError("CI运行缺少稳定ID")
            if options["branch"] and item.get("branch") != options["branch"]:
                continue
            if item.get("status") != "completed" or item.get("conclusion") not in options["conclusions"]:
                continue
            key = f"github:{row['target']}:{item['id']}:{item.get('attempt', 1)}"
            updated = self.time(item["updated_at"]) if item.get("updated_at") else None
            # 新订阅以及首次基线以前完成的运行均不补发。
            if baseline or not updated or updated < max(self.time(state["baseline_at"]), self.time(state["watermark"]) - timedelta(seconds=120)) or updated > self.time(window_end):
                continue
            self.enqueue(row, options, key, {"title": str(item.get("name") or "CI运行")[:120], "detail": f"{row['target']} · {item.get('branch', '')} · {item['conclusion']}", "source_id": str(item["id"]), "source": "github", "conclusion": item["conclusion"]})
        if baseline:
            state.update(watermark=self.iso(now), baseline_at=self.iso(now), page=1)
            return "baseline"
        if data.get("has_more"):
            # 每轮一页；固定水位直到本窗口扫完，不截断后悄悄跳过。
            page = state.get("page", 1)
            if page >= 20:
                raise RuntimeError("CI窗口超过1000条，请缩小分支筛选；水位未推进")
            state.update(page=page + 1, window_end=window_end)
            return "catching_up"
        state.update(watermark=state.pop("window_end", self.iso(now)), page=1)
        return "checked"

    async def _meetings(self, row, options, state, now):
        # 每轮重新读取即将开始窗口；只有完整成功快照才撤销旧时间的待通知。
        end = now + timedelta(minutes=options["lead_minutes"], seconds=row["interval_s"] + 60)
        arguments = {"time_min": self.iso(now), "time_max": self.iso(end), "limit": 100, "metadata_only": True}
        items, cursor = [], None
        for _ in range(3):
            data = await self.service._connector(options["provider"], "list_events", {**arguments, "page_token": cursor})
            page = data.get("items")
            if not isinstance(page, list) or len(page) > 100:
                raise ValueError("日历响应无效")
            items.extend(page)
            cursor = data.get("next_page_token")
            if not cursor:
                break
        if cursor:
            raise RuntimeError("即将开始日程超过300条，请缩小提前时间；未使用不完整快照")
        keep = set()
        prepared = []
        for item in items:
            if item.get("cancelled") or item.get("status") == "cancelled" or item.get("all_day"):
                continue
            start = item.get("start")
            if not isinstance(start, str) or len(start) <= 10:
                continue
            start_at = self.time(start)
            if not item.get("id") or start_at <= now or start_at > end:
                continue
            key = f"meeting:{options['provider']}:{item['id']}:{self.iso(start_at)}"
            keep.add(key)
            due = start_at - timedelta(minutes=options["lead_minutes"])
            # 允许同窗口合并，但不能为了合并等到会议已开始。
            ready = max(due, now) + timedelta(seconds=options["merge_window_s"])
            ready = min(ready, start_at - timedelta(seconds=5))
            local = start_at.astimezone(self.zone(options["timezone"]))
            prepared.append((key, {"title": str(item.get("summary") or "无标题会议")[:200], "detail": f"{local:%Y-%m-%d %H:%M}（{options['timezone']}）开始", "starts_at": self.iso(start_at), "scheduled_at": self.iso(due), "source_id": str(item["id"]), "source": options["provider"]}, ready, start_at))
        with self.service._transaction():
            pending = self.db.execute("SELECT event_key FROM p_notification_queue WHERE subscription_id=? LIMIT 512", (row["id"],)).fetchall()
            for item in pending:
                if item["event_key"] not in keep:
                    self.db.execute("DELETE FROM p_notification_queue WHERE subscription_id=? AND event_key=?", (row["id"], item["event_key"]))
            for key, payload, ready, expires in prepared:
                self.enqueue(row, options, key, payload, ready, expires)
        state["validated_at"] = self.iso(now)
        state["watermark"] = self.iso(now)
        return "checked"

    async def poll(self, row, due_only=False):
        identity, now = row["id"], self.now()
        if not row["enabled"]:
            return {"status": "paused", "message": "暂停订阅不会读取或通知；请先恢复"}
        # 手动读取也遵循持久退避，避免连点绕过第三方限额。
        if self.time(row["next_at"]) > now and (due_only or row["last_status"] != "not_checked"):
            return {"status": "not_due", "next_at": row["next_at"]}
        owner = uuid.uuid4().hex
        if self.db.execute("UPDATE p_subscriptions SET poll_owner=?,poll_until=?,next_at=? WHERE id=? AND revision=? AND (poll_until IS NULL OR poll_until<=?)", (owner, self.iso(now + timedelta(seconds=90)), self.iso(now + timedelta(seconds=row["interval_s"])), identity, row["revision"], self.iso(now))).rowcount != 1:
            return {"status": "in_progress"}
        settings, options, state = self.setting(identity, row["kind"])
        self.cleanup()
        try:
            LOGGER.debug("important_events_poll_start id=%s kind=%s", identity, row["kind"])
            # 日历最多3页，每个连接器有独立25秒限制；整体80秒早于90秒租约。
            reader = {"meeting": self._meetings, "mail": self._mail, "github_ci": self._ci}[row["kind"]]
            status = await asyncio.wait_for(reader(row, options, state, now), timeout=80)
            if not self.db.execute("SELECT 1 FROM p_subscriptions WHERE id=? AND enabled=1 AND revision=? AND poll_owner=?", (identity, row["revision"], owner)).fetchone():
                self.db.execute("DELETE FROM p_notification_queue WHERE subscription_id=?", (identity,))
                return {"status": "superseded"}
            with self.service._transaction():
                self.db.execute("UPDATE p_subscription_settings SET state=?,failures=0,last_error='' WHERE subscription_id=?", (json.dumps(state), identity))
                self.db.execute("UPDATE p_subscriptions SET last_checked=?,last_status=? WHERE id=? AND poll_owner=?", (self.iso(now), status, identity, owner))
            LOGGER.debug("important_events_poll_complete id=%s status=%s", identity, status)
            return {"status": status, "checked_at": self.iso(now), "watermark": state.get("watermark")}
        except (RuntimeError, ValueError, KeyError, TypeError, PermissionError, OverflowError, aiohttp.ClientError, asyncio.TimeoutError):
            if not row["last_error_at"] or self.time(row["last_error_at"]) <= now - timedelta(minutes=5):
                LOGGER.exception("important_events_poll_failed id=%s kind=%s", identity, row["kind"])
                self.db.execute("UPDATE p_subscriptions SET last_error_at=? WHERE id=?", (self.iso(now), identity))
            failures = min(settings["failures"] + 1, 10)
            delay = min(3600, row["interval_s"] * (2 ** min(failures, 5)))
            # 不保存原始异常（第三方文本可能包含令牌或访问链接）。
            message = {"mail": "读取失败：请核对邮箱OAuth授权、筛选条件或元数据完整性；水位未推进", "meeting": "读取失败：请核对日历OAuth授权、时间格式或窗口是否超过300条；本次不通知", "github_ci": "读取失败：请核对GitHub令牌、仓库与分支权限或窗口容量；水位未推进"}[row["kind"]]
            self.db.execute("UPDATE p_subscription_settings SET failures=?,last_error=? WHERE subscription_id=?", (failures, message, identity))
            self.db.execute("UPDATE p_subscriptions SET last_checked=?,last_status='unknown',next_at=? WHERE id=? AND poll_owner=?", (self.iso(now), self.iso(now + timedelta(seconds=delay)), identity, owner))
            return {"status": "unknown", "message": message, "retry_after_s": delay}
        finally:
            self.db.execute("UPDATE p_subscriptions SET poll_owner=NULL,poll_until=NULL WHERE id=? AND poll_owner=?", (identity, owner))

    async def flush(self):
        now = self.now()
        self.cleanup()
        rows = self.db.execute("SELECT s.*,n.last_notice_at,n.options,n.state FROM p_subscriptions s JOIN p_subscription_settings n ON n.subscription_id=s.id WHERE s.enabled=1 AND EXISTS(SELECT 1 FROM p_notification_queue q WHERE q.subscription_id=s.id AND q.ready_at<=?) ORDER BY CASE WHEN s.kind='meeting' THEN 0 ELSE 1 END,(SELECT min(q.expires_at) FROM p_notification_queue q WHERE q.subscription_id=s.id),coalesce(n.last_notice_at,'') LIMIT 32", (self.iso(now),)).fetchall()
        for row in rows:
            options, state = json.loads(row["options"]), json.loads(row["state"])
            if row["last_notice_at"] and self.time(row["last_notice_at"]) + timedelta(seconds=options["min_notify_interval_s"]) > now:
                continue
            if row["kind"] == "meeting" and (row["last_status"] == "unknown" or not state.get("validated_at") or self.time(state["validated_at"]) < now - timedelta(seconds=row["interval_s"] + 30)):
                continue
            frozen = self.db.execute("SELECT batch_id FROM p_notification_queue WHERE subscription_id=? AND batch_id<>'' ORDER BY ready_at,event_key LIMIT 1", (row["id"],)).fetchone()
            if frozen:
                pending = self.db.execute("SELECT * FROM p_notification_queue WHERE subscription_id=? AND batch_id=? ORDER BY ready_at,event_key LIMIT 20", (row["id"], frozen["batch_id"])).fetchall()
            else:
                pending = self.db.execute("SELECT * FROM p_notification_queue WHERE subscription_id=? AND batch_id='' AND ready_at<=? AND expires_at>? ORDER BY ready_at,event_key LIMIT 20", (row["id"], self.iso(now), self.iso(now))).fetchall()
            if not pending:
                continue
            if self.db.execute("SELECT count(*) FROM p_notification_seen").fetchone()[0] + len(pending) > 8192:
                self.db.execute("UPDATE p_subscription_settings SET last_error='去重容量已满，保留待通知；请减少订阅频率或等待30天保留期过后' WHERE subscription_id=?", (row["id"],))
                continue
            with self.service._transaction():
                gate = self.db.execute("SELECT next_at FROM p_notification_dispatch WHERE id=1").fetchone()
                if gate and self.time(gate["next_at"]) > now:
                    return
                self.db.execute("INSERT INTO p_notification_dispatch VALUES(1,?) ON CONFLICT(id) DO UPDATE SET next_at=excluded.next_at", (self.iso(now + timedelta(seconds=60)),))
            keys = [item["event_key"] for item in pending]
            batch_id = frozen["batch_id"] if frozen else hashlib.sha256((row["id"] + ":" + "|".join(sorted(keys))).encode()).hexdigest()
            if not frozen:
                with self.service._transaction():
                    for key in keys:
                        self.db.execute("UPDATE p_notification_queue SET batch_id=? WHERE subscription_id=? AND event_key=?", (batch_id, row["id"], key))
            values = [json.loads(item["payload"]) for item in pending]
            title = {"mail": "指定邮件到达", "meeting": "会议即将开始", "github_ci": "GitHub CI异常"}[row["kind"]]
            detail = "\n".join(f"{item['title']} · {item['detail']}" for item in values)[:2000]
            # 已持久化的批次在进程中断后用相同ID补建；p_events已有时不会再交付。
            result = await self.service._publish("subscription", f"{title}（{len(values)}条）", detail, {"subscription_id": row["id"], "subscription_revision": row["revision"], "target": row["target"], "scene_id": "", "events": values, "external_content_untrusted": True, "source": "structured_metadata", "model_generated": False}, identity=batch_id)
            if result.get("status") == "superseded":
                continue
            with self.service._transaction():
                for key in keys:
                    self.db.execute("INSERT OR IGNORE INTO p_notification_seen VALUES(?,?,?)", (row["id"], key, self.iso(now + timedelta(days=30))))
                    self.db.execute("DELETE FROM p_notification_queue WHERE subscription_id=? AND event_key=?", (row["id"], key))
                self.db.execute("UPDATE p_subscription_settings SET last_notice_at=? WHERE subscription_id=?", (self.iso(now), row["id"]))
            # 全局每60秒最多一批，每批最多20条；订阅自己的限频可以更严格。
            return
