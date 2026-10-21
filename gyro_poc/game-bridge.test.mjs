import test from 'node:test';
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import { once } from 'node:events';
import { createGameBridge } from './game-bridge.mjs';
test('game bridge transmits native packet layout and receives ASI status',async t=>{
  const native=dgram.createSocket('udp4');native.bind(0,'127.0.0.1');await once(native,'listening');
  let resolveStatus;const status=new Promise(resolve=>{resolveStatus=resolve;});
  const bridge=createGameBridge(resolveStatus,{port:native.address().port,statusPort:0});
  t.after(()=>{bridge.close();native.close();});
  const receive=once(native,'message');bridge.send(4,[0,.6,0,.8]);
  const [packet,remote]=await receive;
  assert.equal(packet.length,28);assert.equal(packet.readUInt32LE(0),0x31525947);assert.equal(packet.readUInt32LE(8),4);
  assert.ok(Math.abs(packet.readFloatLE(16)-.6)<1e-6);assert.ok(Math.abs(packet.readFloatLE(24)-.8)<1e-6);
  native.send(Buffer.from('{"type":"game","mode":2,"samples":17}'),remote.port,remote.address);
  assert.deepEqual(await status,{type:'game',mode:2,samples:17});
});
