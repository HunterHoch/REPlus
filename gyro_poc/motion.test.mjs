import test from 'node:test';
import assert from 'node:assert/strict';
import { sampleTake,prepareTake } from './public/take.js';
import { yaw,heading } from './public/orientation.js';
const pose=(t,angle)=>({t,q:yaw(angle)});
test('browser motion repairs short repeated holds without changing raw samples',()=>{
  const samples=[pose(0,0),pose(.01,0),pose(.02,.02),pose(.03,.02),pose(.04,.04),pose(.05,.04),pose(.06,.06)];
  const before=JSON.stringify(samples);
  assert.equal(prepareTake(samples).length,4);
  assert.ok(heading(sampleTake(samples,.025))>.02);
  assert.equal(JSON.stringify(samples),before);
  assert.equal(prepareTake(samples),prepareTake(samples));
});
test('browser cubic has continuous velocity at uneven knots and deterministic shutter samples',()=>{
  const samples=[pose(0,0),pose(.015,.02),pose(.055,.08),pose(.09,.1)];
  for(const t of [.015,.055]){
    const e=.00005,at=heading(sampleTake(samples,t));
    const left=(at-heading(sampleTake(samples,t-e)))/e,right=(heading(sampleTake(samples,t+e))-at)/e;
    assert.ok(Math.abs(left-right)<.02);
  }
  const q=sampleTake(samples,.04);sampleTake(samples,.01);assert.deepEqual(sampleTake(samples,.04),q);
  for(const sample of samples)assert.ok(Math.abs(heading(sampleTake(samples,sample.t))-heading(sample.q))<1e-10);
});
test('browser preserves long pauses, dropouts, endpoints and quaternion signs',()=>{
  const samples=[pose(0,0),pose(.02,0),pose(.2,0),pose(.23,.1)];
  assert.ok(Math.abs(heading(sampleTake(samples,.15)))<1e-12);
  assert.deepEqual(sampleTake(samples,10),samples.at(-1).q);
  const gap=[pose(0,0),pose(.3,.3)];assert.ok(Math.abs(heading(sampleTake(gap,.15))-.15)<1e-12);
  const flipped=[pose(0,0),pose(.02,.02),pose(.04,.04),pose(.06,.06)];flipped[2].q=flipped[2].q.map(v=>-v);
  assert.ok(Math.abs(heading(sampleTake(flipped,.035))-.035)<.00001);
});
