"""原生连接器的有界 JSON 响应边界。"""

import json


class ConnectorHTTPError(RuntimeError):
    def __init__(self, provider, status):
        super().__init__(f"{provider} 请求失败: {status}")
        self.status = status


async def read_json(response, provider):
    if response.status >= 300:
        raise ConnectorHTTPError(provider, response.status)
    raw = bytearray()
    async for chunk in response.content.iter_chunked(8192):
        raw.extend(chunk)
        if len(raw) > 524288:
            raise RuntimeError(f"{provider} 响应超过512 KiB，请缩小查询范围")
    try:
        return json.loads(raw)
    except (ValueError, UnicodeDecodeError) as error:
        raise RuntimeError(f"{provider} 返回了无效 JSON") from error
