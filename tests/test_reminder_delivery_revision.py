"""提醒投递版本/租约闭环：真实SQLite、运行时和设备协议，设备仅本地替身。"""

import asyncio
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import aiohttp

from assistant_core import AssistantStore, IdempotencyConflict, LeaseConflict
from assistant_runtime import AssistantRuntime, NativeDevice

NOW = datetime(2026, 11, 5, 8, 0, tzinfo=timezone.utc)


class ReminderSocket:
    def __init__(self):
        self.closed = False
        self.messages = []
        self.notification = asyncio.Event()

    async def send_json(self, event):
        self.messages.append(event)
        if event.get('type') == 'notification':
            self.notification.set()


class ReminderDeliveryRevisionTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.store = AssistantStore(Path(self.directory.name) / 'reminders.sqlite3', 'offline-revision')
        config = {'runtime_dir':self.directory.name, 'device_id':'offline-revision', 'aliyun_model':'offline', 'aliyun_voice':'Tina'}
        self.runtime = AssistantRuntime(config, self.store, SimpleNamespace(configured=False))
        self.socket = ReminderSocket()
        self.device = NativeDevice(self.socket, self.runtime, lambda _socket: None)
        self.device.screen_cards_supported = True
        self.runtime.native_devices.add(self.device)
        self.sequence = 0

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def create_due(self, created=NOW - timedelta(minutes=1)):
        self.sequence += 1
        with patch('assistant_core.store._utc_now', return_value=created):
            return self.store.create_task('离线提醒版本', (NOW - timedelta(minutes=2)).isoformat(), f'offline-revision-{self.sequence}')

    def current(self, task_id):
        return next(item for item in self.store.list_tasks()['tasks'] if item['task_id'] == task_id)

    async def event(self, payload):
        await self.device.handle(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(payload)))

    async def delivered(self):
        class Clock(datetime):
            @classmethod
            def now(cls, tz=None):
                return NOW.astimezone(tz) if tz else NOW.replace(tzinfo=None)

        with patch('assistant_core.store._utc_now', return_value=NOW), patch('assistant_runtime.datetime', Clock):
            loop = asyncio.create_task(self.runtime.notifications_loop())
            try:
                await asyncio.wait_for(self.socket.notification.wait(), 1)
                return next(item['notification'] for item in self.socket.messages if item.get('type') == 'notification')
            finally:
                loop.cancel()
                await asyncio.gather(loop, return_exceptions=True)

    async def test_notification_payload_is_the_committed_lease_snapshot(self):
        original = self.create_due()
        payload = await self.delivered()
        saved = self.current(original['task_id'])
        for field in ('updated_at', 'reminder_state', 'lease_token', 'lease_until', 'due_at', 'title'):
            self.assertEqual(payload[field], saved[field], f'投递字段{field}必须与提交后数据库一致')
        self.assertNotEqual(payload['updated_at'], original['updated_at'])
        self.assertEqual(payload['status'], 'pending_delivery')
        self.assertEqual(saved['status'], 'pending')

    async def test_expired_lease_reclaim_changes_token_and_revision_and_old_ack_is_rejected(self):
        original = self.create_due()
        first = self.store.claim_due_reminders(NOW, lease_seconds=1)[0]
        second = self.store.claim_due_reminders(NOW + timedelta(seconds=2), lease_seconds=60)[0]
        self.assertNotEqual(first['lease_token'], second['lease_token'])
        self.assertNotEqual(first['updated_at'], second['updated_at'])
        self.assertEqual(second['updated_at'], self.current(original['task_id'])['updated_at'])
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(seconds=3)):
            with self.assertRaises(LeaseConflict):
                self.store.ack_reminder(original['task_id'], first['lease_token'])
            acknowledged = self.store.ack_reminder(original['task_id'], second['lease_token'])
        self.assertEqual(acknowledged['reminder_state'], 'acked')
        self.assertEqual(acknowledged['status'], 'pending')
        self.assertIsNone(acknowledged['completed_at'])

    async def test_claim_version_advances_with_fixed_or_rollback_clock(self):
        for created in (NOW, NOW + timedelta(hours=1)):
            with self.subTest(created=created):
                original = self.create_due(created)
                claimed = self.store.claim_due_reminders(NOW, lease_seconds=60)[0]
                saved = self.current(original['task_id'])
                self.assertGreater(saved['updated_at'], original['updated_at'])
                self.assertEqual(claimed['updated_at'], saved['updated_at'])

    async def test_native_ack_sends_authoritative_revision_then_snooze_uses_it(self):
        original = self.create_due()
        payload = await self.delivered()
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(seconds=1)):
            await self.event({'type':'notification_ack', 'id':payload['id'], 'lease_token':payload['lease_token']})
        revision = next(item['revision'] for item in self.socket.messages if item.get('type') == 'reminder_state')
        after_ack = self.current(original['task_id'])
        self.assertEqual(revision, after_ack['updated_at'])
        self.assertEqual(after_ack['status'], 'pending')
        self.assertEqual(after_ack['reminder_state'], 'acked')
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(seconds=2)):
            with self.assertRaises(IdempotencyConflict):
                await self.event({'type':'screen_action', 'action':'snooze', 'reference_id':payload['id'], 'revision':payload['updated_at']})
            self.assertEqual(self.current(original['task_id']), after_ack)
            await self.event({'type':'screen_action', 'action':'snooze', 'reference_id':payload['id'], 'revision':revision})
        saved = self.current(original['task_id'])
        self.assertEqual(saved['status'], 'pending')
        self.assertEqual(saved['reminder_state'], 'pending')
        self.assertEqual(saved['due_at'], '2026-11-05T08:05:02.000000Z')
        self.assertTrue(any(item.get('type') == 'screen_action_result' and item.get('status') == 'accepted' for item in self.socket.messages))

    async def test_ack_version_advances_when_clock_is_fixed_or_moves_back(self):
        original = self.create_due(NOW)
        claimed = self.store.claim_due_reminders(NOW, lease_seconds=60)[0]
        before_ack = self.current(original['task_id'])
        with patch('assistant_core.store._utc_now', return_value=NOW - timedelta(seconds=1)):
            acknowledged = self.store.ack_reminder(original['task_id'], claimed['lease_token'])
        self.assertGreater(acknowledged['updated_at'], before_ack['updated_at'])
        self.assertEqual(acknowledged['status'], 'pending')
        self.assertEqual(acknowledged['reminder_state'], 'acked')

    async def test_fixed_clock_claim_ack_snooze_versions_advance_and_stale_replays_fail(self):
        original = self.create_due(NOW)
        with patch('assistant_core.store._utc_now', return_value=NOW):
            claimed = self.store.claim_due_reminders(NOW, lease_seconds=60)[0]
            claimed_saved = self.current(original['task_id'])
            acknowledged = self.store.ack_reminder(original['task_id'], claimed['lease_token'])
            with self.assertRaises(IdempotencyConflict):
                self.store.snooze_reminder(original['task_id'], claimed['updated_at'])
            snoozed = self.store.snooze_reminder(original['task_id'], acknowledged['updated_at'])
            with self.assertRaises(IdempotencyConflict):
                self.store.snooze_reminder(original['task_id'], acknowledged['updated_at'])
            with self.assertRaises(LeaseConflict):
                self.store.ack_reminder(original['task_id'], claimed['lease_token'])
        self.assertGreater(claimed_saved['updated_at'], original['updated_at'])
        self.assertEqual(claimed['updated_at'], claimed_saved['updated_at'])
        self.assertGreater(acknowledged['updated_at'], claimed['updated_at'])
        self.assertGreater(snoozed['updated_at'], acknowledged['updated_at'])
        self.assertEqual(claimed['lease_until'], '2026-11-05T08:01:00.000000Z')
        self.assertEqual(snoozed['due_at'], '2026-11-05T08:05:00.000000Z')
        self.assertEqual(snoozed['status'], 'pending')

    async def test_implicit_claim_clock_is_read_only_after_write_transaction_begins(self):
        original = self.create_due()

        def acquired_clock():
            self.assertTrue(self.store.connection.in_transaction, '领取时间必须在写事务取得后读取')
            return NOW

        with patch('assistant_core.store._utc_now', side_effect=acquired_clock) as clock:
            claimed = self.store.claim_due_reminders(lease_seconds=60)[0]
        clock.assert_called_once()
        self.assertEqual(claimed['updated_at'], self.current(original['task_id'])['updated_at'])
        self.assertEqual(claimed['lease_until'], '2026-11-05T08:01:00.000000Z')

    async def test_ack_checks_expiry_using_one_clock_read_after_write_lock(self):
        original = self.create_due()
        claimed = self.store.claim_due_reminders(NOW, lease_seconds=1)[0]
        before = self.current(original['task_id'])

        def expired_after_lock():
            self.assertTrue(self.store.connection.in_transaction, 'ACK有效期必须在写事务取得后判断')
            return NOW + timedelta(seconds=2)

        with patch('assistant_core.store._utc_now', side_effect=expired_after_lock) as clock:
            with self.assertRaises(LeaseConflict):
                self.store.ack_reminder(original['task_id'], claimed['lease_token'])
        clock.assert_called_once()
        self.assertEqual(self.current(original['task_id']), before)
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(microseconds=1)) as clock:
            acknowledged = self.store.ack_reminder(original['task_id'], claimed['lease_token'])
        clock.assert_called_once()
        self.assertEqual(acknowledged['reminder_state'], 'acked')
        self.assertGreater(acknowledged['updated_at'], claimed['updated_at'])

    async def test_snooze_clock_and_due_time_are_checked_after_write_lock(self):
        original = self.create_due()

        def acquired_clock():
            self.assertTrue(self.store.connection.in_transaction, '稍后提醒时间必须在写事务取得后读取')
            return NOW

        with patch('assistant_core.store._utc_now', side_effect=acquired_clock) as clock:
            result = self.store.snooze_reminder(original['task_id'], original['updated_at'], minutes=5)
        clock.assert_called_once()
        self.assertEqual(result['due_at'], '2026-11-05T08:05:00.000000Z')
        self.assertGreater(result['updated_at'], original['updated_at'])


if __name__ == '__main__':
    unittest.main()
