import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { readFile, rm } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import { createApp } from './server.mjs';
import { positionAt } from './public/path.js';

function message(ws,type,condition=()=>true){return new Promise((resolve,reject)=>{
  const timer=setTimeout(()=>{ws.off('message',receive);reject(new Error(`Missing ${type}`));},2000);
  function receive(raw){const data=JSON.parse(raw);if(data.type!==type || !condition(data))return;clearTimeout(timer);ws.off('message',receive);resolve(data);}
  ws.on('message',receive);
});}

test('phone take persists timed poses and can be replayed/exported without recording replay',async t=>{
  const directory=new URL(`./work/recording-test-${randomUUID()}/`,import.meta.url);
  const app=createApp({recordingDirectory:directory});
  await new Promise(resolve=>app.server.listen(0,'127.0.0.1',resolve));
  t.after(async()=>{
    await app.close();
    const root=fileURLToPath(new URL('./work/',import.meta.url));
    assert.ok(fileURLToPath(directory).startsWith(root));
    await rm(directory,{recursive:true,force:true});
  });
  const base=`http://127.0.0.1:${app.server.address().port}`;
  app.setPublicOrigin('https://example.test');
  const session=await(await fetch(`${base}/session`,{method:'POST'})).json();
  const params=new URLSearchParams(new URL(session.phoneUrl).hash.slice(1));
  const phone=new WebSocket(`${base.replace('http','ws')}/socket?${params}`);
  await once(phone,'open');
  phone.send(JSON.stringify({type:'orientation',q:[0,0,0,1]}));
  const started=message(phone,'playback',data=>data.playing);
  phone.send(JSON.stringify({type:'playback',action:'play'}));await started;
  phone.send(JSON.stringify({type:'orientation',q:[0,.1,0,Math.sqrt(.99)],t:.1}));
  const paused=message(phone,'playback',data=>!data.playing);
  const saved=message(phone,'take',data=>data.saved);
  phone.send(JSON.stringify({type:'playback',action:'pause'}));await paused;await saved;
  const data=await(await fetch(`${base}/take?${params}`)).json();
  assert.equal(data.samples.length,2);
  assert.equal(data.samples[1].t,.1);
  assert.deepEqual(data.samples[1].position,positionAt(.1));
  const disk=JSON.parse(await readFile(new URL(`${data.id}.json`,directory),'utf8'));
  assert.deepEqual(disk,data);
  assert.equal((await fetch(`${base}/take?room=${session.id}&token=invalid`)).status,403);
  const watching=message(phone,'playback',data=>data.mode==='take');
  phone.send(JSON.stringify({type:'playback',action:'watch'}));await watching;
  phone.send(JSON.stringify({type:'orientation',q:[0,0,0,1],t:.2}));
  // A sync response fences the preceding orientation through the relay.
  const sync=message(phone,'sync');phone.send(JSON.stringify({type:'sync',sent:1}));await sync;
  const after=await(await fetch(`${base}/take?${params}`)).json();
  assert.deepEqual(after.samples,data.samples);
  phone.close();
});
