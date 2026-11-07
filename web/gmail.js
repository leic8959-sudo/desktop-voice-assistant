const byId = id => document.getElementById(id);
let gmailToken = '';
let gmailStatus = null;
let messagePageToken = '';
let draftPageToken = '';
let selectedDraft = null;
let pendingSend = null;
let authGeneration = 0;
let draftContext = 0;

const statusText = value => value ? '已连接' : '未连接';
const setText = (id, value) => { const node = byId(id); if (node) node.textContent = value; };
const displayValue = value => Array.isArray(value) ? value.map(displayValue).join(', ') : (value && typeof value === 'object' ? (value.email || value.address || value.name || '') : (value == null ? '' : String(value)));
function renderHelp(configured, data) {
  const node = byId('gmail-help'); clearNode(node);
  node.append(document.createTextNode(configured ? 'OAuth 配置已提交。' : '需要 Google Cloud Web client ID/Secret，并启用 Gmail API。'));
  const link = document.createElement('a'); link.href = 'https://console.cloud.google.com/apis/credentials'; link.target = '_blank'; link.rel = 'noopener'; link.textContent = '打开 Google Cloud 凭据控制台'; node.append(document.createTextNode(' '), link, document.createTextNode(`。OAuth 回调地址：${data.redirect_uri || 'http://127.0.0.1:8765/gmail/oauth/callback'}`));
}
const clearNode = node => { while (node?.firstChild) node.removeChild(node.firstChild); };
const showEmpty = (id, text) => { const node = byId(id); clearNode(node); const empty = document.createElement('p'); empty.className = 'empty'; empty.textContent = text; node.append(empty); };
function gmailApi(path, options = {}) {
  if (!gmailToken) return Promise.reject(new Error('请先连接助手'));
  const requestToken = gmailToken;
  const requestGeneration = authGeneration;
  const headers = { ...(options.body ? {'Content-Type': 'application/json'} : {}), Authorization: `Bearer ${gmailToken}`, ...(options.headers || {}) };
  return fetch(path, { ...options, headers }).then(async response => {
    let data = null;
    try { data = await response.json(); } catch { data = {}; }
    if (requestToken !== gmailToken || requestGeneration !== authGeneration) { const error = new Error('认证状态已变化'); error.stale = true; throw error; }
    if (!response.ok) { const error = new Error(data?.detail || data?.message || data?.error?.message || `请求失败（${response.status}）`); error.status = response.status; throw error; }
    return data;
  }).catch(error => { if (requestToken !== gmailToken || requestGeneration !== authGeneration) error.stale = true; throw error; });
}
function setGmailControls() {
  const connected = Boolean(gmailToken && gmailStatus?.connected);
  byId('gmail-authorize').disabled = !gmailToken || !gmailStatus?.configured || connected;
  byId('gmail-disconnect').disabled = !gmailToken || !gmailStatus?.connected;
  byId('gmail-refresh').disabled = !gmailToken;
  byId('gmail-next-messages').disabled = !connected || !messagePageToken;
  byId('gmail-next-drafts').disabled = !connected || !draftPageToken;
}
function renderStatus(data = {}) {
  gmailStatus = data;
  const configured = Boolean(data.configured);
  const connected = Boolean(data.connected);
  setText('gmail-status', connected ? `已连接${data.email ? ` · ${data.email}` : ''}` : configured ? '已配置，待授权' : '未配置');
  renderHelp(configured, data);
  if (typeof data.client_id === 'string') byId('gmail-client-id').value = data.client_id;
  if (typeof data.redirect_uri === 'string') byId('gmail-redirect-uri').value = data.redirect_uri;
  byId('gmail-enable-send').checked = Boolean(data.send_enabled);
  setGmailControls();
}
async function loadGmailStatus() { if (!gmailToken) return; try { renderStatus(await gmailApi('/api/gmail/status')); if (gmailStatus?.connected) { await Promise.all([loadMessages(true), loadDrafts(true)]); } } catch (error) { if (error.stale) return; setText('gmail-status', `状态读取失败：${error.message}`); } }
function renderMessages(items = []) {
  const list = byId('gmail-message-list'); clearNode(list);
  if (!items.length) return showEmpty('gmail-message-list', '没有匹配邮件。');
  for (const item of items) {
    const button = document.createElement('button'); button.type = 'button'; button.className = 'gmail-list-item';
    const subject = document.createElement('strong'); subject.textContent = item.subject || '(无主题)';
    const meta = document.createElement('span'); meta.textContent = displayValue(item.from);
    const snippet = document.createElement('span'); snippet.textContent = item.snippet || '';
    button.append(subject, meta, snippet); button.onclick = () => loadMessage(item.id); list.append(button);
  }
}
async function loadMessages(reset = true) { if (reset) messagePageToken = ''; try { const query = new URLSearchParams({ limit: '20' }); const search = byId('gmail-search').value.trim(); if (search) query.set('q', search); if (messagePageToken) query.set('page_token', messagePageToken); const data = await gmailApi(`/api/gmail/messages?${query}`); messagePageToken = data.next_page_token || ''; renderMessages(Array.isArray(data.items) ? data.items : []); setGmailControls(); } catch (error) { if (error.stale) return; showEmpty('gmail-message-list', error.message); } }
async function loadMessage(id) { try { const message = await gmailApi(`/api/gmail/messages/${encodeURIComponent(id)}`); const detail = byId('gmail-message-detail'); clearNode(detail); detail.hidden = false; const title = document.createElement('h4'); title.textContent = message.subject || '(无主题)'; const meta = document.createElement('p'); meta.className = 'gmail-meta'; meta.textContent = `发件人：${displayValue(message.from)} · 收件人：${displayValue(message.to)}`; const body = document.createElement('pre'); body.className = 'gmail-body'; body.textContent = message.body || ''; const reply = document.createElement('button'); reply.className = 'small-button'; reply.type = 'button'; reply.textContent = '回复草稿'; reply.onclick = () => openReply(message); detail.append(title, meta, body, reply); } catch (error) { if (error.stale) return; setText('gmail-message-detail', error.message); } }
function renderDrafts(items = []) { const list = byId('gmail-draft-list'); clearNode(list); if (!items.length) return showEmpty('gmail-draft-list', '没有草稿。'); for (const item of items) { const message = item.message || item; const button = document.createElement('button'); button.type = 'button'; button.className = 'gmail-list-item'; const subject = document.createElement('strong'); subject.textContent = message.subject || '(无主题)'; const to = document.createElement('span'); to.textContent = displayValue(message.to); button.append(subject, to); button.onclick = () => loadDraft(item.id); list.append(button); } }
async function loadDrafts(reset = true) { if (reset) draftPageToken = ''; try { const query = new URLSearchParams({ limit: '20' }); if (draftPageToken) query.set('page_token', draftPageToken); const data = await gmailApi(`/api/gmail/drafts?${query}`); draftPageToken = data.next_page_token || ''; renderDrafts(Array.isArray(data.items) ? data.items : []); setGmailControls(); } catch (error) { if (error.stale) return; showEmpty('gmail-draft-list', error.message); } }
function showDraftForm(draft = {}, replyTo = '') { draftContext += 1; selectedDraft = draft; byId('gmail-draft-form').hidden = false; byId('gmail-draft-id').value = draft.id || ''; byId('gmail-draft-to').value = displayValue(draft.to); byId('gmail-draft-subject').value = draft.subject || ''; byId('gmail-draft-body').value = draft.body || ''; byId('gmail-draft-form').dataset.replyTo = replyTo; byId('gmail-reply-draft').hidden = !replyTo; if (draft.attachments?.length || draft.body_truncated || draft.body_is_plain === false) setText('gmail-draft-state', '此草稿含附件、富文本或内容过长，请到 Gmail 编辑'); else setText('gmail-draft-state', draft.revision ? `版本：${draft.revision}` : '新草稿'); hideSendPreview(); }
async function loadDraft(id) { try { showDraftForm(await gmailApi(`/api/gmail/drafts/${encodeURIComponent(id)}`)); } catch (error) { if (error.stale) return; setText('gmail-draft-state', error.message); } }
function openReply(message) { showDraftForm({ to: message.from || '', subject: `Re: ${message.subject || ''}`, body: `\n\n原邮件：\n${message.body || ''}` }, message.id); byId('gmail-draft-to').focus(); }
function formDraft() { return { to: byId('gmail-draft-to').value.trim(), subject: byId('gmail-draft-subject').value.trim(), body: byId('gmail-draft-body').value, cc: displayValue(selectedDraft?.cc), bcc: displayValue(selectedDraft?.bcc), ...(byId('gmail-draft-id').value ? {revision: selectedDraft?.revision} : {}), ...(byId('gmail-draft-form').dataset.replyTo ? {reply_to_message_id: byId('gmail-draft-form').dataset.replyTo} : {}) }; }
function draftDirty() { if (!selectedDraft || !byId('gmail-draft-id').value) return false; const current = formDraft(); return current.to !== displayValue(selectedDraft.to) || current.subject !== (selectedDraft.subject || '') || current.body !== (selectedDraft.body || ''); }
async function saveDraft(event) { event.preventDefault(); try { const id = byId('gmail-draft-id').value; const data = await gmailApi(id ? `/api/gmail/drafts/${encodeURIComponent(id)}` : '/api/gmail/drafts', {method: id ? 'PUT' : 'POST', body: JSON.stringify(formDraft())}); showDraftForm(data || formDraft()); await loadDrafts(true); setText('gmail-draft-state', '草稿已保存并同步到 Gmail'); } catch (error) { if (error.stale) return; setText('gmail-draft-state', `保存失败：${error.message}`); } }
function hideSendPreview() { pendingSend = null; byId('gmail-send-preview').hidden = true; setText('gmail-send-preview-text', ''); }
async function prepareSend() { hideSendPreview(); const context = draftContext; try { const id = byId('gmail-draft-id').value; if (!id) throw new Error('请先保存草稿'); if (draftDirty()) throw new Error('草稿有未保存修改，请先保存'); const prepared = await gmailApi(`/api/gmail/drafts/${encodeURIComponent(id)}/prepare-send`, {method: 'POST', body: '{}'}); if (context !== draftContext || draftDirty() || byId('gmail-draft-id').value !== id) return; pendingSend = prepared; const draft = pendingSend.draft; if (!draft) throw new Error('服务端未返回完整发送预览'); setText('gmail-send-preview-text', `收件人：${displayValue(draft.to)}\n抄送：${displayValue(draft.cc)}\n密送：${displayValue(draft.bcc)}\n主题：${draft.subject || ''}\n正文：${draft.body || ''}`); byId('gmail-send-preview').hidden = false; } catch (error) { if (error.stale) return; setText('gmail-draft-state', `无法准备发送：${error.message}`); } }
async function confirmSend() { if (!pendingSend) return; const id = byId('gmail-draft-id').value; const confirmationId = pendingSend.confirmation_id; byId('gmail-confirm-send').disabled = true; try { const result = await gmailApi(`/api/gmail/drafts/${encodeURIComponent(id)}/send`, {method:'POST', body: JSON.stringify({confirmation_id: confirmationId})}); pendingSend = null; if (result.status === 'unknown') { hideSendPreview(); setText('gmail-draft-state', '发送结果未知，请去 Gmail 核查，已停止重试。'); } else if (result.status === 'sent' || result.status === 'already_sent') { setText('gmail-draft-state', result.status === 'already_sent' ? '邮件已发送（服务端已确认）' : '邮件已发送'); hideSendPreview(); await loadDrafts(true); } else { hideSendPreview(); setText('gmail-draft-state', `服务端返回未知状态：${result.status || '空'}，请去 Gmail 核查。`); } } catch (error) { pendingSend = null; hideSendPreview(); if (error.stale) return; setText('gmail-draft-state', `发送结果未知，请去 Gmail 核查，已停止重试：${error.message}`); } finally { byId('gmail-confirm-send').disabled = false; } }
async function saveConfig(event) { event.preventDefault(); if (!gmailToken) return; const button = event.submitter; button.disabled = true; try { const data = await gmailApi('/api/gmail/config', {method:'POST', body: JSON.stringify({client_id: byId('gmail-client-id').value.trim(), client_secret: byId('gmail-client-secret').value, redirect_uri: byId('gmail-redirect-uri').value.trim(), send_enabled: byId('gmail-enable-send').checked})}); byId('gmail-client-secret').value = ''; renderStatus(data || {}); setText('gmail-draft-state', 'OAuth 配置已保存'); } catch (error) { if (error.stale) return; setText('gmail-draft-state', `配置失败：${error.message}`); } finally { button.disabled = false; } }
async function authorize() { const popup = window.open('about:blank', '_blank'); try { const data = await gmailApi('/api/gmail/oauth/start', {method:'POST', body:'{}'}); if (!data.authorization_url) throw new Error('服务端未返回授权地址'); if (popup) { popup.location = data.authorization_url; } else { window.location.href = data.authorization_url; } setText('gmail-status', '已打开 Google 授权页'); } catch (error) { if (error.stale) return; popup?.close(); setText('gmail-status', `授权失败：${error.message}`); } }
async function disconnectGmail() {
  const token = gmailToken;
  clearGmailPrivateData();
  try {
    await gmailApi('/api/gmail/disconnect', {method:'POST',body:'{}'});
    if (gmailToken !== token) return;
    renderStatus(await gmailApi('/api/gmail/status'));
    showEmpty('gmail-message-list', '已断开 Gmail。');
    showEmpty('gmail-draft-list', '已断开 Gmail。');
  } catch (error) { if (error.stale) return; setText('gmail-status', `断开失败：${error.message}`); }
}
function clearGmailPrivateData() { draftContext += 1; authGeneration += 1; messagePageToken = ''; draftPageToken = ''; selectedDraft = null; pendingSend = null; gmailStatus = null; showEmpty('gmail-message-list', '连接助手后加载邮件。'); showEmpty('gmail-draft-list', '连接助手后加载草稿。'); const detail = byId('gmail-message-detail'); clearNode(detail); detail.hidden = true; const form = byId('gmail-draft-form'); form.reset(); form.hidden = true; form.dataset.replyTo = ''; byId('gmail-draft-id').value = ''; byId('gmail-client-id').value = ''; byId('gmail-client-secret').value = ''; byId('gmail-redirect-uri').value = 'http://127.0.0.1:8765/gmail/oauth/callback'; byId('gmail-enable-send').checked = false; hideSendPreview(); renderStatus({}); }
window.addEventListener('assistant-auth-changed', event => { authGeneration += 1; gmailToken = typeof event.detail?.token === 'string' ? event.detail.token : ''; if (!gmailToken) { clearGmailPrivateData(); return; } loadGmailStatus(); });
byId('gmail-config-form').addEventListener('submit', saveConfig); byId('gmail-authorize').onclick = authorize; byId('gmail-disconnect').onclick = disconnectGmail; byId('gmail-refresh').onclick = loadGmailStatus; byId('gmail-search-form').onsubmit = event => { event.preventDefault(); loadMessages(true); }; byId('gmail-next-messages').onclick = () => loadMessages(false); byId('gmail-next-drafts').onclick = () => loadDrafts(false); byId('gmail-new-draft').onclick = () => showDraftForm({}); byId('gmail-draft-form').onsubmit = saveDraft; byId('gmail-reply-draft').onclick = () => setText('gmail-draft-state', '这是回复草稿，请编辑后保存'); byId('gmail-prepare-send').onclick = prepareSend; byId('gmail-confirm-send').onclick = confirmSend; byId('gmail-cancel-send').onclick = hideSendPreview;
['gmail-draft-to', 'gmail-draft-subject', 'gmail-draft-body'].forEach(id => byId(id).addEventListener('input', () => { draftContext += 1; hideSendPreview(); }));
renderStatus({});

