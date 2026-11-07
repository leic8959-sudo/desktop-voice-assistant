import os
import sqlite3
import tempfile
import threading
import time
import unittest
from datetime import datetime, timedelta, timezone

import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

from assistant_core import AssistantStore, IdempotencyConflict, LeaseConflict


class AssistantStoreTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.directory.name, "assistant.sqlite3")
        self.store = AssistantStore(self.path, "device-1")

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def test_reminder_survives_restart_and_ack_is_explicit(self):
        current = datetime.now(timezone.utc)
        due = (current - timedelta(seconds=1)).isoformat()
        task = self.store.create_task("给家人打电话", due, "call-1")
        claimed = self.store.claim_due_reminders(current, lease_seconds=30)
        self.assertEqual(claimed[0]["task_id"], task["task_id"])
        self.assertEqual(claimed[0]["reminder_state"], "leased")
        token = claimed[0]["lease_token"]
        self.store.close()
        reopened = AssistantStore(self.path, "device-1")
        self.assertEqual(reopened.list_tasks()["tasks"][0]["reminder_state"], "leased")
        with self.assertRaises(LeaseConflict):
            reopened.ack_reminder(task["task_id"], "wrong-token")
        acknowledged = reopened.ack_reminder(task["task_id"], token)
        self.assertEqual(acknowledged["reminder_state"], "acked")
        reopened.close()
        self.store = AssistantStore(self.path, "device-1")
        self.assertEqual(self.store.claim_due_reminders(datetime.now(timezone.utc)), [])

    def test_concurrent_claim_only_one_wins_and_expired_lease_recovers(self):
        task = self.store.create_task("提醒", "2026-10-29T01:00:00Z", "lease-1")
        results = []

        def claim():
            with AssistantStore(self.path, "device-1") as store:
                results.append(store.claim_due_reminders("2026-10-29T01:00:01Z", lease_seconds=1))

        threads = [threading.Thread(target=claim) for _ in range(2)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(sum(len(item) for item in results), 1)
        recovered = self.store.claim_due_reminders("2026-10-29T01:00:03Z", lease_seconds=30)
        self.assertEqual(recovered[0]["task_id"], task["task_id"])

    def test_idempotency_replay_and_conflict(self):
        first = self.store.create_task("买牛奶", None, "same")
        replay = self.store.create_task("买牛奶", None, "same")
        self.assertEqual(first["task_id"], replay["task_id"])
        with self.assertRaises(IdempotencyConflict):
            self.store.create_task("买咖啡", None, "same")

    def test_delete_memory_is_not_returned_and_capacity_is_enforced(self):
        store = AssistantStore(self.path, "device-1", max_memories=1)
        store.remember_fact("name", "牛牛")
        store.delete_memory(key="name")
        self.assertEqual(store.recall_memory(key="name")["memories"], [])
        store.remember_fact("city", "上海")
        with self.assertRaises(ValueError):
            store.remember_fact("other", "北京")
        store.close()

    def test_business_timezone_boundary_is_stored_as_utc(self):
        task = self.store.create_task("边界提醒", "2026-10-29T00:00:00+08:00", "boundary")
        self.assertEqual(task["due_at"], "2026-10-28T16:00:00.000000Z")
        self.assertEqual(self.store.claim_due_reminders("2026-10-28T15:59:59Z"), [])
        self.assertEqual(len(self.store.claim_due_reminders("2026-10-28T16:00:00Z")), 1)

    def test_legacy_facts_migrate_once_and_deleted_memory_does_not_return(self):
        self.store.close()
        connection = sqlite3.connect(self.path)
        connection.execute("CREATE TABLE facts (device TEXT, key TEXT, value TEXT, updated TEXT, PRIMARY KEY(device,key))")
        connection.execute("INSERT INTO facts VALUES (?,?,?,?)", ("device-1", "legacy", "value", "2026-10-01T00:00:00+00:00"))
        connection.commit()
        connection.close()
        self.store = AssistantStore(self.path, "device-1")
        memory = self.store.recall_memory(key="legacy")["memories"][0]
        self.store.delete_memory(memory_id=memory["memory_id"])
        self.store.close()
        self.store = AssistantStore(self.path, "device-1")
        self.assertEqual(self.store.recall_memory(key="legacy")["memories"], [])

    def test_memory_and_tasks_are_isolated_by_device_and_cursor_has_no_duplicates(self):
        self.store.remember_fact("scope", "one")
        other = AssistantStore(self.path, "device-2")
        other.remember_fact("scope", "two")
        self.assertEqual(self.store.recall_memory(key="scope")["memories"][0]["value"], "one")
        self.assertEqual(other.recall_memory(key="scope")["memories"][0]["value"], "two")
        self.store.create_task("private task", None, "private-1")
        self.assertEqual(other.list_tasks()["tasks"], [])
        for index in range(3):
            self.store.create_task(f"task-{index}", None, f"cursor-{index}")
        page = self.store.list_tasks(limit=1)
        seen = [item["task_id"] for item in page["tasks"]]
        while page["next_cursor"]:
            page = self.store.list_tasks(limit=1, cursor=page["next_cursor"])
            seen.extend(item["task_id"] for item in page["tasks"])
        self.assertEqual(len(seen), len(set(seen)))
        other.close()

    def test_prune_keeps_pending_task_after_reminder_ack(self):
        current = datetime.now(timezone.utc)
        task = self.store.create_task("待确认提醒对应的待办", (current - timedelta(seconds=1)).isoformat(), "acked-pending")
        claimed = self.store.claim_due_reminders(current, lease_seconds=30)[0]
        self.store.ack_reminder(task["task_id"], claimed["lease_token"])
        self.assertEqual(self.store.prune_tasks(current + timedelta(days=365), retention_days=90), 0)
        self.assertEqual(self.store.list_tasks()["tasks"][0]["status"], "pending")


if __name__ == "__main__":
    unittest.main()
