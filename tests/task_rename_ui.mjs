// 真实页面和本机静态服务器，全部API为受控替身；不连接在用网关。
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile, mkdir} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const modulePath = process.env.PLAYWRIGHT_MODULE_PATH;
const playwright = await import(modulePath ? pathToFileURL(modulePath).href : pathToFileURL(createRequire(import.meta.url).resolve('playwright')).href);
const evidence = path.join(root, 'evidence', 'task-rename-ui-20261105');
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
const task = (overrides = {}) => ({id:'future',title:'模拟未来提醒',status:'pending',due_at:'2035-11-05T03:20:00.000000Z',reminder_state:'pending',updated_at:'2035-11-01T00:00:00.000001Z',...overrides});
const initialItems = () => [task(),task({id:'untimed',title:'模拟无时间',due_at:null}),task({id:'due',title:'模拟已到期',due_at:'2020-01-01T00:00:00Z'}),task({id:'done',title:'模拟已完成',status:'completed'}),task({id:'cancelled',title:'模拟已取消',status:'cancelled'}),task({id:'leased',title:'模拟投递中',reminder_state:'leased'}),task({id:'invalid',title:'模拟无效日期',due_at:'invalid'})];
const panel = page => page.locator('#task-panel');
const row = (page, title) => page.locator('.task-item').filter({has:page.getByText(title,{exact:true})});
const input = page => page.getByLabel('新待办标题',{exact:true});
const save = page => panel(page).getByRole('button',{name:'保存标题',exact:true});
const openRename = (page, title = '模拟无时间') => row(page,title).getByRole('button',{name:'修改标题',exact:true}).click();
const poll = page => page.clock.runFor(2501);
const unknown = page => page.waitForFunction(() => document.querySelector('#toast').textContent.includes('保存结果待核实'));
const success = page => page.waitForFunction(() => document.querySelector('#toast').textContent.includes('待办标题已保存') && !document.querySelector('.task-reschedule'));

async function fixture(timezoneId = 'America/New_York') {
  const context = await browser.newContext({timezoneId,viewport:{width:1280,height:1000}});
  const page = await context.newPage();
  const state = {items:initialItems(),renameCapability:true,addCapability:true,timeCapability:true,posts:[],queries:[],errors:[],onPost:null,onGet:null};
  page.on('pageerror', error => state.errors.push(String(error)));
  await context.route('**/*', async route => {
    const request = route.request(); const url = new URL(request.url());
    if (url.origin !== origin) { outside.push(request.url()); await route.abort(); return; }
    if (!url.pathname.startsWith('/api/')) { await route.continue(); return; }
    let data = {}; let status = 200;
    if (url.pathname === '/api/status') data = {bridge:{running:true},device:{connected:false},cloud:{connected:false},capabilities:{tasks:true,task_date_filter:true,task_add_reminder:state.addCapability,task_reschedule:state.timeCapability,...(state.renameCapability === undefined ? {} : {task_rename:state.renameCapability})}};
    else if (url.pathname === '/api/tasks' && request.method() === 'GET') {
      state.queries.push(Object.fromEntries(url.searchParams));
      data = state.onGet ? await state.onGet(url.searchParams) : {items:state.items};
    } else if (/\/api\/tasks\/[^/]+\/(rename|reschedule)$/.test(url.pathname)) {
      const body = request.postDataJSON(); state.posts.push({path:url.pathname,body});
      if (state.onPost) ({data,status = 200} = await state.onPost(body,url.pathname));
      else {
        const item = state.items.find(item => item.id === decodeURIComponent(url.pathname.split('/')[3]));
        Object.assign(item, body.title === undefined ? {due_at:body.due_at} : {title:body.title}, {updated_at:`2035-11-01T00:00:00.${String(state.posts.length + 1).padStart(6,'0')}Z`});
        data = {item};
      }
    } else if (url.pathname === '/api/voices') data = {current:'Tina',items:[{id:'Tina',name:'模拟音色'}]};
    else if (url.pathname === '/api/llm') data = {current:'qwen_chat',items:[{id:'qwen_chat',name:'模拟模型'}]};
    else if (url.pathname === '/api/home') data = {configured:false,items:[]};
    else if (url.pathname === '/api/memories') data = {items:[]};
    try { await route.fulfill({status,contentType:'application/json',body:JSON.stringify(data)}); }
    catch (error) { if (!page.isClosed() && !request.failure()) throw error; }
  });
  await page.clock.install(); await page.goto(origin + '/assistant');
  await page.getByText('模拟无时间',{exact:true}).waitFor();
  return {page,state,close:async () => { assert.deepEqual(state.errors,[]); await context.close(); }};
}

async function connectToken(page) {
  await page.getByRole('button',{name:'断开',exact:true}).click();
  await page.locator('#token').fill('offline-rename-ui-token');
  await page.getByRole('button',{name:'连接',exact:true}).click();
  await page.getByText('模拟无时间',{exact:true}).waitFor();
}

try {
  for (const timezone of ['America/New_York','Asia/Shanghai']) {
    const view = await fixture(timezone);
    try {
      const {page,state} = view;
      assert.equal(await panel(page).getByRole('button',{name:'修改标题',exact:true}).count(),2);
      for (const title of ['模拟已到期','模拟已完成','模拟已取消','模拟投递中','模拟无效日期']) assert.equal(await row(page,title).getByRole('button',{name:'修改标题',exact:true}).count(),0);
      await openRename(page); assert.equal(await input(page).inputValue(),'模拟无时间');
      await input(page).fill('   '); await save(page).click();
      await page.getByText('请输入1～200字的待办标题。',{exact:true}).waitFor(); assert.equal(state.posts.length,0);
      await input(page).fill('中'.repeat(201));
      await page.locator('.task-reschedule').evaluate(form => form.dispatchEvent(new Event('submit',{cancelable:true})));
      assert.equal(state.posts.length,0);
      // 手工submit可绕过原生maxlength，仍必须按codepoint拒绝201个emoji。
      await input(page).evaluate(node => { node.value = '🐦'.repeat(201); });
      await page.locator('.task-reschedule').evaluate(form => form.dispatchEvent(new Event('submit',{cancelable:true})));
      assert.equal(state.posts.length,0);
      await input(page).fill('中'.repeat(200));
      await panel(page).screenshot({path:path.join(evidence,`desktop-${timezone.replaceAll('/','-')}.png`)});
      await page.setViewportSize({width:390,height:844});
      await panel(page).screenshot({path:path.join(evidence,`mobile-${timezone.replaceAll('/','-')}.png`)});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth),false);
      await save(page).click(); await success(page);
      assert.deepEqual(state.posts,[{path:'/api/tasks/untimed/rename',body:{title:'中'.repeat(200),expected_updated_at:'2035-11-01T00:00:00.000001Z'}}]);
      assert.equal(state.items.find(item => item.id === 'untimed').due_at,null);
      assert.equal(Date.parse('2035-11-01T00:00:00.000002Z'),Date.parse('2035-11-01T00:00:00.000001Z'));
      assert.match(await page.locator('#toast').textContent(),/提醒时间不变/);
      await page.getByText('中'.repeat(200),{exact:true}).waitFor();
      await openRename(page,'中'.repeat(200)); await input(page).fill('🐦'.repeat(200));
      await save(page).click(); await success(page);
      await page.getByText('🐦'.repeat(200),{exact:true}).waitFor();
      assert.equal(state.posts.length,2);
      assert.equal(state.posts[1].body.title,'🐦'.repeat(200));
      assert.equal(state.posts[1].body.expected_updated_at,'2035-11-01T00:00:00.000002Z');
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page,state} = view; await connectToken(page);
      for (const flag of [undefined,false,'true']) {
        state.renameCapability = flag; await poll(page);
        await page.waitForFunction(() => [...document.querySelectorAll('[data-task-rename]')].every(button => button.hidden));
        assert.equal(await panel(page).getByRole('button',{name:'修改标题',exact:true}).count(),0);
        assert.equal(await row(page,'模拟无时间').getByRole('button',{name:'添加提醒',exact:true}).count(),1);
      }
      await row(page,'模拟未来提醒').getByRole('button',{name:'修改时间',exact:true}).click();
      const time = page.getByLabel('新提醒时间（北京时间 UTC+08:00）'); await time.fill('2035-12-01T12:00');
      state.renameCapability = true; await poll(page); state.renameCapability = false; await poll(page);
      assert.equal(await time.inputValue(),'2035-12-01T12:00');
      await panel(page).getByRole('button',{name:'取消修改',exact:true}).click();
      state.renameCapability = true; await poll(page); await openRename(page); await input(page).fill('未提交标题');
      await page.locator('.task-reschedule').evaluate(form => { window.detachedRenameForm = form; });
      state.renameCapability = false; await poll(page);
      await page.waitForFunction(() => !document.querySelector('.task-reschedule'));
      await page.evaluate(() => window.detachedRenameForm.dispatchEvent(new Event('submit',{cancelable:true})));
      assert.equal(state.posts.length,0);
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page,state} = view; await openRename(page); await input(page).fill('草稿标题');
      page.once('dialog', dialog => dialog.dismiss());
      await row(page,'模拟未来提醒').getByRole('button',{name:'修改时间',exact:true}).click();
      assert.equal(await input(page).inputValue(),'草稿标题');
      page.once('dialog', dialog => dialog.accept());
      await row(page,'模拟无时间').getByRole('button',{name:'添加提醒',exact:true}).click();
      assert.equal(await page.locator('.task-reschedule').count(),1);
      assert.equal(await input(page).count(),0);
      assert.equal(await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').inputValue(),'');
      assert.equal(state.posts.length,0);
    } finally { await view.close(); }
  }

  {
    const view = await fixture(); const entered = deferred(); const release = deferred();
    try {
      const {page,state} = view; await openRename(page); await input(page).fill('  新标题  ');
      state.onPost = async body => { entered.resolve(); await release.promise; Object.assign(state.items[1],{title:body.title,updated_at:'2035-11-01T00:00:00.000002Z'}); return {data:{item:state.items[1]}}; };
      await save(page).click(); await entered.promise;
      await page.locator('.task-reschedule').evaluate(form => { form.dispatchEvent(new Event('submit',{cancelable:true})); form.dispatchEvent(new Event('submit',{cancelable:true})); });
      assert.equal(state.posts.length,1); assert.equal(state.posts[0].body.title,'新标题');
      assert.equal(await panel(page).getByRole('button',{name:'下一页',exact:true}).isDisabled(),true);
      assert.equal(await page.getByLabel('提醒日期（北京时间）',{exact:true}).isDisabled(),true);
      release.resolve(); await success(page); assert.equal(state.posts.length,1);
    } finally { release.resolve(); await view.close(); }
  }

  for (const [name,alter,status = 200] of [
    ['id', item => ({...item,id:'wrong-id'})],['title', item => ({...item,title:'错标题'})],
    ['due', item => ({...item,due_at:'2035-12-01T00:00:00Z'})],['state', item => ({...item,reminder_state:'acked'})],
    ['version', item => ({...item,updated_at:'2035-11-01T00:00:00.000001Z'})],['bad-version', item => ({...item,updated_at:'bad'})],
    ['status', item => ({...item,status:'completed'})],
    ['5xx', item => item,503],
  ]) {
    const view = await fixture();
    try {
      const {page,state} = view; state.onPost = async body => ({status,data:{item:alter({...state.items[1],title:body.title,updated_at:'2035-11-01T00:00:00.000002Z'})}});
      await openRename(page); await input(page).fill('目标新标题'); await save(page).click(); await unknown(page);
      assert.equal(await row(page,'模拟无时间').getByRole('button',{name:'修改标题',exact:true}).isDisabled(),true,name);
      assert.equal(state.posts.length,1); assert.doesNotMatch(await page.locator('#toast').textContent(),/标题已保存/);
      await panel(page).getByRole('button',{name:'刷新任务',exact:true}).click();
      await page.waitForFunction(() => !document.querySelector('[data-task-rename]').disabled);
      assert.equal(state.posts.length,1);
    } finally { await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page,state} = view; state.onPost = async () => { Object.assign(state.items[1],{title:'其他端新标题',updated_at:'2035-11-01T00:00:00.000009Z'}); return {status:409,data:{error:{message:'状态已变化'}}}; };
      await openRename(page); await input(page).fill('冲突标题'); await save(page).click();
      await page.waitForFunction(() => document.querySelector('#toast').textContent.includes('待办已发生变化'));
      await page.getByText('其他端新标题',{exact:true}).waitFor(); assert.equal(state.posts.length,1);
      await openRename(page,'其他端新标题');
      const entered = deferred(); const release = deferred();
      state.onPost = async body => { entered.resolve(); await release.promise; return {data:{item:{...state.items[1],title:body.title,updated_at:'2035-11-01T00:00:00.000010Z'}}}; };
      await input(page).fill('新版本再提交'); await save(page).click(); await entered.promise;
      assert.equal(state.posts[1].body.expected_updated_at,'2035-11-01T00:00:00.000009Z');
      release.resolve(); await success(page);
    } finally { await view.close(); }
  }

  {
    const view = await fixture(); const entered = deferred(); const release = deferred();
    try {
      const {page,state} = view; await connectToken(page); await openRename(page); await input(page).fill('超时标题');
      state.onPost = async body => { entered.resolve(); await release.promise; return {data:{item:{...state.items[1],title:body.title,updated_at:'2035-11-01T00:00:00.000002Z'}}}; };
      await save(page).click(); await entered.promise; await page.clock.runFor(15001); await unknown(page);
      assert.equal(await row(page,'模拟无时间').getByRole('button',{name:'修改标题',exact:true}).isDisabled(),true);
      state.renameCapability = false; await poll(page); state.renameCapability = true; await poll(page);
      assert.equal(await row(page,'模拟无时间').getByRole('button',{name:'修改标题',exact:true}).isDisabled(),true);
      assert.equal(state.posts.length,1); release.resolve();
      await panel(page).getByRole('button',{name:'刷新任务',exact:true}).click();
      await page.waitForFunction(() => !document.querySelector('[data-task-rename]').disabled);
      assert.equal(state.posts.length,1);
    } finally { release.resolve(); await view.close(); }
  }

  {
    const view = await fixture(); const entered = deferred(); const release = deferred();
    try {
      const {page,state} = view; await openRename(page); await input(page).fill('旧身份标题');
      const oldItem = {...state.items[1]}; state.onPost = async body => { entered.resolve(); await release.promise; return {data:{item:{...oldItem,title:body.title,updated_at:'2035-11-01T00:00:00.000002Z'}}}; };
      await save(page).click(); await entered.promise;
      await page.getByRole('button',{name:'断开',exact:true}).click();
      state.items = initialItems().map(item => item.id === 'untimed' ? {...item,title:'新身份标题'} : item);
      await page.getByRole('button',{name:'连接',exact:true}).click(); await page.getByText('新身份标题',{exact:true}).waitFor();
      const reads = state.queries.length; release.resolve(); await page.waitForLoadState('networkidle');
      assert.equal(state.queries.length,reads); assert.equal(await page.getByText('旧身份标题',{exact:true}).count(),0);
      assert.doesNotMatch(await page.locator('#toast').textContent(),/待办标题已保存/);
      await openRename(page,'新身份标题'); assert.equal(await input(page).inputValue(),'新身份标题');
    } finally { release.resolve(); await view.close(); }
  }

  {
    const view = await fixture();
    try {
      const {page,state} = view;
      const second = task({id:'page2',title:'日期第二页待办',updated_at:'2035-11-01T00:00:00.000031Z'});
      state.onGet = async params => params.get('cursor') ? {items:[second]} : {items:[task({id:'page1',title:'日期首页待办'})],next_cursor:'rename-page2'};
      await page.getByLabel('提醒日期（北京时间）',{exact:true}).fill('2035-11-05');
      await panel(page).getByRole('button',{name:'应用日期',exact:true}).click(); await page.getByText('日期首页待办',{exact:true}).waitFor();
      await panel(page).getByRole('button',{name:'下一页',exact:true}).click(); await page.getByText('日期第二页待办',{exact:true}).waitFor();
      await openRename(page,'日期第二页待办'); await input(page).fill('日期二页新标题');
      page.once('dialog',dialog => dialog.dismiss()); await panel(page).getByRole('button',{name:'返回首页',exact:true}).click();
      assert.equal(await input(page).inputValue(),'日期二页新标题');
      state.onPost = async body => { Object.assign(second,{title:body.title,updated_at:'2035-11-01T00:00:00.000032Z'}); return {data:{item:second}}; };
      await save(page).click(); await success(page);
      assert.equal(state.posts[0].path,'/api/tasks/page2/rename'); assert.equal(state.posts[0].body.expected_updated_at,'2035-11-01T00:00:00.000031Z');
      assert.equal(second.due_at,'2035-11-05T03:20:00.000000Z');
      assert.equal(state.queries.at(-1).due_from,'2035-11-05T00:00:00+08:00'); assert.equal(state.queries.at(-1).due_before,'2035-11-06T00:00:00+08:00');
      assert.equal(state.queries.at(-1).cursor,undefined); assert.match(await page.locator('#task-page-state').textContent(),/第 1 页/);
      assert.equal(await page.getByLabel('提醒日期（北京时间）',{exact:true}).inputValue(),'2035-11-05');
    } finally { await view.close(); }
  }
  assert.deepEqual(outside,[]);
  console.log('Task rename UI passed: strict capability and independent modes, title bounds, microsecond revision, complete receipt/unknown, one write, CAS refresh, timeout, auth handoff, page2/date preservation, desktop/mobile; outside=0.');
} finally { await browser.close(); await new Promise(resolve => server.close(resolve)); }
