import { createConsoleApi, element } from './console_api.js?v=config-20261104';
import { addConfigForm } from './config_forms.js?v=config-20261113b';

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
const statusLabels = { not_configured: '未配置 / 待授权', unverified: '待检查', available: '可用（本次只读检查）', expired_or_denied: '已过期 / 权限不足', connection_error: '连接错误', disabled: '已撤销访问' };
function addAccessControls(card, item, valid) {
  if (typeof item.status !== 'string') { card.append(element('p', 'muted', '当前后端尚未加载新版连接中心；请在维护升级后使用访问许可和统一状态。')); return; }
  const button = element('button', '', item.status === 'disabled' ? '重新允许伊迪斯访问' : '撤销伊迪斯访问');
  button.disabled = item.config_available === false || item.id === 'mcp' && !item.configured;
  const state = element('p', 'muted', '撤销后停止助手调用并作废待确认动作。提供商账号授权需到提供商设置中另行撤销。');
  button.addEventListener('click', async () => {
    if (!valid() || button.disabled) return;
    button.disabled = true;
    try {
      const data = await api.request(`/api/connectors/${encodeURIComponent(item.id)}/access`, { method: 'POST', body: { enabled: item.status === 'disabled' } });
      if (valid()) { state.textContent = data.message; await load(); }
    } catch (error) { if (valid()) { state.textContent = error.message; button.disabled = false; } }
  });
  card.append(button, state);
}
function addCalendarAuthorization(card, item, valid) {
  if (typeof item.status !== 'string' || !['google_calendar', 'microsoft_calendar'].includes(item.id)) return;
  const details = element('details', 'connector-detail'); details.append(element('summary', '', '授权凭据与续期'));
  const content = element('div'); details.append(content); card.append(details);
  let loaded = false;
  details.addEventListener('toggle', async () => {
    if (!details.open || loaded || !valid()) return;
    loaded = true;
    try {
      const data = await api.request(`/api/computer/oauth/${item.id}/config`);
      if (!valid()) return;
      content.append(element('p', 'muted', `${data.message}。浏览器首次授权尚未集成；请先在提供商完成真实授权后填写凭据。`));
      const form = element('form', 'connector-config-form'); const controls = [];
      const labels = { client_id: '客户端 ID', client_secret: '客户端密钥', refresh_token: '续期令牌', access_token: '访问令牌', expires_at: '到期时间（带时区）', tenant_id: 'Microsoft 租户 ID' };
      for (const field of data.fields || []) {
        if (item.id === 'google_calendar' && field.key === 'tenant_id') continue;
        const label = element('label', '', `${labels[field.key] || field.key} · ${field.configured ? '已配置' : '未配置'}`);
        const input = element('input'); input.type = field.secret ? 'password' : 'text'; input.autocomplete = 'off'; input.placeholder = '留空保留';
        if (!field.secret) input.value = field.value || '';
        input.disabled = data.available === false;
        label.append(input); form.append(label); controls.push([field.key, input]);
      }
      const save = element('button', 'primary', '安全保存授权凭据'); save.type = 'submit'; save.disabled = data.available === false;
      const state = element('p', 'muted'); const refresh = element('button', '', '生成续期预览'); refresh.type = 'button'; refresh.disabled = !data.refresh_ready;
      const confirm = element('button', 'primary', '确认向提供商续期'); confirm.type = 'button'; confirm.hidden = true;
      let pending;
      form.addEventListener('input', () => { pending = null; confirm.hidden = true; });
      form.addEventListener('submit', async event => {
        event.preventDefault(); if (!valid() || save.disabled) return; save.disabled = true;
        try {
          await api.request(`/api/computer/oauth/${item.id}/config`, { method: 'POST', body: { values: Object.fromEntries(controls.filter(([, input]) => input.value).map(([key, input]) => [key, input.value])) } });
          if (valid()) await load();
        } catch (error) { if (valid()) state.textContent = error.message; }
        finally { controls.forEach(([, input]) => { if (input.type === 'password') input.value = ''; }); save.disabled = data.available === false; }
      });
      refresh.addEventListener('click', async () => {
        if (!valid() || refresh.disabled) return; refresh.disabled = true;
        try {
          const preview = await api.request('/api/computer/actions/oauth_refresh/prepare', { method: 'POST', body: { arguments: { provider: item.id } } });
          if (valid()) { pending = preview; state.textContent = JSON.stringify(preview.preview, null, 2); confirm.hidden = false; }
        } catch (error) { if (valid()) state.textContent = error.message; }
        finally { refresh.disabled = !data.refresh_ready; }
      });
      confirm.addEventListener('click', async () => {
        const selected = pending; pending = null; confirm.hidden = true;
        if (!valid() || !selected) return;
        try {
          const result = await api.request('/api/computer/actions/oauth_refresh', { method: 'POST', body: { arguments: selected.arguments, confirmation_token: selected.confirmation_token }, timeout: 30000 });
          if (valid()) state.textContent = `续期请求：${result.status}。请再执行连接检查确认实际权限。`;
        } catch (error) { if (valid()) state.textContent = `续期结果未确认：${error.message}。请刷新状态后核查。`; }
      });
      form.append(save, refresh, confirm, state); content.append(form);
    } catch (error) { if (valid()) { content.textContent = error.message; loaded = false; } }
  });
}
function addMcpPermissions(card, item, valid) {
  if (item.id !== 'mcp') return;
  const details = element('details', 'connector-detail'); details.append(element('summary', '', '允许外部能力'));
  details.append(element('p', 'muted', '先检查连接，再逐项核对能力。只有确认不会修改数据的能力才选择“允许只读”；其他能力选择“每次预览确认”。服务自称只读不会自动获得许可。'));
  const controls = [];
  for (const tool of item.tools || []) {
    const label = element('label', '', `${tool.name}：${tool.description || ''}`); const select = element('select');
    for (const [value, name] of [['blocked', '不允许'], ['confirm', '每次预览确认'], ['read', '允许只读']]) { const option = element('option', '', name); option.value = value; select.append(option); }
    select.value = tool.mode || 'blocked'; label.append(select); details.append(label); controls.push([tool.name, select]);
  }
  const save = element('button', 'primary', '保存允许的能力'); save.disabled = item.status !== 'available' || !controls.length;
  const state = element('p', 'muted');
  save.addEventListener('click', async () => {
    if (!valid() || save.disabled) return; save.disabled = true;
    try {
      await api.request('/api/connectors/mcp/permissions', { method: 'POST', body: { tools: Object.fromEntries(controls.map(([name, select]) => [name, select.value])), revision: item.tool_revision } });
      if (valid()) await load();
    } catch (error) { if (valid()) { state.textContent = error.message; save.disabled = false; } }
  });
  details.append(save, state); card.append(details);
}
function addActionControls(card, item, valid) {
  for (const action of item.actions || []) {
    if (!action.input_schema || item.id === 'gmail') continue;
    const details = element('details', 'connector-detail'); details.append(element('summary', '', action.description || action.name));
    const form = element('form', 'connector-config-form'); const inputs = [];
    for (const [key, definition] of Object.entries(action.input_schema.properties || {})) {
      const label = element('label', '', `${definition.description || key}${action.input_schema.required?.includes(key) ? '（必填）' : ''}`);
      const input = element(definition.enum || definition.type === 'boolean' ? 'select' : ['object', 'array'].includes(definition.type) ? 'textarea' : 'input');
      if (definition.enum) {
        input.append(element('option', '', '请选择'));
        input.firstChild.value = '';
        definition.enum.forEach(value => { const option = element('option', '', String(value)); option.value = JSON.stringify(value); input.append(option); });
      } else if (definition.type === 'boolean') {
        for (const [value, name] of [['', '未填写'], ['true', '是'], ['false', '否']]) { const option = element('option', '', name); option.value = value; input.append(option); }
      }
      else if (input.tagName !== 'TEXTAREA') { input.type = ['integer', 'number'].includes(definition.type) ? 'number' : 'text'; if (definition.minimum !== undefined) input.min = definition.minimum; if (definition.maximum !== undefined) input.max = definition.maximum; }
      if (action.input_schema.required?.includes(key)) input.required = true;
      label.append(input); form.append(label); inputs.push([key, input, definition]);
    }
    const submit = element('button', '', action.requires_confirmation ? '生成操作预览' : '执行查询'); submit.type = 'submit';
    const confirm = element('button', 'primary', '确认执行本次操作'); confirm.type = 'button'; confirm.hidden = true;
    const result = element('pre', 'action-result'); result.setAttribute('role', 'status'); let pending;
    form.addEventListener('input', () => { pending = null; confirm.hidden = true; });
    const base = `/api/connectors/${encodeURIComponent(item.id)}/${encodeURIComponent(action.name)}`;
    form.addEventListener('submit', async event => {
      event.preventDefault(); if (!valid() || submit.disabled) return; submit.disabled = true; pending = null; confirm.hidden = true;
      try {
        const argumentsValue = {};
        for (const [key, input, definition] of inputs) {
          if (input.value === '') continue;
          argumentsValue[key] = definition.enum || ['object', 'array', 'boolean'].includes(definition.type) ? JSON.parse(input.value) : ['integer', 'number'].includes(definition.type) ? Number(input.value) : input.value;
        }
        const data = await api.request(action.requires_confirmation ? `${base}/prepare` : base, { method: 'POST', body: { arguments: argumentsValue }, timeout: 30000 });
        if (valid()) { result.textContent = JSON.stringify(data.preview || data, null, 2); if (action.requires_confirmation) { pending = { arguments: data.preview, confirmation_id: data.confirmation_id }; confirm.hidden = false; } }
      } catch (error) { if (valid()) result.textContent = error.message; }
      finally { submit.disabled = false; }
    });
    confirm.addEventListener('click', async () => {
      const selected = pending; pending = null; confirm.hidden = true; if (!valid() || !selected) return;
      submit.disabled = true;
      try { const data = await api.request(base, { method: 'POST', body: selected, timeout: 30000 }); if (valid()) result.textContent = JSON.stringify(data, null, 2); }
      catch (error) { if (valid()) result.textContent = `结果待核实：${error.message}。请在提供商核查，勿直接重复执行。`; }
      finally { submit.disabled = false; }
    });
    form.append(submit, confirm, result); details.append(form); card.append(details);
  }
}
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
      if (popup) popup.location = url.href; else (window.self !== window.top ? window.top : window).location.href = url.href;
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
  todoist: [], google_calendar: [], microsoft_calendar: [], microsoft_mail: [], amap: [], home_assistant: [], mcp: [],
};
function addCheckControls(card, item, valid) {
  if (!(item.id in checks)) {
    card.append(element('p', 'muted', '该 Webhook 不支持无副作用测试；仅校验格式和安全保存，不发送测试消息。')); return;
  }
  const section = element('div', 'connector-check');
  const result = element('p', 'muted', `${item.message || '尚未验证连接。'}${item.checked_at ? ` · ${item.checked_at}` : ''}`); result.setAttribute('role', 'status');
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
      if (valid()) { result.textContent = data.status === 'verified' ? `本次连接读取成功 · ${data.checked_at || ''}` : `检查状态：${data.status || '未知'}`; await load(); }
    } catch (error) { if (valid()) { result.textContent = `连接检查未通过：${error.message}。修正配置后可重新执行只读检查。`; await load(); } }
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
    const badge = element('span', 'badge', statusLabels[item.status] || (item.configured ? '待检查' : '未配置'));
    header.append(element('h2', '', item.name || item.id), badge); card.append(header);
    card.append(element('p', 'muted', item.config_note || '新配置接口尚未加载，请在维护窗口更新后端；当前不提供保存。'));
    root.append(card);
    let resetCheck;
    if (item.id === 'gmail') resetCheck = addGmailControls(card, valid); else resetCheck = addCheckControls(card, item, valid);
    if (item.config_fields?.length) addConfigForm(card, item, { api, isCurrent: valid, onSaved: data => {
      badge.textContent = data.configured === false ? '已保存 · 缺少配置' : '已保存 · 未验证连接';
      item.config_fields = data.fields || item.config_fields;
      if (resetCheck) resetCheck();
      load();
    } });
    addCalendarAuthorization(card, item, valid);
    addAccessControls(card, item, valid);
    addMcpPermissions(card, item, valid);
    addActionControls(card, item, valid);
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
