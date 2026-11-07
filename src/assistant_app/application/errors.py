"""跨适配器共享的稳定错误模型，避免 API 暴露第三方异常文本。"""

from __future__ import annotations

from dataclasses import dataclass

from assistant_core.store import IdempotencyConflict, LeaseConflict, NotFoundError


@dataclass
class ApplicationError(Exception):
    code: str
    message: str
    retryable: bool = False

    def __str__(self) -> str:
        return self.message


class AuthenticationError(ApplicationError):
    def __init__(self) -> None:
        super().__init__("authentication_failed", "认证失败")


class ConfigurationError(ApplicationError):
    def __init__(self, message: str) -> None:
        super().__init__("configuration_invalid", message)


class LocalToolRejectedError(ApplicationError, PermissionError):
    def __init__(self) -> None:
        super().__init__("local_tool_rejected", "本次操作未获授权，请明确当前请求")


class LocalToolArgumentsError(ApplicationError, ValueError):
    def __init__(self) -> None:
        super().__init__("invalid_tool_arguments", "工具参数无效，请补齐或修正后再请求")


class LocalProjectStorageUnavailableError(ApplicationError, ValueError):
    def __init__(self) -> None:
        super().__init__("project_storage_unavailable", "项目待办尚未准备，请由管理员离线执行迁移；普通待办仍可使用")


class LocalToolNotAvailableError(ApplicationError, ValueError):
    """请求的工具或工作流不存在：调用从未执行，不产生待核实结果。"""

    def __init__(self) -> None:
        super().__init__("tool_not_available", "工具不在允许列表")


class LocalRecordConflictError(ApplicationError, IdempotencyConflict):
    def __init__(self) -> None:
        super().__init__("local_record_conflict", "记录已变化，请重新查询核对；本次未重复写入")


class LocalLeaseConflictError(ApplicationError, LeaseConflict):
    def __init__(self) -> None:
        super().__init__("local_lease_conflict", "提醒租约已失效，请重新查询核对；本次未确认交付")


class LocalRecordNotFoundError(ApplicationError, NotFoundError):
    def __init__(self) -> None:
        super().__init__("local_record_not_found", "本地记录不存在，请重新查询确认目标")


def tool_error_result(error: Exception) -> dict:
    if isinstance(error, (LocalToolRejectedError, LocalToolNotAvailableError, LocalProjectStorageUnavailableError)):
        status = "blocked"
    elif isinstance(error, LocalToolArgumentsError):
        status = "invalid_arguments"
    elif isinstance(error, (LocalRecordConflictError, LocalLeaseConflictError)):
        status = "conflict"
    elif isinstance(error, LocalRecordNotFoundError):
        status = "not_found"
    else:
        return {"ok": False, "status": "unknown", "code": "tool_result_unknown", "retryable": False, "error": "工具执行结果待核实，请先查询实际状态；不会自动重试"}
    return {"ok": False, "status": status, "code": error.code, "retryable": False, "error": error.message}


def tool_result_unconfirmed(result) -> bool:
    if not isinstance(result, dict):
        return False
    if result.get("status") == "completed" and isinstance(result.get("result"), dict):
        return tool_result_unconfirmed(result["result"])
    status = result.get("status")
    return isinstance(status, str) and status in {"unknown", "unconfirmed"} or result.get("request_accepted") is True and result.get("target_observed") is False


def paused_tool_result() -> dict:
    return {"ok": False, "status": "blocked", "code": "tool_chain_paused", "retryable": False, "error": "本轮操作结果待核实，已暂停后续工具；请先查询实际状态"}
