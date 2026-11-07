import { createConsoleApi, element } from './console_api.js';

const byId = id => document.getElementById(id);
let cursor = null;
let generation = 0;
const clear = () => { generation += 1; cursor = null; api.cancel(); byId('memory-token').value = ''; byId('memory-items').replaceChildren(); byId('memory-candidates').replaceChildren(); byId('memory-state').textContent = '认证失效，请重新登录。'; };
const api = createConsoleApi(() => byId('memory-token').value, clear);
const perform = async operation => { try { await operation(); } catch (error) { byId('memory-state').textContent = error.message; } };
const renderItems = (items, append = false) => {
  const root = byId('memory-items');
  if (!append) root.replaceChildren();
  for (const item of items) {
    const article = element('article', 'card');
    article.append(element('strong', '', item.key), element('p', '', item.value), element('p', 'muted', `${item.source === 'automatic' ? '自动提取' : '手动保存'} · 到期 ${item.expires_at || '未设置'}`));
    const edit = element('button', '', '纠正');
    edit.addEventListener('click', () => { byId('memory-key').value = item.key; byId('memory-value').value = item.value; byId('memory-value').focus(); });
    const remove = element('button', '', '忘记');
    remove.addEventListener('click', () => perform(async () => { if (!window.confirm(`删除「${item.key}」并阻止同主题自动重新保存？`)) return; await api.request(`/api/memories/${encodeURIComponent(item.key)}`, { method: 'DELETE' }); await load(); }));
    article.append(edit, remove); root.append(article);
  }
  if (!items.length && !append) root.append(element('p', 'muted', '没有匹配记忆。'));
};
const load = async (next = false) => {
  const current = generation;
  const [settings, data] = await Promise.all([api.request('/api/memory/settings'), api.request(`/api/memories?limit=30${next && cursor ? `&cursor=${encodeURIComponent(cursor)}` : ''}`)]);
  if (current !== generation) return;
  byId('memory-enabled').checked = settings.settings.enabled;
  byId('memory-mode').value = settings.settings.mode;
  byId('memory-model').checked = settings.settings.extract_with_model;
  const extraction = settings.last_extraction;
  const extractionStatus = { idle: '等待对话', queued: '已排队', processing: '正在提取', saved: '已保存', partial_saved: '部分已保存，其余写入失败', unchanged: '内容相同，已延长保留', filtered: '本轮未保存', no_candidate: '没有可保存的稳定偏好或长期事项', candidate_rejected: '候选未满足来源、稳定性或隐私要求', mode_excluded: '当前模式未包含该类记忆', manual_protected: '已有手动记录，请使用下方纠正入口修改', deleted_protected: '该主题或内容已被忘记，保持删除', model_failed: '模型提取失败，可重试或手动保存', storage_failed: '记忆写入失败，可重试', completed: '已处理', failed: '处理失败', queue_full: '队列已满，本轮未处理', review_required: '等待确认' }[extraction.status] || '待核对';
  const reason = { disabled: '自动记忆已关闭', sensitive: '发言包含敏感信息', do_not_store: '已尊重不记或忘记要求', untrusted_source: '来源不是用户发言', invalid_input: '发言长度或格式不符', expired: '等待时间过长，已丢弃', settings_changed: '设置已更新', manual_change: '已手动修改记忆，旧自动候选已作废', storage_or_processing_failed: '处理未完成' }[extraction.reason] || '';
  const refreshState = extraction.context_refresh === 'failed' ? '；已落库，但本轮上下文刷新失败' : '';
  byId('memory-extraction').textContent = `最近提取：${extractionStatus}${reason ? `（${reason}）` : ''} · 保存或更新 ${extraction.saved} 条 · 排队 ${settings.queued} 条${refreshState}；容量 ${settings.capacity} 条，默认保留 ${settings.ttl_days} 天。`;
  const candidates = byId('memory-candidates'); candidates.replaceChildren();
  for (const candidate of settings.candidates) {
    const row = element('p', '', `${candidate.key}：${candidate.value} `); const button = element('button', '', '确认保存');
    button.addEventListener('click', () => perform(async () => { await api.request('/api/memory/approve', { method: 'POST', body: { candidate_id: candidate.id } }); await load(); })); row.append(button); candidates.append(row);
  }
  renderItems(data.items, next); cursor = data.next_cursor; byId('memory-next').disabled = !cursor;
  byId('memory-state').textContent = '已读取真实记忆；关闭自动提取不会删除已有记录。';
};
byId('memory-login').addEventListener('click', () => perform(async () => { await api.request('/api/auth/local-login', { method: 'POST', body: {}, authenticate: false }); await load(); }));
byId('memory-refresh').addEventListener('click', () => perform(() => load()));
byId('memory-next').addEventListener('click', () => perform(() => load(true)));
byId('memory-settings').addEventListener('submit', event => { event.preventDefault(); perform(async () => { await api.request('/api/memory/settings', { method: 'PUT', body: { enabled: byId('memory-enabled').checked, mode: byId('memory-mode').value, extract_with_model: byId('memory-model').checked } }); await load(); }); });
byId('memory-write').addEventListener('submit', event => { event.preventDefault(); perform(async () => { await api.request(`/api/memories/${encodeURIComponent(byId('memory-key').value.trim())}`, { method: 'PUT', body: { value: byId('memory-value').value.trim() } }); byId('memory-value').value = ''; await load(); }); });
byId('memory-search').addEventListener('submit', event => { event.preventDefault(); perform(async () => { const data = await api.request(`/api/memory/search?query=${encodeURIComponent(byId('memory-query').value)}`); renderItems(data.memories); byId('memory-next').disabled = true; }); });
window.addEventListener('pagehide', clear);
