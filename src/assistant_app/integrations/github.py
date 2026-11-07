"""GitHub connector for issue search and controlled issue creation."""

from __future__ import annotations

from typing import Any

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope


class GitHubConnector:
    manifest = ConnectorManifest(
        connector_id="github",
        display_name="GitHub",
        version="1.0",
        oauth_scopes=("repo",),
        actions=(ConnectorAction("search_issues", "搜索 GitHub Issue"), ConnectorAction("repository_status", "查询仓库状态"), ConnectorAction("list_pull_requests", "查询 Pull Request"), ConnectorAction("workflow_runs", "查询 CI 运行状态"), ConnectorAction("create_issue", "创建 GitHub Issue", frozenset({PermissionScope.WRITE}), True)),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any]) -> None:
        self.client = client
        self.config = config

    async def _request(self, method: str, path: str, payload: dict[str, Any] | None = None, params: dict[str, Any] | None = None) -> dict[str, Any]:
        token = self.config.get("github_access_token", "")
        if not token:
            raise RuntimeError("GitHub 尚未配置 Access Token")
        headers = {"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
        async with self.client.request(method, f"https://api.github.com{path}", headers=headers, json=payload, params=params, timeout=aiohttp.ClientTimeout(total=20)) as response:
            data = await response.json(content_type=None)
            if response.status >= 400:
                raise RuntimeError(f"GitHub 请求失败: {response.status}")
            return data

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "search_issues":
            query = str(arguments.get("query", "")).strip()
            if not query:
                raise ValueError("query 不能为空")
            data = await self._request("GET", "/search/issues", params={"q": query[:250], "per_page": min(int(arguments.get("limit", 20)), 100)})
            return {"provider": "github", "total": data.get("total_count", 0), "items": [{"id": item.get("id"), "title": item.get("title", ""), "state": item.get("state"), "url": item.get("html_url", ""), "repository": item.get("repository_url", "")} for item in data.get("items", [])]}
        if action == "create_issue":
            repository = str(arguments.get("repository", "")).strip()
            title = str(arguments.get("title", "")).strip()
            if not repository or not title:
                raise ValueError("repository 和 title 不能为空")
            if "/" not in repository:
                raise ValueError("repository 必须是 owner/name")
            labels = arguments.get("labels", [])
            if not isinstance(labels, list) or len(labels) > 20 or any(not isinstance(label, str) for label in labels):
                raise ValueError("labels 必须是最多20个字符串的数组")
            return await self._request("POST", f"/repos/{repository}/issues", {"title": title[:256], "body": str(arguments.get("body", ""))[:10000], "labels": labels})
        if action == "repository_status":
            repository = str(arguments.get("repository", "")).strip()
            if "/" not in repository:
                raise ValueError("repository 必须是 owner/name")
            data = await self._request("GET", f"/repos/{repository}")
            return {"provider": "github", "repository": data.get("full_name"), "description": data.get("description", ""), "visibility": data.get("visibility"), "default_branch": data.get("default_branch"), "open_issues": data.get("open_issues_count", 0), "stars": data.get("stargazers_count", 0), "forks": data.get("forks_count", 0), "url": data.get("html_url", "")}
        if action == "list_pull_requests":
            repository = str(arguments.get("repository", "")).strip()
            if "/" not in repository:
                raise ValueError("repository 必须是 owner/name")
            data = await self._request("GET", f"/repos/{repository}/pulls", params={"state": arguments.get("state", "open"), "per_page": min(int(arguments.get("limit", 20)), 100)})
            return {"provider": "github", "repository": repository, "items": [{"number": item.get("number"), "title": item.get("title", ""), "state": item.get("state"), "draft": item.get("draft", False), "url": item.get("html_url", ""), "author": (item.get("user") or {}).get("login", "")} for item in data]}
        if action == "workflow_runs":
            repository = str(arguments.get("repository", "")).strip()
            if "/" not in repository:
                raise ValueError("repository 必须是 owner/name")
            limit = max(1, min(int(arguments.get("limit", 10)), 50))
            page = int(arguments.get("page", 1))
            if not 1 <= page <= 20:
                raise ValueError("CI分页需为1～20")
            params = {"per_page": limit, "page": page}
            if arguments.get("branch"):
                params["branch"] = str(arguments["branch"])[:120]
            if arguments.get("created_after"):
                params["created"] = str(arguments["created_after"])[:80] + ".." + str(arguments["created_before"])[:80] if arguments.get("created_before") else ">=" + str(arguments["created_after"])[:80]
            data = await self._request("GET", f"/repos/{repository}/actions/runs", params=params)
            items = data.get("workflow_runs", [])
            return {"provider": "github", "repository": repository, "items": [{"id": item.get("id"), "name": item.get("name", ""), "status": item.get("status"), "conclusion": item.get("conclusion"), "branch": item.get("head_branch"), "attempt": item.get("run_attempt", 1), "created_at": item.get("created_at"), "updated_at": item.get("updated_at"), "url": item.get("html_url", "")} for item in items], "has_more": len(items) == limit and data.get("total_count", 0) > page * limit}
        raise KeyError(action)
