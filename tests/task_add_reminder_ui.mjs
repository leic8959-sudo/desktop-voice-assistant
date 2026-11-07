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
const evidence = path.join(root, 'evidence', 'task-add-reminder-ui-20261105');
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
const initialItems = () => [task(), task({id:'expired', title:'模拟已到期', due_at:'2020-01-01T00:00:00Z'}), task({id:'completed', title:'模拟已完成', status:'completed'}), task({id:'untimed', title:'模拟无时间', due_at:null}), task({id:'empty-date',title:'模拟空日期',due_at:''}), task({id:'invalid-date',title:'模拟无效日期',due_at:'invalid-date'}), task({id:'missing-date',title:'模拟缺失日期',due_at:undefined})];

async function fixture(timezoneId = 'America/New_York', viewport = {width:1280, height:1000}, initialCapability = true) {
  const context = await browser.newContext({timezoneId, viewport});
  const page = await context.newPage();
  const state = {items:initialItems(), posts:[], taskGets:0, onGet:null, onPost:null, onStatus:null, taskCapability:initialCapability, addCapability:true, failTaskGets:false, errors:[]};
  page.on('pageerror', error => state.errors.push(String(error)));
  await context.route('**/*', async route => {
    const request = route.request(); const url = new URL(request.url());
    if (url.origin !== origin) { outside.push(request.url()); await route.abort(); return; }
    if (!url.pathname.startsWith('/api/')) { await route.continue(); return; }
    let data = {}; let status = 200;
    if (url.pathname === '/api/status') { data = {bridge:{running:true}, device:{connected:false}, cloud:{connected:false}, capabilities:{tasks:true, ...(state.addCapability === undefined ? {} : {task_add_reminder:state.addCapability}), ...(state.taskCapability === undefined ? {} : {task_reschedule:state.taskCapability})}}; if (state.onStatus) ({data, status = 200} = await state.onStatus(data)); }
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
  await page.clock.install(); await page.goto(origin + '/assistant');
  await page.getByText('模拟未来提醒', {exact:true}).waitFor();
  return {page, state, context, close:async () => { assert.deepEqual(state.errors, []); await context.close(); }};
}

const panel=page=>page.locator('#task-panel');
const untimed=page=>page.locator('.task-item').filter({has:page.getByText('模拟无时间',{exact:true})});
const future=page=>page.locator('.task-item').filter({has:page.getByText('模拟未来提醒',{exact:true})});
async function openAdd(page) {await untimed(page).getByRole('button',{name:'添加提醒',exact:true}).click();}
async function connectToken(page) {
  await page.getByRole('button',{name:'断开',exact:true}).click();
  await page.locator('#token').fill('offline-add-ui-token');
  await page.getByRole('button',{name:'连接',exact:true}).click();
  await page.getByText('模拟无时间',{exact:true}).waitFor();
}
async function poll(page) {await page.clock.runFor(2501);}

try {
  for(const timezone of ['America/New_York','Asia/Shanghai']) {
    const view=await fixture(timezone);
    try {
      const {page,state}=view;
      assert.equal(await panel(page).locator('.item-actions').getByRole('button',{name:'添加提醒',exact:true}).count(),1);
      for(const title of ['模拟空日期','模拟无效日期','模拟缺失日期','模拟已到期','模拟已完成']) {
        assert.equal(await page.locator('.task-item').filter({has:page.getByText(title,{exact:true})}).getByRole('button',{name:'添加提醒',exact:true}).count(),0);
      }
      await openAdd(page);
      const input=page.getByLabel('新提醒时间（北京时间 UTC+08:00）');
      assert.equal(await input.inputValue(),'');
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();
      assert.equal(state.posts.length,0);
      await input.fill('2020-01-01T12:00');
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();
      await page.locator('.task-reschedule').getByText('请选择晚于当前时刻的北京时间。',{exact:true}).waitFor();
      assert.equal(state.posts.length,0);
      await input.fill('2035-12-01T10:30');
      await page.locator('#toast').evaluate(node=>{node.classList.remove('show');node.textContent='';});
      await panel(page).screenshot({path:path.join(evidence,`desktop-${timezone.replaceAll('/','-')}.png`)});
      await page.setViewportSize({width:390,height:844});
      await panel(page).screenshot({path:path.join(evidence,`mobile-${timezone.replaceAll('/','-')}.png`)});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      const entered=deferred(),release=deferred();
      state.onPost=async()=>{entered.resolve();await release.promise;const item=state.items.find(item=>item.id==='untimed');Object.assign(item,{due_at:'2035-12-01T02:35:00Z',updated_at:'actual-server-revision'});return{data:{item}};};
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();await entered.promise;
      await page.locator('.task-reschedule').evaluate(form=>form.dispatchEvent(new Event('submit',{cancelable:true})));
      assert.equal(state.posts.length,1);
      assert.deepEqual(state.posts[0],{path:'/api/tasks/untimed/reschedule',body:{due_at:'2035-12-01T10:30:00+08:00',expected_updated_at:'2035-11-01T00:00:00.000001Z'}});
      release.resolve();
      await page.waitForFunction(()=>document.querySelector('#toast').textContent.includes('10:35')&&!document.querySelector('.task-reschedule'));
      assert.equal(await untimed(page).getByRole('button',{name:'添加提醒',exact:true}).count(),0);
      assert.equal(await untimed(page).getByRole('button',{name:'修改时间',exact:true}).count(),1);
      assert.equal(state.posts.length,1);
      assert.equal(state.items.filter(item=>item.id==='untimed').length,1);
    } finally {await view.close();}
  }

  {
    const view=await fixture();
    try {
      const {page,state}=view;await connectToken(page);
      for(const flag of [undefined,false,'true']) {
        state.addCapability=flag;await poll(page);
        await page.waitForFunction(()=>document.querySelector('[data-task-reschedule="add"]')?.hidden);
        assert.equal(await untimed(page).getByRole('button',{name:'添加提醒',exact:true}).count(),0);
        assert.equal(await future(page).getByRole('button',{name:'修改时间',exact:true}).count(),1);
      }
      await future(page).getByRole('button',{name:'修改时间',exact:true}).click();
      const input=page.getByLabel('新提醒时间（北京时间 UTC+08:00）');await input.fill('2035-12-02T10:30');
      state.addCapability=true;await poll(page);
      state.addCapability=false;await poll(page);
      assert.equal(await input.inputValue(),'2035-12-02T10:30');
      await panel(page).getByRole('button',{name:'取消修改',exact:true}).click();
      state.addCapability=true;await poll(page);
      await openAdd(page);await input.fill('2035-12-03T10:30');
      await page.locator('.task-reschedule').evaluate(form=>{window.oldAddForm=form;});
      state.addCapability=false;await poll(page);
      await page.waitForFunction(()=>!document.querySelector('.task-reschedule'));
      await page.evaluate(()=>window.oldAddForm.dispatchEvent(new Event('submit',{cancelable:true})));
      assert.equal(state.posts.length,0);
    } finally {await view.close();}
  }

  {
    const view=await fixture();const entered=deferred(),release=deferred();
    try {
      const {page,state}=view;await connectToken(page);await openAdd(page);
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      state.onPost=async()=>{entered.resolve();await release.promise;return{data:{item:task({id:'untimed',due_at:'2035-12-01T02:35:00Z'})}};};
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();await entered.promise;
      await page.clock.runFor(15001);
      await page.waitForFunction(()=>document.querySelector('#toast').textContent.includes('保存结果待核实'));
      assert.equal(await untimed(page).getByRole('button',{name:'添加提醒',exact:true}).isDisabled(),true);
      state.addCapability=false;await poll(page);state.addCapability=true;await poll(page);
      assert.equal(await untimed(page).getByRole('button',{name:'添加提醒',exact:true}).isDisabled(),true);
      assert.equal(state.posts.length,1);
      release.resolve();
      await panel(page).getByRole('button',{name:'刷新任务',exact:true}).click();
      await page.waitForFunction(()=>!document.querySelector('[data-task-reschedule="add"]').disabled);
      assert.equal(state.posts.length,1);
      await openAdd(page);assert.equal(await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').inputValue(),'');
    } finally {release.resolve();await view.close();}
  }

  {
    const view=await fixture();const entered=deferred(),release=deferred();
    try {
      const {page,state}=view;await openAdd(page);
      await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      state.onPost=async()=>{entered.resolve();await release.promise;return{data:{item:task({id:'untimed',due_at:'2035-12-01T02:35:00Z'})}};};
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();await entered.promise;
      await page.getByRole('button',{name:'断开',exact:true}).click();
      state.items=initialItems().map(item=>({...item,title:item.id==='untimed'?'新身份未定时任务':item.title}));
      await page.getByRole('button',{name:'连接',exact:true}).click();await page.getByText('新身份未定时任务',{exact:true}).waitFor();
      const reads=state.taskGets;release.resolve();await page.waitForLoadState('networkidle');
      assert.equal(state.taskGets,reads);
      assert.doesNotMatch(await page.locator('#toast').textContent(),/提醒时间已保存/);
      const row=page.locator('.task-item').filter({has:page.getByText('新身份未定时任务',{exact:true})});
      await row.getByRole('button',{name:'添加提醒',exact:true}).click();
      assert.equal(await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').inputValue(),'');
    } finally {release.resolve();await view.close();}
  }

  {
    const view=await fixture();
    try {
      const {page,state}=view;
      state.onPost=async()=>{state.items.find(item=>item.id==='untimed').updated_at='other-client-revision';return{status:409,data:{error:{message:'记录已变化'}}};};
      await openAdd(page);await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();
      await page.waitForFunction(()=>document.querySelector('#toast').textContent.includes('核对后重新修改'));
      assert.equal(state.posts.length,1);
      await openAdd(page);await page.getByLabel('新提醒时间（北京时间 UTC+08:00）').fill('2035-12-01T10:30');
      const held=deferred();state.onPost=async()=>{await held.promise;return{data:{item:task({id:'untimed',due_at:'2035-12-01T02:30:00Z'})}};};
      await panel(page).getByRole('button',{name:'保存时间',exact:true}).click();
      await page.waitForFunction(()=>document.querySelector('.task-reschedule button[type="submit"]').disabled);
      assert.equal(state.posts.at(-1).body.expected_updated_at,'other-client-revision');
      held.resolve();
    } finally {await view.close();}
  }
  assert.deepEqual(outside,[]);
  console.log('Task add reminder UI passed: strict NULL/mode/capability, Shanghai+NY fixed offset, blank validation, original id/version, loss of add capability preserves future draft, stale form, timeout unknown, auth handoff, CAS refresh, desktop/mobile; outside=0.');
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}

