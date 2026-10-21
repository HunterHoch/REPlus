import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import { createApp } from './server.mjs';
import { fromDevice, multiply, inverse, cssMatrix, groundCamera, heading, yaw } from './public/orientation.js';
import { sampleTake } from './public/take.js';

test('ground reference levels portrait and landscape without erasing tilt', () => {
  for(const [angles,screen] of [[[0,90,0],0],[[90,0,-90],90]]){
    const q=groundCamera(fromDevice(...angles),screen);
    assert.ok(Math.abs(Math.abs(q[3])-1)<1e-12);
  }
  const tilted=groundCamera(fromDevice(35,60,0));
  const aligned=multiply(yaw(.7-heading(tilted)),tilted);
  assert.ok(Math.abs(heading(aligned)-.7)<1e-12);
  // World Y component of the forward vector survives heading alignment.
  assert.ok(Math.abs(2*(tilted[1]*tilted[2]-tilted[0]*tilted[3])-2*(aligned[1]*aligned[2]-aligned[0]*aligned[3]))<1e-12);
});

test('take playback interpolates time and takes the shortest quaternion arc', () => {
  const samples=[{t:0,q:[0,0,0,1]},{t:2,q:yaw(Math.PI/2).map(v=>-v)}];
  assert.ok(Math.abs(heading(sampleTake(samples,1))-Math.PI/4)<1e-12);
  assert.deepEqual(sampleTake(samples,-1),samples[0].q);
  assert.deepEqual(sampleTake(samples,4),samples[1].q);
});
import { positionAt, playbackTime, DURATION } from './public/path.js';

function message(ws,type,predicate = () => true) {
  return new Promise((resolve,reject) => {
    const timer = setTimeout(() => { ws.off('message',receive); reject(new Error(`Missing ${type}`)); },2000);
    function receive(raw) {
      const data = JSON.parse(raw);
      if(data.type !== type || !predicate(data)) return;
      clearTimeout(timer); ws.off('message',receive); resolve(data);
    }
    ws.on('message',receive);
  });
}

test('camera path is independent of aiming and playback pauses/clamps', () => {
  assert.ok(positionAt(0)[0] < 0 && positionAt(DURATION)[0] > 0);
  assert.deepEqual(positionAt(-5),positionAt(0));
  assert.deepEqual(positionAt(100),positionAt(DURATION));
  assert.equal(playbackTime({offset:3,startedAt:1000,playing:true},2500),4.5);
  assert.equal(playbackTime({offset:3,startedAt:1000,playing:false},2500),3);
  assert.equal(playbackTime({offset:0,startedAt:0,playing:true},100000),DURATION);
});

test('orientation remains normalized across poses and recenters to identity', () => {
  for (const pose of [[0,90,0,0],[359,-170,80,90],[120,45,-75,270],[0,0,0,0]]) {
    const q = fromDevice(...pose);
    assert.ok(Math.abs(Math.hypot(...q)-1) < 1e-12);
    const neutral = multiply(inverse(q),q);
    neutral.forEach((v,i) => assert.ok(Math.abs(v-(i===3?1:0))<1e-12));
  }
  assert.equal(cssMatrix([0,0,0,1]), 'matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)');
});

test('physical phone roll, pitch and yaw keep their own axes after recentering', () => {
  const reference = inverse(fromDevice(0,90,0));
  const cases = [
    // Upright to landscape: a quarter turn in the screen plane, not a yaw.
    { angles:[90,0,-90], expected:[0,0,Math.SQRT1_2,Math.SQRT1_2] },
    { angles:[270,0,90], expected:[0,0,-Math.SQRT1_2,Math.SQRT1_2] },
    { angles:[0,45,0], expected:[-Math.sin(Math.PI/8),0,0,Math.cos(Math.PI/8)] },
    { angles:[0,90,45], expected:[0,Math.sin(Math.PI/8),0,Math.cos(Math.PI/8)] }
  ];
  for (const {angles,expected} of cases) {
    const actual = multiply(reference,fromDevice(...angles));
    const sign = actual.reduce((sum,v,i) => sum+v*expected[i],0) < 0 ? -1 : 1;
    actual.forEach((v,i) => assert.ok(Math.abs(v-sign*expected[i]) < 1e-12, `${angles}: axis ${i}`));
  }
});

test('QR pairs a phone and relays orientation, recenter and disconnect', async t => {
  const app = createApp({recordingDirectory:null});
  await new Promise(resolve => app.server.listen(0,'127.0.0.1',resolve));
  t.after(() => app.close());
  const base = `http://127.0.0.1:${app.server.address().port}`;
  app.setPublicOrigin('https://example.test');
  const response = await fetch(`${base}/session`, {method:'POST'});
  assert.equal(response.status,200);
  const session = await response.json();
  assert.ok(session.qr.startsWith('data:image/png;base64,'));
  const phoneParams = new URLSearchParams(new URL(session.phoneUrl).hash.slice(1));
  const room = session.id;
  const viewer = new WebSocket(`${base.replace('http','ws')}/socket?room=${room}&role=viewer&token=${session.token}`);
  const initial = message(viewer,'phone');
  await once(viewer,'open');
  assert.deepEqual(await initial,{type:'phone',connected:false});
  const connected = message(viewer,'phone');
  const phone = new WebSocket(`${base.replace('http','ws')}/socket?${phoneParams}`);
  await once(phone,'open');
  assert.deepEqual(await connected,{type:'phone',connected:true});
  const sample = message(viewer,'orientation');
  const q = fromDevice(40,90,20);
  phone.send(JSON.stringify({type:'orientation',q}));
  assert.deepEqual(await sample,{type:'orientation',q});
  const recenter = message(viewer,'center');
  phone.send(JSON.stringify({type:'center'}));
  assert.deepEqual(await recenter,{type:'center'});
  const startedViewer = message(viewer,'playback',data => data.playing);
  const startedPhone = message(phone,'playback',data => data.playing);
  phone.send(JSON.stringify({type:'playback',action:'play'}));
  const started = await startedViewer;
  assert.deepEqual(await startedPhone,started);
  assert.equal(started.offset,0);
  const paused = message(viewer,'playback',data => !data.playing);
  phone.send(JSON.stringify({type:'playback',action:'pause'}));
  assert.ok((await paused).offset>=0);
  const reset = message(viewer,'playback',data => !data.playing && data.offset===0);
  phone.send(JSON.stringify({type:'playback',action:'restart'}));
  await reset;
  const disconnected = message(viewer,'phone');
  phone.close();
  assert.deepEqual(await disconnected,{type:'phone',connected:false});
  const denied = new WebSocket(`${base.replace('http','ws')}/socket?room=${room}&role=phone&token=wrong`);
  const [error] = await once(denied,'error');
  assert.match(error.message,/403/);
  viewer.close();
});
