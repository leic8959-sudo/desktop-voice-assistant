"""检查工作区是否包含明显不应发布的凭据或运行时产物。"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


TEXT_SUFFIXES = {".py", ".json", ".md", ".ps1", ".yml", ".yaml", ".toml", ".txt"}
SECRET_PATTERNS = (
    re.compile(r"(?i)(api[_-]?key|access[_-]?token|client[_-]?secret|home_assistant_token)\s*[=:]\s*[\"']?[^\"'\s,}]{12,}"),
    re.compile(r"(?i)bearer\s+[a-z0-9._~-]{20,}"),
)
FORBIDDEN_NAMES = {"config.local.json", ".env", ".env.local"}


def scan(root: Path) -> list[str]:
    findings: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file() or any(part in {".git", ".venv", "__pycache__", "runtime"} for part in path.parts):
            continue
        if "tests" in path.parts or path.stem.endswith("_test"):
            continue
        if path.name in FORBIDDEN_NAMES:
            findings.append(f"敏感配置文件: {path.relative_to(root)}")
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8-sig")
        except (OSError, UnicodeError):
            continue
        for pattern in SECRET_PATTERNS:
            match = pattern.search(text)
            if match and not any(token in match.group(0).lower() for token in ("replace", "example", "dummy", "test-key", "local-test", "mock-", "sk-deepseek-key")):
                findings.append(f"疑似凭据: {path.relative_to(root)}")
                break
    return sorted(set(findings))


def main() -> int:
    parser = argparse.ArgumentParser(description="开源发布前敏感信息检查")
    parser.add_argument("root", nargs="?", default=".")
    args = parser.parse_args()
    findings = scan(Path(args.root).resolve())
    if findings:
        print("发现不可直接发布的内容：")
        print("\n".join(f"- {item}" for item in findings))
        return 1
    print("未发现明显敏感配置或禁止发布文件。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
