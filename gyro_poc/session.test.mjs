import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import { createApp } from './server.mjs';

function message(ws,type,predicate=()=>true){return new Promise((resolve,reject)=>{
  const timer=setTimeout(()=>{ws.off('message',receive);reject(new Error(`Missing ${type}`));},2000);
  function receive(raw){const data=JSON.parse(raw);if(data.type!==type||!predicate(data))return;clearTimeout(timer);ws.off('message',receive);resolve(data);}
  ws.on('message',receive);
});}
test('pairing survives viewer refresh; abandoned ownership is reclaimed and active ownership can be explicitly handed off',async t=>{
  const commands=[];
  const app=createApp({recordingDirectory:null,gameBridgeFactory:()=>({send:action=>commands.push(action),close(){}})});
  await new Promise(resolve=>app.server.listen(0,'127.0.0.1',resolve));t.after(()=>app.close());
  const base=`http://127.0.0.1:${app.server.address().port}`;
  app.setPublicOrigin('https://example.test');
  async function pair(){
    const data=await(await fetch(`${base}/session`,{method:'POST'})).json();
    const phone=new WebSocket(`${base.replace('http','ws')}/socket?${new URLSearchParams(new URL(data.phoneUrl).hash.slice(1))}`);await once(phone,'open');
    return {data,phone};
  }
  const a=await pair(),b=await pair();
  const sample={type:'orientation',q:[0,0,0,1],gameQ:[0,0,0,1]};
  a.phone.send(JSON.stringify(sample));
  const claimed=message(a.phone,'gameTarget',data=>data.enabled);
  a.phone.send(JSON.stringify({type:'gameCommand',action:1}));await claimed;
  const refused=message(b.phone,'game',data=>data.code==='CONTROL_OWNED');
  b.phone.send(JSON.stringify({type:'gameCommand',action:1}));await refused;
  const resumed=await(await fetch(`${base}/session?${new URLSearchParams({room:a.data.id,token:a.data.token})}`)).json();
  assert.equal(resumed.id,a.data.id);assert.equal(resumed.phoneUrl,a.data.phoneUrl);
  const viewerUrl=`${base.replace('http','ws')}/socket?${new URLSearchParams({room:a.data.id,token:a.data.token,role:'viewer'})}`;
  const viewer=new WebSocket(viewerUrl);await once(viewer,'open');
  const displaced=once(viewer,'close');
  const refreshed=new WebSocket(viewerUrl);const retained=message(refreshed,'gameTarget',data=>data.enabled);
  await once(refreshed,'open');await retained;assert.equal((await displaced)[0],4001);
  const takeover=message(b.phone,'gameTarget',data=>data.enabled);
  b.phone.send(JSON.stringify({type:'gameCommand',action:1,takeover:true}));await takeover;
  assert.ok(commands.includes(2));
  b.phone.close();await once(b.phone,'close');
  const reclaimed=message(a.phone,'gameTarget',data=>data.enabled);
  a.phone.send(JSON.stringify({type:'gameCommand',action:1}));await reclaimed;
  a.phone.close();refreshed.close();
});
