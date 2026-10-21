import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { mkdtemp,writeFile,rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { WebSocket } from 'ws';
import { createApp } from './server.mjs';

test('stream sends completed JPEGs only to the paired game owner and carries portrait/bake commands',async t=>{
  const dir=await mkdtemp(path.join(tmpdir(),'gyro-stream-'));
  let status;const commands=[];
  const app=createApp({recordingDirectory:null,streamDirectory:dir,gameBridgeFactory:callback=>{status=callback;return {send:action=>commands.push(action),close(){}};}});
  await new Promise(resolve=>app.server.listen(0,'127.0.0.1',resolve));
  t.after(async()=>{await app.close();await rm(dir,{recursive:true,force:true});});
  app.setPublicOrigin('https://example.test');
  const base=`http://127.0.0.1:${app.server.address().port}`;
  async function pair(){
    const info=await(await fetch(`${base}/session`,{method:'POST'})).json();
    const phone=new WebSocket(`${base.replace('http','ws')}/socket?${new URLSearchParams(new URL(info.phoneUrl).hash.slice(1))}`);
    await once(phone,'open');return phone;
  }
  const owner=await pair(),other=await pair();
  owner.send(JSON.stringify({type:'orientation',q:[0,0,0,1],gameQ:[0,0,0,1]}));
  let claimed;const claim=new Promise(resolve=>{claimed=resolve;});
  owner.on('message',(raw,binary)=>{if(!binary&&JSON.parse(raw).type==='gameTarget')claimed();});
  owner.send(JSON.stringify({type:'gameCommand',action:11}));await claim;
  const ownFrames=[],otherFrames=[];
  owner.on('message',(raw,binary)=>{if(binary)ownFrames.push(raw);});
  other.on('message',(raw,binary)=>{if(binary)otherFrames.push(raw);});
  const jpeg=Buffer.from([255,216,1,2,3,255,217]);
  await writeFile(path.join(dir,'frame0.jpg'),jpeg);
  const frame=once(owner,'message');await status({type:'streamFrame',slot:0,sequence:1});
  const [received,binary]=await frame;assert.ok(binary);assert.deepEqual(received,jpeg);
  await writeFile(path.join(dir,'frame1.jpg'),jpeg.subarray(0,-2));
  await status({type:'streamFrame',slot:1,sequence:2});
  await status({type:'streamFrame',slot:99,sequence:3});
  assert.equal(ownFrames.length,1);assert.equal(otherFrames.length,0);
  for(const action of [10,9,7,8,12])owner.send(JSON.stringify({type:'gameCommand',action}));
  const updated=once(owner,'message');await updated;
  // All commands above are delivered together before the server's response.
  assert.deepEqual(commands.filter(action=>action!==0),[11,10,9,7,8,12]);
  owner.close();other.close();
});
