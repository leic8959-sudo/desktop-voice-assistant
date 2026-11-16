import { createConsoleApi, element } from './console_api.js';

const byId = id => document.getElementById(id);
const adminToken = byId('music-admin-token');
const api = createConsoleApi(() => adminToken.value, () => clearAuthentication());
let authenticated = false;
let busy = false;
let poll = null;
let generation = 0;
let configDirty = false;
const message = (text, error = false) => { byId('music-message').textContent = text; byId('music-message').className = error ? 'error' : ''; };
const duration = seconds => Number.isFinite(seconds) ? `${Math.floor(seconds / 60)}:${String(Math.floor(seconds % 60)).padStart(2, '0')}` : '时长未知';
const controls = () => {
  for (const node of document.querySelectorAll('[data-action], [data-noise], #music-search-button, .track button')) node.disabled = !authenticated || busy;
  byId('music-save-config').disabled = !authenticated || busy || byId('music-save-config').dataset.editable === 'false';
};

const clearAuthentication = () => {
  authenticated = false; generation += 1; api.cancel(); clearInterval(poll); poll = null;
  byId('music-access-token').value = ''; byId('music-clear-token').checked = false;
  byId('music-results').replaceChildren(); byId('music-queue').replaceChildren();
  byId('music-title').textContent = '尚未认证'; byId('music-artist').textContent = '';
  byId('music-playback').textContent = '等待认证'; byId('music-device').textContent = '';
  controls(); message('认证已改变，请重新加载。');
};

const render = data => {
  const config = data.configuration || {};
  const stream = data.stream || {};
  const current = data.current;
  byId('music-title').textContent = current?.title || '尚未播放歌曲';
  byId('music-artist').textContent = current ? `${(current.artists || []).join(' / ')}${current.album ? ` · ${current.album}` : ''}` : '';
  const stateNames = { playing: '设备已回执：播放中', paused: '已暂停', preparing: '正在准备音频', starting: '已发送，等待设备回执', streaming: '正在传输，等待设备回执', stopped: '已停止', ended: '已结束', finished: '已结束', idle: '空闲', error: '播放失败' };
  const state = stream.state || 'idle';
  byId('music-playback').textContent = `${stateNames[state] || `服务状态：${state}`}${Number.isFinite(stream.device_position_s) ? ` · 设备进度 ${duration(stream.device_position_s)}` : ''}${data.pending ? ' · 等待本轮语音回答结束' : ''}`;
  const devices = Array.isArray(data.devices) ? data.devices.filter(item => item.online) : [];
  byId('music-device').textContent = devices.length === 1 ? devices[0].supports_music ? `伊迪斯在线 · 固件 ${devices[0].firmware}` : '设备在线，但当前固件尚未支持音乐通道' : devices.length ? '有多个在线设备，请先保留唯一播放设备' : '实体设备离线';
  if (!configDirty) {
    byId('music-base-url').value = config.base_url || 'http://127.0.0.1:8095';
    byId('music-stream-url').value = config.stream_base_url || 'http://127.0.0.1:8097';
  }
  byId('music-save-config').dataset.editable = String(config.available === true);
  byId('music-config-status').textContent = `${config.token_configured ? '访问令牌已保存（不回显）。' : '尚未保存访问令牌。'}${config.message || ''}`;
  const baseUrl = config.base_url || 'http://127.0.0.1:8095';
  try { const url = new URL(baseUrl); if (['http:', 'https:'].includes(url.protocol) && !url.username && !url.password) byId('music-open-ma').href = url.href; } catch { byId('music-open-ma').removeAttribute('href'); }
  const queue = byId('music-queue'); queue.replaceChildren();
  for (const [index, item] of (data.queue || []).entries()) { const node = element('div', 'track'); node.append(element('span', '', `${index + 1}. ${item.title}`), element('span', 'muted', duration(item.duration_s))); queue.append(node); }
  if (!(data.queue || []).length) queue.append(element('p', 'muted', '队列为空。'));
  if (data.error) message(typeof data.error === 'string' ? data.error : '播放发生错误，请查看设备状态。', true);
  controls();
};

const refresh = async () => { const expected = generation; const data = await api.request('/api/music/status'); if (expected !== generation) return; authenticated = true; render(data); };
const run = async action => { if (busy) return; busy = true; controls(); try { await action(); } catch (error) { message(error.message, true); } finally { busy = false; controls(); } };
const load = async () => { await refresh(); message('已读取真实服务状态。点击歌曲旁的播放按钮才会从实体喇叭发声。'); clearInterval(poll); poll = setInterval(() => { if (authenticated && !busy && !document.hidden) refresh().catch(error => message(error.message, true)); }, 5000); };
byId('music-refresh').onclick = () => run(load);
byId('music-login').onclick = () => run(async () => { await api.request('/api/auth/local-login', { method: 'POST', body: {}, authenticate: false }); await load(); });
adminToken.addEventListener('input', clearAuthentication);
byId('music-search').onsubmit = event => { event.preventDefault(); run(async () => {
  const data = await api.request(`/api/music/search?q=${encodeURIComponent(byId('music-query').value.trim())}&limit=5`, { timeout: 45000 });
  const list = byId('music-results'); list.replaceChildren();
  for (const item of data.items || []) {
    const row = element('article', 'track'); const details = element('div'); details.append(element('h3', '', item.title), element('p', '', `${(item.artists || []).join(' / ')}${item.album ? ` · ${item.album}` : ''} · ${duration(item.duration_s)}`));
    const actions = element('div', 'toolbar'); const play = element('button', '', '在伊迪斯播放'); const queue = element('button', '', '加入队列');
    play.type = queue.type = 'button';
    play.onclick = () => run(async () => { const result = await api.request('/api/music/play', { method: 'POST', body: { selection_id: item.selection_id }, timeout: 55000 }); message(result.message || `请求状态：${result.status}。实际播放以设备回执为准。`); await refresh(); });
    queue.onclick = () => run(async () => { const result = await api.request('/api/music/actions/queue', { method: 'POST', body: { selection_id: item.selection_id } }); message(`已加入设备队列，共 ${result.queue_count} 首。`); await refresh(); });
    actions.append(play, queue); row.append(details, actions); list.append(row);
  }
  if (!(data.items || []).length) list.append(element('p', 'muted', '本次尚未返回可播放候选。Music Assistant 首次搜索可能在后台继续，请稍后用相同关键词重试；仍为空时再核查 QQ Music 授权、曲库权限或更换关键词。'));
  message('搜索完成，尚未自动播放。');
}); };
for (const button of document.querySelectorAll('[data-action]')) button.onclick = () => run(async () => { const result = await api.request(`/api/music/actions/${button.dataset.action}`, { method: 'POST', body: {}, timeout: 55000 }); message(result.message || `操作状态：${result.status}。`); await refresh(); });
for (const button of document.querySelectorAll('[data-noise]')) button.onclick = () => run(async () => { const minutes = Number(byId('music-noise-minutes').value); if (!Number.isInteger(minutes) || minutes < 1 || minutes > 30) throw new Error('请输入1～30分钟。'); const result = await api.request('/api/music/actions/noise', { method: 'POST', body: { source: button.dataset.noise, minutes }, timeout: 15000 }); message(result.message || '已交给实体设备，播放状态以回执为准。'); await refresh(); });
byId('music-config').onsubmit = event => { event.preventDefault(); run(async () => {
  const token = byId('music-access-token'); const payload = { base_url: byId('music-base-url').value.trim(), stream_base_url: byId('music-stream-url').value.trim(), clear_token: byId('music-clear-token').checked };
  if (token.value) payload.access_token = token.value.trim();
  try { await api.request('/api/music/config', { method: 'POST', body: payload }); configDirty = false; byId('music-clear-token').checked = false; message('配置已安全保存。搜索时才验证服务连接；QQ 授权请在 Music Assistant 页面完成。'); await refresh(); } finally { token.value = ''; }
}); };
byId('music-config').addEventListener('input', () => { configDirty = true; });
window.addEventListener('pagehide', () => { clearInterval(poll); api.cancel(); byId('music-access-token').value = ''; adminToken.value = ''; });
controls();
