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
const evidence = path.join(root, 'evidence', 'task-date-filter-ui-20261105');
await mkdir(evidence, {recursive:true});
const mime = {'.html':'text/html', '.js':'text/javascript', '.css':'text/css'};
let dateReadHook = null;
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://127.0.0.1').pathname;
  if (pathname === '/api/tasks' && dateReadHook) { await dateReadHook(request, response); return; }
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
const dateItems = (params, all) => { const date = (params.get('due_from') || '').slice(0,10); return !date ? {items:all, next_cursor:null} : params.get('cursor') ? {items:[task({id:'date-31',title:`${date} 日期第二页`,updated_at:'date-page2-revision'})],next_cursor:null} : {items:Array.from({length:30},(_,index)=>task({id:`date-${index}`,title:`${date} 日期任务${String(index+1).padStart(2,'0')}`})),next_cursor:'date/cursor30'}; };
const initialItems = () => Array.from({length:30}, (_, index) => task({id:`first-${index+1}`, title:`首页任务${String(index+1).padStart(2,'0')}`}));
const secondItem = () => task({id:'second-31', title:'第二页任务31', updated_at:'2035-11-01T00:00:00.000031Z'});

async function fixture(timezoneId = 'America/New_York', viewport = {width:1280, height:1000}, initialCapability = true) {
  const context = await browser.newContext({timezoneId, viewport});
  const page = await context.newPage();
  const state = {items:initialItems(), posts:[], taskGets:0, onGet:null, onPost:null, onStatus:null, dateCapability:true, taskCapability:initialCapability, failTaskGets:false, queries:[], errors:[]};
  page.on('pageerror', error => state.errors.push(String(error)));
  await context.route('**/*', async route => {
    const request = route.request(); const url = new URL(request.url());
    if (url.origin !== origin) { outside.push(request.url()); await route.abort(); return; }
    if (!url.pathname.startsWith('/api/')) { await route.continue(); return; }
    let data = {}; let status = 200;
    if (url.pathname === '/api/status') { data = {bridge:{running:true}, device:{connected:false}, cloud:{connected:false}, capabilities:{tasks:true, ...(state.dateCapability === undefined ? {} : {task_date_filter:state.dateCapability}), ...(state.taskCapability === undefined ? {} : {task_reschedule:state.taskCapability})}}; if (state.onStatus) ({data, status = 200} = await state.onStatus(data)); }
    else if (url.pathname === '/api/tasks' && request.method() === 'GET') {
      state.taskGets += 1; state.queries.push(Object.fromEntries(url.searchParams));
      data = state.onGet ? await state.onGet(state.taskGets, url.searchParams.get('cursor') || '', Object.fromEntries(url.searchParams)) : dateItems(url.searchParams, state.items);
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
  await page.clock.install(); await page.goto(origin + '/assistant');
  await page.getByText('首页任务01', {exact:true}).waitFor();
  return {page, state, context, close:async () => { assert.deepEqual(state.errors, []); await context.close(); }};
}

const panel = page => page.locator('#task-panel');
async function applyDate(page, date) {
  await page.getByLabel('提醒日期（北京时间）', {exact:true}).fill(date);
  await panel(page).getByRole('button', {name:'应用日期', exact:true}).click();
}
async function waitDate(page, date) {
  await page.waitForFunction(value => document.querySelector('#task-date-state').textContent.includes(`当前提醒日期：${value}`) && !document.querySelector('#task-date-apply').disabled, date);
}
async function pollStatus(page) { await page.clock.runFor(2501); }
async function connectToken(page) {
  await page.getByRole('button', {name:'断开', exact:true}).click();
  await page.locator('#token').fill('offline-date-ui-token');
  await page.getByRole('button', {name:'连接', exact:true}).click();
  await page.getByText('首页任务01', {exact:true}).waitFor();
}

try {
  for (const timezone of ['America/New_York','Asia/Shanghai']) {
    const view = await fixture(timezone);
    try {
      const {page,state} = view;
      await applyDate(page,'2035-11-05'); await waitDate(page,'2035-11-05');
      const query = state.queries.at(-1);
      assert.deepEqual(query,{limit:'30',due_from:'2035-11-05T00:00:00+08:00',due_before:'2035-11-06T00:00:00+08:00'});
      assert.equal(await page.locator('#task-list .task-item').count(),30);
      await panel(page).getByRole('button',{name:'下一页',exact:true}).click();
      await page.getByText('2035-11-05 日期第二页',{exact:true}).waitFor();
      assert.equal(state.queries.at(-1).cursor,'date/cursor30');
      assert.equal(state.queries.at(-1).due_from,query.due_from);
      await panel(page).getByRole('button',{name:'修改时间',exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      page.once('dialog',dialog=>dialog.dismiss());
      await applyDate(page,'2035-11-06');
      assert.equal(await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').inputValue(),'2035-12-01T10:30');
      await panel(page).screenshot({path:path.join(evidence,`desktop-page2-editor-${timezone.replaceAll('/','-')}.png`)});
      await page.setViewportSize({width:390,height:844});
      await panel(page).screenshot({path:path.join(evidence,`mobile-page2-editor-${timezone.replaceAll('/','-')}.png`)});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      await page.setViewportSize({width:1280,height:1000});
      state.onPost=async()=>({data:{item:{...secondItem(),id:'date-31',due_at:'2035-12-01T02:35:00Z'}}});
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();
      await waitDate(page,'2035-11-05');
      assert.match(await page.locator('#task-page-state').textContent(),/第 1 页/);
      assert.equal(state.posts[0].body.expected_updated_at,'date-page2-revision');
      assert.equal(state.posts[0].path,'/api/tasks/date-31/reschedule');
      assert.equal(await page.getByLabel('提醒日期（北京时间）',{exact:true}).inputValue(),'2035-11-05');
      await page.locator('#toast').evaluate(node=>{node.classList.remove('show');node.textContent='';});
      await panel(page).screenshot({path:path.join(evidence,`desktop-${timezone.replaceAll('/','-')}.png`)});
      await page.setViewportSize({width:390,height:844});
      await panel(page).screenshot({path:path.join(evidence,`mobile-${timezone.replaceAll('/','-')}.png`)});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      await panel(page).getByRole('button',{name:'清除日期',exact:true}).click();
      await page.getByText('首页任务01',{exact:true}).waitFor();
      assert.equal(state.queries.at(-1).due_from,undefined);
    } finally {await view.close();}
  }

  {
    const view=await fixture();
    try {
      const {page,state}=view;
      await applyDate(page,'2035-11-05');await waitDate(page,'2035-11-05');
      state.failTaskGets=true;await applyDate(page,'2035-11-06');
      await page.waitForFunction(()=>document.querySelector('#task-page-state').textContent.includes('读取失败'));
      assert.equal(await page.getByLabel('提醒日期（北京时间）',{exact:true}).inputValue(),'2035-11-05');
      assert.equal(await page.getByText('2035-11-05 日期任务01',{exact:true}).count(),1);
      state.failTaskGets=false;
      for(const invalid of [{items:null},{items:[null]},{items:[{}],next_cursor:7},{items:Array.from({length:31},()=>task())}]) {
        state.onGet=async()=>invalid;
        await applyDate(page,'2035-11-06');
        await page.waitForFunction(()=>document.querySelector('#task-page-state').textContent.includes('列表格式无效'));
        assert.equal(await page.getByLabel('提醒日期（北京时间）',{exact:true}).inputValue(),'2035-11-05');
        assert.equal(await page.getByText('2035-11-05 日期任务01',{exact:true}).count(),1);
      }
      state.onGet=null;await applyDate(page,'2035-11-06');await waitDate(page,'2035-11-06');
    } finally {await view.close();}
  }

  {
    const view=await fixture();const entered=deferred(),release=deferred();
    try {
      const {page,state}=view;await connectToken(page);
      state.onGet=async(_count,_cursor,query)=>{entered.resolve();await release.promise;return{items:[task({title:'失去能力的日期结果不得回填'})]};};
      await applyDate(page,'2035-11-05');await entered.promise;
      state.dateCapability=false;await pollStatus(page);
      await page.waitForFunction(()=>document.querySelector('#task-date-filter').hidden);
      release.resolve();await page.waitForLoadState('networkidle');
      assert.equal(await page.getByText('失去能力的日期结果不得回填',{exact:true}).count(),0);
      assert.equal(await page.getByText('首页任务01',{exact:true}).count(),1);
      assert.match(await page.locator('#task-date-state').textContent(),/未筛选/);
      state.onGet=null;state.dateCapability=true;await pollStatus(page);
      await page.getByLabel('提醒日期（北京时间）',{exact:true}).waitFor();
      await applyDate(page,'2035-11-05');await waitDate(page,'2035-11-05');
    } finally {release.resolve();await view.close();}
  }

  {
    const view=await fixture();
    try {
      const {page,state}=view;await connectToken(page);
      await applyDate(page,'2035-11-05');await waitDate(page,'2035-11-05');
      await panel(page).getByRole('button',{name:'下一页',exact:true}).click();
      await page.getByText('2035-11-05 日期第二页',{exact:true}).waitFor();
      await panel(page).getByRole('button',{name:'修改时间',exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      state.onPost=async()=>({status:503,data:{error:{message:'模拟未知结果'}}});
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();
      await page.locator('.task-reschedule').getByText(/保存结果待核实/).waitFor();
      assert.equal(await panel(page).getByRole('button',{name:'清除日期',exact:true}).isDisabled(),true);
      const before=state.taskGets;state.dateCapability=false;await pollStatus(page);
      await page.waitForFunction(()=>document.querySelector('#task-date-filter').hidden);
      await panel(page).getByRole('button',{name:'刷新任务',exact:true}).click();
      assert.equal(state.taskGets,before);
      assert.match(await page.locator('#task-page-state').textContent(),/恢复支持当前日期筛选/);
      assert.equal(state.posts.length,1);
      state.dateCapability=true;await pollStatus(page);
      state.onGet=async()=>({items:null});
      await panel(page).getByRole('button',{name:'刷新任务',exact:true}).click();
      await page.waitForFunction(()=>document.querySelector('#task-page-state').textContent.includes('列表格式无效'));
      assert.equal(await panel(page).getByRole('button',{name:'返回首页',exact:true}).isDisabled(),true);
      assert.equal(state.posts.length,1);
      state.onGet=null;
      await panel(page).getByRole('button',{name:'刷新任务',exact:true}).click();
      await page.waitForFunction(()=>!document.querySelector('#task-first').disabled);
      assert.match(await page.locator('#task-page-state').textContent(),/第 2 页/);
      assert.equal(state.posts.length,1);
    } finally {await view.close();}
  }
  {
    const view=await fixture();const entered=deferred(),release=deferred();
    try {
      const {page,state}=view;await connectToken(page);
      await applyDate(page,'2035-11-05');await waitDate(page,'2035-11-05');
      await panel(page).getByRole('button',{name:'下一页',exact:true}).click();
      await page.getByText('2035-11-05 日期第二页',{exact:true}).waitFor();
      await panel(page).getByRole('button',{name:'修改时间',exact:true}).click();
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      state.onPost=async()=>{entered.resolve();await release.promise;return{data:{item:task({id:'date-31',due_at:'2035-12-01T02:35:00Z'})}};};
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();await entered.promise;
      const before=state.taskGets;state.dateCapability=false;await pollStatus(page);
      assert.equal(state.taskGets,before);
      release.resolve();
      await page.getByText('首页任务01',{exact:true}).waitFor();
      await page.waitForFunction(()=>document.querySelector('#toast').textContent.includes('提醒时间已保存'));
      assert.match(await page.locator('#task-date-state').textContent(),/未筛选/);
      assert.equal(state.queries.at(-1).due_from,undefined);
      assert.equal(state.posts.length,1);
    } finally {release.resolve();await view.close();}
  }

  {
    const view=await fixture();
    try {
      const {page,state}=view;await connectToken(page);
      await applyDate(page,'2035-11-05');await waitDate(page,'2035-11-05');
      await page.locator('.task-item').filter({has:page.getByText('2035-11-05 日期任务01',{exact:true})}).getByRole('button',{name:'修改时间',exact:true}).click();
      state.dateCapability=false;await pollStatus(page);
      await page.getByText('首页任务01',{exact:true}).waitFor();
      assert.equal(await page.locator('.task-reschedule').count(),0);
      assert.match(await page.locator('#task-date-state').textContent(),/未筛选/);
      assert.equal(state.queries.at(-1).due_from,undefined);
      assert.match(await page.locator('#toast').textContent(),/已关闭未提交/);
    } finally {await view.close();}
  }

  {
    const view=await fixture();const release=deferred(),headersSent=deferred();
    try {
      const {page,state}=view;
      await applyDate(page,'2035-11-05');await waitDate(page,'2035-11-05');
      dateReadHook=async(request,response)=>{request.resume();response.writeHead(200,{'Content-Type':'application/json'});response.flushHeaders();response.write('{"items":');headersSent.resolve();await release.promise;response.end('[]}');};
      const readMatcher=url=>url.origin===origin&&url.pathname==='/api/tasks'&&url.searchParams.get('due_from')?.startsWith('2035-11-06');
      await page.route(readMatcher,route=>route.continue());
      const headers=page.waitForResponse(response=>response.url().includes('due_from=2035-11-06'));
      await applyDate(page,'2035-11-06');await headersSent.promise;assert.equal((await headers).status(),200);
      await page.clock.runFor(12001);
      await page.waitForFunction(()=>document.querySelector('#task-page-state').textContent.includes('读取失败'));
      assert.equal(await page.getByText('2035-11-05 日期任务01',{exact:true}).count(),1);
      assert.equal(await page.getByLabel('提醒日期（北京时间）',{exact:true}).inputValue(),'2035-11-05');
      release.resolve();dateReadHook=null;
      await page.unroute(readMatcher);
      await panel(page).getByRole('button',{name:'刷新任务',exact:true}).click();await waitDate(page,'2035-11-05');
      assert.equal(state.queries.at(-1).due_from,'2035-11-05T00:00:00+08:00');
    } finally {release.resolve();dateReadHook=null;await view.close();}
  }
  {
    const view=await fixture();
    try {
      const {page,state}=view;await connectToken(page);
      for(const flag of [undefined,false,'true']) {
        state.dateCapability=flag;await pollStatus(page);
        await page.waitForFunction(()=>document.querySelector('#task-date-filter').hidden);
        const gets=state.taskGets;
        await page.locator('#task-date').evaluate(input=>{input.value='2035-11-05';});
        await page.locator('#task-date-apply').evaluate(button=>button.dispatchEvent(new Event('click')));
        assert.equal(state.taskGets,gets);
        assert.match(await page.locator('#task-date-state').textContent(),/未筛选/);
      }
    } finally {await view.close();}
  }
  {
    const view=await fixture();const entered=deferred(),release=deferred();
    try {
      const {page,state}=view;
      state.onGet=async()=>{entered.resolve();await release.promise;return{items:[task({title:'旧身份日期结果不得回填'})]};};
      await applyDate(page,'2035-11-05');await entered.promise;
      await page.getByRole('button',{name:'断开',exact:true}).click();
      state.onGet=null;
      await page.getByRole('button',{name:'连接',exact:true}).click();
      await page.getByText('首页任务01',{exact:true}).waitFor();
      release.resolve();await page.waitForLoadState('networkidle');
      assert.equal(await page.getByText('旧身份日期结果不得回填',{exact:true}).count(),0);
      assert.match(await page.locator('#task-date-state').textContent(),/未筛选/);
      assert.equal(await page.getByLabel('提醒日期（北京时间）',{exact:true}).inputValue(),'');
    } finally {release.resolve();await view.close();}
  }
  assert.deepEqual(outside,[]);
  console.log('Task date filter UI passed: timezone day ranges, scoped pagination, failure/invalid200/body-timeout preserves applied filter, capability loss/race, unknown lock, drafts, true page2 revision, desktop/mobile; outside=0.');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}

