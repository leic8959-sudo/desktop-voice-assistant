"""操作到屏幕反馈的白名单；只产生状态摘要，不复制业务正文或秘密。"""

import re
from dataclasses import dataclass

from .errors import LocalToolRejectedError, LocalRecordConflictError, LocalLeaseConflictError, LocalRecordNotFoundError


@dataclass(frozen=True)
class ScreenOperation:
    title: str
    writing: bool = False
    specialized: bool = False
    outcome: str = ""


TOOLS = {
    "remember_fact": ScreenOperation("保存记忆", True),
    "recall_memory": ScreenOperation("查询记忆"),
    "update_memory": ScreenOperation("更新记忆", True),
    "delete_memory": ScreenOperation("删除记忆", True),
    "create_task": ScreenOperation("创建待办或提醒", True),
    "create_project_tasks": ScreenOperation("记录项目事项", True),
    "update_task_progress": ScreenOperation("更新项目进度", True),
    "list_tasks": ScreenOperation("查询待办"),
    "complete_task": ScreenOperation("完成待办", True),
    "cancel_task": ScreenOperation("取消待办", True),
    "snooze_reminder": ScreenOperation("稍后提醒", True, outcome="snooze"),
    "reschedule_task": ScreenOperation("修改提醒时间", True, outcome="snooze"),
    "rename_task": ScreenOperation("修改待办标题", True, outcome="rename"),
    "gmail_search": ScreenOperation("搜索邮件"),
    "gmail_read_message": ScreenOperation("读取邮件"),
    "gmail_list_drafts": ScreenOperation("查询邮件草稿"),
    "gmail_get_draft": ScreenOperation("读取邮件草稿"),
    "gmail_create_draft": ScreenOperation("创建邮件草稿", True),
    "gmail_update_draft": ScreenOperation("更新邮件草稿", True),
    "search_web": ScreenOperation("联网搜索"),
    "list_home_devices": ScreenOperation("查询家居设备"),
    "get_home_state": ScreenOperation("读取家居状态"),
    "execute_home_action": ScreenOperation("控制家居设备", True),
    "set_home_state": ScreenOperation("设置家居状态", True),
    "control_vacuum": ScreenOperation("控制清洁设备", True),
    "control_climate": ScreenOperation("控制空调", True),
    "start_desktop_timer": ScreenOperation("开始倒计时", True, True),
    "desktop_timer_status": ScreenOperation("查询倒计时"),
    "desktop_timer_action": ScreenOperation("更新倒计时", True, True),
}
WORKFLOWS = {"meeting-travel": "查询会议路线", "meeting-notes": "保存会议纪要", "github-issue": "创建 GitHub Issue", "todoist-task": "创建 Todoist 待办"}


def tool_operation(name, arguments, registry):
    if name in TOOLS:
        return TOOLS[name]
    if name == "invoke_workflow":
        workflow = arguments.get("workflow_id")
        if isinstance(workflow, str) and workflow in WORKFLOWS:
            return ScreenOperation(WORKFLOWS[workflow], workflow != "meeting-travel")
    if name == "invoke_connector":
        connector_id, action = arguments.get("connector_id"), arguments.get("action")
        if not isinstance(connector_id, str) or not isinstance(action, str):
            return None
        for manifest in registry.manifests():
            if manifest.connector_id == connector_id:
                descriptor = next((item for item in manifest.actions if item.name == action), None)
                if descriptor:
                    return ScreenOperation(descriptor.description, descriptor.is_mutating)
    return None


def api_operation(method, path, registry):
    # 状态轮询、审计、绑定、固件下载和后台列表不反复抢占设备屏幕。
    if method == "GET":
        if path in {"/api/gmail/status", "/api/connectors/gmail/status"}:
            return ScreenOperation("Gmail 授权状态", outcome="authorization_status")
        gmail_reads = {"/api/gmail/messages": "搜索邮件", "/api/gmail/drafts": "查询邮件草稿"}
        if path in gmail_reads:
            return ScreenOperation(gmail_reads[path])
        if re.fullmatch(r"/api/gmail/(messages|drafts)/[^/]+", path):
            return ScreenOperation("读取邮件或草稿")
        return None
    direct = {
        ("POST", "/api/tasks"): TOOLS["create_task"],
        ("PUT", "/api/voice"): ScreenOperation("保存音色", True, outcome="preference"),
        ("PUT", "/api/llm"): ScreenOperation("保存对话模型", True, outcome="preference"),
        ("PUT", "/api/settings"): ScreenOperation("保存运行偏好", True, outcome="preference"),
        ("POST", "/api/settings/models"): ScreenOperation("保存模型配置", True, outcome="configuration"),
        ("POST", "/api/gmail/config"): ScreenOperation("保存 Gmail 配置", True, outcome="configuration"),
        ("POST", "/api/screen/today"): ScreenOperation("读取今日安排", specialized=True),
        ("POST", "/api/screen/weather"): ScreenOperation("读取天气", specialized=True),
        ("PUT", "/api/device/preferences"): ScreenOperation("保存设备音量与屏幕偏好", True, outcome="preference"),
        ("POST", "/api/screen/timer"): TOOLS["start_desktop_timer"],
        ("POST", "/api/gmail/drafts"): TOOLS["gmail_create_draft"],
    }
    if (method, path) in direct:
        return direct[(method, path)]
    if re.fullmatch(r"/api/screen/timer/(pause|resume|cancel)", path):
        return ScreenOperation({"pause": "暂停计时", "resume": "继续计时", "cancel": "取消计时"}[path.rsplit("/", 1)[1]], True, True)
    if re.fullmatch(r"/api/tasks/[^/]+/(complete|cancel|snooze|reschedule|rename)", path):
        return TOOLS[{"complete": "complete_task", "cancel": "cancel_task", "snooze": "snooze_reminder", "reschedule": "reschedule_task", "rename": "rename_task"}[path.rsplit("/", 1)[1]]]
    if path.startswith("/api/memories/") and method in {"PUT", "DELETE"}:
        return ScreenOperation("保存记忆" if method == "PUT" else "删除记忆", True)
    if re.fullmatch(r"/api/home/[^/]+/(actions|authorization)", path):
        return ScreenOperation("控制家居设备" if path.endswith("actions") else "更新家居授权", True, path.endswith("actions"))
    if re.fullmatch(r"/api/(gmail|connectors/gmail)/oauth/start", path):
        return ScreenOperation("发起 Google 授权", outcome="oauth")
    if re.fullmatch(r"/api/(gmail|connectors/gmail)/disconnect", path):
        return ScreenOperation("断开 Gmail 授权", True, outcome="disconnect")
    if re.fullmatch(r"/api/gmail/drafts/[^/]+/prepare-send", path):
        return ScreenOperation("邮件发送预览", outcome="confirmation")
    if re.fullmatch(r"/api/gmail/drafts/[^/]+/send", path):
        return ScreenOperation("发送邮件", True)
    if method == "PUT" and re.fullmatch(r"/api/gmail/drafts/[^/]+", path):
        return TOOLS["gmail_update_draft"]
    match = re.fullmatch(r"/api/workflows/([^/]+)(/prepare)?", path)
    if match and match[1] in WORKFLOWS:
        return ScreenOperation(WORKFLOWS[match[1]], match[1] != "meeting-travel", match[1] == "meeting-travel", "confirmation" if match[2] else "")
    match = re.fullmatch(r"/api/connectors/([^/]+)/([^/]+)(/prepare)?", path)
    if match:
        if match[2] == "config":
            return ScreenOperation("保存连接器配置", True, outcome="configuration")
        if match[2] == "check":
            return ScreenOperation("只读连接检查", outcome="check")
        operation = tool_operation("invoke_connector", {"connector_id": match[1], "action": match[2]}, registry)
        if operation and match[3]:
            return ScreenOperation(operation.title, operation.writing, outcome="confirmation")
        return operation
    return None


def feedback_card(operation, result=None, *, phase="result", http_status=200, error=None):
    title, state, detail = operation.title, "completed", "已返回结果，请查看后台或听取语音摘要"
    result = result if isinstance(result, dict) else {}
    if result.get("status") == "completed" and isinstance(result.get("result"), dict):
        # 仅解包作状态判定，不展示提供商正文。
        result = result["result"]
    status = result.get("status") if isinstance(result.get("status"), str) else ""
    if phase == "start":
        state, detail = "processing", "正在处理，请稍候；还没有确认执行结果"
    elif isinstance(error, LocalToolRejectedError) or http_status == 403:
        state, detail = "blocked", "本次操作未获授权，请核对权限或当前请求"
    elif isinstance(error, (LocalRecordConflictError, LocalLeaseConflictError, LocalRecordNotFoundError)):
        state, detail = "error", error.message
    elif error is not None or http_status >= 400 or result.get("ok") is False or result.get("error") or status in {"failed", "error", "rejected", "denied"}:
        state, detail = "error", "操作未完成，请查看后台配置、输入或服务状态"
        if operation.writing and (http_status >= 500 or error is not None):
            state, detail = "unknown", "结果待核实，请先查询提供商状态；不会自动重试写入"
    if status in {"unknown", "unconfirmed"}:
        state, detail = "unknown", "结果待核实，请先核查提供商状态；不会自动重试"
    if state == "completed":
        if status in {"pending_confirmation", "executing"} or operation.outcome == "confirmation":
            state, detail = ("processing", "相同请求仍在执行，请稍候核查") if status == "executing" else ("pending_confirmation", "预览已生成，等待你在后台明确确认；尚未执行")
            if state == "pending_confirmation" and not result.get("confirmation_id"):
                state, detail = "unknown", "未获得服务端确认凭证，请到后台刷新预览"
        elif operation.outcome == "configuration":
            state, detail = ("saved", "配置已保存；授权、连接有效性和生效时间请分别核对") if result.get("saved") is True else ("unknown", "服务端未明确确认配置保存，请刷新后台核查")
        elif operation.outcome == "preference":
            state, detail = "saved", "偏好已保存，下次语音会话生效"
        elif operation.outcome == "oauth":
            state, detail = ("pending_authorization", "授权入口已生成，请在浏览器完成登录后刷新状态") if result.get("authorization_url") else ("unknown", "未获得授权入口，请在后台检查 OAuth 配置")
        elif operation.outcome == "authorization_status":
            if not result.get("configured"):
                state, detail = "unconfigured", "OAuth 客户端尚未配置，请在本机后台保存"
            elif not result.get("authorized"):
                state, detail = "pending_authorization", "客户端已配置，仍需在浏览器完成 Google 授权"
            else:
                state, detail = "authorized", "本地已有授权令牌；尚未实时验证权限和有效性"
        elif operation.outcome == "disconnect":
            state, detail = "completed", "Gmail 本地授权已断开"
        elif operation.outcome == "check":
            state, detail = ("verified", "本次只读连接检查通过，不代表全部权限或长期健康") if status == "verified" else ("unknown", "未获得明确连接验证结果，请到后台核对")
        elif operation.title == "联网搜索":
            state, detail = ("completed", "本次检索已获得服务端确认，请听语音或到后台查看摘要") if result.get("ok") is True and result.get("search_confirmed") is True else ("unknown", "没有获得联网检索确认，不把模型回答当成搜索成功")
        elif "target_observed" in result:
            state, detail = ("observed", "平台报告目标状态；不是独立物理确认") if result["target_observed"] else ("unconfirmed", "指令已提交，平台尚未报告目标状态")
        elif status in {"sent", "already_sent"}:
            detail = "邮件服务确认已发送"
        elif isinstance(result.get("timer"), dict):
            state = result["timer"].get("status", "unknown")
            detail = {"running": "计时中，可在屏幕或后台暂停/取消", "paused": "计时已暂停", "finished": "计时结束", "cancelled": "计时已取消"}.get(state, "计时状态待核对")
        elif isinstance(result.get("forecasts"), list) and result["forecasts"]:
            forecast = result["forecasts"][0]
            casts = forecast.get("casts", []) if isinstance(forecast, dict) else []
            if casts and isinstance(casts[0], dict):
                day = casts[0]
                detail = f"{day.get('dayweather', '天气未提供')} · {day.get('nighttemp', '?')}～{day.get('daytemp', '?')} ℃\n预报日期：{day.get('date', '未知')}"
            else:
                state, detail = "empty", "天气预报内容不完整，请稍后重新读取"
        elif operation.outcome == "rename":
            item = result["item"] if isinstance(result.get("item"), dict) else result
            if item.get("status") == "pending" and isinstance(item.get("title"), str) and item["title"].strip() and item.get("updated_at"):
                state, detail = "saved", "标题已保存，提醒时间不变；待办仍未完成"
            else:
                state, detail = "unknown", "未获得明确的标题保存回执，请刷新待办核对"
        elif operation.outcome == "snooze":
            item = result["item"] if isinstance(result.get("item"), dict) else result
            if item.get("status") == "pending" and item.get("due_at"):
                state, detail = "saved", f"下次提醒：{item['due_at']}（UTC）\n待办仍未完成"
            else:
                state, detail = "unknown", "未获得明确的下次提醒时间，请刷新待办核对"
        elif isinstance(result.get("item"), dict) and result["item"].get("status") in {"pending", "completed", "cancelled"}:
            detail = {"pending": "待办或提醒已保存", "completed": "待办已完成", "cancelled": "待办已取消"}[result["item"]["status"]]
        elif status in {"pending", "cancelled"}:
            detail = "待办或提醒已保存" if result["status"] == "pending" else "待办已取消"
        else:
            collections = [(key, result[key]) for key in ("items", "tasks", "memories", "devices") if isinstance(result.get(key), list)]
            if collections:
                count = len(collections[0][1])
                state, detail = ("completed", f"本次返回 {count} 条；不是全部数量，详情在后台或语音中查看") if count else ("empty", "本次没有返回匹配结果，不代表数据一定不存在")
            elif result.get("found") is False:
                state, detail = "empty", "没有找到可用路线，请核对起终点或换一种查询"
            elif operation.title in {"创建邮件草稿", "更新邮件草稿"}:
                detail = "邮件草稿已保存；尚未发送，发送需后台预览并确认"
            elif operation.writing:
                detail = "已返回操作结果；外部业务状态请在后台或提供商核对"
    return {"kind": "task", "title": title, "status": state, "detail": detail, "ttl_s": 8 if state == "processing" else 12}
