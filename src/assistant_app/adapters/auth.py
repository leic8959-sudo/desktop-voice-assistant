"""当前单设备令牌认证适配器，后续可替换为用户/角色令牌服务。"""

from __future__ import annotations

import hmac
from dataclasses import dataclass


def extract_bearer(value: str) -> str | None:
    scheme, separator, token = value.partition(" ")
    if not separator or scheme.lower() != "bearer" or not token:
        return None
    return token


def token_matches(value: str, expected: str) -> bool:
    token = extract_bearer(value)
    return token is not None and hmac.compare_digest(token, expected)
@dataclass(frozen=True)
class Principal:
    subject: str
    roles: frozenset[str]

    def has_role(self, role: str) -> bool:
        return role in self.roles



def authenticate(value: str, expected: str) -> Principal | None:
    return Principal(subject="device", roles=frozenset({"admin"})) if token_matches(value, expected) else None
