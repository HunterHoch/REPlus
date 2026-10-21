#pragma once
#include <cmath>
#include <cstdint>
namespace gyro {
struct PassTransport {
  enum class Step { Wait, Start, Run, End, TimedOut };
  float lo=0,hi=0,last=0;
  uint64_t soughtAt=0,advancedAt=0;
  bool active=false,seeking=false;
  void begin(float start,float finish,uint64_t now){lo=start;hi=finish;last=start;soughtAt=advancedAt=now;active=seeking=true;}
  void clear(){active=seeking=false;}
  Step tick(float t,uint64_t now){
    if(seeking){
      if(std::fabs(t-lo)<=.5f){seeking=false;last=t;advancedAt=now;return Step::Start;}
      return now-soughtAt>10000?Step::TimedOut:Step::Wait;
    }
    const float step=t>last?t-last:0;
    if(t>last+.1f){last=t;advancedAt=now;}
    // Some builds park the final clock just short of the exact clip bound.
    if(t>=hi-.5f || (t>=hi-50 && step>0 && step<=50 && t+step>=hi-.5f) || (t>=hi-20 && now-advancedAt>500))return Step::End;
    return Step::Run;
  }
};
inline float elapsedWithin(float t,float lo,float hi){return t<lo?0:t>hi?hi-lo:t-lo;}
}
