const prepared = new WeakMap();
const dot = (a,b) => a.reduce((sum,v,i)=>sum+v*b[i],0);
const align = (q,ref) => dot(q,ref)<0?q.map(v=>-v):q;
const same = (a,b) => align(a,b).every((v,i)=>Math.abs(v-b[i])<1e-7);
export function prepareTake(samples) {
  const cached=prepared.get(samples);
  if(cached?.length===samples.length && cached.end===samples.at(-1)?.t)return cached.keys;
  const keys=[];
  for(let i=0;i<samples.length;){
    let end=i;
    while(end+1<samples.length && same(samples[end+1].q,samples[i].q))end++;
    keys.push(samples[i]);
    if(end>i && (samples[end].t-samples[i].t>.1 || end+1===samples.length))keys.push(samples[end]);
    i=end+1;
  }
  prepared.set(samples,{length:samples.length,end:samples.at(-1)?.t,keys});
  return keys;
}
function slerp(a,b,u) {
  let cosine=dot(a,b);
  const sign=cosine<0?-1:1;cosine=Math.min(1,Math.abs(cosine));
  const angle=Math.acos(cosine),sin=Math.sin(angle);
  const wa=cosine>.9995?1-u:Math.sin((1-u)*angle)/sin;
  const wb=(cosine>.9995?u:Math.sin(u*angle)/sin)*sign;
  const q=a.map((v,i)=>wa*v+wb*b[i]);
  const n=Math.hypot(...q);return q.map(v=>v/n);
}
export function sampleTake(raw,t) {
  const samples=prepareTake(raw);
  if (!samples.length) return null;
  if(t<=samples[0].t) return samples[0].q;
  let lo=0,hi=samples.length-1;
  if(t>=samples[hi].t) return samples[hi].q;
  while(hi-lo>1){const mid=(lo+hi)>>1;if(samples[mid].t<=t)lo=mid;else hi=mid;}
  const a=samples[lo],b=samples[hi],u=(t-a.t)/(b.t-a.t);
  const h=b.t-a.t;
  if(h>.15 || same(a.q,b.q))return slerp(a.q,b.q,u);
  const poses=[samples[Math.max(0,lo-1)].q,a.q,b.q,samples[Math.min(samples.length-1,hi+1)].q];
  const times=[lo?samples[lo-1].t:a.t-h,a.t,b.t,hi+1<samples.length?samples[hi+1].t:b.t+h];
  for(let i=1;i<4;i++)poses[i]=align(poses[i],poses[i-1]);
  if(times[1]-times[0]>.15){times[0]=a.t-h;poses[0]=poses[1];}
  if(times[3]-times[2]>.15){times[3]=b.t+h;poses[3]=poses[2];}
  const u2=u*u,u3=u2*u;
  const q=poses[1].map((v,i)=>
    (2*u3-3*u2+1)*v+(u3-2*u2+u)*h*(poses[2][i]-poses[0][i])/(times[2]-times[0])+
    (-2*u3+3*u2)*poses[2][i]+(u3-u2)*h*(poses[3][i]-poses[1][i])/(times[3]-times[1]));
  const n=Math.hypot(...q);return q.map(v=>v/n);
}
