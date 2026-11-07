import { createConsoleApi } from './console_api.js?v=20261104';

let devices=[];
let deviceListLoaded=false;
let generation=0;
let loading=false;
let authenticationRequired=false;
const tokenInput=document.getElementById('device-token');
const api=createConsoleApi(()=>tokenInput.value,()=>{authenticationRequired=true;devices=[];deviceListLoaded=false;render();});
const root=document.getElementById('devices');
function render(){root.replaceChildren();if(!devices.length){const empty=document.createElement("p");empty.textContent=deviceListLoaded?'当前没有已绑定设备。添加新设备时才需要屏幕上的六位配对码。':'尚未读取设备列表；管理员登录与设备绑定是两个独立步骤。';root.append(empty);}for(const item of devices){const card=document.createElement('article');card.className='device-card';card.innerHTML=`<div class="device-head"><div><h2></h2><div class="device-meta"></div></div><span class="device-state ${item.state==='offline'?'offline':''}">${item.pairing_id?'已绑定':'旧版连接'} · ${item.state==='online'?'在线':'离线'}</span></div><div class="device-kpis"><div class="device-kpi"><small>设备 ID</small><strong></strong></div><div class="device-kpi"><small>固件</small><strong></strong></div><div class="device-kpi"><small>最近心跳</small><strong></strong></div></div><div class="device-meta"></div><footer><button class="small-button" data-action="detail">查看详情</button><button class="small-button" data-action="rotate">轮换凭据</button><button class="small-button" data-action="unpair">解绑</button></footer>`;const values=card.querySelectorAll('.device-kpi strong');values[0].textContent=item.id||'—';values[1].textContent=item.firmware||'—';values[2].textContent=item.last_seen||'暂无记录';card.querySelectorAll('.device-meta')[1].textContent='工作区：'+(item.workspace||'未分配');card.querySelector('h2').textContent=item.name||'未命名设备';card.querySelector('.device-meta').textContent=item.model;card.querySelector('[data-action=rotate]').disabled=true;
card.querySelector('[data-action=rotate]').title='凭据轮换尚未实现';
card.querySelector('[data-action=detail]').onclick=()=>alert(`设备：${item.name}\nID：${item.id}\n状态：${item.state}\n固件：${item.firmware}\n工作区：${item.workspace}`);
const unpair=card.querySelector('[data-action=unpair]');unpair.disabled=!item.pairing_id;
unpair.onclick=async()=>{if(!confirm(`解绑 ${item.name}？设备将断开。当前固件不会自动重新显示配对码，请先确认已有设备端重新配对方式。`))return;unpair.disabled=true;try{await api.request(`/api/devices/${encodeURIComponent(item.id)}/unpair`,{method:'POST'});await loadDevices();}catch(error){alert(error.message);unpair.disabled=false;}};root.append(card)}}
const pageState=document.getElementById('device-page-state');
async function loadDevices(){
 if(loading)return false;
 loading=true;const current=generation;authenticationRequired=false;
 document.getElementById('refresh').disabled=true;
 pageState.setAttribute('role','status');pageState.textContent='正在读取已绑定设备…';
 try{
  let data;
  try{data=await api.request('/api/devices');}
  catch(error){
   if(current!==generation)return false;
   if(!authenticationRequired||tokenInput.value.trim()||!['localhost','127.0.0.1','[::1]'].includes(location.hostname))throw error;
   await api.request('/api/auth/local-login',{method:'POST',authenticate:false});
   if(current!==generation)return false;
   authenticationRequired=false;data=await api.request('/api/devices');
  }
  if(current!==generation)return false;
  if(!Array.isArray(data.items))throw Error('设备列表格式不正确');
  // 兼容尚未重启的网关：未注册且不在线的默认配置项不是真实设备。
  devices=data.items.filter(item=>item.pairing_id||item.state==='online');deviceListLoaded=true;
  const paired=devices.filter(item=>item.pairing_id).length;
  pageState.textContent=devices.length?`已恢复 ${paired} 台已绑定设备，${devices.filter(item=>item.state==='online').length} 台在线。再次打开后台无需重新配对。`:'当前没有已绑定设备；仅添加新设备时需要配对码。';
  render();return true;
 }catch(error){
  if(current!==generation)return false;
  devices=[];deviceListLoaded=false;
  pageState.textContent=authenticationRequired?'管理员尚未登录，请点击本机登录或填写管理员令牌后刷新。已有设备无需重新绑定，也无需配对码。':`设备读取失败：${error.message}。读取失败不代表绑定丢失，请刷新重试。`;
  pageState.setAttribute('role','alert');render();return false;
 }finally{if(current===generation){loading=false;document.getElementById('refresh').disabled=false;}}
}
render();document.getElementById('refresh').onclick=loadDevices;document.getElementById('pair').onclick=async()=>{if(!await loadDevices())return;document.getElementById('pairing-modal').classList.add('open');document.getElementById('pair-code-input').focus()};document.getElementById('pair-cancel').onclick=()=>document.getElementById('pairing-modal').classList.remove('open');document.getElementById('pair-confirm').onclick=async()=>{
 const state=document.getElementById('pair-state');
 const code=document.getElementById('pair-code-input').value.replace(/\s/g,'');
 if(!/^\d{6}$/.test(code)){state.textContent='请输入设备显示的六位数字';return;}
 const button=document.getElementById('pair-confirm'); button.disabled=true;
 try{const data=await api.request('/api/devices/pairing/claim',{method:'POST',body:{code}}); state.textContent=`已绑定 ${data.device_id}，等待设备领取凭据并上线`; document.getElementById('pair-code-input').value=''; await loadDevices();}catch(error){state.textContent=error.message;}finally{button.disabled=false;}
};document.addEventListener('keydown',e=>{if(e.key==='Escape')document.getElementById('pairing-modal').classList.remove('open')});

const clearDeviceView=()=>{generation+=1;api.cancel();loading=false;devices=[];deviceListLoaded=false;render();document.getElementById('refresh').disabled=false;document.getElementById('pairing-modal').classList.remove('open');document.getElementById('pair-code-input').value='';document.getElementById('pair-state').textContent='';};
document.getElementById('local-login').onclick=async()=>{clearDeviceView();tokenInput.value='';const current=generation;try{await api.request('/api/auth/local-login',{method:'POST',authenticate:false});if(current===generation)await loadDevices();}catch(error){if(current===generation)pageState.textContent=error.message;}};
tokenInput.addEventListener('input',()=>{clearDeviceView();pageState.textContent='管理员认证信息已改变，请刷新设备列表；无需重新绑定设备。';});
tokenInput.addEventListener('keydown',event=>{if(event.key==='Enter')loadDevices();});
window.addEventListener('pagehide',()=>{clearDeviceView();tokenInput.value='';});
window.addEventListener('pageshow',event=>{if(event.persisted)loadDevices();});
loadDevices();
