import { createConsoleApi, element } from './console_api.js?v=knowledge-20261104';

const byId = id => document.getElementById(id);
const state = message => { byId('state').textContent = message; };
let documentCursor = null;
let meetingCursor = null;
let selectedDocument = null;
let generation = 0;
let recording = null;
let recordingPending = false;
let pendingAudio = null;
let audioUrl = null;
const maxAudioSeconds = 300;
const maxPcmBytes = maxAudioSeconds * 16000 * 2;
const api = createConsoleApi(() => byId('token').value, () => {
  generation += 1;
  api.cancel();
  byId('token').value = '';
  byId('results').replaceChildren();
  byId('documents').replaceChildren();
  byId('meetings').replaceChildren();
  byId('documentEditor').hidden = true;
  selectedDocument = null;
  byId('documentText').value = '';
  byId('minutesOut').textContent = '';
  byId('languageOut').textContent = '';
  state('管理员认证失效，请重新登录。');
});
const show = (id, text) => { byId(id).textContent = text; };
const modelLabel = data => `来源：${data.source || '当前用户资料'} · ${data.provider || ''} / ${data.model || ''}`;

async function action(button, operation) {
  button.disabled = true;
  const current = generation;
  try {
    await operation(current);
  } catch (error) {
    if (current === generation) state(error.message);
  } finally {
    button.disabled = false;
    updateAudioButtons();
  }
}

async function loadStatus() {
  const data = await api.request('/api/knowledge/status');
  const usage = data.usage;
  show('capacity', `资料 ${usage.document_count}/${data.limits.documents} 份，${usage.document_chars}/${data.limits.total_chars} 字符；会议 ${usage.meeting_count}/${data.limits.meetings} 份。文本模型：${data.text_model.provider}/${data.text_model.model}；ASR：${data.asr_configured ? '已配置凭据，尚未验证连通性' : '未配置'}。当前为单用户资料空间。`);
}

async function loadDocuments(append = false) {
  const cursor = append ? documentCursor : '';
  const data = await api.request(`/api/knowledge/documents?cursor=${encodeURIComponent(cursor || '')}`);
  if (!append) byId('documents').replaceChildren();
  for (const item of data.items) {
    const row = element('div', 'item');
    row.append(element('strong', '', item.filename), element('p', 'meta', `${item.content_chars} 字符 · 过期时间 ${item.expires_at}`));
    const view = element('button', '', '查看 / 编辑 / 删除');
    view.type = 'button';
    view.onclick = () => action(view, async () => {
      const document = await api.request(`/api/knowledge/documents/${encodeURIComponent(item.document_id)}`);
      selectedDocument = document;
      show('selectedName', document.filename);
      byId('documentText').value = document.content;
      byId('documentEditor').hidden = false;
      byId('documentEditor').scrollIntoView({ block: 'center', behavior: 'smooth' });
    });
    row.append(view);
    byId('documents').append(row);
  }
  if (!append && !data.items.length) byId('documents').append(element('p', 'hint', '尚未导入资料。'));
  documentCursor = data.next_cursor;
  byId('documentsMore').hidden = !documentCursor;
}

async function loadMeetings(append = false) {
  const data = await api.request(`/api/meetings?cursor=${encodeURIComponent(append ? meetingCursor || '' : '')}`);
  if (!append) byId('meetings').replaceChildren();
  for (const item of data.items) {
    const row = element('div', 'item');
    row.append(element('strong', '', item.title), element('p', 'meta', `创建于 ${item.created_at} · 过期时间 ${item.expires_at}`));
    const view = element('button', '', '查看纪要');
    const remove = element('button', '', '删除会议');
    view.type = remove.type = 'button';
    view.onclick = () => action(view, async () => {
      const data = await api.request(`/api/meetings/${encodeURIComponent(item.meeting_id)}`);
      byId('title').value = data.title;
      byId('transcript').value = data.transcript;
      show('minutesOut', data.minutes);
      byId('minutesOut').scrollIntoView({ block: 'center', behavior: 'smooth' });
    });
    remove.onclick = () => {
      if (!window.confirm(`删除会议“${item.title}”的转写和纪要？`)) return;
      action(remove, async () => {
        await api.request(`/api/meetings/${encodeURIComponent(item.meeting_id)}`, { method: 'DELETE' });
        await loadMeetings();
        await loadStatus();
        state('会议转写和纪要已从当前资料空间删除。');
      });
    };
    row.append(view, remove);
    byId('meetings').append(row);
  }
  if (!append && !data.items.length) byId('meetings').append(element('p', 'hint', '尚未保存会议纪要。'));
  meetingCursor = data.next_cursor;
  byId('meetingsMore').hidden = !meetingCursor;
}

byId('login').onclick = () => action(byId('login'), async () => {
  await api.request('/api/auth/local-login', { method: 'POST', body: {}, authenticate: false });
  state('本机登录成功，可以加载资料。');
});
byId('refresh').onclick = () => action(byId('refresh'), async () => {
  await Promise.all([loadStatus(), loadDocuments(), loadMeetings()]);
  state('已读取当前助手的真实资料和会议。');
});
byId('documentsMore').onclick = () => action(byId('documentsMore'), () => loadDocuments(true));
byId('meetingsMore').onclick = () => action(byId('meetingsMore'), () => loadMeetings(true));
byId('upload').onclick = () => action(byId('upload'), async () => {
  const file = byId('file').files[0];
  if (!file) throw new Error('请选择资料文件。');
  if (file.size > 2 * 1024 * 1024) throw new Error('文件超过 2 MiB 上限。');
  const multipart = new FormData();
  multipart.append('file', file);
  const data = await api.request('/api/knowledge/import', { method: 'POST', multipart, timeout: 30000 });
  state(`已导入 ${data.filename}，${data.chunks} 个片段。`);
  await loadDocuments();
  await loadStatus();
});
byId('search').onclick = () => action(byId('search'), async () => {
  const data = await api.request(`/api/knowledge/search?q=${encodeURIComponent(byId('query').value)}`);
  byId('results').replaceChildren();
  for (const item of data.items) {
    const card = element('article', 'item');
    card.append(element('strong', '', item.citation), element('p', '', item.text), element('p', 'meta', `字符位置 ${item.start_char}–${item.end_char} · 更新 ${item.updated_at}`));
    byId('results').append(card);
  }
  if (!data.items.length) byId('results').append(element('p', 'hint', '没有命中的原文片段，可缩短关键词重试。'));
  state(`检索返回 ${data.items.length} 条来源片段。`);
});
byId('query').addEventListener('keydown', event => { if (event.key === 'Enter') byId('search').click(); });
byId('closeDocument').onclick = () => { selectedDocument = null; byId('documentEditor').hidden = true; byId('documentText').value = ''; };
byId('saveDocument').onclick = () => action(byId('saveDocument'), async () => {
  if (!selectedDocument) throw new Error('请先选择资料。');
  await api.request(`/api/knowledge/documents/${encodeURIComponent(selectedDocument.document_id)}`, { method: 'PUT', body: { filename: selectedDocument.filename, content: byId('documentText').value }, timeout: 30000 });
  state('资料正文与全文索引已更新。');
  await loadDocuments();
  await loadStatus();
});
byId('deleteDocument').onclick = () => {
  if (!selectedDocument || !window.confirm(`删除资料“${selectedDocument.filename}”及全部索引？`)) return;
  action(byId('deleteDocument'), async () => {
    await api.request(`/api/knowledge/documents/${encodeURIComponent(selectedDocument.document_id)}`, { method: 'DELETE' });
    byId('closeDocument').click();
    byId('results').replaceChildren();
    await loadDocuments();
    await loadStatus();
    state('资料与全文索引已删除。');
  });
};

function updateAudioButtons() {
  byId('recordStart').disabled = Boolean(recording) || recordingPending;
  byId('recordStop').disabled = !recording;
  byId('audioFile').disabled = Boolean(recording) || recordingPending;
  byId('clearAudio').disabled = recordingPending;
  byId('transcribe').disabled = !pendingAudio || !byId('audioConsent').checked || Boolean(recording) || recordingPending || byId('transcribe').dataset.busy === 'true';
}
function setPendingAudio(blob) {
  if (audioUrl) URL.revokeObjectURL(audioUrl);
  pendingAudio = blob;
  audioUrl = blob ? URL.createObjectURL(blob) : null;
  byId('audioPreview').hidden = !audioUrl;
  if (audioUrl) byId('audioPreview').src = audioUrl;
  else byId('audioPreview').removeAttribute('src');
  byId('audioConsent').checked = false;
  updateAudioButtons();
}
function wavBlob(chunks, size) {
  const header = new ArrayBuffer(44);
  const view = new DataView(header);
  const write = (offset, text) => [...text].forEach((character, index) => view.setUint8(offset + index, character.charCodeAt(0)));
  write(0, 'RIFF'); view.setUint32(4, 36 + size, true); write(8, 'WAVE'); write(12, 'fmt ');
  view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 1, true);
  view.setUint32(24, 16000, true); view.setUint32(28, 32000, true); view.setUint16(32, 2, true); view.setUint16(34, 16, true);
  write(36, 'data'); view.setUint32(40, size, true);
  return new Blob([header, ...chunks], { type: 'audio/wav' });
}
async function stopRecording(discard = false) {
  const active = recording;
  if (!active) return;
  recordingPending = true;
  recording = null;
  clearInterval(active.timer);
  active.node.port.onmessage = null;
  active.source.disconnect();
  active.node.disconnect();
  active.gain.disconnect();
  active.stream.getTracks().forEach(track => track.stop());
  await active.context.close();
  recordingPending = false;
  if (!discard && active.size) {
    setPendingAudio(wavBlob(active.chunks, active.size));
    show('recordState', `录音已结束，${(active.size / 32000).toFixed(1)}秒；可预听后确认上传。`);
  } else show('recordState', '麦克风已关闭，未保留录音。');
  updateAudioButtons();
}
byId('recordStart').onclick = async () => {
  if (recording || recordingPending) return;
  if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia || !window.AudioWorkletNode) {
    state('录音需要 localhost 或 HTTPS 和支持 AudioWorklet 的浏览器；也可以上传符合格式的 WAV。');
    return;
  }
  recordingPending = true;
  updateAudioButtons();
  show('recordState', '等待麦克风许可…');
  let stream;
  let context;
  try {
    stream = await navigator.mediaDevices.getUserMedia({ audio: { channelCount: 1, echoCancellation: true, noiseSuppression: true }, video: false });
    if (!recordingPending) { stream.getTracks().forEach(track => track.stop()); return; }
    context = new AudioContext();
    await context.audioWorklet.addModule('/static/recorder.js');
    await context.resume();
    if (!recordingPending) { stream.getTracks().forEach(track => track.stop()); await context.close(); return; }
    const source = context.createMediaStreamSource(stream);
    const node = new AudioWorkletNode(context, 'pcm-capture');
    const gain = context.createGain();
    gain.gain.value = 0;
    const active = { stream, context, source, node, gain, chunks: [], size: 0, started: Date.now(), timer: null };
    recording = active;
    setPendingAudio(null);
    byId('audioFile').value = '';
    node.port.onmessage = event => {
      if (recording !== active) return;
      const chunk = new Uint8Array(event.data);
      if (active.size + chunk.byteLength > maxPcmBytes) { stopRecording().catch(error => state(error.message)); return; }
      active.chunks.push(chunk);
      active.size += chunk.byteLength;
    };
    source.connect(node); node.connect(gain); gain.connect(context.destination);
    active.timer = setInterval(() => {
      show('recordState', `录音中 ${(active.size / 32000).toFixed(1)} 秒 / ${maxAudioSeconds} 秒`);
      if (Date.now() - active.started >= maxAudioSeconds * 1000) stopRecording().catch(error => state(error.message));
    }, 250);
    show('recordState', '麦克风已开启，正在录音；点击结束后才可提交转写。');
  } catch (error) {
    recording = null;
    stream?.getTracks().forEach(track => track.stop());
    if (context && context.state !== 'closed') await context.close();
    show('recordState', `录音未开始：${error.message}`);
  } finally {
    recordingPending = false;
    updateAudioButtons();
  }
};
byId('recordStop').onclick = () => stopRecording().catch(error => state(error.message));
byId('clearAudio').onclick = async () => {
  recordingPending = false;
  await stopRecording(true);
  setPendingAudio(null);
  byId('audioFile').value = '';
  show('recordState', '本次录音已清除，麦克风已关闭。');
};
byId('audioFile').onchange = () => {
  const file = byId('audioFile').files[0];
  if (file && file.size > maxPcmBytes + 4096) { state('音频超过约 9.2 MiB 上限。'); byId('audioFile').value = ''; return; }
  setPendingAudio(file || null);
};
byId('audioConsent').onchange = updateAudioButtons;
byId('transcribe').onclick = () => action(byId('transcribe'), async current => {
  if (!pendingAudio || !byId('audioConsent').checked) throw new Error('请先选择录音并确认处理授权。');
  byId('transcribe').dataset.busy = 'true';
  const multipart = new FormData();
  multipart.append('authorized', 'true');
  multipart.append('file', pendingAudio, 'meeting.wav');
  state('正在把本次录音发送给阿里 ASR，按实时节奏处理，通常需要等待录音时长，最长360秒…');
  try {
    const data = await api.request('/api/meetings/audio', { method: 'POST', multipart, timeout: 400000 });
    if (current !== generation) return;
    byId('transcript').value = data.transcript;
    state(`转写完成：${data.duration_seconds}秒，${data.provider}/${data.model}。请检查文本，再生成纪要；音频未在服务端保存。`);
  } finally {
    byId('transcribe').dataset.busy = 'false';
  }
});
byId('minutes').onclick = () => action(byId('minutes'), async current => {
  show('minutesOut', '正在根据转写生成结论与待办预览…');
  try {
    const data = await api.request('/api/meetings/minutes', { method: 'POST', body: { title: byId('title').value || '会议', transcript: byId('transcript').value }, timeout: 130000 });
    if (current !== generation) return;
    show('minutesOut', `${data.minutes}\n\n${modelLabel(data)}\n${data.storage_message}。未创建外部任务。`);
    await loadMeetings();
    await loadStatus();
  } catch (error) { show('minutesOut', `生成失败：${error.message}`); throw error; }
});
byId('translate').onclick = () => action(byId('translate'), async current => {
  const data = await api.request('/api/language/translate', { method: 'POST', body: { text: byId('languageText').value, source: byId('source').value, target: byId('target').value }, timeout: 130000 });
  if (current === generation) show('languageOut', `${data.text}\n\n${modelLabel(data)}`);
});
const practice = (button, correction) => action(button, async current => {
  if (correction && !byId('answer').value.trim()) throw new Error('请先填写你的回答。');
  const data = await api.request('/api/language/practice', { method: 'POST', body: { text: byId('languageText').value, language: byId('target').value, level: byId('level').value, answer: correction ? byId('answer').value : '' }, timeout: 130000 });
  if (current === generation) show('languageOut', `${data.exercise}\n\n${modelLabel(data)}`);
});
byId('practice').onclick = () => practice(byId('practice'), false);
byId('correct').onclick = () => practice(byId('correct'), true);
window.addEventListener('pagehide', () => { recordingPending = false; api.cancel(); stopRecording(true); if (audioUrl) URL.revokeObjectURL(audioUrl); });
updateAudioButtons();
