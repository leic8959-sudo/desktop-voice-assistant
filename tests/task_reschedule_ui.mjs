// 真实页面、本机静态服务器与全量API替身；不会连接在用网关或第三方服务。
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile, mkdir} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const modulePath = process.env.PLAYWRIGHT_MODULE_PATH;
const playwright = await import(modulePath ? pathToFileURL(modulePath).href : pathToFileURL(createRequire(import.meta.url).resolve('playwright')).href);
const evidence = path.join(root, 'evidence', 'task-reschedule-ui-20261105');
await mkdir(evidence, {recursive:true});
const mime = {'.html':'text/html', '.js':'text/javascript', '.css':'text/css'};
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://127.0.0.1').pathname;
  const relative = pathname === '/assistant' ? 'assistant.html' : pathname.startsWith('/static/') ? pathname.slice(8) : '';
  const target = path.resolve(root, 'web', relative);
  if (!relative || !target.startsWith(path.join(root, 'web') + path.sep)) { response.writeHead(404).end(); return; }
  try { response.writeHead(200, {'Content-Type':mime[path.extname(target)] || 'application/octet-stream'}); response.end(await readFile(target)); }
  catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await playwright.chromium.launch({headless:true});
const outside = [];
const deferred = () => { let resolve; const promise = new Promise(done => { resolve = done; }); return {promise, resolve}; };
const task = (overrides = {}) => ({id:'future-one', title:'模拟未来提醒', due_at:'2035-11-05T03:20:00.000000Z', updated_at:'2035-11-01T00:00:00.000001Z', status:'pending', ...overrides});
const initialItems = () => [task(), task({id:'expired', title:'模拟已到期', due_at:'2020-01-01T00:00:00Z'}), task({id:'completed', title:'模拟已完成', status:'completed'}), task({id:'untimed', title:'模拟无时间', due_at:null})];

async function fixture(timezoneId = 'America/New_York', viewport = {width:1280, height:1000}, initialCapability = true) {
  const context = await browser.newContext({timezoneId, viewport});
  const page = await context.newPage();
  const state = {items:initialItems(), posts:[], taskGets:0, onGet:null, onPost:null, onStatus:null, taskCapability:initialCapability, failTaskGets:false, errors:[]};
  page.on('pageerror', error => state.errors.push(String(error)));
  await context.route('**/*', async route => {
    const request = route.request(); const url = new URL(request.url());
    if (url.origin !== origin) { outside.push(request.url()); await route.abort(); return; }
    if (!url.pathname.startsWith('/api/')) { await route.continue(); return; }
    let data = {}; let status = 200;
    if (url.pathname === '/api/status') { data = {bridge:{running:true}, device:{connected:false}, cloud:{connected:false}, capabilities:{tasks:true, ...(state.taskCapability === undefined ? {} : {task_reschedule:state.taskCapability})}}; if (state.onStatus) ({data, status = 200} = await state.onStatus(data)); }
    else if (url.pathname === '/api/tasks' && request.method() === 'GET') {
      state.taskGets += 1;
      data = state.onGet ? await state.onGet(state.taskGets) : {items:state.items};
      if (state.failTaskGets) { status = 503; data = {error:{message:'模拟列表刷新失败'}}; }
    } else if (url.pathname.endsWith('/reschedule')) {
      const body = request.postDataJSON(); state.posts.push({path:url.pathname, body});
      if (state.onPost) ({data, status = 200} = await state.onPost(body));
      else { state.items[0] = {...state.items[0], due_at:'2035-11-06T04:40:00.000000Z', updated_at:'2035-11-01T00:00:00.000002Z'}; data = {item:state.items[0]}; }
    } else if (url.pathname === '/api/voices') data = {current:'Tina', items:[{id:'Tina', name:'模拟音色'}]};
    else if (url.pathname === '/api/llm') data = {current:'qwen_chat', items:[{id:'qwen_chat', name:'模拟模型'}]};
    else if (url.pathname === '/api/home') data = {configured:false, items:[]};
    else if (url.pathname === '/api/memories') data = {items:[]};
    try { await route.fulfill({status, contentType:'application/json', body:JSON.stringify(data)}); } catch (error) { if (!page.isClosed() && !request.failure()) throw error; }
  });
  await page.goto(origin + '/assistant');
  await page.getByText('模拟未来提醒', {exact:true}).waitFor();
  return {page, state, context, close:async () => { assert.deepEqual(state.errors, []); await context.close(); }};
}

try {
  for (const timezone of ['America/New_York', 'Asia/Shanghai']) {
    const view = await fixture(timezone);
    try {
      const {page, state} = view;
      assert.equal(await page.getByRole('button', {name:'修改时间', exact:true}).count(), 1);
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      const input = page.getByLabel('新提醒时间（北京时间 UTC+08:00）');
      assert.equal(await input.inputValue(), '2035-11-05T11:20');
      await input.fill('2035-11-06T12:30');
      const holding = deferred(); const entered = deferred(); state.onPost = async body => { entered.resolve(); await holding.promise; state.items[0] = {...state.items[0], due_at:'2035-11-06T04:40:00.000000Z', updated_at:'2035-11-01T00:00:00.000002Z'}; return {data:{item:state.items[0]}}; };
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await entered.promise;
      await page.waitForFunction(() => document.querySelector('.task-reschedule button[type=submit]')?.disabled);
      await page.locator('.task-reschedule').evaluate(form => { form.dispatchEvent(new Event('submit', {cancelable:true})); form.dispatchEvent(new Event('submit', {cancelable:true})); });
      assert.equal(state.posts.length, 1);
      assert.deepEqual(state.posts[0].body, {due_at:'2035-11-06T12:30:00+08:00', expected_updated_at:'2035-11-01T00:00:00.000001Z'});
      holding.resolve();
      await page.waitForFunction(() => document.querySelector('#task-list').textContent.includes('12:40') && document.querySelector('#toast').textContent.includes('提醒时间已保存'));
      assert.match(await page.locator('#toast').textContent(), /12:40.*待办仍未完成/);
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await input.fill('2035-12-01T10:30');
      await page.locator('#task-panel').screenshot({path:path.join(evidence, `desktop-${timezone.replaceAll('/', '-')}.png`)});
      page.once('dialog', dialog => dialog.dismiss());
      await page.getByRole('button', {name:'刷新任务', exact:true}).click();
      assert.equal(await input.inputValue(), '2035-12-01T10:30');
      await page.locator('#toast').evaluate(node => { node.classList.remove('show'); node.textContent = ''; });
      await page.setViewportSize({width:390, height:844});
      await page.locator('#task-panel').screenshot({path:path.join(evidence, `mobile-${timezone.replaceAll('/', '-')}.png`)});
      const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
      assert.equal(overflow, false);
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page, state} = view;
      state.onPost = async () => { state.items[0] = {...state.items[0], title:'模拟其他端已改期', due_at:'2035-12-02T02:00:00Z', updated_at:'new-revision'}; return {status:409, data:{error:{message:'提醒状态已变化'}}}; };
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await page.getByText('模拟其他端已改期', {exact:true}).waitFor();
      assert.equal(state.posts.length, 1);
      assert.match(await page.locator('#toast').textContent(), /核对后重新修改/);
      assert.equal(await page.locator('.task-reschedule').count(), 0);
    } finally { await view.close(); }
  }

  for (const conflict of [false, true]) {
    const view = await fixture();
    try {
      const {page, state} = view;
      state.onPost = async () => { state.failTaskGets = true; return conflict ? {status:409, data:{error:{message:'提醒已变化'}}} : {data:{item:task({due_at:'2035-11-06T04:40:00Z'})}}; };
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await page.waitForFunction(() => document.querySelector('#toast').textContent.includes('列表刷新失败'));
      const message = await page.locator('#toast').textContent();
      assert.match(message, conflict ? /提醒已发生变化.*未重复提交/ : /提醒时间已保存.*12:40.*列表刷新失败/);
      if (conflict) assert.doesNotMatch(message, /已刷新/);
      assert.equal(state.posts.length, 1);
    } finally { await view.close(); }
  }

  for (const invalid of [{due_at:'invalid-time'}, {id:'other-task'}]) {
    const view = await fixture();
    try {
      const {page, state} = view;
      state.onPost = async () => ({data:{item:task(invalid)}});
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await page.locator('.task-reschedule').getByText(/保存结果待核实/).waitFor();
      assert.doesNotMatch(await page.locator('#toast').textContent(), /提醒时间已保存/);
      assert.equal(state.posts.length, 1);
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    const holding = deferred();
    try {
      const {page, state} = view;
      await page.clock.install();
      state.onPost = async () => { await holding.promise; return {data:{item:state.items[0]}}; };
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await page.clock.runFor(15001);
      await page.locator('.task-reschedule').getByText(/保存结果待核实/).waitFor();
      assert.equal(state.posts.length, 1);
      assert.equal(await page.getByRole('button', {name:'保存中…', exact:true}).isDisabled(), true);
      holding.resolve();
      await page.getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.getByRole('button', {name:'修改时间', exact:true}).waitFor({state:'visible'});
      assert.equal(state.posts.length, 1);
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      assert.equal(await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').inputValue(), '2035-11-05T11:20');
    } finally { holding.resolve(); await view.close(); }
  }

  {
    const view = await fixture();
    const holding = deferred(); const entered = deferred();
    try {
      const {page, state} = view;
      state.onGet = async () => { entered.resolve(); await holding.promise; return {items:[task({title:'旧身份GET不得回填'})]}; };
      await page.getByRole('button', {name:'刷新任务', exact:true}).click();
      await entered.promise;
      await page.getByRole('button', {name:'断开', exact:true}).click();
      assert.equal(await page.locator('#api-state').textContent(), '尚未认证');
      assert.equal(await page.getByRole('button', {name:'连接', exact:true}).isEnabled(), true);
      state.onGet = null; state.items = [task({title:'新身份列表'})];
      await page.getByRole('button', {name:'连接', exact:true}).click();
      await page.getByText('新身份列表', {exact:true}).waitFor();
      holding.resolve();
      await page.waitForLoadState('networkidle');
      assert.equal(await page.getByText('旧身份GET不得回填', {exact:true}).count(), 0);
      assert.equal(await page.getByText('新身份列表', {exact:true}).count(), 1);
    } finally { holding.resolve(); await view.close(); }
  }

  {
    const view = await fixture();
    const holding = deferred(); const entered = deferred();
    try {
      const {page, state} = view;
      state.onPost = async () => { entered.resolve(); await holding.promise; return {data:{item:task({due_at:'2035-12-09T00:00:00Z'})}}; };
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await entered.promise;
      await page.getByRole('button', {name:'断开', exact:true}).click();
      assert.equal(await page.locator('#api-state').textContent(), '尚未认证');
      assert.equal(await page.getByRole('button', {name:'连接', exact:true}).isEnabled(), true);
      state.items = [task({title:'重连后的任务'})];
      await page.getByRole('button', {name:'连接', exact:true}).click();
      await page.getByText('重连后的任务', {exact:true}).waitFor();
      holding.resolve();
      await page.waitForLoadState('networkidle');
      assert.equal(await page.getByText('重连后的任务', {exact:true}).count(), 1);
      assert.doesNotMatch(await page.locator('#toast').textContent(), /提醒时间已保存/);
      assert.equal(state.posts.length, 1);
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      assert.equal(await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').inputValue(), '2035-11-05T11:20');
    } finally { holding.resolve(); await view.close(); }
  }
  {
    const view = await fixture('America/New_York', {width:1280, height:1000}, undefined);
    try {
      const {page, state} = view;
      await page.clock.install();
      await page.getByRole('button', {name:'断开', exact:true}).click();
      await page.locator('#token').fill('offline-ui-capability-token');
      state.taskCapability = undefined;
      await page.getByRole('button', {name:'连接', exact:true}).click();
      await page.getByText('模拟未来提醒', {exact:true}).waitFor();
      assert.equal(await page.getByRole('button', {name:'修改时间', exact:true}).count(), 0);
      for (const unavailable of [false, 'true']) {
        state.taskCapability = unavailable;
        await page.clock.runFor(2501);
        assert.equal(await page.getByRole('button', {name:'修改时间', exact:true}).count(), 0);
      }
      state.taskCapability = true;
      await page.clock.runFor(2501);
      await page.getByRole('button', {name:'修改时间', exact:true}).waitFor();
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      const input = page.getByLabel('新提醒时间（北京时间 UTC+08:00）');
      await input.fill('2035-12-01T10:30');
      await page.clock.runFor(2501);
      assert.equal(await input.inputValue(), '2035-12-01T10:30');
      await page.locator('.task-reschedule').evaluate(form => { window.detachedRescheduleForm = form; });
      state.taskCapability = false;
      await page.clock.runFor(2501);
      await page.waitForFunction(() => !document.querySelector('.task-reschedule'));
      await page.evaluate(() => window.detachedRescheduleForm.dispatchEvent(new Event('submit', {cancelable:true})));
      assert.equal(state.posts.length, 0);
      assert.equal(await page.getByRole('button', {name:'修改时间', exact:true}).count(), 0);
    } finally { await view.close(); }
  }

  for (const oldFailure of [false, true]) {
    const view = await fixture();
    const entered = deferred(); const release = deferred();
    try {
      const {page, state} = view;
      await page.clock.install();
      await page.getByRole('button', {name:'断开', exact:true}).click();
      await page.locator('#token').fill('offline-ui-old-status-token');
      await page.getByRole('button', {name:'连接', exact:true}).click();
      await page.getByRole('button', {name:'修改时间', exact:true}).waitFor();
      state.onStatus = async data => { entered.resolve(); await release.promise; return oldFailure ? {status:401, data:{error:{message:'旧身份失效'}}} : {data}; };
      await page.clock.runFor(2501);
      await entered.promise;
      await page.getByRole('button', {name:'断开', exact:true}).click();
      state.onStatus = null; state.taskCapability = undefined; state.items = [task({title:'旧status不得覆盖的新身份'})];
      await page.locator('#token').fill('offline-ui-new-status-token');
      await page.getByRole('button', {name:'连接', exact:true}).click();
      await page.getByText('旧status不得覆盖的新身份', {exact:true}).waitFor();
      release.resolve();
      await page.waitForLoadState('networkidle');
      assert.equal(await page.getByRole('button', {name:'修改时间', exact:true}).count(), 0);
      assert.equal(await page.getByText('旧status不得覆盖的新身份', {exact:true}).count(), 1);
      assert.equal(await page.getByRole('button', {name:'断开', exact:true}).isEnabled(), true);
    } finally { release.resolve(); await view.close(); }
  }
  assert.deepEqual(outside, []);
  console.log('Task reschedule UI passed: real page, Shanghai/NY timezone, native events, server receipt, duplicate guard, conflict, timeout, old GET/POST auth isolation, desktop/mobile screenshots; outside requests=0.');
} finally {
  await browser.close();
  await new Promise(resolve => server.close(resolve));
}
