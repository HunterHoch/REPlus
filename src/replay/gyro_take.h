#pragma once
#include "replay/quat.h"
#include <vector>
#include <algorithm>
namespace gyro {
struct Sample { float t,p[3]; rquat::Quat q; float fov; };
inline Sample sampleAt(const std::vector<Sample>& samples,float t) {
  if(t<=samples.front().t)return samples.front();
  if(t>=samples.back().t)return samples.back();
  const auto hi=std::lower_bound(samples.begin(),samples.end(),t,[](const Sample& s,float at){return s.t<at;});
  const auto& lo=*(hi-1);const float u=(t-lo.t)/(hi->t-lo.t);
  Sample result{t,{},rquat::slerp(lo.q,hi->q,u),lo.fov+(hi->fov-lo.fov)*u};
  for(int i=0;i<3;++i)result.p[i]=lo.p[i]+(hi->p[i]-lo.p[i])*u;
  return result;
}
}
