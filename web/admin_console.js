import { createConsoleApi, element } from './console_api.js?v=20261104';
import { createSettingsPanel } from './console_settings.js?v=0310-20261104';

const byId = id => document.getElementById(id);
const tokenInput = byId('admin-token');
const drawer = byId('detail-drawer');
const paths = { status: '/api/status', connectors: '/api/connectors', audit: '/api/audit/recent', devices: '/api/devices' };
const states = Object.fromEntries(Object.keys(paths).map(key => [key, { data: null, loadedAt: 0, error: '', loading: false }]));
const routes = {
  overview: ['伊迪斯工作台', '查看当前状态，快速进入日常任务。'],
  workflows: ['工作流', '查看真实依赖；会议出行规划支持手动只读查询。'],
  audit: ['审计日志', '仅显示当前日志末尾的有界近期记录，不推算缺失时间。'],
  credentials: ['权限与凭据', '查看声明权限与认证边界。'],
  settings: ['系统设置', '查看状态接口公开的运行配置摘要。'],
  assistant: ['个人助手', '打开已有助手功能，沿用原有服务端确认流程。'],
  connectors: ['连接器中心', '查看与配置连接器；配置就绪不代表外部授权有效。'],
  devices: ['设备管理', '查看绑定设备状态、配对与固件。'],
  productivity: ['提醒与主动助理', '安排提醒与待办。'],
  memory: ['记忆管理', '查看与维护助手记忆。'],
  music: ['互联网点歌', '搜索与播放音乐。'],
  knowledge: ['知识与会议', '知识与会议资料管理。'],
  desktop: ['桌面工具', '桌面辅助工具。'],
  computer: ['电脑与媒体', '电脑控制与媒体播放。'],
};
// 内嵌到控制台的独立功能页；侧栏常驻，路由切换只替换 iframe。
const embedRoutes = {
  connectors: '/static/connectors.html',
  devices: '/static/devices.html',
  productivity: '/static/productivity.html',
  memory: '/static/memory.html',
  music: '/static/music.html',
  knowledge: '/static/knowledge.html',
  desktop: '/static/desktop.html',
  computer: '/static/computer.html',
};
let embedAnchor = '';
let embedGeneration = 0;
const embedCache = new Map();
let generation = 0;
let busy = false;
let authenticated = false;
let refreshTimer;
let auditFilter = 'all';
let previousFocus;
let renderedAuthentication = false;
const api = createConsoleApi(() => tokenInput.value, () => resetAuthentication('管理员认证不可用：请在服务所在电脑点击本机登录，或检查管理员令牌。', true));
const settingsPanel = createSettingsPanel(byId('settings-controls'), api, () => authenticated);
const fresh = key => states[key].data !== null && !states[key].error && !states[key].loading && Date.now() - states[key].loadedAt < 45000;
const timeText = value => new Date(value).toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false });
const closeDrawer = () => { if (drawer.open) drawer.close(); };
const openDrawer = (title, description, items = []) => {
  if (!drawer.open) previousFocus = document.activeElement;
  byId('drawer-title').textContent = title;
  byId('drawer-description').textContent = description;
  byId('drawer-content').replaceChildren(...items.map(item => element('div', 'drawer-item', item)));
  if (!drawer.open) drawer.showModal();
};
const link = (text, href) => { const node = element('a', 'button', text); node.href = href; return node; };
const setBusy = value => {
  busy = value;
  ['admin-load', 'admin-refresh', 'local-login'].forEach(id => { byId(id).disabled = value; });
};
function resetAuthentication(message, clearToken = false) {
  generation += 1;
  api.cancel();
  clearTimeout(refreshTimer);
  authenticated = false;
  settingsPanel.clear();
  if (clearToken) tokenInput.value = '';
  Object.values(states).forEach(state => Object.assign(state, { data: null, loadedAt: 0, error: '', loading: false }));
  closeDrawer();
  byId('drawer-content').replaceChildren();
  byId('drawer-description').textContent = '';
  setBusy(false);
  byId('page-message').textContent = message;
  render();
}
function scheduleRefresh() {
  clearTimeout(refreshTimer);
  if (!authenticated || document.hidden) return;
  refreshTimer = setTimeout(() => { if (drawer.open) scheduleRefresh(); else load(); }, 30000);
}
async function load(keys = Object.keys(paths)) {
  if (busy) return;
  clearTimeout(refreshTimer);
  closeDrawer();
  setBusy(true);
  const current = generation;
  keys.forEach(key => { states[key].loading = true; });
  renderFreshness();
  await Promise.allSettled(keys.map(async key => {
    const state = states[key];
    try {
      const data = await api.request(paths[key]);
      if (current !== generation) return;
      if (!data || typeof data !== 'object' || (key !== 'status' && !Array.isArray(data.items)) || (key === 'status' && !data.bridge)) throw new Error('接口数据结构不符合当前页面要求。');
      Object.assign(state, { data, loadedAt: Date.now(), error: '' });
      authenticated = true;
    } catch (error) {
      if (current !== generation) return;
      state.error = error.message;
    } finally {
      if (current === generation) { state.loading = false; render(); }
    }
  }));
  if (current !== generation) return;
  setBusy(false);
  renderFreshness();
  byId('page-message').textContent = Object.values(states).some(state => state.error) ? authenticated ? '部分数据读取失败；旧快照已标记，可按分区重试。' : '未取得接口数据，请查看分区错误并重试。' : '已读取当前接口快照。';
  scheduleRefresh();
  if (byId('main-content').dataset.route === 'settings' && authenticated && !byId('settings-controls').querySelector('form')) settingsPanel.load();
}
function renderFreshness() {
  const allFresh = Object.keys(states).every(fresh);
  const hasData = Object.values(states).some(state => state.data !== null);
  const mode = byId('data-mode');
  const hasErrors = Object.values(states).some(state => state.error);
  mode.textContent = busy ? '正在更新快照' : allFresh ? '近期接口快照' : hasErrors ? hasData ? '部分读取失败' : '读取失败 · 请登录或重试' : hasData ? '快照已过期' : '等待认证 / 加载';
  mode.className = `badge ${allFresh ? 'good' : hasData || hasErrors ? 'warn' : ''}`;
  byId('auth-summary').textContent = authenticated ? '切换认证' : '管理员登录';
  if (authenticated !== renderedAuthentication) byId('auth-panel').open = !authenticated;
  renderedAuthentication = authenticated;
  document.querySelectorAll('[data-state]').forEach(node => {
    const key = node.dataset.state;
    const state = states[key];
    const suffix = state.loadedAt ? ` · 上次读取 ${timeText(state.loadedAt)}（北京时间）` : '';
    const message = state.loading ? `更新中${state.data ? ' · 旧快照' : ''}` : state.error ? `读取失败：${state.error}${state.data ? ' · 旧快照' : ''}` : state.data ? fresh(key) ? '近期接口快照' : '快照已过期，请刷新' : '尚未加载';
    node.replaceChildren(element('span', '', `${message}${suffix}`));
    if ((state.error || (state.data && !fresh(key))) && !busy) {
      const retry = element('button', '', '重试本区');
      retry.type = 'button';
      retry.addEventListener('click', () => load([key]));
      node.append(retry);
    }
  });
  renderWorkbench();
}
const snapshotSuffix = key => !states[key].data ? '' : states[key].loading ? ' · 更新中，旧快照' : states[key].error ? ' · 读取失败，旧快照' : !fresh(key) ? ' · 快照过期' : '';
// 兼容尚未重启的网关，不把离线默认配置项计为设备。
const availableDevices = () => states.devices.data?.items?.filter(item => item.pairing_id || item.state === 'online' || item.state === 'disabled');
const deviceOnline = item => item.state === 'online' && item.enabled !== false && item.disabled !== true;
function renderWorkbench() {
  const status = states.status.data;
  const suffix = snapshotSuffix('status');
  byId('summary-gateway').textContent = `${status?.bridge?.running === true ? '运行中' : status?.bridge?.running === false ? '已停止' : '未知'}${suffix}`;
  byId('summary-cloud').textContent = `云语音：${status?.cloud?.connected === true ? '活动中' : status?.cloud?.connected === false ? '待机' : '未知'}`;
  byId('summary-model').textContent = `${status?.bridge?.model || '未知'}${suffix}`;
  byId('summary-provider').textContent = `提供方：${status?.bridge?.llm_provider || '未知'}`;
  const search = status?.capabilities?.web_search;
  byId('summary-search').textContent = `${search === true ? '已开启' : search === false ? '已关闭' : '未知'}${suffix}`;
  const devices = availableDevices();
  byId('summary-devices').textContent = devices ? `${devices.filter(deviceOnline).length} / ${devices.length}${snapshotSuffix('devices')}` : '未知';
  byId('summary-device-note').textContent = devices ? '在线 / 本次返回的有效设备' : '等待读取设备列表';
}
const itemButton = (name, description, status, onClick) => {
  const button = element('button', 'item-button');
  button.type = 'button';
  const head = element('span', 'item-head');
  head.append(element('strong', '', name), element('span', 'badge', status));
  button.append(head, element('span', 'item-meta', description));
  button.addEventListener('click', onClick);
  return button;
};
function renderConnectors() {
  const items = states.connectors.data?.items || [];
  byId('connector-list').replaceChildren(...items.map(item => itemButton(item.name || item.id, `${item.id} · ${item.action_count ?? item.actions?.length ?? 0} 项声明能力`, item.configured === true ? '已配置 · 未验证连接' : item.configured === false ? '未配置' : '配置未知', () => {
    openDrawer(item.name || item.id, '配置项存在不等于第三方授权有效。', (item.actions || []).map(action => `${action.name}：${action.description || '无说明'} · ${(action.scopes || []).join('、') || '未声明权限'}${action.requires_confirmation ? ' · 需服务端确认' : ''}`));
    const detail = element('a', 'button', '配置说明与能力详情');
    detail.href = '#/connectors';
    detail.addEventListener('click', () => { embedAnchor = `#${encodeURIComponent(item.id)}`; });
    byId('drawer-content').append(detail);
  })));
  if (!items.length) byId('connector-list').append(element('p', 'empty', states.connectors.data ? '暂无已注册连接器。' : '等待真实连接器数据。'));
  byId('permission-list').replaceChildren(...items.map(item => {
    const block = element('div', 'drawer-item');
    block.append(element('strong', '', item.name || item.id), element('p', 'muted', `声明 OAuth 范围：${(item.oauth_scopes || []).join('、') || '未声明'}`));
    (item.actions || []).forEach(action => block.append(element('p', 'muted', `${action.name} · ${(action.scopes || []).join('、') || '未声明'} · ${action.requires_confirmation ? '需服务端确认' : '未声明确认要求'}`)));
    return block;
  }));
  byId('metric-connectors').textContent = states.connectors.data ? items.length : '—';
  byId('metric-configured').textContent = states.connectors.data ? `${items.filter(item => item.configured).length} 个有配置，未验证外部连接` : '待读取';
}
function renderStatus() {
  const status = states.status.data;
  const health = byId('health-list');
  const settings = byId('settings-list');
  health.replaceChildren();
  settings.replaceChildren();
  if (!status) {
    health.append(element('p', 'empty', '等待运行状态数据。'));
    settings.append(element('p', 'empty', '等待运行配置数据。'));
  } else {
    const rows = [
      ['桥接服务', status.bridge?.running === true ? '运行中' : status.bridge?.running === false ? '已停止' : '未提供'],
      ['云语音会话', status.cloud?.connected === true ? '活动中' : status.cloud?.connected === false ? '待机' : '未知'],
      ['语音设备会话', status.device?.connected === true ? '存在连接' : status.device?.connected === false ? '无连接' : '未提供'],
    ];
    rows.forEach(([name, value]) => { const row = element('div', 'health-row'); row.append(element('span', 'muted', name), element('strong', '', value)); health.append(row); });
    const fields = [['模型提供方', status.bridge?.llm_provider], ['模型', status.bridge?.model], ['当前活动提供方', status.bridge?.active_provider], ['记忆后端', status.bridge?.memory_backend], ['业务时区', status.bridge?.timezone]];
    fields.forEach(([name, value]) => { const row = element('div'); row.append(element('dt', '', name), element('dd', '', value ?? '接口未提供')); settings.append(row); });
  }
  const workflows = status?.workflows || [];
  byId('workflow-list').replaceChildren(...workflows.map(workflow => {
    const button = itemButton(workflow.name || workflow.id, `${workflow.id} · 依赖：${(workflow.required_connectors || []).join('、') || '接口未提供'} · ${workflow.read_only ? '只读' : '涉及写入'}`, workflow.ready === true ? '配置就绪 · 未验证执行' : workflow.ready === false ? '待配置' : '配置未知', () => showWorkflow(workflow));
    button.dataset.workflowId = workflow.id;
    return button;
  }));
  if (!workflows.length) byId('workflow-list').append(element('p', 'empty', status ? '服务端未返回工作流。' : '等待工作流数据。'));
  byId('metric-workflows').textContent = status ? `${workflows.filter(item => item.ready).length} / ${workflows.length}` : '—';
}
function showWorkflow(workflow) {
  openDrawer(workflow.name || workflow.id, `${fresh('status') ? '近期快照' : '旧快照'}：${workflow.ready ? '配置条件已满足，外部执行结果尚未验证。' : '缺少配置，当前无法执行。'}`, [`标识：${workflow.id}`, `依赖：${(workflow.required_connectors || []).join('、') || '未提供'}`, workflow.read_only ? '本工作流声明为只读。' : '写入动作需要服务端生成的短期确认凭证，参数变化会使凭证失效。']);
  const root = byId('drawer-content');
  root.append(link('查看连接器配置', '#/connectors'), document.createTextNode(' '), link('查看近期审计', '#/audit'));
  if (workflow.id === 'meeting-travel' && workflow.read_only && workflow.ready && fresh('status') && authenticated) addTravelForm(root);
  else if (workflow.id === 'meeting-travel' && !fresh('status')) root.append(element('p', 'notice', '请先刷新运行状态，再打开只读查询表单。'));
  else if (['meeting-notes', 'github-issue', 'todoist-task'].includes(workflow.id)) {
    if (!workflow.ready) root.append(element('p', 'notice', '依赖连接器尚未配置；请先到连接器中心完成服务端配置与授权。'));
    else if (!authenticated) root.append(element('p', 'notice', '请先完成管理员认证。'));
    else if (!fresh('status')) root.append(element('p', 'notice', '工作流状态快照已过期，请先刷新后再生成预览。'));
    else addWriteWorkflowForm(root, workflow);
  }
}

function addWriteWorkflowForm(root, workflow) {
  const form = element('form', 'workflow-form');
  const field = (name, label, type = 'text', options = []) => {
    const wrapper = element('label', '', label);
    const input = element(type === 'textarea' ? 'textarea' : type === 'select' ? 'select' : 'input');
    input.name = name;
    if (type === 'textarea') input.rows = 5;
    else if (type === 'select') options.forEach(([value, text]) => { const option = element('option', '', text); option.value = value; input.append(option); });
    else input.type = type;
    input.required = name !== 'body' && name !== 'due_string';
    wrapper.append(input); form.append(wrapper); return input;
  };
  let inputs;
  if (workflow.id === 'meeting-notes') inputs = { parent_id: field('parent_id', 'Notion 父页面或数据库 ID'), parent_type: field('parent_type', '父级类型', 'select', [['page', '页面（page）'], ['database', '数据库（database）']]), title: field('title', '标题'), content: field('content', '正文', 'textarea') };
  if (workflow.id === 'github-issue') inputs = { repository: field('repository', 'GitHub 仓库（owner/name）'), title: field('title', 'Issue 标题'), body: field('body', 'Issue 正文（可选）', 'textarea') };
  if (workflow.id === 'todoist-task') inputs = { content: field('content', 'Todoist 待办内容'), due_string: field('due_string', '截止时间（可选）'), priority: field('priority', '优先级', 'select', [['1', '普通（1）'], ['2', '中（2）'], ['3', '高（3）'], ['4', '紧急（4）']]) };
  const prepare = element('button', 'primary', '生成服务端预览'); prepare.type = 'submit'; form.append(prepare);
  const result = element('pre', '', '填写参数后生成预览；尚未调用外部服务。'); result.setAttribute('role', 'status'); form.append(result); root.append(form);
  let pending = false;
  form.addEventListener('submit', async event => {
    event.preventDefault(); if (pending) return; pending = true; prepare.disabled = true; result.textContent = '正在生成短期确认凭证…';
    const payload = Object.fromEntries(Object.entries(inputs).map(([key, input]) => [key, input.value]));
    if (workflow.id === 'todoist-task') payload.priority = Number(payload.priority || 1);
    try {
      const preview = await api.request(`/api/workflows/${encodeURIComponent(workflow.id)}/prepare`, { method: 'POST', body: payload });
      result.textContent = `${JSON.stringify(preview.preview, null, 2)}\n\n确认凭证有效期至：${preview.expires_at}\n参数或管理员变化都会使凭证失效。`;
      const confirm = element('button', 'primary', '确认并执行外部写入'); confirm.type = 'button'; form.append(confirm);
      confirm.addEventListener('click', async () => {
        confirm.disabled = true; result.textContent += '\n\n执行中…';
        try {
          const executed = await api.request(`/api/workflows/${encodeURIComponent(workflow.id)}`, { method: 'POST', body: {...payload, confirmation_id: preview.confirmation_id}, timeout: 30000 });
          result.textContent = `${result.textContent}\n\n${JSON.stringify(executed, null, 2)}`;
        } catch (error) { result.textContent += `\n\n执行失败或结果待核实：${error.message}\n请重新生成预览并再次确认，页面不会自动重试。`; confirm.disabled = true; }
      }, { once: true });
    } catch (error) { result.textContent = `无法生成预览：${error.message}`; }
    finally { pending = false; prepare.disabled = false; }
  });
}
function addTravelForm(root) {
  const form = element('form');
  const field = (name, label, type = 'text') => {
    const wrapper = element('label', '', label);
    const input = element('input'); input.name = name; input.type = type; input.required = true;
    wrapper.append(input); form.append(wrapper); return input;
  };
  const label = element('label', '', '日历来源');
  const calendar = element('select'); calendar.name = 'calendar_connector';
  const configured = states.connectors.data?.items || [];
  ['google_calendar', 'microsoft_calendar'].filter(id => configured.some(item => item.id === id && item.configured)).forEach(id => { const option = element('option', '', id); option.value = id; calendar.append(option); });
  label.append(calendar); form.append(label);
  const start = field('time_min', `开始时间（浏览器时区：${Intl.DateTimeFormat().resolvedOptions().timeZone}）`, 'datetime-local');
  const end = field('time_max', '结束时间（同上）', 'datetime-local');
  const origin = field('origin', '出发地经纬度（经度,纬度）');
  origin.placeholder = '例如 121.4737,31.2304';
  const submit = element('button', 'primary', '查询日历与路线（只读）'); submit.type = 'submit'; submit.disabled = !calendar.options.length;
  const result = element('pre', '', '查询由你手动发起；会将时间范围和出发地发送至已配置日历 / 地图服务。');
  result.setAttribute('role', 'status');
  form.append(submit); root.append(form, result);
  let pending = false;
  form.addEventListener('submit', async event => {
    event.preventDefault();
    if (pending) return;
    if (!authenticated || !states.connectors.data || states.connectors.error || states.status.error) { result.textContent = '状态不完整或认证已失效，请关闭详情并刷新后重试。'; return; }
    if (new Date(end.value) <= new Date(start.value)) { result.textContent = '结束时间必须晚于开始时间。'; return; }
    const coordinates = origin.value.trim().split(',').map(value => value.trim());
    if (coordinates.length !== 2 || coordinates.some(value => !value || !Number.isFinite(Number(value))) || Math.abs(Number(coordinates[0])) > 180 || Math.abs(Number(coordinates[1])) > 90) { result.textContent = '请填写有效的经度,纬度。'; return; }
    pending = true; submit.disabled = true; result.textContent = '正在读取日历和路线…';
    const current = generation;
    try {
      const data = await api.request('/api/workflows/meeting-travel', { method: 'POST', timeout: 30000, body: { calendar_connector: calendar.value, time_min: new Date(start.value).toISOString(), time_max: new Date(end.value).toISOString(), origin: coordinates.join(',') } });
      if (current === generation && drawer.open && root.contains(form)) result.textContent = JSON.stringify(data, null, 2);
    } catch (error) { if (current === generation && drawer.open && root.contains(form)) result.textContent = error.message; }
    finally { pending = false; submit.disabled = false; }
  });
}
const auditOutcome = outcome => {
  const value = String(outcome ?? '').toLowerCase();
  if (value === 'success' || (/^\d{3}$/.test(value) && Number(value) >= 200 && Number(value) < 300)) return 'success';
  if (['failed', 'failure', 'blocked', 'error', 'denied'].includes(value) || (/^[45]\d{2}$/.test(value))) return 'failure';
  return 'unknown';
};
const auditTime = timestamp => {
  if (typeof timestamp !== 'string' || !/(Z|[+-]\d{2}:\d{2})$/i.test(timestamp) || Number.isNaN(Date.parse(timestamp))) return '时间未记录 / 无时区';
  return `${timeText(timestamp)}（北京时间）`;
};
function renderAudit() {
  const lines = states.audit.data?.items || [];
  const root = byId('audit-list');
  const query = byId('audit-search').value.trim().toLowerCase();
  root.replaceChildren();
  const labels = { success: '成功', failure: '失败 / 拦截', unknown: '未知' };
  const overview = byId('main-content').dataset.route === 'overview';
  let matched = 0;
  for (const line of [...lines].reverse()) {
    let entry;
    try { entry = typeof line === 'string' ? JSON.parse(line) : line; } catch { entry = null; }
    if (!entry || typeof entry !== 'object') entry = { action: '无法解析的旧审计记录', outcome: 'unknown' };
    const result = auditOutcome(entry.outcome);
    const searchable = [entry.action, entry.route, entry.outcome, entry.request_id, entry.method, entry.remote].join(' ').toLowerCase();
    if ((auditFilter !== 'all' && result !== auditFilter) || (query && !searchable.includes(query))) continue;
    matched += 1;
    if (overview && matched > 5) continue;
    const row = itemButton(entry.action || '审计事件', `${auditTime(entry.timestamp)} · ${entry.method || ''} ${entry.route || ''} · ${entry.outcome ?? '结果未记录'}`, labels[result], () => openDrawer(entry.action || '审计事件', '日志记录不等同于外部业务执行成功；HTTP 成功仅表示该 API 返回成功。', [`时间：${auditTime(entry.timestamp)}`, `结果：${entry.outcome ?? '未记录'}`, `方法：${entry.method || '未记录'}`, `路由：${entry.route || '未记录'}`, `请求 ID：${entry.request_id || '未记录'}`, `来源：${entry.remote || '未记录'}`]));
    row.querySelector('.badge').classList.add(result === 'success' ? 'good' : result === 'failure' ? 'failure' : 'warn');
    root.append(row);
  }
  if (!root.children.length) root.append(element('p', 'empty', states.audit.data ? '当前范围内没有匹配的审计记录。' : '等待审计数据。'));
  byId('metric-audit').textContent = states.audit.data ? lines.length : '—';
  const range = states.audit.data ? `本次返回 ${lines.length} 条，筛选匹配 ${matched} 条。${overview ? `总览显示最近 ${Math.min(matched, 5)} 条，进入审计详情查看完整匹配。` : '显示本次返回的全部匹配记录。'}` : '等待审计数据。';
  byId('audit-range').textContent = `${range}服务端最多返回 ${states.audit.data?.limit ?? 30} 条，尾部窗口 ${states.audit.data?.max_bytes ?? 65536} 字节。${states.audit.data?.truncated ? '本次窗口未覆盖完整日志。' : ''}不是全量或今日统计。`;
}
function applySearch() {
  const query = byId('command-search').value.trim().toLowerCase();
  const overview = byId('main-content').dataset.route === 'overview';
  [['connector-list', 'connector-summary'], ['workflow-list', 'workflow-summary']].forEach(([id, summaryId]) => {
    const root = byId(id);
    root.querySelector('[data-search-empty]')?.remove();
    const buttons = [...root.querySelectorAll('.item-button')];
    let matched = 0;
    buttons.forEach(node => {
      const matches = !query || node.textContent.toLowerCase().includes(query);
      if (matches) matched += 1;
      node.hidden = !matches || (overview && !query && matched > 4);
    });
    if (buttons.length && !matched) { const empty = element('p', 'empty', '没有匹配的结果。'); empty.dataset.searchEmpty = ''; root.append(empty); }
    byId(summaryId).textContent = !buttons.length ? '' : query ? `匹配 ${matched} / ${buttons.length} 项，已展示全部匹配。` : overview && buttons.length > 4 ? `展示 4 / ${buttons.length} 项，搜索可查看全部匹配，或进入详情。` : `共 ${buttons.length} 项。`;
  });
}
function renderDevices() {
  const devices = availableDevices();
  byId('metric-devices').textContent = devices ? `${devices.filter(deviceOnline).length} / ${devices.length}` : '—';
  const root = byId('device-summary');
  root.replaceChildren();
  if (!devices?.length) { root.append(element('p', 'empty', devices ? '暂无有效设备，请到设备管理添加或查看。' : '等待设备列表。')); return; }
  devices.slice(0, 3).forEach(item => {
    const row = element('div', 'device-summary-row');
    const description = element('div');
    description.append(element('strong', '', item.name || item.id || '未命名设备'), element('small', '', `固件：${item.firmware || '未知'}`));
    const disabled = item.state === 'disabled' || item.enabled === false || item.disabled === true;
    row.append(description, element('span', `badge ${deviceOnline(item) ? 'good' : ''}`, disabled ? '已禁用' : deviceOnline(item) ? '在线' : item.state === 'offline' ? '离线' : '状态未知'));
    root.append(row);
  });
  if (devices.length > 3) root.append(link(`查看全部 ${devices.length} 台设备`, '#/devices'));
}
function render() {
  renderConnectors(); renderStatus(); renderAudit(); applySearch(); renderDevices();
  renderFreshness();
}
function closeNavigation(restoreFocus = false) {
  byId('nav-toggle').setAttribute('aria-expanded', 'false');
  byId('main-nav').classList.remove('is-open');
  if (restoreFocus) byId('nav-toggle').focus();
}
const frame = () => byId('embed-frame');
// 网关对页面响应统一携带 X-Frame-Options: DENY，iframe 直接以 src 加载子页面会被浏览器拒绝；
// 同源 fetch 后经 srcdoc 注入，srcdoc 文档继承父源，不受该头限制。
const loadEmbedSource = async route => {
  if (!embedCache.has(route)) {
    const response = await fetch(embedRoutes[route], { credentials: 'same-origin', cache: 'no-store' });
    if (!response.ok) throw new Error(`功能页面读取失败（HTTP ${response.status}）`);
    embedCache.set(route, `<!doctype html>${await response.text()}`);
  }
  return embedCache.get(route);
};
// 同源前提下接管子页面导航：返回控制台、功能页互跳改走父级路由，独立页（assistant）保持顶层打开。
const EMBED_ROUTE_KEYS = Object.keys(embedRoutes);
const decorateFrameLinks = () => {
  const anchorId = embedAnchor.startsWith('#') ? decodeURIComponent(embedAnchor.slice(1)) : '';
  embedAnchor = '';
  let doc;
  try { doc = frame().contentDocument; } catch { return; }
  if (!doc) return;
  doc.querySelectorAll('a[href^="/static/"]').forEach(node => node.addEventListener('click', event => {
    const href = node.getAttribute('href');
    if (href.includes('admin_console_demo')) { event.preventDefault(); parent.location.hash = '#/overview'; return; }
    const route = EMBED_ROUTE_KEYS.find(key => href.startsWith(embedRoutes[key]));
    if (!route) return;
    event.preventDefault();
    const target = new URL(href, location.origin);
    embedAnchor = target.hash || '';
    parent.location.hash = `#/${route}`;
  }));
  if (anchorId) doc.getElementById(anchorId)?.scrollIntoView({ block: 'start' });
};
const syncFrame = async route => {
  const element = frame();
  if (element.dataset.route === route && element.srcdoc) return;
  const current = ++embedGeneration;
  element.dataset.route = route;
  element.removeAttribute('srcdoc');
  try {
    const source = await loadEmbedSource(route);
    if (current !== embedGeneration) return;
    element.srcdoc = source;
  } catch (error) {
    if (current !== embedGeneration) return;
    element.srcdoc = `<!doctype html><html lang="zh-CN"><body style="font:14px/1.6 system-ui;background:#07070a;color:#eeeef4;display:grid;place-items:center;height:100vh"><p>${error.message}，请刷新重试。</p></body></html>`;
  }
};
function navigate() {
  let route = location.hash.replace(/^#\/?/, '').split('/')[0] || 'overview';
  if (!Object.hasOwn(routes, route)) { route = 'overview'; history.replaceState(null, '', '#/overview'); }
  closeDrawer();
  closeNavigation();
  byId('main-content').dataset.route = route;
  if (embedRoutes[route]) syncFrame(route);
  if (route === 'settings' && !byId('settings-controls').children.length) settingsPanel.load();
  byId('page-title').textContent = routes[route][0]; byId('page-description').textContent = routes[route][1];
  document.querySelectorAll('[data-views]').forEach(node => { node.hidden = !node.dataset.views.split(' ').includes(route); });
  document.querySelectorAll('.nav [data-route]').forEach(node => { if (node.dataset.route === route) node.setAttribute('aria-current', 'page'); else node.removeAttribute('aria-current'); });
  renderAudit(); applySearch();
}
byId('nav-toggle').addEventListener('click', () => {
  const open = byId('nav-toggle').getAttribute('aria-expanded') !== 'true';
  byId('nav-toggle').setAttribute('aria-expanded', String(open));
  byId('main-nav').classList.toggle('is-open', open);
});
document.querySelector('.sidebar').addEventListener('click', event => { if (event.target.closest('a')) closeNavigation(); });
document.addEventListener('keydown', event => { if (event.key === 'Escape' && byId('nav-toggle').getAttribute('aria-expanded') === 'true') closeNavigation(true); });
byId('admin-load').addEventListener('click', () => load());
byId('settings-reload').addEventListener('click', () => settingsPanel.load());
byId('admin-refresh').addEventListener('click', () => load());
byId('local-login').addEventListener('click', async () => {
  if (busy) return;
  resetAuthentication('正在建立本机管理员会话。', true);
  setBusy(true);
  const current = generation;
  try { await api.request('/api/auth/local-login', { method: 'POST', authenticate: false }); if (current === generation) { setBusy(false); await load(); } }
  catch (error) { if (current === generation) { byId('page-message').textContent = error.message; setBusy(false); } }
});
tokenInput.addEventListener('input', () => resetAuthentication('认证信息已改变，点击加载状态重新读取。'));
tokenInput.addEventListener('keydown', event => { if (event.key === 'Enter') load(); });
byId('command-search').addEventListener('input', applySearch);
byId('audit-search').addEventListener('input', renderAudit);
document.querySelectorAll('[data-filter]').forEach(button => button.addEventListener('click', () => { auditFilter = button.dataset.filter; document.querySelectorAll('[data-filter]').forEach(node => node.setAttribute('aria-pressed', String(node === button))); renderAudit(); }));
byId('drawer-close').addEventListener('click', closeDrawer);
drawer.addEventListener('click', event => { if (event.target === drawer) { const rect = drawer.getBoundingClientRect(); if (event.clientX < rect.left || event.clientX > rect.right) closeDrawer(); } });
drawer.addEventListener('close', () => { if (!drawer.open) { byId('drawer-content').replaceChildren(); (previousFocus?.isConnected ? previousFocus : byId('main-content')).focus(); } });
document.addEventListener('keydown', event => { if (event.key === '/' && !drawer.open && !['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName) && !byId('command-search').closest('[hidden]')) { event.preventDefault(); byId('command-search').focus(); } });
window.addEventListener('hashchange', navigate);
byId('embed-frame').addEventListener('load', decorateFrameLinks);
document.addEventListener('visibilitychange', () => { if (document.hidden) clearTimeout(refreshTimer); else { renderFreshness(); if (authenticated && !drawer.open) load(); else scheduleRefresh(); } });
window.addEventListener('pagehide', () => resetAuthentication('页面已离开，请重新加载状态。', true));
window.addEventListener('pageshow', event => { if (event.persisted) load(); });
setInterval(renderFreshness, 5000);
// 本机打开时先静默认证再读取数据，避免每次打开都显示“认证失效 / 设备未知”。
async function bootstrap() {
  if (['localhost', '127.0.0.1', '[::1]'].includes(location.hostname) && !tokenInput.value.trim()) {
    try { await api.request('/api/auth/local-login', { method: 'POST', authenticate: false }); } catch { /* 非本机会话保持登录面板 */ }
  }
  load();
}
navigate(); render(); bootstrap();
