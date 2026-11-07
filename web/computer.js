import { createConsoleApi, element } from './console_api.js?v=computer-20261104';

const byId = id => document.getElementById(id);
const token = byId('computer-token');
const dialog = byId('computer-confirm');
let generation = 0;
let authenticated = false;
let busy = false;
let pending = null;
let poll;
let loadedAt = 0;
const api = createConsoleApi(() => token.value, () => reset('管理员认证失效，请重新登录。', true));
const showResult = message => { byId('computer-result').textContent = message; };
const invalidatePreview = () => { pending = null; if (dialog.open) dialog.close(); };
const setBusy = value => {
  busy = value;
  document.querySelectorAll('#computer-controls button').forEach(button => { button.disabled = value || !authenticated || button.dataset.unavailable === 'true'; });
  byId('computer-refresh').disabled = value;
  byId('computer-login').disabled = value;
  byId('computer-confirm-execute').disabled = value || !pending;
};
function reset(message, clearToken = false) {
  generation += 1;
  api.cancel();
  clearTimeout(poll);
  authenticated = false;
  loadedAt = 0;
  invalidatePreview();
  if (clearToken) token.value = '';
  document.querySelectorAll('.oauth-secret').forEach(input => { input.value = ''; });
  byId('computer-state').textContent = message;
  byId('computer-media-state').textContent = '尚未读取';
  byId('computer-queue').replaceChildren();
  byId('computer-oauth').replaceChildren();
  setBusy(false);
}
function render(data) {
  const media = data.media || {};
  byId('computer-state').textContent = `${data.message || '已读取'} · ${new Date().toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false })}（北京时间快照） · ${data.instance_scope || ''}`;
  const sourceNames = new Map((data.sources || []).map(source => [source.id, `${({ music: '音乐', podcast: '播客', noise: '环境声', user_audio: '音源' })[source.kind] || '音源'} · ${source.name}`]));
  const bookmark = media.last_podcast;
  byId('computer-media-state').textContent = `${({ playing: '播放中', stopped: '已停止', paused: '已暂停', paused_voice: '因语音会话已暂停', error: '播放错误' })[media.status] || media.status || '未知'}${media.current ? ` · ${sourceNames.get(media.current.source) || media.current.source}` : ''} · 位置${media.position_s || 0}秒 · 计时剩余约${media.remaining_s || 0}秒 · ${data.active_voice ? '语音会话活跃' : '未检测到语音会话'}${media.pending ? ` · 等待语音结束，${media.pending.expires_in_s}秒后超时取消` : ''}${bookmark ? ` · 上次播客：${sourceNames.get(bookmark.source) || '音源不可用'}，${Math.round(bookmark.position_ms / 1000)}秒${bookmark.completed ? '（已播完）' : ''}` : ''}${media.error ? ` · ${media.error}` : ''}`;
  const selection = byId('computer-source').value;
  byId('computer-source').replaceChildren(...(data.sources || []).map(source => { const option = element('option', '', sourceNames.get(source.id)); option.value = source.id; return option; }));
  if ((data.sources || []).some(source => source.id === selection)) byId('computer-source').value = selection;
  byId('computer-queue').replaceChildren(...(media.queue || []).map(item => element('li', '', `${sourceNames.get(item.source) || '音源不可用'} · 最多${item.minutes}分钟`)));
  if (!byId('computer-oauth').children.length) renderOauth(data.oauth || []);
  else for (const state of data.oauth || []) {
    const output = document.getElementById(`oauth-state-${state.provider}`);
    if (output) output.textContent = oauthMessage(state);
  }
}
const oauthMessage = state => `${state.name || state.provider} · ${state.status} · ${state.message || ''}${state.expires_at ? ` · 到期UTC：${state.expires_at}` : ' · 到期时间未知'} · 未执行日历/邮件权限检查`;
function renderOauth(states) {
  const current = generation;
  for (const state of states) {
    const section = element('section', 'oauth-provider');
    section.append(element('h3', '', state.name || state.provider));
    const status = element('p', 'muted', oauthMessage(state)); status.id = `oauth-state-${state.provider}`; status.setAttribute('role', 'status');
    section.append(status);
    const form = element('form'); const fields = element('div', 'oauth-fields'); const inputs = new Map();
    const labels = { client_id: 'OAuth Client ID', client_secret: 'Client Secret（可选，依客户端类型）', refresh_token: 'Refresh Token', access_token: '已有 Access Token（可选）', expires_at: 'Access Token 到期时间（带时区ISO 8601）', tenant_id: 'Microsoft Tenant（默认common）' };
    for (const field of state.fields || []) {
      if (state.provider === 'google_calendar' && field.key === 'tenant_id') continue;
      const label = element('label', '', labels[field.key] || field.key); const input = element('input', field.secret ? 'oauth-secret' : '');
      input.type = field.secret ? 'password' : 'text'; input.autocomplete = 'off'; input.maxLength = 8192;
      if (field.secret) input.placeholder = field.configured ? '已配置，留空保留（不回显）' : '尚未配置'; else input.value = field.value || '';
      label.append(input); fields.append(label); inputs.set(field.key, input);
    }
    const save = element('button', '', '安全保存续期配置'); save.type = 'submit';
    const refresh = element('button', '', '预览真实续期请求'); refresh.type = 'button';
    save.disabled = refresh.disabled = !state.available;
    save.dataset.unavailable = refresh.dataset.unavailable = String(!state.available);
    form.append(fields, save, refresh); section.append(form); byId('computer-oauth').append(section);
    form.addEventListener('submit', async event => {
      event.preventDefault(); if (busy || !authenticated) return;
      invalidatePreview(); setBusy(true);
      const values = Object.fromEntries([...inputs].filter(([, input]) => input.value).map(([key, input]) => [key, input.value]));
      try {
        const result = await api.request(`/api/computer/oauth/${state.provider}/config`, { method: 'POST', body: { values } });
        if (current === generation) {
          status.textContent = oauthMessage(result);
          for (const field of result.fields || []) { const input = inputs.get(field.key); if (input?.type === 'password') input.placeholder = field.configured ? '已配置，留空保留（不回显）' : '尚未配置'; }
          showResult('续期配置已安全保存；未向提供商验证。');
        }
      } catch (error) { if (current === generation) showResult(`配置未确认保存：${error.message}`); }
      finally { for (const [, input] of inputs) if (input.type === 'password') input.value = ''; if (current === generation) setBusy(false); }
    });
    refresh.addEventListener('click', () => prepare('oauth_refresh', { provider: state.provider }));
  }
}
async function load() {
  if (busy) return;
  setBusy(true); const current = generation;
  try {
    const data = await api.request('/api/computer/status');
    if (current !== generation) return;
    authenticated = true; loadedAt = Date.now(); render(data);
  } catch (error) { if (current === generation) byId('computer-state').textContent = `状态读取失败：${error.message}；之前显示的是旧快照。`; }
  finally { if (current === generation) { setBusy(false); schedule(); } }
}
function schedule() { clearTimeout(poll); if (authenticated && !document.hidden) poll = setTimeout(load, 5000); }
async function prepare(action, argumentsValue, file = null) {
  if (busy || !authenticated) return;
  invalidatePreview(); setBusy(true); const current = generation;
  try {
    if (['desktop_open_app', 'desktop_media_key', 'desktop_volume', 'desktop_lock'].includes(action)) {
      const result = await api.request(`/api/computer/actions/${action}`, { method: 'POST', body: { arguments: argumentsValue }, timeout: 15000 });
      if (current === generation) { showResult(`${result.status}：${result.message || '请求已处理，请核对电脑状态。'}`); loadedAt = Date.now(); }
      return;
    }
    const result = await api.request(`/api/computer/actions/${action}/prepare`, { method: 'POST', body: { arguments: argumentsValue } });
    if (current !== generation) return;
    pending = { action, arguments: result.arguments, token: result.confirmation_token, file, expires: Date.parse(result.expires_at) };
    byId('computer-confirm-summary').textContent = result.preview.label;
    byId('computer-preview').textContent = JSON.stringify(result.preview, null, 2);
    byId('computer-expiry').textContent = `凭证到期：${new Date(result.expires_at).toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false })}（北京时间）；参数改变后需重新预览。`;
    byId('computer-confirm-state').textContent = '';
    dialog.showModal();
  } catch (error) { if (current === generation) showResult(`预览未生成：${error.message}`); }
  finally { if (current === generation) { setBusy(false); schedule(); } }
}
byId('computer-confirm-execute').addEventListener('click', async () => {
  if (busy || !pending || !authenticated) return;
  if (Date.now() >= pending.expires) { invalidatePreview(); showResult('确认已过期，请重新预览。'); return; }
  const current = generation; const selected = pending; setBusy(true);
  try {
    let result;
    if (selected.file) {
      const multipart = new FormData();
      multipart.append('metadata', JSON.stringify({ arguments: selected.arguments, confirmation_token: selected.token }));
      multipart.append('file', selected.file);
      result = await api.request('/api/computer/media/upload', { method: 'POST', multipart, timeout: 120000 });
    } else result = await api.request(`/api/computer/actions/${selected.action}`, { method: 'POST', body: { arguments: selected.arguments, confirmation_token: selected.token }, timeout: 30000 });
    if (current !== generation) return;
    invalidatePreview(); showResult(`${result.status}：${result.message || '请求已处理，请刷新状态核对。'}`);
  } catch (error) {
    if (current === generation) { invalidatePreview(); showResult(`执行结果未确认：${error.message}。请先刷新核对，不自动重试。`); }
  } finally { if (current === generation) { setBusy(false); await load(); } }
});
byId('computer-controls').addEventListener('click', event => {
  const action = event.target.closest('[data-action]')?.dataset.action;
  if (!action) return;
  const argumentsValue = action === 'desktop_open_app' ? { app: byId('computer-app').value } : action === 'desktop_media_key' ? { key: byId('computer-key').value } : action === 'desktop_volume' ? { direction: byId('computer-direction').value, steps: Number(byId('computer-steps').value) } : ['media_play', 'media_queue'].includes(action) ? { source: byId('computer-source').value, minutes: Number(byId('computer-minutes').value) } : {};
  prepare(action, argumentsValue);
});
byId('computer-upload').addEventListener('click', async () => {
  if (busy || !authenticated) return;
  const file = byId('computer-file').files[0];
  if (!file || !/\.(wav|mp3|ogg|flac|m4a)$/i.test(file.name) || file.size > 10 * 1024 * 1024 || file.name.length > 120) { showResult('请明确选择10MiB以内的WAV/MP3/OGG/FLAC/M4A音源，文件名最多120字符。'); return; }
  if (!crypto.subtle) { showResult('当前浏览器环境无法安全计算SHA256，请使用本机localhost或HTTPS页面上传。'); return; }
  const current = generation; setBusy(true);
  let digest;
  try { digest = [...new Uint8Array(await crypto.subtle.digest('SHA-256', await file.arrayBuffer()))].map(value => value.toString(16).padStart(2, '0')).join(''); }
  catch (error) { showResult(`文件摘要计算失败：${error.message}`); }
  finally { if (current === generation) setBusy(false); }
  if (digest && current === generation) prepare('media_upload', { filename: file.name, size: file.size, sha256: digest, kind: byId('computer-kind').value, title: byId('computer-title').value.trim() || file.name.replace(/\.[^.]+$/, '') }, file);
});
byId('computer-controls').addEventListener('input', invalidatePreview);
byId('computer-cancel').addEventListener('click', invalidatePreview);
dialog.addEventListener('cancel', invalidatePreview);
byId('computer-refresh').addEventListener('click', load);
byId('computer-login').addEventListener('click', async () => {
  if (busy) return;
  reset('正在建立本机管理员会话…', true); setBusy(true); const current = generation;
  try { await api.request('/api/auth/local-login', { method: 'POST', authenticate: false }); if (current === generation) { setBusy(false); await load(); } }
  catch (error) { if (current === generation) { byId('computer-state').textContent = error.message; setBusy(false); } }
});
token.addEventListener('input', () => { const value = token.value; reset('令牌已改变，请重新加载认证。'); token.value = value; });
window.addEventListener('pagehide', () => reset('页面已离开，电脑媒体可能仍在播放。', true));
window.addEventListener('pageshow', event => { if (event.persisted) load(); });
document.addEventListener('visibilitychange', () => { if (document.hidden) clearTimeout(poll); else if (authenticated) load(); });
setInterval(() => { if (loadedAt && Date.now() - loadedAt > 15000) byId('computer-state').textContent = '电脑状态快照超过15秒，请刷新核对。'; }, 5000);
setBusy(false); load();
