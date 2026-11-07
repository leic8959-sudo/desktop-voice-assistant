import { createConsoleApi, element } from './console_api.js?v=desktop-20261104';
const byId = id => document.getElementById(id);
const token = byId('desktop-token');
let generation = 0;
let authenticated = false;
let timer = null;
let snapshotAt = 0;
let requestKey = '';
let requestParameters = '';
let busy = false;
let poll;
const api = createConsoleApi(() => token.value, () => clear('管理员认证失效，请重新登录。'));
function clear(message) {
  generation += 1; authenticated = false; api.cancel(); clearTimeout(poll); timer = null; snapshotAt = 0; requestKey = ''; token.value = '';
  ['desktop-devices', 'desktop-timer-actions', 'desktop-reminders'].forEach(id => byId(id).replaceChildren());
  ['desktop-today-state', 'desktop-weather-state', 'desktop-delivery'].forEach(id => { byId(id).textContent = '尚未读取'; });
  byId('desktop-state').textContent = message; byId('desktop-timer-status').textContent = '尚未加载'; renderClock();
}
function renderClock() {
  if (!timer) { byId('desktop-timer-clock').textContent = '--:--:--'; return; }
  let remaining = timer.remaining_s;
  if (timer.status === 'running') remaining = Math.max(0, remaining - Math.floor((performance.now() - snapshotAt) / 1000));
  byId('desktop-timer-clock').textContent = [Math.floor(remaining / 3600), Math.floor(remaining / 60) % 60, remaining % 60].map(value => String(value).padStart(2, '0')).join(':');
  if (timer.status === 'running' && remaining === 0) byId('desktop-timer-status').textContent = '时间已到，等待后台确认状态。';
}
function renderTimer() {
  byId('desktop-timer-actions').replaceChildren();
  const labels = { running: '计时中', paused: '已暂停', finished: '计时结束', cancelled: '已取消' };
  byId('desktop-timer-status').textContent = timer ? `${timer.title} · ${labels[timer.status] || '未知'}` : '暂无计时器';
  if (timer && ['running', 'paused', 'finished'].includes(timer.status)) {
    const actions = timer.status === 'finished' ? ['cancel'] : [timer.status === 'paused' ? 'resume' : 'pause', 'cancel'];
    for (const action of actions) {
      const button = element('button', '', { resume: '继续', pause: '暂停', cancel: timer.status === 'finished' ? '知道了' : '取消计时' }[action]);
      button.addEventListener('click', () => changeTimer(action)); byId('desktop-timer-actions').append(button);
    }
  }
  renderClock();
}
function showDelivery(data) {
  const delivery = data.delivery || data;
  byId('desktop-delivery').textContent = delivery.message || delivery.status || '未获得设备回执';
}
function renderDevicePreferences(values) {
  for (const [id, key] of [['conversation-volume', 'conversation_volume'], ['reminder-volume', 'reminder_volume'], ['quiet-start', 'quiet_start'], ['quiet-end', 'quiet_end'], ['brightness', 'brightness'], ['night-brightness', 'night_brightness'], ['night-start', 'night_start'], ['night-end', 'night_end'], ['screen-off-seconds', 'screen_off_seconds']]) byId(id).value = values[key];
  for (const [id, key] of [['conversation-muted', 'conversation_muted'], ['reminders-muted', 'reminders_muted'], ['dnd-enabled', 'dnd_enabled'], ['night-mode-enabled', 'night_mode_enabled']]) byId(id).checked = values[key];
  byId('device-battery-state').textContent = `电量：${values.battery?.status === 'known' ? `${values.battery.percent}%` : '未知（未发现可验证的 AXP2101 读数接口）'}`;
}
function renderCompletions(items) {
  const root = byId('desktop-completions'); root.replaceChildren();
  for (const item of items || []) {
    const article = element('article'); article.append(element('p', '', `${item.title} · ${item.status}`), element('p', 'muted', item.detail));
    if (item.unread) { const button = element('button', '', '标记已读'); button.addEventListener('click', async () => { button.disabled = true; try { await api.request(`/api/screen/completions/${encodeURIComponent(item.id)}/ack`, { method: 'POST', body: {} }); article.remove(); } catch (error) { button.disabled = false; byId('desktop-delivery').textContent = `完成提醒确认失败：${error.message}`; } }); article.append(button); }
    root.append(article);
  }
  if (!(items || []).length) root.append(element('p', 'muted', '暂无未读完成提醒。'));
}
async function load() {
  if (busy) return;
  busy = true; const current = generation;
  try {
    const data = await api.request('/api/screen/state');
    if (current !== generation) return;
    authenticated = true; timer = data.timer; snapshotAt = performance.now(); renderTimer();
    renderDevicePreferences(data.device_preferences || {});
    renderCompletions(data.completions || []);
    byId('desktop-state').textContent = '已读取后台状态；每5秒更新本地状态，未自动访问第三方。';
    const devices = data.devices || [];
    byId('desktop-devices').replaceChildren(...devices.map(device => element('p', 'muted', `${device.device_id} · ${device.supports_cards ? '支持信息卡' : '固件不支持新卡片，需维护更新'}${device.last_receipt ? ` · 最近回执：${{ shown: '已绘制（未做实机视觉验收）', queued: '等待空闲显示', expired: '已过期' }[device.last_receipt.status] || '未知'} · ${device.last_receipt.timestamp}` : ''}`)));
    if (!devices.length) byId('desktop-devices').append(element('p', 'notice', '没有在线设备；计时仍可在后台保存。'));
  } catch (error) { if (current === generation) byId('desktop-state').textContent = `状态读取失败：${error.message}`; }
  finally { busy = false; schedule(); }
}
function schedule() { clearTimeout(poll); if (authenticated && !document.hidden) poll = setTimeout(load, 5000); }
async function changeTimer(action) {
  if (!timer || busy || !authenticated) return;
  busy = true; const current = generation;
  try {
    const result = await api.request(`/api/screen/timer/${action}`, { method: 'POST', body: { timer_id: timer.id, revision: timer.revision } });
    if (current === generation) { timer = result.timer; snapshotAt = performance.now(); renderTimer(); showDelivery(result); }
  } catch (error) { if (current === generation) byId('desktop-timer-status').textContent = `操作未确认：${error.message}。请刷新状态，不自动重试。`; }
  finally { busy = false; schedule(); }
}
byId('desktop-timer-form').addEventListener('submit', async event => {
  event.preventDefault(); if (!authenticated || busy) return;
  const title = byId('desktop-timer-title').value.trim(); const minutes = Number(byId('desktop-timer-minutes').value);
  if (!Number.isInteger(minutes) || minutes < 1 || minutes > 1440) return;
  const parameters = JSON.stringify({ title, seconds: minutes * 60 });
  if (!requestKey || requestParameters !== parameters) { requestKey = Array.from(crypto.getRandomValues(new Uint8Array(16)), value => value.toString(16).padStart(2, '0')).join(''); requestParameters = parameters; }
  busy = true; const current = generation;
  try {
    const result = await api.request('/api/screen/timer', { method: 'POST', body: { title, seconds: minutes * 60, request_id: requestKey } });
    if (current === generation) { timer = result.timer; snapshotAt = performance.now(); renderTimer(); showDelivery(result); requestKey = ''; }
  } catch (error) { if (current === generation) byId('desktop-timer-status').textContent = `创建结果未确认：${error.message}。刷新可核对；同参数重试沿用原请求 ID。`; }
  finally { busy = false; schedule(); }
});
async function manualRead(path, body, output, button) {
  if (!authenticated || busy) return;
  busy = true; button.disabled = true; const current = generation; output.textContent = '正在读取，等待后台与设备回执…';
  try { const result = await api.request(path, { method: 'POST', body, timeout: 30000 }); if (current === generation) { output.textContent = result.delivery.message; showDelivery(result); } }
  catch (error) { if (current === generation) output.textContent = `读取失败：${error.message}。可手动重试读取。`; }
  finally { busy = false; button.disabled = false; schedule(); }
}
byId('desktop-today').addEventListener('click', event => manualRead('/api/screen/today', { calendar_connector: byId('desktop-calendar').value }, byId('desktop-today-state'), event.currentTarget));
byId('desktop-weather').addEventListener('click', event => manualRead('/api/screen/weather', { city: byId('desktop-weather-city').value }, byId('desktop-weather-state'), event.currentTarget));
byId('device-preferences-form').addEventListener('submit', async event => {
  event.preventDefault(); if (!authenticated || busy) return;
  busy = true; const current = generation; const body = { conversation_volume: Number(byId('conversation-volume').value), reminder_volume: Number(byId('reminder-volume').value), conversation_muted: byId('conversation-muted').checked, reminders_muted: byId('reminders-muted').checked, dnd_enabled: byId('dnd-enabled').checked, quiet_start: byId('quiet-start').value, quiet_end: byId('quiet-end').value, brightness: Number(byId('brightness').value), night_brightness: Number(byId('night-brightness').value), night_mode_enabled: byId('night-mode-enabled').checked, night_start: byId('night-start').value, night_end: byId('night-end').value, screen_off_seconds: Number(byId('screen-off-seconds').value) };
  try { const result = await api.request('/api/device/preferences', { method: 'PUT', body }); if (current === generation) { renderDevicePreferences(result.preferences); byId('device-preferences-state').textContent = `已保存；设备已接收 ${result.sent_devices} 台，其他设备下次连接同步。未收到硬件应用回执。`; } }
  catch (error) { if (current === generation) byId('device-preferences-state').textContent = `保存失败：${error.message}`; }
  finally { busy = false; schedule(); }
});
byId('desktop-reminders-refresh').addEventListener('click', async () => {
  if (!authenticated || busy) return;
  busy = true; const current = generation;
  try {
    const result = await api.request('/api/screen/reminders'); if (current !== generation) return;
    byId('desktop-reminders').replaceChildren();
    for (const item of result.items || []) {
      const article = element('article'); article.append(element('p', '', item.title));
      const button = element('button', '', '稍后5分钟');
      button.addEventListener('click', async () => {
        if (button.disabled) return; button.disabled = true;
        try { await api.request(`/api/tasks/${encodeURIComponent(item.id)}/snooze`, { method: 'POST', body: { updated_at: item.updated_at } }); if (current === generation) article.replaceChildren(element('p', '', '已稍后5分钟提醒')); }
        catch (error) { if (current === generation) article.append(element('p', 'notice', `操作未确认：${error.message}。刷新后核查。`)); }
      }); article.append(button); byId('desktop-reminders').append(article);
    }
    if (!result.items?.length) byId('desktop-reminders').append(element('p', 'muted', '当前没有到期提醒。'));
  } catch (error) { if (current === generation) byId('desktop-reminders').textContent = error.message; }
  finally { busy = false; schedule(); }
});
byId('desktop-login').addEventListener('click', async () => {
  if (busy) return; clear('正在建立本机会话…'); const current = generation;
  try { await api.request('/api/auth/local-login', { method: 'POST', authenticate: false }); if (current === generation) load(); }
  catch (error) { if (current === generation) byId('desktop-state').textContent = error.message; }
});
byId('desktop-refresh').addEventListener('click', load);
token.addEventListener('input', () => { const value = token.value; clear('令牌改变，请重新加载。'); token.value = value; });
document.addEventListener('visibilitychange', () => { if (document.hidden) clearTimeout(poll); else if (authenticated) load(); });
window.addEventListener('pagehide', () => clear('页面已离开。'));
window.addEventListener('pageshow', event => { if (event.persisted) load(); });
setInterval(renderClock, 1000); load();
