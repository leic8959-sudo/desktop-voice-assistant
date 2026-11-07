"""重要事件的本地模拟验收：临时SQLite、假连接器、假卡片，不使用账号或设备。"""
import copy
import json
import tempfile
import unittest
import uuid
from datetime import datetime, timedelta, timezone
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch
from zoneinfo import ZoneInfo

from src.assistant_app.application.productivity import ProductivityService, _iso
from src.assistant_app.integrations.microsoft_calendar import MicrosoftCalendarConnector


class NoNetwork:
    def __getattr__(self, name):
        raise AssertionError(f"模拟验收不允许网络访问：{name}")


class FakeRegistry:
    def __init__(self):
        self.responses = {}
        self.calls = []

    async def invoke(self, connector, action, arguments, context):
        self.calls.append((connector, action, copy.deepcopy(arguments)))
        response = self.responses[(connector, action)]
        if isinstance(response, BaseException):
            raise response
        return copy.deepcopy(response)


class ImportantEventsTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.now = datetime(2026, 11, 4, 4, 0, tzinfo=timezone.utc)
        self.clock = patch("src.assistant_app.application.productivity._now", side_effect=lambda: self.now)
        self.clock.start()
        self.registry = FakeRegistry()
        self.gmail = SimpleNamespace(status=lambda: {"configured": True, "authorized": True}, search=AsyncMock(return_value={"items": []}))
        self.runtime = SimpleNamespace(connector_registry=self.registry, gmail=self.gmail, send_screen_card=AsyncMock(return_value={"status": "no_device"}))
        self.store = SimpleNamespace(device_id="important-events-test", business_timezone=ZoneInfo("Asia/Shanghai"))
        self.config = {"runtime_dir": self.directory.name}
        self.service = ProductivityService(self.config, self.store, self.runtime, NoNetwork())

    async def asyncTearDown(self):
        await self.service.close()
        self.clock.stop()
        self.directory.cleanup()

    async def create(self, kind, **options):
        target = {"meeting": "primary", "mail": "inbox", "github_ci": "example/project"}[kind]
        result = await self.service.subscribe({"kind": kind, "target": target, "interval_s": 60, "merge_window_s": 0, "min_notify_interval_s": 30, "idempotency_key": uuid.uuid4().hex, **options})
        return result["item"]

    def advance(self, seconds=60):
        self.now += timedelta(seconds=seconds)

    def queued(self, identity):
        return [dict(row) for row in self.service.db.execute("SELECT * FROM p_notification_queue WHERE subscription_id=? ORDER BY event_key", (identity,))]

    def state(self, identity):
        row = self.service.db.execute("SELECT state FROM p_subscription_settings WHERE subscription_id=?", (identity,)).fetchone()
        return json.loads(row["state"])

    async def events(self):
        return (await self.service.events())["items"]

    def calendar(self, items, **extra):
        self.registry.responses[("google_calendar", "list_events")] = {"items": items, **extra}

    def run_item(self, identity, conclusion="failure", attempt=1, **extra):
        return {"id": identity, "name": "build", "branch": "main", "status": "completed", "conclusion": conclusion, "attempt": attempt, "updated_at": _iso(self.now), **extra}

    async def test_meeting_near_start_timezone_and_all_day(self):
        item = await self.create("meeting", timezone="Europe/Moscow", lead_minutes=10)
        self.calendar([{"id": "near", "summary": "设计评审", "start": "2026-11-04T12:07:00+08:00"}, {"id": "all-day", "summary": "全天", "start": "2026-11-04", "all_day": True}])
        self.assertEqual((await self.service.poll_subscription(item["id"]))["status"], "checked")
        await self.service.important_events.flush()
        events = await self.events()
        self.assertEqual(len(events), 1)
        self.assertIn("07:07（Europe/Moscow）", events[0]["detail"])
        self.assertEqual(events[0]["data"]["events"][0]["starts_at"], "2026-11-04T04:07:00.000000Z")
        self.assertEqual(len(events[0]["data"]["events"]), 1)
        self.assertFalse(self.service.speech.enabled)
        self.assertEqual(self.runtime.send_screen_card.await_count, 1)

    async def test_meeting_cancel_removes_pending(self):
        item = await self.create("meeting", lead_minutes=10)
        self.calendar([{"id": "cancel-me", "summary": "评审", "start": _iso(self.now + timedelta(minutes=11))}])
        await self.service.poll_subscription(item["id"])
        self.assertEqual(len(self.queued(item["id"])), 1)
        self.advance()
        self.calendar([{"id": "cancel-me", "cancelled": True}])
        await self.service.poll_subscription(item["id"])
        await self.service.important_events.flush()
        self.assertEqual(self.queued(item["id"]), [])
        self.assertEqual(await self.events(), [])

    async def test_meeting_reschedule_replaces_stable_schedule(self):
        item = await self.create("meeting", lead_minutes=10, merge_window_s=30)
        self.calendar([{"id": "move-me", "summary": "旧标题", "start": _iso(self.now + timedelta(minutes=8))}])
        await self.service.poll_subscription(item["id"])
        old_key = self.queued(item["id"])[0]["event_key"]
        self.advance()
        new_start = _iso(self.now + timedelta(minutes=9))
        self.calendar([{"id": "move-me", "summary": "改期评审", "start": new_start}])
        await self.service.poll_subscription(item["id"])
        pending = self.queued(item["id"])
        self.assertEqual(len(pending), 1)
        self.assertNotEqual(pending[0]["event_key"], old_key)
        self.advance(30)
        await self.service.important_events.flush()
        events = await self.events()
        self.assertEqual(events[0]["data"]["events"][0]["starts_at"], new_start)
        self.assertIn("改期评审", events[0]["detail"])
        self.assertNotIn("旧标题", events[0]["detail"])

    async def test_calendar_incomplete_or_bad_timezone_does_not_notify(self):
        item = await self.create("meeting")
        self.calendar([{"id": "naive", "summary": "错误时区", "start": "2026-11-04T04:05:00"}])
        with self.assertLogs("assistant.important_events", level="ERROR"):
            self.assertEqual((await self.service.poll_subscription(item["id"]))["status"], "unknown")
        await self.service.important_events.flush()
        self.assertEqual(await self.events(), [])
        self.advance(120)
        self.calendar([], next_page_token="more")
        self.assertEqual((await self.service.poll_subscription(item["id"]))["status"], "unknown")
        self.assertEqual(self.queued(item["id"]), [])

    async def test_microsoft_naive_time_requires_explicit_utc(self):
        connector = MicrosoftCalendarConnector(NoNetwork(), {"microsoft_calendar_access_token": "fake"})
        connector._request = AsyncMock(return_value={"value": [{"id": "event", "subject": "模拟", "start": {"dateTime": "2026-11-04T04:05:00", "timeZone": "China Standard Time"}}]})
        arguments = {"time_min": _iso(self.now), "time_max": _iso(self.now + timedelta(hours=1)), "metadata_only": True}
        with self.assertRaises(ValueError):
            await connector.invoke("list_events", arguments, None)
        connector._request.return_value["value"][0]["start"]["timeZone"] = "UTC"
        self.assertEqual((await connector.invoke("list_events", arguments, None))["items"][0]["start"], "2026-11-04T04:05:00Z")

    async def test_mail_baseline_filters_subject_substring_and_no_body(self):
        item = await self.create("mail", sender="boss@example.com", subject_contains="build")
        base = self.now
        self.assertEqual((await self.service.poll_subscription(item["id"]))["status"], "baseline")
        self.gmail.search.assert_not_awaited()
        self.advance()
        def mail(identity, sender, subject, received):
            return {"id": identity, "from": sender, "subject": subject, "received_at": _iso(received)}
        self.gmail.search.return_value = {"items": [mail("old", "boss@example.com", "build", base - timedelta(seconds=1)), mail("new", "Boss <BOSS@example.com>", "Rebuild report", base + timedelta(seconds=10)), mail("other", "other@example.com", "build", base + timedelta(seconds=11)), mail("wrong-title", "boss@example.com", "agenda", base + timedelta(seconds=12))]}
        await self.service.poll_subscription(item["id"])
        await self.service.important_events.flush()
        events = await self.events()
        self.assertEqual([entry["source_id"] for entry in events[0]["data"]["events"]], ["new"])
        query = self.gmail.search.call_args.args[0]
        self.assertIn("in:inbox", query)
        self.assertNotIn("subject:", query)
        self.assertEqual(self.gmail.search.call_args.kwargs["limit"], 20)
        self.assertNotIn("body", str(events[0]["data"]))

    async def test_mail_pagination_persists_window_until_exhausted(self):
        item = await self.create("mail", subject_contains="notice")
        await self.service.poll_subscription(item["id"])
        baseline = self.state(item["id"])["watermark"]
        self.advance()
        self.gmail.search.return_value = {"items": [{"id": "first", "from": "sender@example.com", "subject": "notice", "received_at": _iso(self.now - timedelta(seconds=10))}], "next_page_token": "second-page"}
        self.assertEqual((await self.service.poll_subscription(item["id"]))["status"], "catching_up")
        state = self.state(item["id"])
        self.assertEqual(state["watermark"], baseline)
        fixed_end = state["window_end"]
        self.advance()
        self.gmail.search.return_value = {"items": [{"id": "second", "from": "sender@example.com", "subject": "notice", "received_at": fixed_end}]}
        await self.service.poll_subscription(item["id"])
        self.assertEqual(self.gmail.search.call_args.kwargs["page_token"], "second-page")
        self.assertEqual(self.state(item["id"])["watermark"], fixed_end)
        self.assertNotIn("cursor", self.state(item["id"]))
        await self.service.important_events.flush()
        self.assertEqual(len((await self.events())[0]["data"]["events"]), 2)

    async def test_ci_failure_only_branch_attempt_and_old_watermark(self):
        item = await self.create("github_ci", branch="main", conclusions=["failure"])
        self.registry.responses[("github", "workflow_runs")] = {"items": [self.run_item(1)], "has_more": False}
        await self.service.poll_subscription(item["id"])
        self.assertEqual(self.queued(item["id"]), [])
        base = self.now
        self.advance()
        self.registry.responses[("github", "workflow_runs")] = {"items": [self.run_item(1, updated_at=_iso(base - timedelta(seconds=1))), self.run_item(2, "success"), self.run_item(3), self.run_item(4, "timed_out"), self.run_item(5, branch="feature")], "has_more": False}
        await self.service.poll_subscription(item["id"])
        await self.service.important_events.flush()
        self.assertEqual([entry["source_id"] for entry in (await self.events())[0]["data"]["events"]], ["3"])
        self.advance()
        self.registry.responses[("github", "workflow_runs")] = {"items": [self.run_item(3, attempt=2)], "has_more": False}
        await self.service.poll_subscription(item["id"])
        await self.service.important_events.flush()
        self.assertEqual(len(await self.events()), 2)
        self.service.db.execute("DELETE FROM p_notification_seen")
        self.advance(300)
        # 先让一次空窗口推进水位，再重新读到旧失败，模拟去重保留期已结束。
        self.registry.responses[("github", "workflow_runs")] = {"items": [], "has_more": False}
        await self.service.poll_subscription(item["id"])
        self.advance()
        self.registry.responses[("github", "workflow_runs")] = {"items": [self.run_item(3, attempt=2, updated_at=_iso(base + timedelta(seconds=120)))], "has_more": False}
        await self.service.poll_subscription(item["id"])
        await self.service.important_events.flush()
        self.assertEqual(len(await self.events()), 2)

    async def test_pause_clears_queued_notifications_and_blocks_read(self):
        item = await self.create("meeting", merge_window_s=30)
        self.calendar([{"id": "paused", "summary": "待暂停", "start": _iso(self.now + timedelta(minutes=5))}])
        await self.service.poll_subscription(item["id"])
        self.assertEqual(len(self.queued(item["id"])), 1)
        await self.service.subscription_action({"id": item["id"], "revision": 1, "action": "pause", "idempotency_key": uuid.uuid4().hex})
        self.advance()
        self.assertEqual((await self.service.poll_subscription(item["id"]))["status"], "paused")
        await self.service.important_events.flush()
        self.assertEqual(await self.events(), [])
        self.assertEqual(self.queued(item["id"]), [])
        self.assertEqual(len(self.registry.calls), 1)

    async def test_merge_deadline_global_rate_limit_and_meeting_priority(self):
        mail = await self.create("mail", subject_contains="notice", merge_window_s=30)
        meeting = await self.create("meeting", merge_window_s=30)
        await self.service.poll_subscription(mail["id"])
        self.advance()
        self.gmail.search.return_value = {"items": [{"id": str(index), "from": "s@example.com", "subject": "notice", "received_at": _iso(self.now)} for index in range(3)]}
        await self.service.poll_subscription(mail["id"])
        self.calendar([{"id": "soon", "summary": "优先会议", "start": _iso(self.now + timedelta(minutes=2))}])
        await self.service.poll_subscription(meeting["id"])
        await self.service.important_events.flush()
        self.assertEqual(await self.events(), [])
        self.advance(30)
        await self.service.important_events.flush()
        events = await self.events()
        self.assertEqual(len(events), 1)
        self.assertIn("会议", events[0]["title"])
        await self.service.important_events.flush()
        self.assertEqual(len(await self.events()), 1)
        self.advance(60)
        await self.service.important_events.flush()
        events = await self.events()
        self.assertEqual(len(events), 2)
        mail_event = next(event for event in events if "邮件" in event["title"])
        self.assertEqual(len(mail_event["data"]["events"]), 3)

    async def test_frozen_batch_restarts_without_redelivering_recorded_event(self):
        item = await self.create("mail", subject_contains="notice")
        row = self.service.db.execute("SELECT * FROM p_subscriptions WHERE id=?", (item["id"],)).fetchone()
        options = item["filters"]
        self.service.important_events.enqueue(row, options, "gmail:a", {"title": "notice a", "detail": "模拟元数据"})
        original_publish = self.service._publish
        async def publish_then_crash(*args, **kwargs):
            await original_publish(*args, **kwargs)
            raise RuntimeError("模拟事件落盘后进程中断")
        self.service._publish = publish_then_crash
        with self.assertRaisesRegex(RuntimeError, "进程中断"):
            await self.service.important_events.flush()
        batch_id = self.queued(item["id"])[0]["batch_id"]
        self.assertTrue(batch_id)
        self.assertEqual(self.runtime.send_screen_card.await_count, 1)
        await self.service.close()
        self.service = ProductivityService(self.config, self.store, self.runtime, NoNetwork())
        self.advance(60)
        row = self.service.db.execute("SELECT * FROM p_subscriptions WHERE id=?", (item["id"],)).fetchone()
        self.service.important_events.enqueue(row, options, "gmail:b", {"title": "notice b", "detail": "后来新增"})
        await self.service.important_events.flush()
        self.assertEqual(self.runtime.send_screen_card.await_count, 1)
        self.assertEqual([entry["event_key"] for entry in self.queued(item["id"])], ["gmail:b"])
        self.advance(60)
        await self.service.important_events.flush()
        self.assertEqual(self.runtime.send_screen_card.await_count, 2)
        self.assertEqual(len(await self.events()), 2)
        self.service.important_events.enqueue(row, options, "gmail:a", {"title": "旧事件", "detail": "重复读取"})
        self.assertEqual(self.queued(item["id"]), [])

    async def test_missing_credentials_unknown_backoff_and_manual_rate_limit(self):
        item = await self.create("mail", subject_contains="notice")
        self.gmail.status = lambda: {"configured": False, "authorized": False}
        with self.assertLogs("assistant.important_events", level="ERROR"):
            result = await self.service.poll_subscription(item["id"])
        self.assertEqual(result["status"], "unknown")
        self.assertEqual(result["retry_after_s"], 120)
        self.assertNotIn("watermark", self.state(item["id"]))
        self.assertEqual((await self.service.poll_subscription(item["id"]))["status"], "not_due")
        self.gmail.search.assert_not_awaited()
        self.advance(120)
        result = await self.service.poll_subscription(item["id"])
        self.assertEqual(result["retry_after_s"], 240)
        await self.service.important_events.flush()
        self.assertEqual(await self.events(), [])


if __name__ == "__main__":
    unittest.main()
