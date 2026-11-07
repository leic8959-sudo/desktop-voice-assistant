"""临时 SQLite 管理员会话验证，不访问真实登录和凭据。"""

import hashlib
import sqlite3
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
from pathlib import Path
from unittest.mock import patch

from src.assistant_app.infrastructure.admin_sessions import (
    MAX_SESSIONS,
    SESSION_TTL_SECONDS,
    AdminSessionCapacityError,
    AdminSessionStoreError,
    PersistentAdminSessions,
)


class PersistentAdminSessionsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "sessions.sqlite3"
        self.binding = "synthetic-device-credential"
        self.store = PersistentAdminSessions(self.path, self.binding)

    def tearDown(self):
        self.directory.cleanup()

    def test_lazy_creation_and_cookie_survives_new_instance(self):
        self.assertFalse(self.path.exists())
        with patch("src.assistant_app.infrastructure.admin_sessions.time.time", return_value=1000):
            token, expires = self.store.issue()
            self.assertEqual(expires, 1000 + SESSION_TTL_SECONDS)
            self.assertTrue(PersistentAdminSessions(self.path, self.binding).valid(token))

    def test_expiry_is_absolute_and_issue_prunes_expired(self):
        clock = "src.assistant_app.infrastructure.admin_sessions.time.time"
        with patch(clock, return_value=1000):
            token, expires = self.store.issue()
        with patch(clock, return_value=expires - 1):
            self.assertTrue(self.store.valid(token))
        with patch(clock, return_value=expires):
            self.assertFalse(self.store.valid(token))
            next_token, _ = self.store.issue()
            self.assertTrue(self.store.valid(next_token))
        with closing(sqlite3.connect(self.path)) as connection:
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM admin_sessions").fetchone()[0], 1)

    def test_changed_binding_invalidates_and_prunes_old_sessions(self):
        token, _ = self.store.issue()
        changed = PersistentAdminSessions(self.path, "different-synthetic-credential")
        self.assertFalse(changed.valid(token))
        replacement, _ = changed.issue()
        self.assertTrue(changed.valid(replacement))
        self.assertFalse(self.store.valid(token))
        self.assertFalse(self.store.valid(replacement))

    def test_only_digests_and_epoch_are_saved(self):
        token, expires = self.store.issue()
        with closing(sqlite3.connect(self.path)) as connection:
            row = connection.execute("SELECT token_hash, credential_binding, expires_at FROM admin_sessions").fetchone()
        self.assertEqual(row, (hashlib.sha256(token.encode()).hexdigest(), hashlib.sha256(self.binding.encode()).hexdigest(), expires))
        for file in self.path.parent.iterdir():
            data = file.read_bytes()
            self.assertNotIn(token.encode(), data)
            self.assertNotIn(self.binding.encode(), data)

    def test_capacity_is_bounded_across_concurrent_instances(self):
        def issue(_):
            try:
                return PersistentAdminSessions(self.path, self.binding).issue()[0]
            except AdminSessionCapacityError:
                return None

        with ThreadPoolExecutor(max_workers=8) as executor:
            results = list(executor.map(issue, range(MAX_SESSIONS + 8)))
        tokens = [token for token in results if token]
        self.assertEqual(len(tokens), MAX_SESSIONS)
        self.assertEqual(len(set(tokens)), MAX_SESSIONS)
        with self.assertRaises(AdminSessionCapacityError):
            self.store.issue()

    def test_revoke_survives_reload_and_invalid_inputs_fail_closed(self):
        token, _ = self.store.issue()
        self.store.revoke(token)
        self.assertFalse(PersistentAdminSessions(self.path, self.binding).valid(token))
        for value in (None, "", "x" * 1000, "?'" * 22):
            self.assertFalse(self.store.valid(value))
            self.store.revoke(value)

    def test_save_failure_raises_without_issuing_cookie(self):
        with patch.object(self.store, "_connect", side_effect=sqlite3.OperationalError("synthetic disk failure")):
            with self.assertLogs("src.assistant_app.infrastructure.admin_sessions", level="ERROR") as logs:
                with self.assertRaises(AdminSessionStoreError):
                    self.store.issue()
            self.assertIn("Traceback", logs.output[0])
            self.assertFalse(self.path.exists())

    def test_insert_failure_rolls_back_pruning(self):
        token, _ = self.store.issue()
        with closing(sqlite3.connect(self.path)) as connection, connection:
            connection.execute("CREATE TRIGGER reject_insert BEFORE INSERT ON admin_sessions BEGIN SELECT RAISE(ABORT, 'synthetic save failure'); END")
        with self.assertLogs("src.assistant_app.infrastructure.admin_sessions", level="ERROR"):
            with self.assertRaises(AdminSessionStoreError):
                self.store.issue()
        self.assertTrue(self.store.valid(token))

    def test_commit_failure_does_not_return_or_persist_session(self):
        class FailingCommitConnection(sqlite3.Connection):
            def __exit__(self, exception_type, exception, traceback):
                self.rollback()
                raise sqlite3.OperationalError("synthetic commit failure")

        connection = sqlite3.connect(self.path, factory=FailingCommitConnection)
        connection.execute("CREATE TABLE admin_sessions (token_hash TEXT PRIMARY KEY, credential_binding TEXT, expires_at INTEGER)")
        with patch.object(self.store, "_connect", return_value=connection):
            with self.assertLogs("src.assistant_app.infrastructure.admin_sessions", level="ERROR"):
                with self.assertRaises(AdminSessionStoreError):
                    self.store.issue()
        with closing(sqlite3.connect(self.path)) as reopened:
            self.assertEqual(reopened.execute("SELECT COUNT(*) FROM admin_sessions").fetchone()[0], 0)

    def test_corrupt_database_fails_closed_with_complete_error(self):
        self.path.write_bytes(b"synthetic corrupt database")
        with self.assertLogs("src.assistant_app.infrastructure.admin_sessions", level="ERROR") as logs:
            self.assertFalse(self.store.valid("a" * 43))
        self.assertIn("Traceback", logs.output[0])
        with self.assertLogs("src.assistant_app.infrastructure.admin_sessions", level="ERROR"):
            with self.assertRaises(AdminSessionStoreError):
                self.store.issue()


if __name__ == "__main__":
    unittest.main()
