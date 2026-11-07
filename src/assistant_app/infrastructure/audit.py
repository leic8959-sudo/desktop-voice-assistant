"""本地结构化审计日志，默认脱敏且有界轮转。"""

from __future__ import annotations

import json
import logging
import hashlib
from datetime import datetime, timezone
from logging.handlers import RotatingFileHandler
from pathlib import Path


def create_audit_logger(directory: str | Path) -> logging.Logger:
    directory = Path(directory).resolve()
    logger_name = "assistant.audit." + hashlib.sha1(str(directory).encode(), usedforsecurity=False).hexdigest()[:12]
    logger = logging.getLogger(logger_name)
    if logger.handlers:
        return logger
    directory.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(directory / "audit.log", maxBytes=5_000_000, backupCount=5, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    logger.propagate = False
    return logger


def record(logger: logging.Logger, *, action: str, outcome: str, **fields: object) -> None:
    safe_fields = {key: ("[redacted]" if any(secret in key.lower() for secret in ("token", "secret", "password", "authorization", "api_key")) else value) for key, value in fields.items()}
    logger.info(json.dumps({"action": action, "outcome": outcome, **safe_fields, "timestamp": datetime.now(timezone.utc).isoformat()}, ensure_ascii=False, default=str))


def read_recent(path: Path, *, limit: int = 30, max_bytes: int = 65_536) -> dict:
    # 固定读取预算，跳过窗口起点的残行和末尾尚未写完的记录。
    try:
        with path.open("rb") as stream:
            size = stream.seek(0, 2)
            start = max(0, size - max_bytes)
            previous = b"\n"
            if start:
                stream.seek(start - 1)
                previous = stream.read(1)
            else:
                stream.seek(0)
            content = stream.read(max_bytes)
    except FileNotFoundError:
        return {"items": [], "limit": limit, "max_bytes": max_bytes, "truncated": False}
    lines = content.splitlines()
    if start and previous != b"\n" and lines:
        lines.pop(0)
    incomplete = bool(content and not content.endswith(b"\n"))
    if incomplete and lines:
        lines.pop()
    return {"items": [line.decode("utf-8", errors="replace") for line in lines[-limit:]], "limit": limit, "max_bytes": max_bytes, "truncated": bool(start or incomplete or len(lines) > limit)}


def close_audit_logger(logger: logging.Logger) -> None:
    for handler in logger.handlers[:]:
        handler.flush()
        handler.close()
        logger.removeHandler(handler)
