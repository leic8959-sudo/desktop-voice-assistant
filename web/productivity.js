import { createConsoleApi, element } from './console_api.js';

const byId = id => document.getElementById(id);
const api = createConsoleApi(() => byId('token').value, () => reset('认证失效，请重新登录。'));
const zones = ['Asia/Shanghai', 'Europe/Moscow', 'UTC', 'America/New_York', 'Europe/London'];
let snapshot = {}, devices = [], editingScene = null, generation = 0, timezoneInitialized = false;
const pendingRequests = new Map();
const labels = { active: '启用', paused: '已暂停', cancelled: '已取消', pending: '待处理', baseline: '已建立基线', unchanged: '未变化', changed: '有变化', checked: '读取成功', catching_up: '正在有界追赶下一页', unknown: '未知，需核对', not_checked: '尚未读取', completed: '已完成', no_device: '无设备连接', sent: '已发送，显示待设备确认', unsupported_firmware: '固件不支持卡片' };
const show = data => { byId('output').textContent = typeof data === 'string' ? data : JSON.stringify(data, null, 2); };
const clock = value => value ? new Date(value).toLocaleString('zh-CN', { timeZone: snapshot.timezone || 'Asia/Shanghai', hour12: false }) : '等待本次处理后生成';
const button = (title, operation) => { const node = element('button', '', title); node.type = 'button'; node.addEventListener('click', () => run(node, operation)); return node; };
const options = (node, entries, selected = '') => { node.replaceChildren(); for (const [value, title] of entries) { const option = element('option', '', title); option.value = value; option.selected = value === selected; node.append(option); } };
const field = (title, input) => { const label = element('label', '', title); label.append(input); return label; };
const zoneSelect = value => { const node = element('select'); options(node, [...new Set([...zones, value].filter(Boolean))].map(x => [x, x]), value); return node; };
const data = form => Object.fromEntries(new FormData(form));
const requestId = () => { const bytes = new Uint8Array(16); crypto.getRandomValues(bytes); return Array.from(bytes, value => value.toString(16).padStart(2, '0')).join(''); };

function reset(message) {
  generation += 1; api.cancel(); snapshot = {}; devices = []; editingScene = null; pendingRequests.clear();
  for (const id of ['routines', 'subscriptions', 'scenes', 'events', 'summaries', 'steps']) byId(id).replaceChildren();
  byId('scene-preview').hidden = true; byId('state').textContent = message;
  byId('speech-form').reset(); byId('speech-state').textContent = '尚未读取播报授权。';
}

async function run(node, operation) {
  node.disabled = true;
  try { await operation(); }
  catch (error) { show(error.message); byId('state').textContent = error.message; }
  finally { node.disabled = node.dataset.finalized === 'true'; }
}

async function write(path, body) {
  // 网络超时后同参数继续使用同一个幂等键；成功后才移除。
  const signature = JSON.stringify([path, body]);
  const key = pendingRequests.get(signature) || requestId(); pendingRequests.set(signature, key);
  const result = await api.request(path, { method: 'POST', body: { ...body, idempotency_key: key }, timeout: 35000 });
  pendingRequests.delete(signature); return result;
}

async function load() {
  const current = ++generation;
  byId('state').textContent = '正在读取本地状态…';
  const values = await Promise.all(['routines', 'summaries', 'subscriptions', 'scenes', 'events', 'speech'].map(category => api.request(`/api/productivity/${category}`)));
  if (current !== generation) return;
  snapshot = { routines: values[0].items, summaries: values[1].items, subscriptions: values[2].items, scenes: values[3].items, events: values[4].items, timezone: values[0].timezone, speech: values[5].item };
  if (!timezoneInitialized) { options(document.querySelector('#routine-form [name=timezone]'), [...new Set([...zones, snapshot.timezone])].map(value => [value, value]), snapshot.timezone); timezoneInitialized = true; }
  render(); byId('state').textContent = `已读取 · ${new Date().toLocaleTimeString('zh-CN')} · 时区 ${snapshot.timezone}`;
}

function renderRoutines() {
  const root = byId('routines'); root.replaceChildren();
  for (const item of snapshot.routines) {
    const card = element('article', 'item');
    const frequency = { daily: '每天', weekly: '每周', monthly: '每月', interval: `每${item.rule.interval_minutes}分钟` }[item.rule.frequency];
    card.append(element('h3', '', item.title), element('p', '', `${frequency} ${item.rule.time || ''} · ${labels[item.status] || item.status} · ${item.timezone}`), element('p', '', `下一次：${item.next_at ? new Date(item.next_at).toLocaleString('zh-CN', { timeZone: item.timezone, hour12: false }) : '等待本次处理后生成'}`));
    const actions = element('div', 'actions');
    const act = async (action, occurrence) => { show(await write(`/api/productivity/routines/${item.id}/${action}`, { revision: item.revision, ...(occurrence ? { occurrence_id: occurrence.id } : {}) })); await load(); };
    if (item.status !== 'cancelled') {
      actions.append(button(item.status === 'paused' ? '恢复' : '暂停', () => act(item.status === 'paused' ? 'resume' : 'pause')), button('取消规则', () => act('cancel')));
      if (!item.occurrences.length && item.next_at) actions.append(button('跳过下一次', () => act('skip')));
      for (const occurrence of item.occurrences) { card.append(element('p', '', `本次待处理：${clock(occurrence.scheduled_at)}`)); actions.append(button('本次已完成', () => act('complete', occurrence)), button('跳过本次', () => act('skip', occurrence))); }
    } else actions.append(button('删除已取消规则', () => act('delete')));
    card.append(actions); root.append(card);
  }
  if (!root.children.length) root.append(element('p', 'hint', '尚无重复提醒。'));
}

function renderSummaries() {
  const root = byId('summaries'); root.replaceChildren();
  for (const item of snapshot.summaries) {
    const form = element('form', 'summary-settings');
    const heading = element('h3', 'wide', item.slot === 'morning' ? '早间摘要' : '晚间摘要');
    const enabled = element('input'); enabled.type = 'checkbox'; enabled.checked = Boolean(item.enabled);
    const time = element('input'); time.type = 'time'; time.value = item.time; time.required = true;
    const timezone = zoneSelect(item.timezone);
    const calendar = element('select'); options(calendar, [['', '仅本地待办'], ['google_calendar', 'Google日历'], ['microsoft_calendar', 'Microsoft日历']], item.calendar_connector);
    const weather = element('select'); options(weather, [['', '不读取天气'], ['110000', '北京'], ['310000', '上海'], ['440100', '广州'], ...(item.weather_city && !['110000', '310000', '440100'].includes(item.weather_city) ? [[item.weather_city, item.weather_city]] : [])], item.weather_city);
    const save = element('button', 'primary', '保存'); save.type = 'submit';
    const body = () => ({ slot: item.slot, enabled: enabled.checked, time: time.value, timezone: timezone.value, calendar_connector: calendar.value, weather_city: weather.value, revision: item.revision });
    form.append(heading, field('启用', enabled), field('每天时间', time), field('时区', timezone), field('日历来源', calendar), field('天气城市', weather), save, button('立即读取此摘要', async () => { show(await api.request('/api/productivity/summary', { method: 'POST', body: body(), timeout: 140000 })); await load(); }), element('small', 'wide', `下次：${clock(item.next_at)}`));
    form.addEventListener('submit', event => { event.preventDefault(); run(save, async () => { show(await write('/api/productivity/summaries', body())); await load(); }); }); root.append(form);
  }
}

function renderSubscriptions() {
  const root = byId('subscriptions'); root.replaceChildren();
  for (const item of snapshot.subscriptions) {
    const card = element('article', 'item'); const actions = element('div', 'actions');
    const names = { meeting: '会议提前提醒', mail: '指定邮件', github_ci: 'GitHub CI异常', ha_state: 'HA传感器' };
    card.append(element('h3', '', `${names[item.kind] || item.kind} · ${item.target}`), element('p', '', `${item.enabled ? '订阅中' : '已暂停'} · ${labels[item.last_status] || item.last_status} · 上次读取：${item.last_checked ? clock(item.last_checked) : '尚无'}`));
    if (item.filters) {
      const filters = item.filters;
      const details = [filters.provider, `轮询${item.interval_s}秒`, `合并${filters.merge_window_s}秒`, `通知间隔${filters.min_notify_interval_s}秒`, `待通知${item.pending_notifications}条`];
      if (item.kind === 'meeting') details.push(`提前${filters.lead_minutes}分钟`, filters.timezone);
      if (filters.sender) details.push(`发件人 ${filters.sender}`);
      if (filters.subject_contains) details.push(`主题包含 ${filters.subject_contains}`);
      if (item.kind === 'github_ci') details.push(`分支 ${filters.branch || '全部'}`, `结果 ${filters.conclusions.join(' / ')}`);
      card.append(element('p', 'hint', details.join(' · ')), element('p', 'hint', `下一次读取：${clock(item.next_at)}${item.watermark ? ' · 水位：' + clock(item.watermark) : ''}`));
      if (item.last_error) card.append(element('p', 'error', item.last_error));
    }
    if (item.scene_id) card.append(element('p', 'hint', '状态变化后仅提示预览场景，尚未自动执行。'));
    const act = async action => { show(await write(`/api/productivity/subscriptions/${item.id}/${action}`, { revision: item.revision })); await load(); };
    actions.append(button(item.enabled ? '暂停' : '恢复', () => act(item.enabled ? 'pause' : 'resume')), button('立即读取', async () => { show(await api.request(`/api/productivity/subscriptions/${item.id}/check`, { method: 'POST', body: {}, timeout: 110000 })); await load(); }), button('删除', () => act('delete'))); card.append(actions); root.append(card);
  }
  if (!root.children.length) root.append(element('p', 'hint', '尚无订阅。'));
  options(byId('subscription-scene'), [['', '仅通知'], ...snapshot.scenes.map(x => [x.id, x.name])]);
}

async function previewScene(id) {
  const current = generation;
  const result = await api.request(`/api/productivity/scenes/${encodeURIComponent(id)}/preview`, { method: 'POST', body: {}, timeout: 300000 });
  if (current !== generation) return;
  const root = byId('scene-preview'); root.replaceChildren(); root.hidden = false;
  root.append(element('h3', '', `确认场景：${result.name}`), element('p', 'hint', result.message));
  const list = element('ol'); result.steps.forEach(step => list.append(element('li', '', `${step.device_name} → ${step.action_label}${Object.keys(step.parameters).length ? ' · ' + Object.entries(step.parameters).map(([key, value]) => `${key}=${value}`).join('，') : ''}`))); root.append(list, element('p', 'hint', `确认凭证到期：${clock(result.expires_at)}`));
  const confirm = button('确认执行以上步骤', async () => {
    // 确认按钮只提交本次服务端凭证。网络失败不另造确认，重试读取同一执行账本。
    const outcome = await api.request('/api/productivity/scenes/confirm', { method: 'POST', body: { confirmation_id: result.confirmation_id, confirmed: true }, timeout: 450000 });
    show(outcome); confirm.textContent = outcome.status === 'completed' ? '本次已执行' : '本次结果需核对'; confirm.dataset.finalized = 'true'; confirm.disabled = true; await load();
  }); root.append(confirm, button('取消本次预览', () => { root.hidden = true; root.replaceChildren(); })); root.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function renderScenes() {
  const root = byId('scenes'); root.replaceChildren();
  for (const item of snapshot.scenes) {
    const card = element('article', 'item'); const actions = element('div', 'actions');
    card.append(element('h3', '', item.name), element('p', 'hint', `${item.steps.length}个步骤 · 修改时间 ${clock(item.updated_at)}`));
    actions.append(button('预览并确认', () => previewScene(item.id)), button('编辑', async () => { if (!devices.length) await loadHome(); editingScene = item; byId('scene-form').elements.name.value = item.name; byId('steps').replaceChildren(); item.steps.forEach(addStep); byId('cancel-edit').hidden = false; byId('scene-preview').hidden = true; }), button('删除', async () => { show(await write(`/api/productivity/scenes/${item.id}/delete`, { revision: item.revision })); await load(); })); card.append(actions); root.append(card);
  }
  if (!root.children.length) root.append(element('p', 'hint', '先读取已授权设备，然后添加场景步骤。'));
}

function renderEvents() {
  const root = byId('events'); root.replaceChildren();
  for (const item of snapshot.events) {
    const card = element('article', 'item'); const heading = element('h3', '', item.title); heading.append(element('span', 'badge', item.unread ? '未读' : '已读'));
    card.append(heading, element('p', '', item.detail), element('small', '', `${clock(item.created_at)} · 卡片 ${labels[item.delivery.status] || item.delivery.status || '无交付回执'}`));
    if (item.delivery.speech) { const speech = item.delivery.speech; const names = { sent: '音频已发送，设备播放未确认', skipped: '本次已跳过', cancelled: '本次已取消', failed: '合成或发送失败', pending: '状态未知或尚未结束', unknown: '结果未知' }; const reasons = { disabled: '未启用', expired_event: '提醒已过期', category_disabled: '此类内容未授权', speech_busy: '已有播报', voice_session_active: '正在对话', voice_session_started: '新对话已开始', device_busy: '设备忙碌', do_not_disturb: '免打扰时段', reminders_muted: '提醒已静音', conversation_output_muted: '会话音频已静音', device_offline: '设备离线', no_online_device: '无在线设备', multiple_devices: '多台设备在线，未选择播报目标', firmware_protocol_unknown: '设备协议版本未确认', tts_not_configured: '语音合成尚未配置', disabled_by_owner: '用户关闭播报', service_closed: '服务已关闭', publisher_closed: '本次通知已停止', cancelled: '本次已取消' }; card.append(element('p', 'hint', `主动语音：${names[speech.status] || '状态未知'}${speech.reason ? ' · ' + (reasons[speech.reason] || '本次条件不满足') : ''}`)); }
    const actions = element('div', 'actions');
    if (item.unread) actions.append(button('标为已读', async () => { await api.request(`/api/productivity/events/${item.id}/ack`, { method: 'POST', body: {} }); await load(); }));
    if (item.data.scene_id) actions.append(button('预览关联场景', () => previewScene(item.data.scene_id)));
    card.append(actions); root.append(card);
  }
  if (!root.children.length) root.append(element('p', 'hint', '暂无通知记录。'));
}

function renderSpeech() {
  const form = byId('speech-form'); form.elements.enabled.checked = snapshot.speech.enabled;
  form.querySelectorAll('[name=kind]').forEach(input => { input.checked = snapshot.speech.kinds.includes(input.value); });
  byId('speech-state').textContent = snapshot.speech.enabled ? '已授权；仅在空闲、非免打扰且有单一兼容设备时播报。' : '主动语音关闭；通知继续保留屏幕卡片和事件记录。';
}
function render() { renderRoutines(); renderSummaries(); renderSubscriptions(); renderScenes(); renderEvents(); renderSpeech(); }

async function loadHome() {
  const loaded = []; let cursor = '';
  for (let page = 0; page < 4; page += 1) {
    const result = await api.request(`/api/productivity/home-catalog${cursor ? '?cursor=' + encodeURIComponent(cursor) : ''}`, { timeout: 35000 });
    if (result.stale || result.discovery_error) throw new Error('家居目录读取失败，不能用旧目录添加场景。');
    loaded.push(...(result.devices || [])); cursor = result.next_cursor || ''; if (!cursor) break;
  }
  devices = loaded.slice(0, 100);
  byId('home-state').textContent = `本次返回${devices.length}个已授权设备；只有当前可用且支持的动作可选。`;
  if (!byId('steps').children.length && devices.length) addStep();
}

function addStep(saved = null) {
  const root = byId('steps');
  if (root.children.length >= 10) throw new Error('场景最多10步。');
  const section = element('div', 'step');
  const device = element('select'); const action = element('select'); const parameters = element('div', 'parameters');
  options(device, [['', '选择设备'], ...devices.filter(x => x.available).map(x => [x.device_id, x.name])], saved?.device_id);
  const renderParameters = () => {
    parameters.replaceChildren(); const item = devices.find(x => x.device_id === device.value); const command = item?.actions.find(x => x.id === action.value);
    for (const [name, definition] of Object.entries(command?.parameters?.properties || {})) {
      const input = element(definition.enum ? 'select' : 'input'); input.dataset.parameter = name; input.dataset.type = definition.type || 'string';
      if (definition.enum) options(input, definition.enum.map(value => [String(value), definition.enumLabels?.[value] || String(value)]), saved?.parameters?.[name] === undefined ? String(definition.enum[0]) : String(saved.parameters[name]));
      else {
        input.type = definition.type === 'number' || definition.type === 'integer' ? 'number' : 'text';
        if (definition.minimum !== undefined) input.min = definition.minimum;
        if (definition.maximum !== undefined) input.max = definition.maximum;
        input.step = definition.multipleOf || (definition.type === 'integer' ? '1' : 'any');
        input.value = saved?.parameters?.[name] ?? definition.default ?? '';
      }
      input.required = command.parameters.required?.includes(name) || false;
      const title = { temperature: '设定温度', hvac_mode: '空调模式', state: '目标开关状态' }[name] || definition.title || name;
      parameters.append(field(title, input));
    }
  };
  const renderActions = () => {
    const item = devices.find(x => x.device_id === device.value);
    options(action, [['', '选择动作'], ...(item?.actions || []).filter(x => x.permission !== 'not_authorized').map(x => [x.id, x.label || x.id])], saved?.action);
    renderParameters();
  };
  device.required = true; action.required = true; device.addEventListener('change', renderActions); action.addEventListener('change', renderParameters);
  section.append(field('设备', device), field('动作', action), parameters, button('上移', () => { if (section.previousElementSibling) root.insertBefore(section, section.previousElementSibling); }), button('删除此步', () => section.remove()));
  section.readStep = () => {
    const values = {};
    for (const input of parameters.querySelectorAll('[data-parameter]')) {
      if (input.value === '' && !input.required) continue;
      values[input.dataset.parameter] = ['number', 'integer'].includes(input.dataset.type) ? Number(input.value) : input.dataset.type === 'boolean' ? input.value === 'true' : input.value;
    }
    return { device_id: device.value, action: action.value, parameters: values };
  };
  root.append(section); renderActions();
}

document.querySelectorAll('.timezone').forEach(node => options(node, zones.map(value => [value, value]), 'Asia/Shanghai'));
const routineForm = byId('routine-form');
const showFrequency = () => {
  const frequency = routineForm.elements.frequency.value;
  routineForm.querySelectorAll('[data-frequency]').forEach(node => { node.hidden = node.dataset.frequency === 'clock' ? frequency === 'interval' : node.dataset.frequency !== frequency; });
};
routineForm.elements.frequency.addEventListener('change', showFrequency); showFrequency();
routineForm.addEventListener('submit', event => {
  event.preventDefault(); const submit = routineForm.querySelector('[type=submit]');
  run(submit, async () => {
    const values = data(routineForm); const rule = { frequency: values.frequency, anchor: values.anchor };
    if (values.frequency === 'interval') rule.interval_minutes = Number(values.interval_minutes);
    else rule.time = values.time;
    if (values.frequency === 'weekly') rule.weekdays = [...routineForm.querySelectorAll('[name=weekday]:checked')].map(input => Number(input.value));
    if (values.frequency === 'monthly') rule.month_day = Number(values.month_day);
    const payload = { title: values.title, timezone: values.timezone, rule, ...(values.start_at.trim() ? { start_at: values.start_at.trim() } : {}) };
    show(await write('/api/productivity/routines', payload)); routineForm.elements.title.value = ''; await load();
  });
});
const subscriptionForm = byId('subscription-form');
const showSubscriptionFields = () => {
  const kind = subscriptionForm.elements.kind.value;
  subscriptionForm.querySelectorAll('[data-subscription]').forEach(node => {
    const visible = node.dataset.subscription.split(' ').includes(kind); node.hidden = !visible;
    node.querySelectorAll('input,select').forEach(input => { input.disabled = !visible; });
  });
  const providers = kind === 'meeting' ? [['google_calendar', 'Google日历'], ['microsoft_calendar', 'Microsoft日历']] : [['gmail', 'Gmail'], ['microsoft_mail', 'Microsoft邮件']];
  options(subscriptionForm.elements.provider, providers);
  subscriptionForm.elements.target.required = ['github_ci', 'ha_state'].includes(kind);
  subscriptionForm.elements.interval_s.max = kind === 'meeting' ? '300' : '86400';
  if (kind === 'meeting' && Number(subscriptionForm.elements.interval_s.value) > 300) subscriptionForm.elements.interval_s.value = '300';
};
subscriptionForm.elements.kind.addEventListener('change', showSubscriptionFields); showSubscriptionFields();
subscriptionForm.addEventListener('submit', event => {
  event.preventDefault(); const form = event.currentTarget;
  run(form.querySelector('[type=submit]'), async () => {
    const values = data(form); values.interval_s = Number(values.interval_s);
    if (values.kind === 'meeting') { values.target = 'primary'; values.lead_minutes = Number(values.lead_minutes); }
    if (values.kind === 'mail') {
      values.target = 'inbox';
      if (!values.sender?.trim() && !values.subject_contains?.trim()) throw new Error('请填写发件人或主题关键词。');
    }
    if (values.kind !== 'ha_state') { values.merge_window_s = Number(values.merge_window_s); values.min_notify_interval_s = Number(values.min_notify_interval_s); }
    if (values.kind === 'github_ci') {
      values.conclusions = [...form.querySelectorAll('[name=conclusion]:checked')].map(input => input.value); delete values.conclusion;
      if (!values.conclusions.length) throw new Error('至少选择一种CI异常结果。');
    }
    show(await write('/api/productivity/subscriptions', values)); form.elements.target.value = ''; await load();
  });
});
byId('scene-form').addEventListener('submit', event => {
  event.preventDefault(); const form = event.currentTarget;
  run(form.querySelector('[type=submit]'), async () => {
    const steps = [...byId('steps').children].map(node => node.readStep());
    if (!steps.length) throw new Error('至少添加一步。');
    const body = { name: form.elements.name.value.trim(), steps, ...(editingScene ? { id: editingScene.id, revision: editingScene.revision } : {}) };
    show(await write('/api/productivity/scenes', body)); editingScene = null; form.reset(); byId('steps').replaceChildren(); byId('cancel-edit').hidden = true; byId('scene-preview').hidden = true; await load();
  });
});
byId('speech-form').addEventListener('submit', event => {
  event.preventDefault(); const form = event.currentTarget;
  run(form.querySelector('[type=submit]'), async () => { if (!snapshot.speech) throw new Error('请先认证并读取播报授权。'); const enabled = form.elements.enabled.checked; const kinds = [...form.querySelectorAll('[name=kind]:checked')].map(input => input.value); show(await write('/api/productivity/speech', { enabled, kinds, confirmed: enabled, revision: snapshot.speech.revision })); await load(); });
});
byId('add-step').addEventListener('click', () => { try { if (!devices.length) throw new Error('请先读取已授权家居设备。'); addStep(); } catch (error) { show(error.message); } });
byId('cancel-edit').addEventListener('click', () => { editingScene = null; byId('scene-form').reset(); byId('steps').replaceChildren(); byId('cancel-edit').hidden = true; });
byId('load-home').addEventListener('click', event => run(event.currentTarget, loadHome));
byId('refresh').addEventListener('click', event => run(event.currentTarget, load));
byId('summary-now').addEventListener('click', event => run(event.currentTarget, async () => { show(await api.request('/api/productivity/summary', { method: 'POST', body: {}, timeout: 90000 })); await load(); }));
byId('local-login').addEventListener('click', event => run(event.currentTarget, async () => { reset('正在登录…'); byId('token').value = ''; await api.request('/api/auth/local-login', { method: 'POST', authenticate: false }); await load(); }));
byId('token').addEventListener('input', () => reset('令牌已变更，请刷新。'));
window.addEventListener('pagehide', () => { reset('页面已离开'); byId('token').value = ''; });
load().catch(error => { byId('state').textContent = error.message; });
