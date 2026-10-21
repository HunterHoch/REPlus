import { fromDevice, multiply, groundCamera, heading, yaw } from './orientation.js';
import { sampleTake } from './take.js';
import { DURATION, playbackTime } from './path.js';
import { createScene } from './scene.js';
const $ = id => document.getElementById(id);
const status = text => { $('status').textContent = text; };
const params = new URLSearchParams(location.hash.slice(1));
const isPhone = params.get('role') === 'phone';
let scene;
try { scene = createScene($('viewport')); }
catch { status('WebGL is unavailable in this browser.'); throw new Error('WebGL unavailable'); }
let socket, raw, screenAngle = 0, headingOffset = [0,0,0,1], aim = scene.initialAim;
let take = null, takeAvailable = false;
let gameTarget=false, gameStatus=null, lastGameStatus=0;
let lastFrame=0, frameUrl, frameLoading=false, streamStarting=false, streamRequested=false;
function portraitView(vertical){
  $('game-preview').classList.toggle('vertical',vertical);
  $('vertical').setAttribute('aria-pressed',String(vertical));$('horizontal').setAttribute('aria-pressed',String(!vertical));
}
const gameCommand=(action,takeover=false)=>send({type:'gameCommand',action,takeover});
function setTarget(enabled){
  gameTarget=enabled;$('target').value=enabled?'game':'browser';
  $('viewport').hidden=enabled;$('restart').hidden=enabled;$('download').hidden=enabled;$('game-live').hidden=!enabled;
  $('game-preview').hidden=!enabled;$('stream-controls').hidden=!enabled;
  if(!enabled){lastFrame=0;streamRequested=false;streamStarting=false;}
  $('hint').textContent=enabled?'Press Stream, choose Horizontal or Vertical, then Record pass. The clip starts from the beginning and saves at the end. REPlus export loads the saved phone motion directly, including motion blur and DOF samples. Marker conversion is optional. Vertical uses the full game render resolution sideways, then rotates the finished video upright.':'Scan the QR, enable gyro, then Record pass. Stop & save, then Watch take to test the same smoother motion interpolation used by REPlus export.';
  controls();
}
let hasSensor = false, enabled = false, connected = false, phoneConnected = false;
let playback = {offset:0,startedAt:0,playing:false};
let serverOffset = Date.now()-performance.now(), bestRtt = Infinity;
let credentials;
const now = () => performance.now()+serverOffset;
const time = () => playbackTime(playback,now());
function send(data) {
  if (socket?.readyState === WebSocket.OPEN && socket.bufferedAmount < 2048) socket.send(JSON.stringify(data));
}
function updateAim() {
  if (!raw) return;
  aim = multiply(headingOffset,groundCamera(raw,screenAngle));
}
function center() {
  if (!raw) return;
  if(gameTarget){gameCommand(3);return;}
  if(playback.mode==='take')return;
  screenAngle = screen.orientation?.angle ?? window.orientation ?? 0;
  const measured = heading(groundCamera(raw,screenAngle));
  if(measured===null){status('Point the phone toward the horizon to align heading.');return;}
  headingOffset = yaw(heading(scene.aimAtSubject(time()))-measured);
  updateAim();
  send({type:'orientation',q:aim,t:time(),gameQ:groundCamera(raw,screenAngle)});
}
let controlsState;
function controls() {
  const state=[connected,hasSensor,phoneConnected,gameTarget,takeAvailable,playback.playing,playback.mode,playback.offset>0,time()<DURATION,gameStatus?.mode,gameStatus?.baking,Math.min(gameStatus?.samples??0,2),gameStatus?.streaming,!!lastFrame&&performance.now()-lastFrame<=2000,streamRequested,streamStarting].join('|');
  if(state===controlsState)return;
  controlsState=state;
  const ready = connected && (!isPhone || hasSensor);
  $('play').disabled = !ready;
  $('restart').disabled = !ready;
  $('center').disabled = !connected || (isPhone ? !hasSensor : !phoneConnected);
  $('play').textContent = playback.playing && playback.mode!=='take' ? 'Stop & save' : playback.offset>0 && time()<DURATION && playback.mode!=='take' ? 'Resume recording' : 'Record pass';
  $('watch').disabled = !connected || !takeAvailable;
  $('download').disabled = !connected || !takeAvailable;
  if(gameTarget){
    const recording=gameStatus?.mode===2, baking=!!gameStatus?.baking;
    $('play').disabled=!ready || (!(recording||baking) && (!gameStatus?.streaming || !lastFrame || performance.now()-lastFrame>2000));
    $('stream').disabled=!connected || recording || baking;
    $('stream').textContent=streamRequested?'Stop stream':streamStarting?'Starting stream…':'Stream';
    $('horizontal').disabled=$('vertical').disabled=!connected || recording || baking;
    $('bake').disabled=!connected || recording || baking || !(gameStatus?.samples>1);
    $('undo-bake').disabled=!connected || recording || baking;
    $('play').textContent=gameStatus?.baking?'Stop bake':gameStatus?.mode===2?'Stop & save':'Record pass';
    $('watch').textContent='Replay game take';
    $('watch').disabled=!connected || !(gameStatus?.samples>0) || recording || baking;
    $('center').disabled=!connected || (isPhone&&!hasSensor) || !!gameStatus?.baking;
    $('game-live').disabled=!connected || (isPhone&&!hasSensor) || recording || baking;
  }else $('watch').textContent='Watch take';
}
async function loadTake() {
  const response=await fetch(`/take?${new URLSearchParams(credentials)}`);
  if(!response.ok)throw new Error('No recorded take available yet.');
  return response.json();
}
function connect() {
  socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/socket?${new URLSearchParams(credentials)}`);
  socket.onopen = () => {
    connected = true; bestRtt = Infinity;
    send({type:'sync',sent:performance.now()});
    status(isPhone ? (enabled ? 'Gyro connected' : 'Connected · enable gyro') : 'Waiting for phone');
    controls();
    if (isPhone && hasSensor) send({type:'orientation',q:aim});
  };
  socket.onmessage = event => {
    if(event.data instanceof Blob){
      if(!gameTarget || frameLoading || !streamRequested)return;
      frameLoading=true;
      const url=URL.createObjectURL(event.data),img=$('game-frame');
      img.onload=()=>{if(frameUrl)URL.revokeObjectURL(frameUrl);frameUrl=url;frameLoading=false;lastFrame=performance.now();img.hidden=false;$('stream-status').textContent='';controls();};
      img.onerror=()=>{URL.revokeObjectURL(url);frameLoading=false;};
      img.src=url;return;
    }
    const data = JSON.parse(event.data);
    if (data.type === 'sync') {
      const rtt = performance.now()-data.sent;
      if (rtt < bestRtt) { bestRtt = rtt; serverOffset = data.serverTime-(data.sent+performance.now())/2; }
    }
    if (data.type === 'phone') {
      phoneConnected = data.connected;
      status(data.connected ? 'Phone connected' : 'Waiting for phone');
    }
    if (data.type === 'orientation' && !isPhone) aim = data.q;
    if(data.type==='gameTarget'){setTarget(data.enabled);$('take-control').hidden=true;}
    if(data.type==='game'){
      if(data.code==='CONTROL_OWNED')$('take-control').hidden=false;
      gameStatus=data;lastGameStatus=performance.now();
      portraitView(!!data.vertical);
      if(streamStarting && hasSensor){streamStarting=false;streamRequested=true;gameCommand(11);}
      if(!streamStarting)streamRequested=!!data.streaming;
      if(data.streamError)$('stream-status').textContent=data.streamError;
      if(gameTarget){
        status(data.error || `FiveM · ${data.baking?'Baking markers':['Off','Live','Recording','Take playback'][data.mode]??'waiting'}`);
        if(!data.error)$('take-status').textContent=`Clip ${((data.elapsedMs??0)/1000).toFixed(2)} / ${((data.durationMs??0)/1000).toFixed(2)} s · Take ${((data.takeDurationMs??0)/1000).toFixed(2)} s · ${data.samples} samples${data.saved?' · saved on PC':''}${data.bakeStatus&&data.bakeStatus!=='No bake yet'?' · '+data.bakeStatus:''}`;
      }
    }
    if(data.type==='streamError')$('stream-status').textContent=data.error;
    if(data.type==='take'){
      takeAvailable=data.samples>=2;
      $('take-status').textContent=data.error || `${data.samples} samples · ${data.duration.toFixed(1)} s${data.saved?' · saved on PC':''}`;
    }
    if (data.type === 'playback') {
      playback = data;
      if(data.mode==='take')loadTake().then(value=>{take=value;}).catch(error=>status(error.message));
      else take=null;
      if (bestRtt === Infinity) serverOffset = data.serverTime-performance.now();
      if (data.offset === 0 && !data.playing) {
        if (isPhone) center();
        else if (!phoneConnected) aim = scene.aimAtSubject(0);
      }
    }
    if (data.type === 'center' && isPhone) center();
    controls();
  };
  socket.onclose = event => {
    connected = false;
    lastFrame=0;streamRequested=false;streamStarting=false;
    playback = {offset:time(),startedAt:now(),playing:false};
    if(event.code===4001){status('Connection moved to another tab');controls();return;}
    status('Disconnected · reconnecting…'); controls(); setTimeout(connect,1500);
  };
  socket.onerror = () => status('Connection failed');
}
$('play').onclick = () => {
  if(gameTarget){gameCommand(gameStatus?.baking || gameStatus?.mode===2?5:4);return;}
  if (time()>=DURATION || playback.mode==='take') send({type:'playback',action:'restart'});
  send({type:'playback',action:playback.mode!=='take' && playback.playing && time()<DURATION ? 'pause' : 'play'});
};
$('restart').onclick = () => send({type:'playback',action:'restart'});
$('target').onchange=()=>{const enabled=$('target').value==='game';setTarget(enabled);gameCommand(enabled?1:0);};
$('stream').onclick=async()=>{
  if(streamRequested){streamRequested=false;streamStarting=false;lastFrame=0;gameCommand(12);$('stream-status').textContent='Stream stopped.';controls();return;}
  lastFrame=0;streamStarting=true;$('stream-status').textContent='Starting game stream…';
  if(isPhone&&!enabled)await $('enable').onclick();
  if(!isPhone||hasSensor){streamStarting=false;streamRequested=true;gameCommand(11);}
  else if(!enabled){streamStarting=false;$('stream-status').textContent='Enable motion access to start streaming.';}
  controls();
};
$('horizontal').onclick=()=>gameCommand(9);
$('vertical').onclick=()=>gameCommand(10);
$('bake').onclick=()=>gameCommand(7);
$('undo-bake').onclick=()=>gameCommand(8);
$('take-control').onclick=()=>gameCommand(1,true);
$('game-live').onclick=()=>gameCommand(gameStatus?.mode===1?2:1);
$('watch').onclick = () => {if(gameTarget)gameCommand(6);else send({type:'playback',action:'watch'});};
$('download').onclick = async () => {
  try {
    const data=await loadTake();
    const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));
    const link=document.createElement('a');link.href=url;link.download=`gyro-take-${data.id}.json`;link.click();
    setTimeout(()=>URL.revokeObjectURL(url),10000);
  }catch(error){status(error.message);}
};
$('center').onclick = () => { if (isPhone) center(); else send({type:'center'}); };
if (isPhone) {
  document.body.classList.add('phone');
  $('enable').hidden = false;
  $('hint').textContent = 'Enable gyro, aim toward the box, then Record pass. Gravity keeps the horizon; Align heading only changes left/right alignment. Stop & save, then Watch take.';
  credentials = {room:params.get('room'),token:params.get('token'),role:'phone'};
  connect();
  $('enable').onclick = async () => {
    try {
      if (!isSecureContext) throw new Error('Motion needs HTTPS. Scan the PC QR again.');
      if (!window.DeviceOrientationEvent) throw new Error('Orientation sensors are unavailable.');
      if (typeof DeviceOrientationEvent.requestPermission === 'function' && await DeviceOrientationEvent.requestPermission() !== 'granted') throw new Error('Allow motion access in browser settings, then retry.');
      if (!enabled) window.addEventListener('deviceorientation',event => {
        if (![event.alpha,event.beta,event.gamma].every(Number.isFinite)) return;
        raw = fromDevice(event.alpha,event.beta,event.gamma);
        if (!hasSensor) { hasSensor = true; center(); controls(); status('Gyro ready · gravity referenced'); }
        updateAim();
        // Sensor events already have their own cadence. A second 16ms gate
        // drops valid events when a nominal 60Hz interval lands just under it.
        send({type:'orientation',q:aim,t:time(),gameQ:groundCamera(raw,screenAngle)});
      });
      enabled = true;
      $('enable').textContent = 'Gyro enabled';
      $('enable').disabled = true;
      setTimeout(() => { if (!hasSensor) { status('No sensor data · check motion permissions'); $('enable').disabled = false; } },4000);
      navigator.wakeLock?.request('screen').catch(() => {});
    } catch (error) { status(error.message); }
  };
  document.addEventListener('visibilitychange',() => {
    if (document.hidden) { if(gameTarget)gameCommand(5);else send({type:'playback',action:'pause'}); status('Paused · return to resume'); }
  });
} else {
  $('pair').hidden = false;
  try {
    let cached;
    try{cached=JSON.parse(sessionStorage.getItem('gyro-session'));}catch{}
    let response=cached?await fetch(`/session?${new URLSearchParams({room:cached.id,token:cached.token})}`):null;
    if(!response?.ok)response=await fetch('/session',{method:'POST'});
    if (!response.ok) throw new Error(await response.text());
    const session = await response.json();
    sessionStorage.setItem('gyro-session',JSON.stringify({id:session.id,token:session.token}));
    $('qr').src = session.qr;
    credentials = {room:session.id,token:session.token,role:'viewer'};
    connect();
  } catch(error) { status(error.message); }
}
setInterval(() => { if (connected) send({type:'sync',sent:performance.now()}); },5000);
function draw() {
  const t = time();
  if(!gameTarget)scene.draw(t,playback.mode==='take' && take ? sampleTake(take.samples,t) : aim);
  if(gameTarget&&gameStatus?.code!=='CONTROL_OWNED'&&performance.now()-lastGameStatus>3000)status('Waiting for REPlus · open a clip in the editor');
  if(gameTarget&&streamRequested&&lastFrame&&performance.now()-lastFrame>2000)$('stream-status').textContent='Waiting for fresh game frames…';
  if(!gameTarget)$('time').textContent = `${t.toFixed(1)} / ${(playback.end??DURATION).toFixed(1)} s`;
  controls();
  requestAnimationFrame(draw);
}
draw();
