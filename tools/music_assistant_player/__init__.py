"""Music Assistant 2.10.9 的本地播放器扩展，不包含音乐服务凭据。"""

from .provider import XiaoyanziProvider


async def setup(mass, manifest, config):
    return XiaoyanziProvider(mass, manifest, config, set())
