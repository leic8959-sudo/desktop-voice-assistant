"""首轮联调使用电脑侧有界记忆；不是设备 SD 卡实现。"""

import json
import sqlite3
from datetime import datetime, timedelta, timezone


class MemoryStore:
    def __init__(self, path):
        self.connection = sqlite3.connect(path)
        self.connection.execute("CREATE TABLE IF NOT EXISTS facts (device TEXT, key TEXT, value TEXT, updated TEXT, PRIMARY KEY(device, key))")
        self.connection.execute("CREATE INDEX IF NOT EXISTS facts_expiry ON facts(updated)")
        self.connection.commit()

    def prune(self):
        cutoff = (datetime.now(timezone.utc) - timedelta(days=180)).isoformat()
        self.connection.execute("DELETE FROM facts WHERE updated < ?", (cutoff,))
        self.connection.commit()

    def save(self, device, key, value):
        if not isinstance(key, str) or not isinstance(value, str) or not 1 <= len(key) <= 64 or not 1 <= len(value) <= 512:
            raise ValueError("记忆键限 1～64 字，内容限 1～512 字")
        count = self.connection.execute("SELECT count(*) FROM facts WHERE device=?", (device,)).fetchone()[0]
        exists = self.connection.execute("SELECT 1 FROM facts WHERE device=? AND key=?", (device, key)).fetchone()
        if count >= 100 and not exists:
            raise ValueError("本机原型记忆上限 100 条，请先人工清理")
        self.connection.execute("INSERT INTO facts VALUES (?,?,?,?) ON CONFLICT(device,key) DO UPDATE SET value=excluded.value,updated=excluded.updated", (device, key, value, datetime.now(timezone.utc).isoformat()))
        self.connection.commit()
        return {"saved": True, "storage": "pc_prototype", "key": key}

    def recall(self, device, key=None):
        if key:
            rows = self.connection.execute("SELECT key,value FROM facts WHERE device=? AND key=? LIMIT 1", (device, key)).fetchall()
        else:
            rows = self.connection.execute("SELECT key,value FROM facts WHERE device=? ORDER BY updated DESC LIMIT 10", (device,)).fetchall()
        return {"storage": "pc_prototype", "facts": dict(rows)}

    def context(self, device):
        return json.dumps(self.recall(device), ensure_ascii=False)

    def close(self):
        self.connection.close()
