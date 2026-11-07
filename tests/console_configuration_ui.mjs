// 使用实际前端模块与最小 DOM 夹具，检查表单交互；不使用真实网络或秘密。
import assert from 'node:assert/strict';

class Node {
  constructor(tag = '') { this.tagName = tag.toUpperCase(); this.children = []; this.listeners = new Map(); this.dataset = {}; this.value = ''; this.checked = false; this.disabled = false; this._text = ''; }
  set textContent(text) { this._text = String(text); this.children = []; }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(''); }
  append(...children) { for (const child of children) { child.parent = this; if (this.tagName === 'SELECT' && !this.children.length) this.value = child.value; this.children.push(child); } }
  replaceChildren(...children) { for (const child of this.children) child.parent = null; this.children = []; this._text = ''; this.append(...children); }
  setAttribute(name, value) { this[name] = value; }
  addEventListener(name, handler) { const list = this.listeners.get(name) || []; list.push(handler); this.listeners.set(name, list); }
  async emit(name) { for (const handler of this.listeners.get(name) || []) await handler({ preventDefault() {} }); }
  get isConnected() { return Boolean(this.root || this.parent?.isConnected); }
  querySelectorAll(selector) { return descendants(this).filter(node => selector.startsWith('.') ? String(node.className || '').split(' ').includes(selector.slice(1)) : node.tagName.toLowerCase() === selector); }
  querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
}
const descendants = root => root.children.flatMap(child => [child, ...descendants(child)]);
const ids = new Map(['token', 'list', 'state', 'load', 'local-login', 'search'].map(id => { const node = new Node(); node.root = true; return [id, node]; }));
globalThis.document = { createElement: tag => new Node(tag), createTextNode: text => { const node = new Node(); node.textContent = text; return node; }, getElementById: id => ids.get(id) };
globalThis.location = { hostname: '127.0.0.1', protocol: 'http:', hash: '' };
globalThis.window = { addEventListener() {}, open() { return { close() {}, opener: null }; } };
const originalInterval = globalThis.setInterval; globalThis.setInterval = () => 0;
const { addConfigForm } = await import('../web/config_forms.js');
const field = (key, secret = true) => ({ key, label: key, secret, configured: true, editable: true, source: 'config_file', help: 'fake' });
const requests = [];
const api = { request: async (path, options) => { requests.push({path, ...options}); return {saved: true, message: 'saved'}; } };
const root = new Node(); root.root = true;
const inputs = () => descendants(root).filter(node => node.tagName === 'INPUT' || node.tagName === 'SELECT');
const form = () => root.querySelector('form');

addConfigForm(root, { id: 'amap', config_fields: [{...field('amap_web_service_key'), value: 'must-never-prefill'}] }, {api});
let secret = inputs().find(node => node.name === 'amap_web_service_key');
assert.equal(secret.type, 'password'); assert.equal(secret.value, '');
secret.value = 'a'.repeat(32); await form().emit('submit');
assert.equal(requests.at(-1).path, '/api/connectors/amap/config');
assert.equal(requests.at(-1).body.values.amap_web_service_key, 'a'.repeat(32));
assert.equal(secret.value, '');
await form().emit('submit'); assert.deepEqual(requests.at(-1).body.values, {});
inputs().find(node => node.type === 'checkbox').checked = true; await form().emit('submit');
assert.deepEqual(requests.at(-1).body.clear_fields, ['amap_web_service_key']);

root.replaceChildren(); addConfigForm(root, {id: 'github', config_fields: [{...field('github_access_token'), source: 'environment', editable: false}]}, {api});
assert.equal(inputs()[0].disabled, true);
root.replaceChildren(); addConfigForm(root, {id: 'github', config_fields: [field('github_access_token')]}, {api: {request: async () => {throw new Error('fake failed save');}}});
secret = inputs()[0]; secret.value = 'fake-secret'; await form().emit('submit'); assert.equal(secret.value, ''); assert.match(root.textContent, /保存失败/);

root.replaceChildren(); addConfigForm(root, {id: 'gmail', config_fields: [field('gmail_client_id'), field('gmail_client_secret'), {...field('gmail_send_enabled', false), value: true}]}, {api});
inputs().find(node => node.name === 'gmail_client_id').value = 'fake.apps.googleusercontent.com';
inputs().find(node => node.name === 'gmail_client_secret').value = 'fake-gmail-secret';
await form().emit('submit'); assert.equal(requests.at(-1).path, '/api/gmail/config'); assert.equal(requests.at(-1).body.send_enabled, true);
assert.ok(inputs().filter(node => node.type === 'password').every(node => node.value === ''));

root.replaceChildren(); addConfigForm(root, {id: 'models', config_fields: [field('deepseek_api_key')]}, {api});
inputs()[0].value = 'fake-deepseek-key'; await form().emit('submit'); assert.equal(requests.at(-1).path, '/api/settings/models');
root.replaceChildren(); addConfigForm(root, {id: 'github', config_fields: [field('github_access_token')], config_available: false}, {api});
assert.equal(root.querySelector('button').disabled, true);

const registry = ['gmail', 'amap', 'google_calendar', 'microsoft_calendar', 'microsoft_mail', 'notion', 'github', 'todoist', 'slack', 'feishu', 'home_assistant'].map(id => ({id, name: id, configured: false, config_available: true, config_note: 'fake metadata', config_fields: [field(id === 'amap' ? 'amap_web_service_key' : 'fake_key')], actions: []}));
const fetches = [];
globalThis.fetch = async (path, options) => {
  fetches.push({path, ...options});
  const data = path === '/api/connectors' ? {items: registry} : path.endsWith('/status') ? {configured: false} : {status: 'verified', checked_at: '2026-11-04T00:00:00Z'};
  return new Response(JSON.stringify(data), {status: 200});
};
await import('../web/connectors.js');
for (let index = 0; index < 4; index += 1) await new Promise(resolve => setImmediate(resolve));
const list = ids.get('list'); assert.equal(list.querySelectorAll('.connector-card').length, 11);
const amap = list.children.find(node => node.id === 'amap');
assert.equal(amap.querySelector('select').children[0].value, '110000');
await amap.querySelectorAll('button').find(node => node.textContent === '执行只读连接检查').emit('click');
assert.equal(fetches.at(-1).path, '/api/connectors/amap/check'); assert.deepEqual(JSON.parse(fetches.at(-1).body), {arguments: {city: '110000'}});
for (const id of ['slack', 'feishu']) {
  const card = list.children.find(node => node.id === id);
  assert.ok(!card.querySelectorAll('button').some(node => node.textContent === '执行只读连接检查'));
  assert.match(card.textContent, /不支持无副作用测试/);
}
await ids.get('token').emit('input'); assert.equal(list.querySelectorAll('.connector-card').length, 0);
const { createSettingsPanel } = await import('../web/console_settings.js');
const settingsRoot = new Node(); settingsRoot.root = true;
const settingsRequests = [];
const settingsApi = {request: async (path, options = {}) => {
  settingsRequests.push({path, ...options});
  if (options.method) return {saved: true};
  if (path === '/api/settings') return {items: [{key: 'enable_search', value: false, editable: true}], model_config: [field('deepseek_api_key')], secure_storage_available: true, maintenance_note: 'maintenance'};
  return {current: 'fake-option', items: [{id: 'fake-option', name: 'fake-option'}]};
}};
const settings = createSettingsPanel(settingsRoot, settingsApi, () => true);
await settings.load();
assert.equal(settingsRoot.querySelectorAll('form').length, 4);
await settingsRoot.querySelectorAll('form')[0].emit('submit');
assert.equal(settingsRequests.at(-1).path, '/api/settings');
assert.deepEqual(settingsRequests.at(-1).body, {enable_search: false});
for (const [index, path] of [[2, '/api/llm'], [3, '/api/voice']]) {
  await settingsRoot.querySelectorAll('form')[index].emit('submit'); assert.equal(settingsRequests.at(-1).path, path);
}
settings.clear(); assert.equal(settingsRoot.children.length, 0);
globalThis.setInterval = originalInterval;
console.log('UI mock checks passed: real form events, secret cleanup, environment read-only, Gmail reuse, model API, 11 cards, Amap city selector, no Webhook sending, auth cleanup, settings/search/model/voice forms.');
