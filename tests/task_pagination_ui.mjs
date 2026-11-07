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
const evidence = path.join(root, 'evidence', 'task-pagination-ui-20261105');
await mkdir(evidence, {recursive:true});
const mime = {'.html':'text/html', '.js':'text/javascript', '.css':'text/css'};
let completeResponseHook = null;
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://127.0.0.1').pathname;
  if (pathname === '/api/tasks/second-31/complete' && completeResponseHook) { await completeResponseHook(request, response); return; }
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
const initialItems = () => Array.from({length:30}, (_, index) => task({id:`first-${index+1}`, title:`首页任务${String(index+1).padStart(2,'0')}`}));
const secondItem = () => task({id:'second-31', title:'第二页任务31', updated_at:'2035-11-01T00:00:00.000031Z'});

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
      data = state.onGet ? await state.onGet(state.taskGets, url.searchParams.get('cursor') || '') : {items:url.searchParams.get('cursor') ? [secondItem()] : state.items, next_cursor:url.searchParams.get('cursor') ? null : 'cursor/30'};
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
  await page.getByText('首页任务01', {exact:true}).waitFor();
  return {page, state, context, close:async () => { assert.deepEqual(state.errors, []); await context.close(); }};
}

async function next(page) {
  await page.locator('#task-panel').getByRole('button', {name:'下一页', exact:true}).click();
  await page.getByText('第二页任务31', {exact:true}).waitFor();
}

try {
  {
    const view = await fixture();
    try {
      const {page, state} = view;
      assert.equal(await page.locator('#task-list .task-item').count(), 30);
      assert.match(await page.locator('#hero-result').textContent(), /本页有 30 件/);
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).isDisabled(), true);
      await next(page);
      assert.equal(await page.locator('#task-list .task-item').count(), 1);
      assert.equal(await page.getByText('首页任务01', {exact:true}).count(), 0);
      assert.match(await page.locator('#task-page-state').textContent(), /第 2 页.*本页 1 条/);
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'下一页', exact:true}).isDisabled(), true);
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      const input = page.getByLabel('新提醒时间（北京时间 UTC+08:00）');
      await input.fill('2035-12-01T10:30');
      page.once('dialog', dialog => dialog.dismiss());
      await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).click();
      assert.equal(await input.inputValue(), '2035-12-01T10:30');
      await page.locator('#task-panel').screenshot({path:path.join(evidence, 'desktop-page2-editor.png')});
      await page.setViewportSize({width:390, height:844});
      await page.locator('#task-panel').screenshot({path:path.join(evidence, 'mobile-page2-editor.png')});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
      state.onPost = async body => ({data:{item:{...secondItem(), due_at:'2035-12-01T02:35:00Z', updated_at:'new-second-revision'}}});
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await page.getByText('首页任务01', {exact:true}).waitFor();
      await page.waitForFunction(() => document.querySelector('#task-page-state').textContent.includes('第 1 页'));
      assert.equal(state.posts[0].path, '/api/tasks/second-31/reschedule');
      assert.deepEqual(state.posts[0].body, {due_at:'2035-12-01T10:30:00+08:00', expected_updated_at:'2035-11-01T00:00:00.000031Z'});
      assert.match(await page.locator('#toast').textContent(), /10:35/);
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page, state} = view;
      state.failTaskGets = true;
      await page.locator('#task-panel').getByRole('button', {name:'下一页', exact:true}).click();
      await page.locator('#task-page-state').getByText(/读取失败/).waitFor();
      assert.equal(await page.locator('#task-list .task-item').count(), 30);
      assert.equal(await page.getByText('首页任务01', {exact:true}).count(), 1);
      state.failTaskGets = false;
      await next(page);
      state.failTaskGets = true;
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.locator('#task-page-state').getByText(/读取失败/).waitFor();
      assert.equal(await page.getByText('第二页任务31', {exact:true}).count(), 1);
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'下一页', exact:true}).isDisabled(), true);
      state.failTaskGets = false;
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.waitForFunction(() => document.querySelector('#task-page-state').textContent.includes('第 2 页'));
      assert.equal(await page.getByText('第二页任务31', {exact:true}).count(), 1);
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    const entered = deferred(); const release = deferred();
    try {
      const {page, state} = view;
      await next(page);
      state.onGet = async (_count, cursor) => {
        if (cursor) { entered.resolve(); await release.promise; return {items:[task({title:'旧第二页不得覆盖'})], next_cursor:null}; }
        return {items:state.items, next_cursor:'cursor/30'};
      };
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await entered.promise;
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).isDisabled(), true);
      await page.getByRole('button', {name:'完成', exact:true}).click();
      await page.getByText('首页任务01', {exact:true}).waitFor();
      release.resolve();
      await page.waitForLoadState('networkidle');
      assert.equal(await page.getByText('旧第二页不得覆盖', {exact:true}).count(), 0);
      assert.match(await page.locator('#task-page-state').textContent(), /第 1 页/);
    } finally { release.resolve(); await view.close(); }
  }

  for (const action of ['取消', '创建']) {
    const view = await fixture();
    try {
      const {page} = view;
      await next(page);
      if (action === '创建') {
        await page.locator('#task-title').fill('分页模拟新建任务');
        await page.getByRole('button', {name:'添加提醒', exact:true}).click();
      } else await page.getByRole('button', {name:'取消', exact:true}).click();
      await page.getByText('首页任务01', {exact:true}).waitFor();
      await page.waitForFunction(() => document.querySelector('#task-page-state').textContent.includes('第 1 页') && !document.querySelector('#task-next').disabled);
      assert.match(await page.locator('#toast').textContent(), /已返回首页/);
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    const entered = deferred(); const release = deferred();
    try {
      const {page, state} = view;
      await next(page);
      const originalRoute = async route => {
        entered.resolve(); await release.promise;
        await route.fulfill({status:200, contentType:'application/json', body:JSON.stringify({item:{...secondItem(), status:'completed'}})});
      };
      await page.route(origin + '/api/tasks/second-31/complete', originalRoute);
      await page.getByRole('button', {name:'完成', exact:true}).click();
      await entered.promise;
      await page.getByRole('button', {name:'断开', exact:true}).click();
      state.items = [task({title:'新身份当前首页'})];
      await page.getByRole('button', {name:'连接', exact:true}).click();
      await page.getByText('新身份当前首页', {exact:true}).waitFor();
      const getsBefore = state.taskGets;
      release.resolve();
      await page.waitForLoadState('networkidle');
      assert.equal(state.taskGets, getsBefore);
      assert.equal(await page.getByText('新身份当前首页', {exact:true}).count(), 1);
      assert.doesNotMatch(await page.locator('#toast').textContent(), /已完成/);
    } finally { release.resolve(); await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page, state} = view;
      state.onGet = async (_count, cursor) => ({items:cursor ? [] : state.items, next_cursor:cursor ? null : 'old-phantom-cursor'});
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.waitForFunction(() => !document.querySelector('#task-next').disabled);
      await page.locator('#task-panel').getByRole('button', {name:'下一页', exact:true}).click();
      await page.getByText('本页暂无任务，可返回首页或刷新查看。', {exact:true}).waitFor();
      assert.match(await page.locator('#hero-result').textContent(), /不代表全部任务为空/);
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'下一页', exact:true}).isDisabled(), true);
      await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).click();
      await page.getByText('首页任务01', {exact:true}).waitFor();
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page, state} = view;
      await next(page);
      state.onPost = async () => ({status:503, data:{error:{message:'模拟结果未知'}}});
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await page.locator('.task-reschedule').getByText(/保存结果待核实/).waitFor();
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).isDisabled(), true);
      assert.equal(state.posts.length, 1);
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.waitForFunction(() => document.querySelector('#task-page-state').textContent.includes('第 2 页') && !document.querySelector('#task-first').disabled);
      assert.equal(await page.getByText('第二页任务31', {exact:true}).count(), 1);
      assert.equal(state.posts.length, 1);
    } finally { await view.close(); }
  }
  {
    const view = await fixture();
    const entered = deferred(); const release = deferred();
    try {
      const {page, state} = view;
      await next(page);
      state.onGet = async (_count, cursor) => { entered.resolve(); await release.promise; return {items:cursor ? [secondItem()] : state.items, next_cursor:null}; };
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await entered.promise;
      await page.getByRole('button', {name:'修改时间', exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      state.onPost = async () => ({status:503, data:{error:{message:'模拟改期结果未知'}}});
      await page.getByRole('button', {name:'保存时间', exact:true}).click();
      await page.locator('.task-reschedule').getByText(/保存结果待核实/).waitFor();
      release.resolve();
      await page.waitForLoadState('networkidle');
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).isDisabled(), true);
      assert.equal(await page.getByRole('button', {name:'保存中…', exact:true}).isDisabled(), true);
      assert.equal(state.posts.length, 1);
      state.onGet = null;
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.waitForFunction(() => !document.querySelector('#task-first').disabled);
      assert.match(await page.locator('#task-page-state').textContent(), /第 2 页/);
      assert.equal(state.posts.length, 1);
    } finally { release.resolve(); await view.close(); }
  }
  {
    const view = await fixture();
    const entered = deferred(); const release = deferred();
    let writes = 0;
    try {
      const {page} = view;
      await page.clock.install();
      await next(page);
      await page.route(origin + '/api/tasks/second-31/complete', async route => {
        writes += 1; entered.resolve(); await release.promise;
        try { await route.fulfill({status:200, contentType:'application/json', body:'{}'}); } catch (error) { if (!route.request().failure()) throw error; }
      });
      await page.getByRole('button', {name:'完成', exact:true}).click();
      await entered.promise;
      await page.clock.runFor(15001);
      await page.waitForFunction(() => document.querySelector('#toast').textContent.includes('任务更新结果待核实'));
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).isDisabled(), true);
      assert.equal(await page.getByRole('button', {name:'完成', exact:true}).isDisabled(), true);
      assert.equal(writes, 1);
      release.resolve();
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.waitForFunction(() => !document.querySelector('#task-first').disabled);
      assert.match(await page.locator('#task-page-state').textContent(), /第 2 页/);
      assert.equal(writes, 1);
    } finally { release.resolve(); await view.close(); }
  }
  {
    const view = await fixture();
    const release = deferred(); const headersSent = deferred();
    let writes = 0;
    try {
      const {page} = view;
      await page.clock.install();
      await next(page);
      completeResponseHook = async (request, response) => {
        writes += 1; request.resume();
        response.writeHead(200, {'Content-Type':'application/json'});
        response.flushHeaders(); response.write('{"item":'); headersSent.resolve();
        await release.promise;
        response.end('{"status":"completed"}}');
      };
      await page.route(origin + '/api/tasks/second-31/complete', route => route.continue());
      const headers = page.waitForResponse(response => response.url() === origin + '/api/tasks/second-31/complete');
      await page.getByRole('button', {name:'完成', exact:true}).click();
      await headersSent.promise;
      assert.equal((await headers).status(), 200);
      await page.clock.runFor(15001);
      await page.waitForFunction(() => document.querySelector('#toast').textContent.includes('任务更新结果待核实'));
      assert.doesNotMatch(await page.locator('#toast').textContent(), /已完成/);
      assert.equal(writes, 1);
      assert.equal(await page.locator('#task-panel').getByRole('button', {name:'返回首页', exact:true}).isDisabled(), true);
      release.resolve();
      await page.locator('#task-panel').getByRole('button', {name:'刷新任务', exact:true}).click();
      await page.waitForFunction(() => !document.querySelector('#task-first').disabled);
      assert.match(await page.locator('#task-page-state').textContent(), /第 2 页/);
      assert.equal(writes, 1);
    } finally { release.resolve(); completeResponseHook = null; await view.close(); }
  }
  assert.deepEqual(outside, []);
  console.log('Task pagination UI passed: bounded replacement, page failure/retry, draft, page2 reschedule and complete home, old GET/write identity isolation, legacy empty cursor, unknown lock, desktop/mobile; outside=0.');
} finally {
  await browser.close();
  await new Promise(resolve => server.close(resolve));
}

