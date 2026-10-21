#pragma once
#include "replay/gyro_take.h"
#include <string>
#include <istream>
#include <sstream>
#include <iomanip>
namespace gyro {
struct Track {
  std::vector<Sample> samples;
  std::vector<Sample> motion;
  bool vertical=false;
  static bool sameRotation(rquat::Quat a,const rquat::Quat& b){
    a=rquat::align(a,b);
    return fabsf(a.x-b.x)<1e-7f&&fabsf(a.y-b.y)<1e-7f&&fabsf(a.z-b.z)<1e-7f&&fabsf(a.w-b.w)<1e-7f;
  }
  void prepareMotion(){
    motion.clear();motion.reserve(samples.size());
    // Older takes stamped the same received pose on several game frames.
    // Remove only short repeated holds; keep genuine pauses and both endpoints.
    for(size_t i=0;i<samples.size();){
      size_t end=i;
      while(end+1<samples.size()&&sameRotation(samples[end+1].q,samples[i].q))++end;
      motion.push_back(samples[i]);
      if(end>i&&(samples[end].t-samples[i].t>100.f || end+1==samples.size()))motion.push_back(samples[end]);
      i=end+1;
    }
  }
  // Replay time only: repeated, reverse and out-of-order shutter samples are valid.
  bool rotationAt(float t,rquat::Quat& q) const {
    const auto& keys=motion.empty()?samples:motion;
    if(keys.size()<2 || !std::isfinite(t))return false;
    if(t<=keys.front().t){q=keys.front().q;return true;}
    if(t>=keys.back().t){q=keys.back().q;return true;}
    const auto hi=std::lower_bound(keys.begin(),keys.end(),t,[](const Sample& s,float at){return s.t<at;});
    const size_t right=hi-keys.begin(),left=right-1;
    // Do not invent curvature across a genuine dropout or a sustained pause.
    const float dt=keys[right].t-keys[left].t;
    if(dt>150.f || sameRotation(keys[left].q,keys[right].q)){q=rquat::slerp(keys[left].q,keys[right].q,(t-keys[left].t)/dt);return true;}
    rquat::Quat poses[4]={keys[left?left-1:left].q,keys[left].q,keys[right].q,keys[right+1<keys.size()?right+1:right].q};
    float times[4]={left?keys[left-1].t:keys[left].t-dt,keys[left].t,keys[right].t,right+1<keys.size()?keys[right+1].t:keys[right].t+dt};
    for(int i=1;i<4;++i)poses[i]=rquat::align(poses[i],poses[i-1]);
    // Don't let a long neighbouring gap distort a short segment's tangent.
    if(times[1]-times[0]>150.f){times[0]=times[1]-dt;poses[0]=poses[1];}
    if(times[3]-times[2]>150.f){times[3]=times[2]+dt;poses[3]=poses[2];}
    q=rquat::hermite(poses,times,t);return true;
  }
};
inline bool readTrack(std::istream& in,const std::string& wanted,Track& result){
  std::string line,identity;Track loaded;
  std::getline(in,line);if(line!="# REPlus Gyro Pass v1; GTA world axes: X right, Y forward, Z up")return false;
  std::getline(in,line);if(line.rfind("# scope ",0)!=0)return false;
  std::istringstream scopeLine(line.substr(8));scopeLine>>std::quoted(identity);if(identity!=wanted)return false;
  std::getline(in,line);
  if(line.rfind("# vertical ",0)==0){if(line!="# vertical 0"&&line!="# vertical 1")return false;loaded.vertical=line=="# vertical 1";std::getline(in,line);}
  if(line!="time_ms,x,y,z,qx,qy,qz,qw,fov")return false;
  while(std::getline(in,line)){
    if(line.empty())continue;
    std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);Sample s;
    if(!(row>>s.t>>s.p[0]>>s.p[1]>>s.p[2]>>s.q.x>>s.q.y>>s.q.z>>s.q.w>>s.fov) || !std::isfinite(s.t) || s.t<0 || !std::isfinite(s.q.dot(s.q)) || fabsf(s.q.dot(s.q)-1)>.02f || !std::isfinite(s.fov)||s.fov<1||s.fov>130 || loaded.samples.size()>=36000 || (!loaded.samples.empty()&&s.t<=loaded.samples.back().t))return false;
    for(float v:s.p)if(!std::isfinite(v)||fabsf(v)>1e7f)return false;
    s.q=s.q.normalized();loaded.samples.push_back(s);
  }
  if(in.bad()||loaded.samples.size()<2)return false;
  loaded.prepareMotion();result=std::move(loaded);return true;
}
}
