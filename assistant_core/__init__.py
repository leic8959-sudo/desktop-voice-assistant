"""桌面助手的持久化领域能力。"""

from .store import (
    AssistantStore,
    IdempotencyConflict,
    LeaseConflict,
    NotFoundError,
)

__all__ = ["AssistantStore", "IdempotencyConflict", "LeaseConflict", "NotFoundError"]
