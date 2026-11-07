import json
from aiohttp import web

def register_home_api(app, states_provider, *, device_id='mock-device', entity_ids=(), domain_services=None):
    entity_ids = list(entity_ids)
    domain_services = domain_services or {}
    async def states(request):
        return web.json_response(states_provider())
    async def services(request):
        return web.json_response([{'domain': domain, 'services': {name: {} for name in names}} for domain, names in domain_services.items()])
    async def websocket(request):
        socket = web.WebSocketResponse(max_msg_size=1_000_000)
        await socket.prepare(request)
        await socket.send_json({'type': 'auth_required'})
        async for message in socket:
            if message.type != web.WSMsgType.TEXT:
                continue
            payload = json.loads(message.data)
            if payload.get('type') == 'auth':
                await socket.send_json({'type': 'auth_ok'})
            elif payload.get('type') in ('config/device_registry/list', 'config/entity_registry/list', 'config/area_registry/list'):
                kind = payload['type']
                if kind.endswith('device_registry/list'):
                    result = [{'id': device_id, 'name': 'Mock device', 'name_by_user': 'Mock device', 'area_id': 'room'}]
                elif kind.endswith('entity_registry/list'):
                    result = [{'entity_id': entity_id, 'device_id': device_id, 'area_id': 'room', 'name': entity_id} for entity_id in entity_ids]
                else:
                    result = [{'area_id': 'room', 'name': '测试房间'}]
                await socket.send_json({'id': payload.get('id'), 'type': 'result', 'success': True, 'result': result})
        return socket
    app.router.add_get('/api/states', states)
    app.router.add_get('/api/services', services)
    app.router.add_get('/api/websocket', websocket)
