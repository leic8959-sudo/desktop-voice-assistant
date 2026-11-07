const byId = id => document.getElementById(id);
const labels = {idle:'待机',connecting:'连接中',listening:'正在听',thinking:'思考中',speaking:'正在回答',executing:'执行中',error:'连接错误'};
const tokenInput = byId('token');
let socket = null;
let voiceReady = false;
let voiceReadyPromise = null;
let voiceReadyResolve = null;
let voiceReadyReject = null;
let voiceStarting = false;
let voiceSaving = false;
let availableVoices = [];
let apiToken = '';
let apiAuthenticated = false;
let audioContext = null;
let media = null;
let capture = null;
let source = null;
let mute = null;
let recording = false;
let workletLoaded = false;
let voiceState = 'idle';
let nextPlay = 0;
let responseFinished = false;
let pollingTimer = null;
const playing = new Set();
const events = [];
let homeCursor = '';
let authGeneration = 0;
let taskLoadGeneration = 0;
let taskEditor = null;
let taskWrite = null;
let taskUncertain = false;
let taskRescheduleAvailable = false;
let taskAddReminderAvailable = false;
let taskRenameAvailable = false;
let taskCursor = '';
let taskNextCursor = '';
let taskPageNumber = 1;
let taskPageCount = 0;
let taskLoading = false;
let taskPageError = '';
let taskDateFilterAvailable = false;
let taskDate = '';

const statusText = value => value === true ? '已连接' : value === false ? '未连接' : value && typeof value !== 'object' ? String(value) : '未知';
const escapeTime = value => { if (!value) return ''; const date = new Date(value); return Number.isNaN(date.getTime()) ? String(value) : new Intl.DateTimeFormat('zh-CN', {timeZone:'Asia/Shanghai', month:'numeric', day:'numeric', hour:'2-digit', minute:'2-digit'}).format(date); };
function log(message) { events.push(`${new Date().toLocaleTimeString('zh-CN')} ${message}`); if (events.length > 80) events.shift(); byId('events').textContent = events.join('\n'); }
function toast(message) { const node = byId('toast'); node.textContent = message; node.classList.add('show'); clearTimeout(toast.timer); toast.timer = setTimeout(() => node.classList.remove('show'), 2800); }
function setVoiceState(value, detail = '') { voiceState = value; byId('voice-state').textContent = `${labels[value] || value}${detail ? ` · ${detail}` : ''}`; byId('connection-state').className = `connection-pill ${value === 'error' ? '' : value === 'connecting' ? 'busy' : socket?.readyState === WebSocket.OPEN ? 'ok' : ''}`; byId('connection-state').querySelector('span').textContent = socket?.readyState === WebSocket.OPEN ? (labels[value] || value) : value === 'error' ? '连接错误' : '未连接'; }
function addBubble(kind, text) { if (!text) return; const empty = document.querySelector('.conversation-empty'); empty?.remove(); const bubble = document.createElement('div'); bubble.className = `bubble ${kind}`; const label = document.createElement('span'); label.className = 'bubble-label'; label.textContent = kind === 'user' ? '你' : '小熊'; const body = document.createElement('span'); body.textContent = text; bubble.append(label, body); byId('conversation').appendChild(bubble); byId('conversation').scrollTop = byId('conversation').scrollHeight; }
function localDateTimeToIso(value) { if (!value) return null; const match = value.match(/^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/); if (!match) return null; const [, year, month, day, hour, minute] = match; return `${year}-${month}-${day}T${hour}:${minute}:00+08:00`; }
function makeKey() { return globalThis.crypto?.randomUUID?.() || `${Date.now()}-${Math.random().toString(36).slice(2)}`; }
async function api(path, options = {}) { if (!apiToken && !apiAuthenticated) throw new Error('请先登录并连接助手'); const response = await fetch(path, { ...options, headers: {'Content-Type':'application/json', ...(apiToken ? {Authorization:`Bearer ${apiToken}`} : {}), ...(options.headers || {})} }); let body = null; try { body = await response.json(); } catch {} if (!response.ok) { const error = new Error(body?.error?.message || `接口请求失败（${response.status}）`); error.status = response.status; throw error; } return body; }
async function apiGet(path) { return api(path); }
function renderTasks(items = []) { const list = byId('task-list'); list.replaceChildren(); if (!items.length) { list.innerHTML = '<div class="empty">本页暂无任务，可返回首页或刷新查看。</div>'; byId('hero-result').textContent = '本页没有返回任务，不代表全部任务为空。'; return; } const pending = items.filter(item => item.status !== 'completed' && item.status !== 'cancelled'); byId('hero-result').textContent = pending.length ? `本页有 ${pending.length} 件未完成任务，首项是「${pending[0].title || '未命名任务'}」。` : '本页没有未完成任务，其他页请继续查看。'; for (const item of items) { const row = document.createElement('article'); row.className = `task-item ${item.status === 'completed' ? 'done' : ''}`; const main = document.createElement('div'); main.className = 'task-main'; const title = document.createElement('div'); title.className = 'task-title'; title.textContent = item.title || '未命名任务'; const meta = document.createElement('div'); meta.className = 'task-meta'; meta.textContent = item.status === 'cancelled' ? '已取消' : item.status === 'completed' ? '已完成' : item.due_at ? `提醒：${escapeTime(item.due_at)}` : '未设置时间'; main.append(title, meta); const actions = document.createElement('div'); actions.className = 'item-actions'; if (item.status !== 'completed' && item.status !== 'cancelled') { const complete = document.createElement('button'); complete.className = 'small-button'; complete.textContent = '完成'; complete.onclick = () => updateTask(item.id, 'complete'); const cancel = document.createElement('button'); cancel.className = 'small-button'; cancel.textContent = '取消'; cancel.onclick = () => updateTask(item.id, 'cancel'); actions.append(complete, cancel); if (item.status === 'pending' && item.updated_at && (item.due_at === null || Number.isFinite(Date.parse(item.due_at)) && Date.parse(item.due_at) > Date.now())) { if (item.reminder_state !== 'leased') { const rename = document.createElement('button'); rename.className = 'small-button'; rename.textContent = '修改标题'; rename.dataset.taskRename = 'rename'; rename.hidden = !taskRenameAvailable; rename.onclick = () => editTask(item, row, 'rename'); actions.append(rename); } const edit = document.createElement('button'); edit.className = 'small-button'; edit.textContent = item.due_at === null ? '添加提醒' : '修改时间'; edit.dataset.taskReschedule = item.due_at === null ? 'add' : 'reschedule'; edit.hidden = item.due_at === null ? !taskAddReminderAvailable : !taskRescheduleAvailable; edit.disabled = Boolean(taskWrite) || taskUncertain; edit.onclick = () => editTask(item, row); actions.append(edit); } } row.append(main, actions); list.append(row); } updateTaskWriteControls(); }
function shanghaiDateTime(value) {
  const parts = new Intl.DateTimeFormat('sv-SE', {timeZone:'Asia/Shanghai', year:'numeric', month:'2-digit', day:'2-digit', hour:'2-digit', minute:'2-digit', hourCycle:'h23'}).formatToParts(new Date(value));
  const part = type => parts.find(item => item.type === type).value;
  return `${part('year')}-${part('month')}-${part('day')}T${part('hour')}:${part('minute')}`;
}
function taskModeAvailable(mode) { return mode === 'rename' ? taskRenameAvailable : mode === 'add' ? taskAddReminderAvailable : taskRescheduleAvailable; }
function clearTaskEditor() { taskEditor?.form.remove(); taskEditor = null; }
function updateTaskWriteControls() { const disabled = Boolean(taskWrite) || taskUncertain || Boolean(taskDate) && !taskDateFilterAvailable; byId('task-form').querySelector('button').disabled = disabled; byId('task-list').querySelectorAll('.item-actions button').forEach(button => { button.disabled = disabled; if (button.dataset.taskReschedule) button.hidden = !taskModeAvailable(button.dataset.taskReschedule); if (button.dataset.taskRename) button.hidden = !taskRenameAvailable; }); }
function updateTaskCapability(available, addAvailable, renameAvailable) { taskRescheduleAvailable = available === true; taskAddReminderAvailable = addAvailable === true; taskRenameAvailable = renameAvailable === true; if (taskEditor && !taskModeAvailable(taskEditor.mode)) clearTaskEditor(); updateTaskWriteControls(); }
function updateTaskPager() {
  const unavailable = Boolean(taskDate) && !taskDateFilterAvailable;
  byId('task-first').disabled = !apiAuthenticated || taskLoading || Boolean(taskWrite) || taskUncertain || unavailable || !taskCursor;
  byId('task-next').disabled = !apiAuthenticated || taskLoading || Boolean(taskWrite) || taskUncertain || unavailable || !taskNextCursor;
  byId('task-page-state').textContent = taskLoading ? '正在读取任务…' : taskPageError || `第 ${taskPageNumber} 页 · 本页 ${taskPageCount} 条 · 每页最多30条`;
  byId('task-date-filter').hidden = !taskDateFilterAvailable;
  byId('task-date').disabled = byId('task-date-apply').disabled = byId('task-date-clear').disabled = !apiAuthenticated || !taskDateFilterAvailable || taskLoading || Boolean(taskWrite) || taskUncertain;
  byId('task-date-state').textContent = unavailable ? '日期筛选暂不可用，当前日期结果尚未重新核对；请恢复支持后刷新。' : taskDate ? `当前提醒日期：${taskDate}（北京时间）；不包含未设时间待办。` : '当前未筛选日期；按日期筛选不包含未设时间待办。';
}
function updateTaskDateCapability(available) {
  const previous = taskDateFilterAvailable; taskDateFilterAvailable = available === true;
  if (previous && !taskDateFilterAvailable) {
    taskLoadGeneration += 1; taskLoading = false;
    if (!taskDate) { byId('task-date').value = ''; taskPageError = '日期筛选不可用，日期查询未生效；保留未筛选结果。'; }
    else if (!taskWrite && !taskUncertain) {
      if (taskEditor) toast('日期筛选服务不可用，已关闭未提交的时间修改。');
      clearTaskEditor(); taskDate = ''; byId('task-date').value = ''; taskCursor = taskNextCursor = ''; taskPageNumber = 1; taskPageCount = 0;
      byId('task-list').replaceChildren(); taskPageError = '日期筛选不可用，正在重新读取未筛选首页。';
      loadTasks(true, '', 1, '');
    }
  }
  updateTaskWriteControls(); updateTaskPager();
}
function invalidateTaskIdentity() { authGeneration += 1; taskLoadGeneration += 1; clearTaskEditor(); taskUncertain = false; taskDate = ''; taskDateFilterAvailable = false; byId('task-date').value = ''; taskCursor = taskNextCursor = ''; taskPageNumber = 1; taskPageCount = 0; taskLoading = false; taskPageError = ''; updateTaskCapability(false); updateTaskPager(); }
function taskDateRange(date) {
  if (!date) return '';
  if (!/^\d{4}-\d{2}-\d{2}$/.test(date)) throw new Error('请选择有效提醒日期');
  const start = new Date(`${date}T00:00:00Z`);
  if (!Number.isFinite(start.getTime()) || start.toISOString().slice(0, 10) !== date) throw new Error('请选择有效提醒日期');
  const next = new Date(start.getTime() + 86400000).toISOString().slice(0, 10);
  return `&due_from=${encodeURIComponent(`${date}T00:00:00+08:00`)}&due_before=${encodeURIComponent(`${next}T00:00:00+08:00`)}`;
}
async function loadTasks(force = false, cursor = taskCursor, pageNumber = taskPageNumber, date = taskDate) {
  if (taskEditor && !force) return false;
  if ((date && !taskDateFilterAvailable) || (taskUncertain && date !== taskDate)) { taskPageError = '保存结果待核实，请恢复支持当前日期筛选的服务后刷新核对。'; updateTaskPager(); return false; }
  const identity = authGeneration;
  const request = ++taskLoadGeneration;
  const controller = new AbortController(); const timeout = setTimeout(() => controller.abort(), 12000);
  taskLoading = true; taskPageError = ''; updateTaskPager();
  try {
    const data = await api(`/api/tasks?limit=30${cursor ? `&cursor=${encodeURIComponent(cursor)}` : ''}${taskDateRange(date)}`, {signal:controller.signal});
    if (controller.signal.aborted) throw new DOMException('任务读取已超时', 'AbortError');
    if (identity !== authGeneration || request !== taskLoadGeneration || (taskEditor && !force) || date && !taskDateFilterAvailable) return false;
    if (!Array.isArray(data?.items) || data.items.length > 30 || data.items.some(item => !item || typeof item !== 'object' || Array.isArray(item)) || (data.next_cursor != null && typeof data.next_cursor !== 'string')) throw new Error('服务端任务列表格式无效');
    clearTaskEditor(); renderTasks(data.items); taskUncertain = false; taskDate = date; byId('task-date').value = date; taskCursor = cursor; taskNextCursor = data.next_cursor || ''; taskPageNumber = pageNumber; taskPageCount = data.items.length; updateTaskWriteControls(); return true;
  } catch (error) {
    if (identity !== authGeneration || request !== taskLoadGeneration) return false;
    byId('task-date').value = taskDate;
    taskPageError = `读取失败，当前页保持不变，请重试：${error.message}`;
    log(`任务：${error.message}`); return false;
  } finally {
    clearTimeout(timeout);
    if (identity === authGeneration && request === taskLoadGeneration) { taskLoading = false; updateTaskPager(); }
  }
}
function applyTaskDate(clear = false) {
  if (!taskDateFilterAvailable || taskLoading || taskWrite || taskUncertain) return;
  const date = clear ? '' : byId('task-date').value;
  if (!clear && !date) { toast('请选择提醒日期。'); return; }
  if (taskEditor && !window.confirm('更改日期将关闭尚未提交的任务修改，是否继续？')) return;
  clearTaskEditor(); loadTasks(true, '', 1, date);
}
function refreshTaskHome() { return loadTasks(true, '', 1, taskDateFilterAvailable ? taskDate : ''); }
function navigateTasks(first = false) {
  if (taskLoading || taskWrite || taskUncertain) return;
  if (taskEditor && !window.confirm('切换页面将关闭尚未提交的任务修改，是否继续？')) return;
  clearTaskEditor(); loadTasks(true, first ? '' : taskNextCursor, first ? 1 : taskPageNumber + 1);
}
function beginTaskWrite() {
  const write = {identity:authGeneration}; taskWrite = write;
  taskLoadGeneration += 1; taskLoading = false; taskPageError = '';
  updateTaskWriteControls(); updateTaskPager(); return write;
}
async function taskWriteRequest(path, body) {
  const controller = new AbortController(); const timeout = setTimeout(() => controller.abort(), 15000);
  try {
    const result = await api(path, {method:'POST', body:JSON.stringify(body), signal:controller.signal});
    if (controller.signal.aborted) throw new DOMException('任务请求已超时', 'AbortError');
    return result;
  }
  finally { clearTimeout(timeout); }
}
function editTask(item, row, mode = item.due_at === null ? 'add' : 'reschedule') {
  const rename = mode === 'rename'; const saveLabel = rename ? '保存标题' : '保存时间';
  if (taskWrite || taskUncertain || !apiAuthenticated || !taskModeAvailable(mode) || taskDate && !taskDateFilterAvailable) return;
  if (taskEditor && !window.confirm('打开另一项任务修改将关闭尚未提交的修改，是否继续？')) return;
  clearTaskEditor();
  const form = document.createElement('form'); form.className = 'task-reschedule';
  const label = document.createElement('label'); label.textContent = rename ? '新待办标题 · 1～200字' : '新提醒时间 · 北京时间（UTC+08:00）';
  const input = document.createElement('input'); input.type = rename ? 'text' : 'datetime-local'; input.required = true; input.value = rename ? item.title : mode === 'add' ? '' : shanghaiDateTime(item.due_at); if (rename) input.maxLength = 400; input.setAttribute('aria-label', rename ? '新待办标题' : '新提醒时间（北京时间 UTC+08:00）'); label.append(input);
  const save = document.createElement('button'); save.className = 'small-button'; save.type = 'submit'; save.textContent = saveLabel;
  const cancel = document.createElement('button'); cancel.className = 'small-button'; cancel.type = 'button'; cancel.textContent = '取消修改';
  const status = document.createElement('p'); status.className = 'hint'; status.setAttribute('role', 'status');
  form.append(label, save, cancel, status); row.append(form);
  const editor = {form, mode, identity:authGeneration}; taskEditor = editor;
  cancel.onclick = () => { if (!taskWrite) clearTaskEditor(); };
  form.onsubmit = async event => {
    event.preventDefault();
    if (taskWrite || taskUncertain || !taskModeAvailable(mode) || taskDate && !taskDateFilterAvailable || taskEditor !== editor || editor.identity !== authGeneration) return;
    const title = rename ? input.value.trim() : ''; const due = rename ? item.due_at : localDateTimeToIso(input.value);
    if (rename ? !title || Array.from(title).length > 200 : !due || Date.parse(due) <= Date.now()) { status.textContent = rename ? '请输入1～200字的待办标题。' : '请选择晚于当前时刻的北京时间。'; return; }
    const write = beginTaskWrite();
    input.disabled = save.disabled = cancel.disabled = true; save.textContent = '保存中…'; status.textContent = '正在保存，请勿重复提交。';
    byId('task-list').querySelectorAll('.item-actions button').forEach(button => button.disabled = true);
    let reload = false; let receipt = ''; let conflict = false;
    try {
      const data = await taskWriteRequest(`/api/tasks/${encodeURIComponent(item.id)}/${rename ? 'rename' : 'reschedule'}`, rename ? {title, expected_updated_at:item.updated_at} : {due_at:due, expected_updated_at:item.updated_at});
      if (write.identity !== authGeneration) return;
      if (data?.item?.id !== item.id || data.item.status !== 'pending') throw new Error('服务端未返回匹配的未完成任务');
      if (rename) {
        if (data.item.title !== title || data.item.due_at !== item.due_at || data.item.reminder_state !== item.reminder_state || typeof data.item.updated_at !== 'string' || !data.item.updated_at.trim() || !Number.isFinite(Date.parse(data.item.updated_at)) || data.item.updated_at === item.updated_at) throw new Error('服务端未返回匹配标题、时间、提醒状态和新版本的保存回执');
        receipt = '待办标题已保存，提醒时间不变，待办仍未完成。';
      } else {
        if (!Number.isFinite(Date.parse(data.item.due_at))) throw new Error('服务端未返回匹配任务的有效提醒时间');
        receipt = `提醒时间已保存：${escapeTime(data.item.due_at)}（北京时间），待办仍未完成。`;
      }
      reload = true;
    } catch (error) {
      if (write.identity !== authGeneration) return;
      if (error.status === 409) { conflict = true; reload = true; }
      else if (!error.status || error.status >= 500) { taskUncertain = true; status.textContent = '保存结果待核实，请刷新任务核对后再修改；不会自动重发。'; toast(status.textContent); }
      else { status.textContent = error.message; input.disabled = save.disabled = cancel.disabled = false; save.textContent = saveLabel; }
      log(`任务修改：${error.message}`);
    } finally {
      if (taskWrite === write) taskWrite = null;
      updateTaskWriteControls();
      updateTaskPager();
      if (write.identity === authGeneration) {
        if (reload) {
          clearTaskEditor(); const refreshed = await refreshTaskHome();
          if (write.identity === authGeneration) toast(conflict ? (refreshed ? `${rename ? '待办' : '提醒'}已发生变化，已刷新；请核对后重新修改。` : `${rename ? '待办' : '提醒'}已发生变化，列表刷新失败，请手动刷新核对；未重复提交。`) : receipt + (refreshed ? '' : '列表刷新失败，请手动刷新核对。'));
        }
      }
    }
  };
  input.focus();
}
async function updateTask(id, action) {
  if (taskWrite || taskUncertain || taskDate && !taskDateFilterAvailable) return;
  const write = beginTaskWrite();
  try {
    await taskWriteRequest(`/api/tasks/${encodeURIComponent(id)}/${action}`, {});
    if (write.identity !== authGeneration) return;
    clearTaskEditor(); const refreshed = await refreshTaskHome();
    if (write.identity === authGeneration) toast((action === 'complete' ? '已完成' : '已取消') + (refreshed ? '，已返回首页。' : '；首页刷新失败，请手动刷新核对。'));
  } catch (error) {
    if (write.identity !== authGeneration) return;
    if (!error.status || error.status >= 500) taskUncertain = true;
    toast(taskUncertain ? '任务更新结果待核实，请刷新当前页核对；不会自动重发。' : error.message); log(`任务更新：${error.message}`);
  } finally {
    if (taskWrite === write) taskWrite = null;
    updateTaskWriteControls(); updateTaskPager();
  }
}
function renderMemories(items = []) { const list = byId('memory-list'); list.replaceChildren(); if (!items.length) { list.innerHTML = '<div class="empty">还没有保存的记忆。</div>'; return; } for (const item of items) { const row = document.createElement('article'); row.className = 'memory-item'; const main = document.createElement('div'); main.className = 'memory-main'; const key = document.createElement('div'); key.className = 'memory-key'; key.textContent = item.key; const value = document.createElement('div'); value.className = 'memory-value'; value.textContent = item.value; const time = document.createElement('div'); time.className = 'memory-time'; time.textContent = item.updated_at ? `更新于 ${escapeTime(item.updated_at)}` : ''; main.append(key, value, time); const actions = document.createElement('div'); actions.className = 'item-actions'; const edit = document.createElement('button'); edit.className = 'small-button'; edit.textContent = '编辑'; edit.onclick = () => editMemory(item); const remove = document.createElement('button'); remove.className = 'small-button'; remove.textContent = '删除'; remove.onclick = () => deleteMemory(item.key); actions.append(edit, remove); row.append(main, actions); list.append(row); } }
async function loadMemories() { try { const data = await apiGet('/api/memories?limit=30'); renderMemories(data?.items || []); } catch (error) { byId('memory-list').innerHTML = `<div class="empty">记忆暂时不可用：${error.message}</div>`; log(`记忆：${error.message}`); } }
async function editMemory(item) { const value = window.prompt(`编辑「${item.key}」`, item.value || ''); if (value === null) return; try { await api(`/api/memories/${encodeURIComponent(item.key)}`, {method:'PUT', body:JSON.stringify({value})}); await loadMemories(); toast('记忆已更新'); } catch (error) { toast(error.message); } }
async function deleteMemory(key) { if (!window.confirm(`删除记忆「${key}」？`)) return; try { await api(`/api/memories/${encodeURIComponent(key)}`, {method:'DELETE'}); await loadMemories(); toast('记忆已删除'); } catch (error) { toast(error.message); } }
const homeStateText = item => {
  if (item.available === false) return '不可用';
  return (item.display_fields || [{label:'状态', value:item.state?.value || '未知', unit:''}]).map(field => `${field.label} ${field.value}${field.unit || ''}`).join(' · ');
};
function renderHome(items = [], configured = false) {
  const list = byId('home-list'); list.replaceChildren();
  if (!configured && !items.length) { list.innerHTML = '<div class="empty">尚未连接 Home Assistant。</div>'; return; }
  if (!items.length) { list.innerHTML = '<div class="empty">没有发现家居设备。</div>'; return; }
  for (const item of items) {
    const row = document.createElement('div'); row.className = 'home-item';
    const main = document.createElement('div'); const name = document.createElement('strong'); name.textContent = item.name || item.device_id; main.append(name);
    const meta = document.createElement('div'); meta.className = 'task-meta'; meta.textContent = `${item.area || '未分配房间'} · ${homeStateText(item)}${item.enabled ? ' · 已授权' : ' · 未授权'}`; main.append(meta);
    const state = document.createElement('div'); state.className = 'home-state';
    const authorization = document.createElement('button'); authorization.className = `toggle ${item.enabled ? 'on' : ''}`; authorization.textContent = item.enabled ? '已启用' : '启用'; authorization.onclick = () => setHomeAuthorization(item, !item.enabled, authorization); state.append(authorization);
    let actionContainer = state;
    const availableActions = (item.actions || []).filter(command => command.supported !== false);
    const primaryActions = availableActions.filter(command => command.group !== 'secondary');
    const secondaryActions = availableActions.filter(command => command.group === 'secondary');
    for (const [actionIndex, action] of [...primaryActions, ...secondaryActions].entries()) {
      if (actionIndex === primaryActions.length && secondaryActions.length) { const details = document.createElement('details'); const summary = document.createElement('summary'); summary.textContent = '更多功能'; details.append(summary); state.append(details); actionContainer = details; }
      const controls = {};
      for (const [key, schema] of Object.entries(action.parameters?.properties || {})) {
        const fixed = Array.isArray(schema.enum) && schema.enum.length === 1;
        if (fixed) { controls[key] = {value:schema.default ?? schema.enum[0], type:'text'}; continue; }
        const control = Array.isArray(schema.enum) ? document.createElement('select') : document.createElement('input');
        control.setAttribute('aria-label', `${item.name || item.device_id}${schema.title || key}`); control.disabled = !item.enabled || item.available === false;
        if (Array.isArray(schema.enum)) { for (const value of schema.enum) { const option = document.createElement('option'); option.value = value; option.textContent = schema.enumLabels?.[value] || value; control.append(option); } }
        else if (schema.type === 'number') { control.type = 'number'; if (schema.minimum !== undefined) control.min = String(schema.minimum); if (schema.maximum !== undefined) control.max = String(schema.maximum); if (schema.multipleOf !== undefined) control.step = String(schema.multipleOf); control.value = schema.default ?? ''; }
        if (schema.default !== undefined) control.value = String(schema.default);
        control.setAttribute('title', schema.title || key); controls[key] = control; actionContainer.append(control);
      }
      const button = document.createElement('button'); button.className = 'small-button'; button.textContent = action.label || action.id; button.disabled = !item.enabled || item.available === false;
      button.onclick = () => runHomeAction(item, action, button, controls); actionContainer.append(button);
    }
    row._homeItem = item; row.append(main, state); list.append(row);
  }
}
async function loadHome(append = false, refresh = false) { try { if (!append) homeCursor = ''; const suffix = homeCursor ? `&cursor=${encodeURIComponent(homeCursor)}` : ''; const refreshQuery = refresh ? '&refresh=true' : ''; const data = await apiGet(`/api/home?enabled_only=false&limit=30${suffix}${refreshQuery}`); const existing = append ? [...byId('home-list').querySelectorAll('.home-item')].map(node => node._homeItem).filter(Boolean) : []; renderHome([...existing, ...(data?.items || [])], Boolean(data?.configured)); homeCursor = data?.next_cursor || ''; const more = byId('more-home'); more.hidden = !homeCursor; if (data?.stale) log('家居发现失败，显示上次成功目录'); } catch (error) { byId('home-list').innerHTML = `<div class="empty">家居发现暂时不可用：${error.message}</div>`; log(`家居：${error.message}`); } }
async function setHomeAuthorization(item, enabled, button) { button.disabled = true; try { await api(`/api/home/${encodeURIComponent(item.device_id)}/authorization`, {method:'POST', body:JSON.stringify({enabled})}); await loadHome(); toast(enabled ? '设备已启用' : '设备已停用'); } catch (error) { button.disabled = false; toast(error.message); log(`家居授权：${error.message}`); } }
async function runHomeAction(item, action, button, controls = {}) {
  const parameters = {};
  for (const [key, control] of Object.entries(controls)) { if (control.reportValidity && !control.reportValidity()) return; parameters[key] = control.type === 'number' ? control.valueAsNumber : control.value; }
  button.disabled = true;
  try { const result = await api(`/api/home/${encodeURIComponent(item.device_id)}/actions`, {method:'POST', body:JSON.stringify({action:action.id, parameters})}); toast(result.target_observed ? '平台已报告目标状态' : '指令已提交，状态尚未确认'); }
  catch (error) { toast(error.message); log(`家居控制：${error.message}`); }
  finally { await loadHome(); }
}
function renderVoices(data = {}) { const select = byId('voice-select'); const rawItems = Array.isArray(data.items) ? data.items : availableVoices; const items = rawItems.filter(item => item && typeof item.id === 'string' && item.id && typeof item.name === 'string' && item.name); availableVoices = items; const current = typeof data.current === 'string' && data.current ? data.current : ''; const active = typeof data.active_voice === 'string' && data.active_voice ? data.active_voice : ''; const findItem = value => items.find(item => item.id === value || item.name === value); const currentItem = findItem(current); const activeItem = findItem(active); const selected = current; select.replaceChildren(); for (const item of items) { const option = document.createElement('option'); option.value = item.id; option.textContent = item.name; option.dataset.description = typeof item.description === 'string' ? item.description : ''; select.append(option); } if (current && !currentItem) { const option = document.createElement('option'); option.value = current; option.textContent = '当前配置音色'; option.dataset.description = '服务端当前配置'; select.append(option); } if (!select.options.length) { select.innerHTML = '<option>暂无可选音色</option>'; select.disabled = true; byId('voice-save').disabled = true; byId('voice-setting-state').textContent = '暂无可选音色'; byId('voice-description').textContent = '服务端尚未返回可用音色。'; return; } const matching = [...select.options].find(option => option.value === selected || option.textContent === selected); select.value = matching?.value || select.options[0].value; select.disabled = false; byId('voice-save').disabled = false; const currentName = currentItem?.name || (current ? '当前配置音色' : '已选音色'); const activeName = activeItem?.name || (active ? '当前对话音色' : ''); const differs = active && current && (activeItem && currentItem ? activeItem.id !== currentItem.id : active !== current); byId('voice-setting-state').textContent = current ? `已选：${currentName}（下次对话生效）${differs ? ` · 当前对话：${activeName}` : ''}` : '已加载'; const updateDescription = () => { const option = select.selectedOptions[0]; byId('voice-description').textContent = option?.dataset.description || '暂无服务端说明'; }; select.onchange = updateDescription; updateDescription(); }
async function loadVoices() { const requestToken = apiToken; try { const data = await apiGet('/api/voices'); if (requestToken !== apiToken) return; renderVoices(data || {}); } catch (error) { if (requestToken !== apiToken) return; const select = byId('voice-select'); select.replaceChildren(); select.innerHTML = '<option>音色暂时不可用</option>'; select.disabled = true; byId('voice-save').disabled = true; byId('voice-setting-state').textContent = '加载失败'; byId('voice-description').textContent = `音色暂时不可用：${error.message}`; log(`音色：${error.message}`); } }
async function saveVoice() { if (voiceSaving || !apiAuthenticated || byId('voice-select').disabled) return; const requestToken = apiToken; const select = byId('voice-select'); const voice = select.value; if (!voice) return; voiceSaving = true; const button = byId('voice-save'); button.disabled = true; button.textContent = '保存中…'; try { const data = await api('/api/voice', {method:'PUT', body:JSON.stringify({voice})}); if (requestToken !== apiToken) return; renderVoices(data || {current:voice, items:[{id:voice, name:voice}]}); toast('音色已保存，下次对话生效'); } catch (error) { if (requestToken !== apiToken) return; byId('voice-setting-state').textContent = '保存失败'; toast(error.message); log(`保存音色：${error.message}`); } finally { voiceSaving = false; button.textContent = '保存音色'; button.disabled = !apiAuthenticated || byId('voice-select').disabled; } }
let llmSaving = false;
function renderLlm(data = {}) { const select = byId('llm-select'); const items = (Array.isArray(data.items) ? data.items : []).filter(item => item && typeof item.id === 'string' && item.id); const current = typeof data.current === 'string' ? data.current : ''; const active = typeof data.active_provider === 'string' ? data.active_provider : ''; const model = typeof data.model === 'string' ? data.model : ''; select.replaceChildren(); for (const item of items) { const option = document.createElement('option'); option.value = item.id; option.textContent = item.available ? item.name : `${item.name}（未配置凭据）`; option.dataset.description = typeof item.description === 'string' ? item.description : ''; option.disabled = !item.available; select.append(option); } if (!select.options.length) { select.innerHTML = '<option>暂无可选模型</option>'; select.disabled = true; byId('llm-save').disabled = true; byId('llm-setting-state').textContent = '暂无可选模型'; return; } const matching = [...select.options].find(option => option.value === current); select.value = matching?.value || select.options[0].value; const locked = ![...select.options].some(option => option.value === current && !option.disabled); select.disabled = locked; byId('llm-save').disabled = locked; const currentItem = items.find(item => item.id === current); const activeName = items.find(item => item.id === active)?.name || ''; byId('llm-setting-state').textContent = current ? `已选：${currentItem?.name || current}（下次对话生效）${active && active !== current ? ` · 当前对话：${activeName}` : ''}${model ? ` · 模型 ${model}` : ''}` : '已加载'; const updateDescription = () => { const option = select.selectedOptions[0]; byId('llm-description').textContent = option?.dataset.description || '暂无服务端说明'; }; select.onchange = updateDescription; updateDescription(); }
async function loadLlm() { const requestToken = apiToken; try { const data = await apiGet('/api/llm'); if (requestToken !== apiToken) return; renderLlm(data || {}); } catch (error) { if (requestToken !== apiToken) return; const select = byId('llm-select'); select.replaceChildren(); select.innerHTML = '<option>模型暂时不可用</option>'; select.disabled = true; byId('llm-save').disabled = true; byId('llm-setting-state').textContent = '加载失败'; byId('llm-description').textContent = `模型暂时不可用：${error.message}`; log(`模型：${error.message}`); } }
async function saveLlm() { if (llmSaving || !apiAuthenticated || byId('llm-select').disabled) return; const requestToken = apiToken; const provider = byId('llm-select').value; if (!provider) return; llmSaving = true; const button = byId('llm-save'); button.disabled = true; button.textContent = '保存中…'; try { const data = await api('/api/llm', {method:'PUT', body:JSON.stringify({provider})}); if (requestToken !== apiToken) return; renderLlm(data || {}); toast('模型已保存，下次对话生效'); } catch (error) { if (requestToken !== apiToken) return; byId('llm-setting-state').textContent = '保存失败'; toast(error.message); log(`保存模型：${error.message}`); } finally { llmSaving = false; button.textContent = '保存模型'; button.disabled = !apiAuthenticated || byId('llm-select').disabled; } }
function renderNotifications(items = []) { const list = byId('notification-list'); list.replaceChildren(); for (const item of items.filter(entry => entry.status === 'pending_delivery')) { const node = document.createElement('div'); node.className = 'notification'; const ack = document.createElement('button'); ack.textContent = '知道了'; ack.onclick = () => acknowledge(item.id, node); node.append(ack, document.createTextNode(`${item.title || '提醒'}${item.due_at ? ` · ${escapeTime(item.due_at)}` : ''}`)); list.append(node); } }
async function acknowledge(id, node) { if (document.visibilityState !== 'visible' || !node?.isConnected) return; try { await api(`/api/notifications/${encodeURIComponent(id)}/ack`, {method:'POST', body:'{}'}); await loadStatus(); } catch (error) { toast(error.message); } }
function renderConnectors(items = []) { const root = byId('connector-status'); if (!root) return; root.replaceChildren(); for (const item of items) { const row = document.createElement('div'); row.className = 'connector-item'; row.textContent = (item.name || item.id) + ': ' + (item.configured ? '已配置' : '未配置') + (item.action_count ? ` · ${item.action_count}项能力` : ''); row.title = Array.isArray(item.actions) ? item.actions.map(action => typeof action === 'string' ? action : action.name).filter(Boolean).join('、') : ''; root.append(row); } }
function renderStatus(data) { updateTaskCapability(data?.capabilities?.task_reschedule, data?.capabilities?.task_add_reminder, data?.capabilities?.task_rename); updateTaskDateCapability(data?.capabilities?.task_date_filter); const values = {bridge:data?.bridge?.connected ?? data?.bridge?.running ?? data?.bridge?.ready ?? data?.bridge?.status ?? data?.bridge, device:data?.device?.connected, cloud:data?.cloud?.connected ?? data?.cloud?.status ?? data?.cloud}; for (const [key, value] of Object.entries(values)) { const node = document.querySelector(`[data-status="${key}"]`); node.textContent = statusText(value); node.className = value === true || value === 'running' ? 'good' : ''; } const capabilities = byId('capabilities'); capabilities.replaceChildren(); const capabilityLabels = {voice:'语音对话', live_voice:'实时语音', text:'文字对话', tasks:'待办', reminders:'提醒', memories:'记忆', home:'家居控制', multi_llm:'多模型接入'}; for (const [key, value] of Object.entries(data?.capabilities || {})) { if (!(key in capabilityLabels) || value === false || value === null || value === undefined) continue; const badge = document.createElement('span'); badge.className = 'capability'; const label = capabilityLabels[key]; badge.textContent = value === true ? label : `${label}: ${String(value)}`; capabilities.append(badge); } renderNotifications(data?.notifications || []); renderConnectors(data?.connectors || []); const usage = data?.usage || data?.metrics?.usage; if (usage && typeof usage === 'object') { const total = usage.total_tokens ?? usage.tokens ?? usage.audio_tokens; byId('usage-value').textContent = total === undefined ? '已返回' : String(total); byId('usage-note').textContent = usage.updated_at ? `更新于 ${escapeTime(usage.updated_at)}` : '服务端返回的最近一轮用量'; } byId('usage-date').textContent = new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',month:'numeric',day:'numeric'}).format(new Date()); }
function clearPrivateData() { invalidateTaskIdentity(); window.dispatchEvent(new CustomEvent('assistant-auth-changed', {detail:{token:''}})); byId('hero-result').textContent = '连接后显示今天的安排。'; byId('task-list').innerHTML = '<div class="empty">连接后加载任务。</div>'; byId('memory-list').innerHTML = '<div class="empty">连接后加载记忆。</div>'; byId('home-list').innerHTML = '<div class="empty">连接后读取家居配置。</div>'; byId('notification-list').replaceChildren(); byId('capabilities').replaceChildren(); byId('connector-status').replaceChildren(); byId('conversation').innerHTML = '<div class="conversation-empty">连接后，你的对话会出现在这里。</div>'; byId('screen-card-state').textContent = '设备连接后可显示'; byId('usage-value').textContent = '—'; byId('usage-note').textContent = '认证后显示服务返回的最近一轮用量。'; availableVoices = []; const voiceSelect = byId('voice-select'); voiceSelect.replaceChildren(); voiceSelect.innerHTML = '<option>连接后加载音色</option>'; voiceSelect.disabled = true; byId('voice-save').disabled = true; byId('voice-setting-state').textContent = '连接后加载'; byId('voice-description').textContent = '选择音色后显示服务端说明。'; const llmSelect = byId('llm-select'); llmSelect.replaceChildren(); llmSelect.innerHTML = '<option>连接后加载模型</option>'; llmSelect.disabled = true; byId('llm-save').disabled = true; byId('llm-setting-state').textContent = '连接后加载'; byId('llm-description').textContent = '选择对话模型后显示服务端说明。'; for (const node of document.querySelectorAll('[data-status]')) { node.textContent = '—'; node.className = ''; } }
async function loadStatus() { const identity = authGeneration; try { const data = await apiGet('/api/status'); if (identity !== authGeneration) return; apiAuthenticated = true; byId('api-state').textContent = 'API 已认证'; renderStatus(data || {}); } catch (error) { if (identity !== authGeneration) return; apiAuthenticated = false; stopPolling(); if (error.status === 401 || error.status === 403) clearPrivateData(); byId('api-state').textContent = 'API 离线或不可用'; log(`状态：${error.message}`); } }
async function refreshAll() { await Promise.all([loadStatus(), loadTasks(), loadMemories(), loadHome(), loadVoices(), loadLlm()]); }
function stopPolling() { if (pollingTimer) { clearInterval(pollingTimer); pollingTimer = null; } }
function updatePolling() { stopPolling(); if (apiToken && apiAuthenticated && document.visibilityState === 'visible') pollingTimer = setInterval(() => { if (apiToken && apiAuthenticated && document.visibilityState === 'visible') loadStatus(); }, 2500); }
async function initAudio() { if (!audioContext) { audioContext = new AudioContext(); workletLoaded = false; } await audioContext.resume(); }
function stopAudio() { for (const node of playing) { try { node.stop(); } catch {} } playing.clear(); nextPlay = 0; responseFinished = false; }
function stopCapture(commit) { recording = false; if (capture) { capture.port.onmessage = null; capture.disconnect(); capture = null; } if (source) { source.disconnect(); source = null; } if (mute) { mute.disconnect(); mute = null; } if (media) { media.getTracks().forEach(track => track.stop()); media = null; } byId('record').textContent = '开始录音'; if (commit && socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify({type:'listen',state:'stop'})); }
function playPcm(arrayBuffer) { if (!audioContext || arrayBuffer.byteLength % 2) return; if (nextPlay - audioContext.currentTime > 30) { log('待播音频超过30秒，已停止播放'); stopAudio(); return; } const samples = new Int16Array(arrayBuffer); const buffer = audioContext.createBuffer(1,samples.length,24000); const output = buffer.getChannelData(0); for (let index = 0; index < samples.length; index++) output[index] = samples[index] / 32768; const node = audioContext.createBufferSource(); node.buffer = buffer; node.connect(audioContext.destination); const start = Math.max(audioContext.currentTime + .02, nextPlay); nextPlay = start + buffer.duration; playing.add(node); node.onended = () => { playing.delete(node); if (!playing.size && responseFinished) setVoiceState('listening'); }; node.start(start); }
function setApiControls(connected) { byId('connect').disabled = connected; byId('disconnect').disabled = !connected; byId('record').disabled = !connected; byId('send').disabled = !connected; byId('abort').disabled = true; }
function setVoiceControls(ready) { byId('connect').disabled = apiAuthenticated; byId('disconnect').disabled = !apiAuthenticated; byId('record').disabled = !apiAuthenticated || !ready; byId('send').disabled = !apiAuthenticated || !ready; byId('abort').disabled = !apiAuthenticated || !ready; }
function setApiPill(label, connected = true) { const pill = byId('connection-state'); pill.className = `connection-pill ${connected ? 'ok' : ''}`; pill.querySelector('span').textContent = label; }
function rejectVoiceReady(error) { if (voiceReadyReject) { voiceReadyReject(error); voiceReadyReject = null; voiceReadyResolve = null; } }
async function ensureVoice() {
  if (!apiAuthenticated) throw new Error('请先连接助手');
  if (voiceReady && socket?.readyState === WebSocket.OPEN) return;
  if (socket?.readyState === WebSocket.CONNECTING && voiceReadyPromise) return voiceReadyPromise;
  voiceReady = false;
  setVoiceControls(false);
  voiceReadyPromise = new Promise((resolve, reject) => { voiceReadyResolve = resolve; voiceReadyReject = reject; });
  await initAudio();
  setVoiceState('connecting');
  socket = new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/debug/ws`);
  socket.binaryType = 'arraybuffer';
  const timeout = setTimeout(() => { rejectVoiceReady(new Error('语音连接等待超时')); socket?.close(); }, 12000);
  socket.onopen = () => { socket.send(JSON.stringify({type:'auth',token:apiToken})); log('语音连接已建立，等待服务端就绪'); };
  socket.onclose = () => {
    clearTimeout(timeout); stopCapture(false); stopAudio(); voiceReady = false;
    rejectVoiceReady(new Error('语音连接已结束')); setApiControls(apiAuthenticated);
    setVoiceState('idle'); if (apiAuthenticated) setApiPill('API 已连接'); log('语音连接结束');
  };
  socket.onerror = () => { log('语音连接失败，请检查服务端和令牌'); setVoiceState('error'); };
  socket.onmessage = event => {
    try {
      if (event.data instanceof ArrayBuffer) { playPcm(event.data); return; }
      const message = JSON.parse(event.data);
      if (message.type === 'custom') {
        setVoiceState(message.payload.state, message.payload.detail);
        if (message.payload.state === 'listening') {
          voiceReady = true; clearTimeout(timeout);
          if (voiceReadyResolve) { voiceReadyResolve(); voiceReadyResolve = null; voiceReadyReject = null; }
          setVoiceControls(true); log('语音会话已就绪');
        }
      } else if (message.type === 'interrupt') stopAudio();
      else if (message.type === 'tts') {
        if (message.state === 'start') { responseFinished = false; setVoiceState('speaking'); }
        if (message.state === 'stop') { responseFinished = true; if (!playing.size) setVoiceState('listening'); }
        if (message.text) { addBubble('assistant', message.text); log(`回复：${message.text}`); }
      } else if (message.type === 'stt') { addBubble('user', message.text); log(`识别：${message.text}`); }
      else if (message.type === 'alert') { log(message.message || '服务端告警'); setVoiceState('error'); }
      else if (message.type === 'usage') {
        const usage = message.usage || {}; const total = usage.total_tokens ?? usage.tokens ?? usage.audio_tokens;
        byId('usage-value').textContent = total === undefined ? '已返回' : String(total); byId('usage-note').textContent = '本次对话的最近一轮用量'; log(`用量：${JSON.stringify(usage)}`);
      } else if (message.type === 'metric') log(`首段音频：${message.first_audio_ms ?? '未提供'}ms`);
    } catch (error) { log(`消息处理失败：${error.message}`); stopAudio(); }
  };
  return voiceReadyPromise;
}
async function connectApi() {
  invalidateTaskIdentity();
  apiToken = tokenInput.value.trim(); apiAuthenticated = true;
  try {
    try { await apiGet('/api/status'); }
    catch (error) {
      if (apiToken || error.status !== 401 || !['localhost','127.0.0.1','[::1]'].includes(location.hostname)) throw error;
      await api('/api/auth/local-login', {method:'POST',body:'{}'});
    }
    clearPrivateData(); apiAuthenticated = true; setApiControls(true); byId('record').disabled = true; byId('send').disabled = true;
    byId('api-state').textContent = '已恢复管理员登录'; setApiPill('API 已连接', true);
    await refreshAll(); updatePolling();
    if (apiAuthenticated) { window.dispatchEvent(new CustomEvent('assistant-auth-changed', {detail:{token:apiToken}})); setApiControls(true); }
  } catch (error) { apiAuthenticated = false; apiToken = ''; throw error; }
}
function disconnect() {
  stopPolling(); apiToken = ''; apiAuthenticated = false; tokenInput.value = ''; rejectVoiceReady(new Error('已断开连接')); socket?.close(); socket = null; voiceReady = false; clearPrivateData(); setApiControls(false); byId('api-state').textContent = '尚未认证'; setVoiceState('idle');
}
byId('connect').onclick = async () => { try { await connectApi(); } catch (error) { log(error.message); toast(error.message); setApiControls(false); setVoiceState('error'); } };
byId('disconnect').onclick = disconnect;
byId('abort').onclick = () => { if (socket?.readyState !== WebSocket.OPEN) return; stopAudio(); socket.send(JSON.stringify({type:'abort'})); setVoiceState('listening'); };
byId('record').onclick = async () => { if (recording) { stopCapture(true); setVoiceState('thinking'); return; } if (voiceStarting) return; voiceStarting = true; byId('record').disabled = true; try { await ensureVoice(); stopAudio(); socket.send(JSON.stringify({type:'abort'})); await initAudio(); media = await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:true,noiseSuppression:true,autoGainControl:true}}); if (!workletLoaded) { await audioContext.audioWorklet.addModule('/static/recorder.js'); workletLoaded = true; } if (socket.readyState !== WebSocket.OPEN) throw new Error('连接已经结束'); source = audioContext.createMediaStreamSource(media); capture = new AudioWorkletNode(audioContext,'pcm-capture'); mute = audioContext.createGain(); mute.gain.value = 0; capture.port.onmessage = event => { if (!recording || socket.readyState !== WebSocket.OPEN) return; if (socket.bufferedAmount > 65536) { log('上行拥塞，已停止录音'); stopCapture(false); socket.close(); return; } socket.send(event.data); }; socket.send(JSON.stringify({type:'listen',state:'start',mode:'manual'})); recording = true; source.connect(capture).connect(mute).connect(audioContext.destination); byId('record').textContent = '结束录音并发送'; setVoiceState('listening'); } catch (error) { stopCapture(false); log(error.message); toast(error.message); setApiControls(apiAuthenticated); } finally { voiceStarting = false; byId('record').disabled = !apiAuthenticated || !voiceReady; } };
byId('send').onclick = async () => { const text = byId('question').value.trim(); if (!text || !apiAuthenticated) return; byId('send').disabled = true; try { await ensureVoice(); stopCapture(false); stopAudio(); socket.send(JSON.stringify({type:'abort'})); socket.send(JSON.stringify({type:'text',text})); byId('question').value = ''; addBubble('user', text); log(`文字：${text}`); setVoiceState('thinking'); } catch (error) { toast(error.message); log(error.message); } finally { byId('send').disabled = !apiAuthenticated || !voiceReady; } };
byId('question').onkeydown = event => { if (event.key === 'Enter' && !event.shiftKey && !byId('send').disabled) { event.preventDefault(); byId('send').click(); } };
byId('task-form').onsubmit = async event => {
  event.preventDefault(); if (taskWrite || taskUncertain || taskDate && !taskDateFilterAvailable) return;
  const title = byId('task-title').value.trim(); const dueInput = byId('task-due').value; if (!title) return;
  const write = beginTaskWrite();
  try {
    await taskWriteRequest('/api/tasks', {title,due_at:localDateTimeToIso(dueInput),idempotency_key:makeKey()});
    if (write.identity !== authGeneration) return;
    byId('task-title').value = ''; byId('task-due').value = ''; clearTaskEditor(); const refreshed = await refreshTaskHome();
    if (write.identity === authGeneration) toast((dueInput ? '提醒已创建' : '待办已添加') + (refreshed ? '，已返回首页。' : '；首页刷新失败，请手动刷新核对。'));
  } catch (error) {
    if (write.identity !== authGeneration) return;
    if (!error.status || error.status >= 500) taskUncertain = true;
    toast(taskUncertain ? '创建结果待核实，请刷新当前页核对；不会自动重发。' : error.message); log(`创建任务：${error.message}`);
  } finally {
    if (taskWrite === write) taskWrite = null;
    updateTaskWriteControls(); updateTaskPager();
  }
};
byId('memory-form').onsubmit = async event => { event.preventDefault(); const key = byId('memory-key').value.trim(); const value = byId('memory-value').value.trim(); if (!key || !value) return; try { await api(`/api/memories/${encodeURIComponent(key)}`, {method:'PUT', body:JSON.stringify({value})}); byId('memory-key').value = ''; byId('memory-value').value = ''; await loadMemories(); toast('记忆已保存'); } catch (error) { toast(error.message); log(`保存记忆：${error.message}`); } };
byId('task-date-apply').onclick = () => applyTaskDate(); byId('task-date-clear').onclick = () => applyTaskDate(true); byId('task-first').onclick = () => navigateTasks(true); byId('task-next').onclick = () => navigateTasks(); byId('refresh-tasks').onclick = () => { if (taskLoading) return; if (taskWrite) { toast('正在保存，完成后再刷新任务。'); return; } if (taskEditor && !taskUncertain && !window.confirm('刷新将关闭尚未提交的任务修改，是否继续？')) return; clearTaskEditor(); loadTasks(true); }; byId('refresh-memories').onclick = loadMemories; byId('refresh-home').onclick = () => loadHome(false, true); byId('more-home').onclick = () => loadHome(true, false); byId('voice-form').onsubmit = event => { event.preventDefault(); saveVoice(); }; byId('llm-form').onsubmit = event => { event.preventDefault(); saveLlm(); }; document.querySelectorAll('[data-scroll]').forEach(button => button.onclick = () => byId(button.dataset.scroll)?.scrollIntoView({behavior:'smooth'})); document.addEventListener('visibilitychange', updatePolling);
byId('today-label').textContent = new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',weekday:'long',month:'long',day:'numeric'}).format(new Date());
window.addEventListener('beforeunload', () => { stopCapture(false); stopAudio(); stopPolling(); socket?.close(); });
setApiControls(false); setVoiceState('idle');


connectApi().catch(error => { byId('api-state').textContent = '请登录管理员；已有设备无需重新配对'; log(error.message); });
