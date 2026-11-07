import { createConsoleApi, element } from './console_api.js?v=config-20261104';
import { addConfigForm } from './config_forms.js?v=config-20261104';

const byId = id => document.getElementById(id);
const tokenInput = byId('token');
const root = byId('list');
let generation = 0;
let busy = false;
let items = [];
let loadedAt = 0;
let lastError = '';
const api = createConsoleApi(() => tokenInput.value, () => reset('管理员认证失效，已清空配置与秘密输入。', true));
const setBusy = value => { busy = value; byId('load').disabled = value; byId('local-login').disabled = value; };
function reset(message, clearToken = false) {
  generation += 1; api.cancel(); items = []; loadedAt = 0; lastError = '';
  if (clearToken) tokenInput.value = '';
  root.replaceChildren(element('p', 'empty', '等待管理员认证后重新加载。'));
  byId('state').textContent = message; setBusy(false);
}
function showFreshness() {
  if (!loadedAt) return;
  const stale = Date.now() - loadedAt >= 45000;
  const time = new Date(loadedAt).toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false });
  byId('state').textContent = `${busy ? '刷新中' : lastError ? `读取失败：${lastError}` : stale ? '配置快照已过期，请刷新' : `已读取 ${items.length} 个连接器配置状态`} · ${time}（北京时间）；配置状态不代表连接已验证。`;
}
async function load() {
  if (busy) return;
  setBusy(true); const current = ++generation; api.cancel();
  root.replaceChildren(element('p', 'empty', '正在读取配置状态…'));
  try {
    const data = await api.request('/api/connectors');
    if (current !== generation) return;
    if (!Array.isArray(data.items)) throw new Error('服务端连接器格式不正确。');
    items = data.items; loadedAt = Date.now(); lastError = ''; render();
  } catch (error) {
    if (current !== generation) return;
    lastError = error.message; root.replaceChildren(element('p', 'empty', `读取失败：${error.message}`)); byId('state').textContent = lastError;
  } finally { if (current === generation) { setBusy(false); showFreshness(); } }
}
const link = (text, href) => { const node = element('a', 'button', text); node.href = href; return node; };
function addGmailControls(card, valid) {
  const section = element('div', 'connector-check');
  const state = element('p', 'muted', '尚未读取 Gmail 授权状态'); state.setAttribute('role', 'status');
  const refresh = element('button', '', '刷新授权状态');
  const authorize = element('button', 'primary', '发起 Google 授权'); authorize.disabled = true;
  const refreshStatus = async () => {
    refresh.disabled = true;
    try {
      const data = await api.request('/api/connectors/gmail/status');
      if (!valid()) return;
      authorize.disabled = !data.configured || data.available === false;
      state.textContent = !data.configured ? 'OAuth 客户端未配置，请在下方表单保存。' : data.available === false ? 'OAuth 客户端存在，但安全凭据存储不可用。' : data.authorized ? '已有 OAuth 授权令牌；令牌当前有效性未向 Google 验证。' : 'OAuth 客户端已配置；尚未完成 Google 授权。';
    } catch (error) { if (valid()) state.textContent = `状态读取失败：${error.message}`; }
    finally { refresh.disabled = false; }
  };
  refresh.addEventListener('click', refreshStatus);
  authorize.addEventListener('click', async () => {
    if (!valid()) return;
    authorize.disabled = true; const popup = window.open('about:blank', '_blank'); if (popup) popup.opener = null;
    try {
      const data = await api.request('/api/connectors/gmail/oauth/start', { method: 'POST' });
      if (!valid()) { popup?.close(); return; }
      const url = new URL(data.authorization_url);
      if (url.origin !== 'https://accounts.google.com' || url.pathname !== '/o/oauth2/v2/auth') throw new Error('授权地址无效。');
      if (popup) popup.location = url.href; else window.location.href = url.href;
      state.textContent = '请在 Google 授权页完成操作，之后返回刷新授权状态。';
    } catch (error) { popup?.close(); if (valid()) state.textContent = `授权未发起：${error.message}`; }
    finally { if (valid()) authorize.disabled = false; }
  });
  section.append(state, refresh, authorize, link('已有 Gmail 管理（发送仍需独立预览确认）', '/static/assistant.html#gmail-panel')); card.append(section);
  refreshStatus();
  return refreshStatus;
}
const checks = {
  github: ['repository', '目标仓库 owner/name（只读权限检查）'],
  notion: ['query', '目标页面搜索词（只读检查）'],
  todoist: [], google_calendar: [], microsoft_calendar: [], microsoft_mail: [], amap: [], home_assistant: [],
};
function addCheckControls(card, item, valid) {
  if (!(item.id in checks)) {
    card.append(element('p', 'muted', '该 Webhook 不支持无副作用测试；仅校验格式和安全保存，不发送测试消息。')); return;
  }
  const section = element('div', 'connector-check');
  const result = element('p', 'muted', '尚未验证连接。检查成功仅代表本次只读请求成功。'); result.setAttribute('role', 'status');
  let input;
  if (item.id === 'amap') {
    const label = element('label', '', '天气检查城市代码（不用于保存 Key）'); input = element('select');
    for (const [value, name] of [['110000', '北京 110000'], ['310000', '上海 310000'], ['440100', '广州 440100']]) { const option = element('option', '', name); option.value = value; input.append(option); }
    label.append(input); section.append(label);
  } else if (checks[item.id].length) {
    input = element('input'); input.placeholder = checks[item.id][1]; section.append(input);
  }
  const button = element('button', '', '执行只读连接检查');
  button.addEventListener('click', async () => {
    if (!valid() || button.disabled) return;
    button.disabled = true; result.textContent = '正在读取提供商状态…';
    const argumentsValue = item.id === 'amap' ? { city: input.value } : input ? { [checks[item.id][0]]: input.value.trim() } : {};
    try {
      const data = await api.request(`/api/connectors/${encodeURIComponent(item.id)}/check`, { method: 'POST', body: { arguments: argumentsValue }, timeout: 30000 });
      if (valid()) result.textContent = data.status === 'verified' ? `本次连接读取成功 · ${data.checked_at || ''}。保存状态与授权范围需分别核对。` : `检查状态：${data.status || '未知'}`;
    } catch (error) { if (valid()) result.textContent = `连接检查未通过：${error.message}。修正配置后可重新执行只读检查。`; }
    finally { button.disabled = false; }
  });
  section.append(button, result); card.append(section);
  return () => { result.textContent = '配置已变化；尚未重新验证连接。'; };
}
function render() {
  const current = generation;
  const query = byId('search').value.trim().toLowerCase();
  root.replaceChildren();
  for (const item of items) {
    const card = element('article', 'card connector-card'); card.id = item.id;
    card.dataset.search = JSON.stringify([item.id, item.name, item.actions]).toLowerCase();
    card.hidden = Boolean(query && !card.dataset.search.includes(query));
    const valid = () => current === generation && card.isConnected;
    const header = element('div', 'connector-card-head');
    const badge = element('span', 'badge', item.configured ? '有配置 · 连接未验证' : '缺少配置');
    header.append(element('h2', '', item.name || item.id), badge); card.append(header);
    card.append(element('p', 'muted', item.config_note || '新配置接口尚未加载，请在维护窗口更新后端；当前不提供保存。'));
    root.append(card);
    let resetCheck;
    if (item.id === 'gmail') resetCheck = addGmailControls(card, valid); else resetCheck = addCheckControls(card, item, valid);
    if (item.config_fields?.length) addConfigForm(card, item, { api, isCurrent: valid, onSaved: data => {
      badge.textContent = data.configured === false ? '已保存 · 缺少配置' : '已保存 · 未验证连接';
      item.config_fields = data.fields || item.config_fields;
      if (resetCheck) resetCheck();
    } });
    const details = element('details', 'connector-detail'); details.append(element('summary', '', '声明权限与动作'));
    details.append(element('p', 'muted', `声明 OAuth 范围：${(item.oauth_scopes || []).join('、') || '未声明'}；不代表当前账号已授权。`));
    for (const action of item.actions || []) details.append(element('p', 'muted', `${action.name}：${action.description || ''} · ${action.requires_confirmation ? '需服务端确认' : '只读动作'}`));
    card.append(details, link('查看工作流', '/static/admin_console_demo.html#/workflows'));
  }
  if (!items.length) root.append(element('p', 'empty', '服务端未返回已注册连接器。'));
}
byId('load').addEventListener('click', load);
byId('local-login').addEventListener('click', async () => {
  if (busy) return;
  reset('正在建立本机管理员会话…', true); setBusy(true); const current = generation;
  try { await api.request('/api/auth/local-login', { method: 'POST', authenticate: false }); if (current === generation) { setBusy(false); await load(); } }
  catch (error) { if (current === generation) { byId('state').textContent = error.message; setBusy(false); } }
});
byId('search').addEventListener('input', () => { const query = byId('search').value.trim().toLowerCase(); root.querySelectorAll('.connector-card').forEach(card => { card.hidden = Boolean(query && !card.dataset.search.includes(query)); }); });
tokenInput.addEventListener('input', () => reset('令牌已改变，请点击加载重新认证。'));
tokenInput.addEventListener('keydown', event => { if (event.key === 'Enter') load(); });
window.addEventListener('pagehide', () => reset('页面已离开，请重新加载连接器。', true));
window.addEventListener('pageshow', event => { if (event.persisted) load(); });
setInterval(showFreshness, 5000);
load();
