#include "replay/gyro_track.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>

static bool same(rquat::Quat a,rquat::Quat b){return fabsf(fabsf(a.dot(b))-1)<1e-5f;}
int main(int argc,char** argv){
  const std::string header="# REPlus Gyro Pass v1; GTA world axes: X right, Y forward, Z up\n# scope \"test:1\"\n";
  const std::string rows="time_ms,x,y,z,qx,qy,qz,qw,fov\n100,1,2,3,0,0,0,1,55\n200,4,5,6,0,0,0.70710678,0.70710678,55\n";
  gyro::Track track;std::istringstream legacy(header+rows);assert(gyro::readTrack(legacy,"test:1",track));assert(!track.vertical);
  rquat::Quat a,b,c;assert(track.rotationAt(125,a));assert(track.rotationAt(175,b));assert(track.rotationAt(125,c));assert(same(a,c));assert(!same(a,b));
  // Walking can seek in any order, including shuffled DOF shutter samples.
  for(float t:{175.f,100.f,150.f,125.f,200.f,150.f}){
    rquat::Quat q,repeated;assert(track.rotationAt(t,q));assert(track.rotationAt(t,repeated));assert(same(q,repeated));
  }
  // Sliding samples continuously as replay time advances. Repeated presents hold.
  rquat::Quat prev;bool first=true;
  for(float t=100;t<=200;t+=5){
    rquat::Quat q,repeated;assert(track.rotationAt(t,q));assert(track.rotationAt(t,repeated));assert(same(q,repeated));if(!first)assert(!same(q,prev));prev=q;first=false;
    float right[3],forward[3],up[3];rquat::toMatrixRows(q,right,forward,up);
    float path[3]={11,12,13},lens[3];for(int i=0;i<3;++i)lens[i]=path[i]+right[i]*.1f+up[i]*.2f;
    assert(path[0]==11&&path[1]==12&&path[2]==13);assert(fabsf(lens[2]-13.2f)<1e-5f);
  }
  // A partial pass holds its first/last aim outside its recorded interval.
  assert(track.rotationAt(0,a)&&same(a,track.samples.front().q));assert(track.rotationAt(999,a)&&same(a,track.samples.back().q));
  assert(!track.rotationAt(std::numeric_limits<float>::quiet_NaN(),a));
  std::istringstream wrong(header+rows);assert(!gyro::readTrack(wrong,"other:1",track));
  std::istringstream invalid(header+"# vertical 5\n"+rows);assert(!gyro::readTrack(invalid,"test:1",track));
  std::istringstream duplicate(header+"time_ms,x,y,z,qx,qy,qz,qw,fov\n100,1,2,3,0,0,0,1,55\n100,1,2,3,0,0,0,1,55\n");assert(!gyro::readTrack(duplicate,"test:1",track));
  if(argc>1){
    std::ifstream in(argv[1]);assert(gyro::readTrack(in,"Project 15:7869990332145647615",track));assert(track.vertical);assert(track.samples.size()==599);
    // Stored portrait roll is applied exactly once, never composed again.
    for(const auto& s:track.motion){assert(track.rotationAt(s.t,a));assert(same(a,s.q));}
    for(float t:{71410.2266f,75000.f,79060.3203f,75000.f}){assert(track.rotationAt(t,a));assert(track.rotationAt(t,b));assert(same(a,b));}
    std::cout<<"Preserved pass: "<<track.samples.size()<<" samples, "<<track.samples.back().t-track.samples.front().t<<" ms, portrait\n";
  }
}
