// 控制台页面共用同源认证和超时；令牌只从当前输入框读取，不持久化。
export const createConsoleApi = (getToken, onUnauthorized) => {
  const controllers = new Set();
  const cancel = () => controllers.forEach(controller => controller.abort());
  const request = async (path, { method = 'GET', body, multipart, timeout = 12000, authenticate = true } = {}) => {
    if (multipart !== undefined && (!(multipart instanceof FormData) || body !== undefined)) throw new Error('上传必须提供 FormData，且不能同时提供 JSON。');
    const controller = new AbortController();
    controllers.add(controller);
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; controller.abort(); }, timeout);
    const headers = {};
    const token = authenticate ? getToken().trim() : '';
    if (token) headers.Authorization = `Bearer ${token}`;
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    try {
      const response = await fetch(path, { method, headers, credentials: 'same-origin', cache: 'no-store', signal: controller.signal, body: multipart === undefined ? body === undefined ? undefined : JSON.stringify(body) : multipart });
      if (controller.signal.aborted) throw new DOMException('请求已取消', 'AbortError');
      if (response.status === 401 && authenticate) {
        onUnauthorized();
        throw new Error('管理员认证失效，请重新登录或输入管理员令牌。');
      }
      const text = await response.text();
      if (controller.signal.aborted) throw new DOMException('请求已取消', 'AbortError');
      let data;
      try { data = text ? JSON.parse(text) : {}; } catch { data = null; }
      if (!response.ok) throw new Error(data?.error?.message || data?.message || (path === '/api/auth/local-login' ? `本机登录失败（${response.status}），请在服务所在电脑打开 http://localhost:8765。` : `请求失败（HTTP ${response.status}）`));
      if (data === null) throw new Error('接口未返回 JSON，请检查服务地址。');
      return data;
    } catch (error) {
      if (error.name === 'AbortError') throw new Error(timedOut ? '请求超时，请重试；未确认外部调用结果。' : '请求已取消。');
      throw error;
    } finally {
      clearTimeout(timer);
      controllers.delete(controller);
    }
  };
  return { request, cancel };
};

export const element = (tag, className, text) => {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
};
