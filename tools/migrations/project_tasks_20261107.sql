-- 先停止网关并备份 SQLite，再由管理员针对明确的数据库路径离线执行。
-- 本文件不会由应用启动加载；metadata 沿用既有 JSON 对象。
BEGIN IMMEDIATE;
CREATE TABLE IF NOT EXISTS project_task_sequences (
    device_id TEXT NOT NULL,
    project TEXT NOT NULL,
    next_position INTEGER NOT NULL CHECK(next_position > 0),
    PRIMARY KEY(device_id, project)
);
CREATE TABLE IF NOT EXISTS project_task_batches (
    device_id TEXT NOT NULL,
    idempotency_key TEXT NOT NULL,
    request_hash TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    PRIMARY KEY(device_id, idempotency_key)
);
CREATE INDEX IF NOT EXISTS project_task_batches_expiry ON project_task_batches(device_id, expires_at);
CREATE INDEX IF NOT EXISTS tasks_device_project_due
    ON tasks(device_id, json_extract(metadata,'$.project'), (due_at IS NULL), due_at, task_id);
CREATE INDEX IF NOT EXISTS tasks_device_project_status_due
    ON tasks(device_id, json_extract(metadata,'$.project'), status, (due_at IS NULL), due_at, task_id);
COMMIT;
