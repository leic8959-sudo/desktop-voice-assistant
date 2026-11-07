import { element } from './console_api.js?v=config-20261104';
import { addConfigForm } from './config_forms.js?v=config-20261104';

export const createSettingsPanel = (root, api, authenticated) => {
  let generation = 0;
  let busy = false;
  const clear = () => { generation += 1; busy = false; root.replaceChildren(); };
  const load = async () => {
    if (busy) return;
    if (!authenticated()) { root.textContent = '请先完成管理员认证，再加载系统设置。'; return; }
    busy = true; const current = ++generation;
    const valid = () => current === generation && authenticated();
    root.replaceChildren(element('p', 'muted', '正在读取设置…'));
    const results = await Promise.allSettled(['/api/settings', '/api/llm', '/api/voices'].map(path => api.request(path)));
    if (!valid()) return;
    root.replaceChildren();
    const main = results[0];
    if (main.status === 'fulfilled') {
      const data = main.value;
      const search = data.items.find(item => item.key === 'enable_search');
      if (search) {
        const form = element('form', 'settings-form'); const label = element('label', '', '联网搜索（下次语音会话生效）');
        const input = element('input'); input.type = 'checkbox'; input.checked = search.value; input.disabled = !search.editable;
        label.append(input); const button = element('button', 'primary', '保存搜索开关'); button.disabled = !search.editable;
        const state = element('p', 'muted', search.editable ? '搜索使用阿里凭据；开关不代表检索服务已验证。' : '受环境变量管理，后台不能覆盖。');
        form.append(label, button, state); root.append(form);
        form.addEventListener('submit', async event => {
          event.preventDefault(); button.disabled = true;
          try { const saved = await api.request('/api/settings', { method: 'PUT', body: { enable_search: input.checked } }); if (valid()) state.textContent = saved.saved ? '已保存；下次语音会话生效。' : '服务端未确认保存。'; }
          catch (error) { if (valid()) state.textContent = `保存未确认：${error.message}`; }
          finally { button.disabled = !search.editable; }
        });
      }
      addConfigForm(root, { id: 'models', config_fields: data.model_config, config_available: data.secure_storage_available }, { api, isCurrent: valid });
      const details = element('details', 'connector-detail'); details.append(element('summary', '', '只读运行设置与维护边界'), element('p', 'muted', data.maintenance_note));
      for (const item of data.items.filter(item => item.key !== 'enable_search')) details.append(element('p', 'muted', `${item.label}：${item.value ?? (item.configured ? '已配置（不显示秘密）' : '未配置')} · 需维护窗口 / 重启`));
      root.append(details);
    } else root.append(element('p', 'notice', `配置入口读取失败：${main.reason.message}`));
    const descriptors = [
      { index: 1, label: '对话模型', path: '/api/llm', key: 'provider' },
      { index: 2, label: 'Realtime 音色', path: '/api/voice', key: 'voice' },
    ];
    for (const descriptor of descriptors) {
      const result = results[descriptor.index];
      if (result.status !== 'fulfilled') { root.append(element('p', 'notice', `${descriptor.label}读取失败：${result.reason.message}`)); continue; }
      const data = result.value; const form = element('form', 'settings-form'); const label = element('label', '', descriptor.label); const select = element('select');
      for (const item of data.items || []) { const option = element('option', '', `${item.name || item.id}${item.available === false ? '（缺少凭据）' : ''}`); option.value = item.id; option.disabled = item.available === false; select.append(option); }
      select.value = data.current;
      const save = element('button', 'primary', `保存${descriptor.label}`); const state = element('p', 'muted', '下次语音会话生效；不重启设备或打断当前对话。');
      label.append(select); form.append(label, save, state); root.append(form);
      form.addEventListener('submit', async event => {
        event.preventDefault(); save.disabled = true;
        try { await api.request(descriptor.path, { method: 'PUT', body: { [descriptor.key]: select.value } }); if (valid()) state.textContent = '已保存；下次语音会话生效。'; }
        catch (error) { if (valid()) state.textContent = `保存失败：${error.message}`; }
        finally { save.disabled = false; }
      });
    }
    busy = false;
  };
  return { load, clear };
};
