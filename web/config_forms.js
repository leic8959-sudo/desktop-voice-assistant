import { element } from './console_api.js?v=config-20261104';

const sourceLabels = { environment: '环境变量管理', secure_override: '安全存储', config_file: '启动配置', saved_preference: '后台偏好' };
export const addConfigForm = (root, item, { api, isCurrent = () => true, onSaved = () => {} }) => {
  const section = element('details', 'connector-detail');
  section.append(element('summary', '', '配置与安全保存'));
  const form = element('form', 'connector-config-form');
  const controls = new Map();
  const available = item.config_available !== false;
  const isLocal = ['localhost', '127.0.0.1', '[::1]', '::1'].includes(location.hostname);
  const transportAllowed = item.id === 'gmail' ? isLocal : isLocal || location.protocol === 'https:';
  for (const field of item.config_fields || []) {
    const wrapper = element('label', '', `${field.label} · ${sourceLabels[field.source] || '启动配置'} · ${field.configured ? '有值' : '未配置'}`);
    const input = element(field.options ? 'select' : field.key === 'home_entities' ? 'textarea' : 'input');
    input.name = field.key; input.autocomplete = 'off';
    if (field.options) {
      const empty = element('option', '', '保持当前值'); empty.value = ''; input.append(empty);
      field.options.forEach(option => { const node = element('option', '', option.name || option.id); node.value = option.id; input.append(node); });
      if (field.value !== undefined) input.value = field.value === null ? 'null' : String(field.value);
    } else if (field.key === 'gmail_send_enabled') {
      input.type = 'checkbox'; input.checked = Boolean(field.value);
    } else {
      if (input.tagName !== 'TEXTAREA') {
        input.type = field.secret ? 'password' : field.kind === 'integer' ? 'number' : 'text';
        if (field.minimum !== undefined) input.min = field.minimum;
        if (field.maximum !== undefined) input.max = field.maximum;
      }
      input.placeholder = '留空保留已有值';
      if (!field.secret && field.value !== undefined) input.value = typeof field.value === 'object' ? JSON.stringify(field.value) : String(field.value);
    }
    input.disabled = !field.editable || !available || !transportAllowed;
    wrapper.append(input, element('small', 'muted', field.help));
    const clear = element('input'); clear.type = 'checkbox'; clear.disabled = input.disabled;
    const clearLabel = element('label', 'config-clear');
    clearLabel.append(clear, document.createTextNode('明确清除该字段'));
    if (field.key === 'gmail_send_enabled' || field.key === 'gmail_redirect_uri') clear.disabled = true;
    if (item.id === 'models' && !field.secret) clear.disabled = true;
    controls.set(field.key, { input, clear, field }); form.append(wrapper, clearLabel);
  }
  const save = element('button', 'primary', '安全保存配置'); save.type = 'submit';
  save.disabled = !available || !transportAllowed;
  const state = element('p', 'muted', !available ? '当前环境没有安全秘密存储；后台只读，请通过部署环境变量管理。' : !transportAllowed ? '秘密仅允许直连本机或 HTTPS 提交；Gmail 配置仅允许本机。' : '留空保留已有秘密；保存与授权、连接检查是独立状态。');
  state.setAttribute('role', 'status'); form.append(save, state); section.append(form); root.append(section);
  let pending = false;
  form.addEventListener('submit', async event => {
    event.preventDefault(); if (pending || save.disabled || !isCurrent()) return;
    pending = true; save.disabled = true; state.textContent = '正在安全保存…';
    try {
      let path; let body;
      if (item.id === 'gmail') {
        path = '/api/gmail/config'; body = { clear_fields: [] };
        const names = { gmail_client_id: 'client_id', gmail_client_secret: 'client_secret', gmail_redirect_uri: 'redirect_uri', gmail_send_enabled: 'send_enabled', gmail_proxy_url: 'proxy_url' };
        for (const [key, control] of controls) {
          if (control.input.disabled) continue;
          if (control.clear.checked) body.clear_fields.push(names[key]);
          else if (key === 'gmail_send_enabled') body[names[key]] = control.input.checked;
          else if (control.input.value) body[names[key]] = control.input.value;
        }
      } else {
        path = item.id === 'models' ? '/api/settings/models' : `/api/connectors/${encodeURIComponent(item.id)}/config`;
        body = { values: {}, clear_fields: [] };
        for (const [key, control] of controls) {
          if (control.input.disabled) continue;
          if (control.clear.checked) body.clear_fields.push(key);
          else if (control.input.value !== '') body.values[key] = control.field.kind === 'integer' ? Number(control.input.value) : control.input.value;
        }
      }
      const data = await api.request(path, { method: 'POST', body, timeout: 30000 });
      if (!isCurrent()) return;
      if (data.saved !== true) throw new Error('服务端没有确认配置已保存。');
      state.textContent = data.message || '配置已保存，请单独检查授权与连接。';
      onSaved(data);
    } catch (error) { if (isCurrent()) state.textContent = `保存失败或结果未确认：${error.message}。可先刷新配置状态核查。`; }
    finally {
      for (const control of controls.values()) { if (control.field.secret) control.input.value = ''; control.clear.checked = false; }
      pending = false; save.disabled = !available || !transportAllowed;
    }
  });
  return section;
};
