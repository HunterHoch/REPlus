#include "replay/gyro_track.h"
#include <cassert>
#include <iostream>
static gyro::Sample pose(float t,float angle){return {t,{0,0,0},rquat::fromAxisAngle(0,0,1,angle),55};}
static float yaw(rquat::Quat q){return 2*atan2f(q.z,q.w);}
int main(){
  gyro::Track track;
  track.samples={pose(0,0),pose(10,0),pose(20,.02f),pose(30,.02f),pose(40,.04f),pose(50,.04f),pose(60,.06f)};
  const auto original=track.samples;track.prepareMotion();
  assert(track.samples.size()==7&&track.motion.size()==4);
  assert(track.samples[1].t==original[1].t&&track.samples[1].q.dot(original[1].q)>.999f);
  // No repeated game-frame holds between real motion updates.
  rquat::Quat q;track.rotationAt(25,q);assert(yaw(q)>.02f);
  for(float t:{20.f,40.f}){
    rquat::Quat before,at,after;
    track.rotationAt(t-.1f,before);track.rotationAt(t,at);track.rotationAt(t+.1f,after);
    const float left=(yaw(at)-yaw(before))/.1f,right=(yaw(after)-yaw(at))/.1f;
    assert(fabsf(left-right)<.00002f);
  }
  // Uneven sample spacing still gives continuous velocity at a knot.
  track.samples={pose(0,0),pose(15,.02f),pose(55,.08f),pose(90,.10f)};track.prepareMotion();
  for(float t:{15.f,55.f}){
    rquat::Quat before,at,after;track.rotationAt(t-.05f,before);track.rotationAt(t,at);track.rotationAt(t+.05f,after);
    assert(fabsf((yaw(at)-yaw(before))/.05f-(yaw(after)-yaw(at))/.05f)<.00002f);
  }
  // Preserve intentional long stationary holds and gaps; never extrapolate.
  track.samples={pose(0,0),pose(20,0),pose(200,0),pose(230,.1f)};track.prepareMotion();
  track.rotationAt(150,q);assert(fabsf(yaw(q))<1e-6f);
  track.rotationAt(-100,q);assert(fabsf(yaw(q))<1e-6f);
  track.rotationAt(500,q);assert(fabsf(yaw(q)-.1f)<1e-6f);
  // Hemisphere flips are the same orientation, not a camera spin.
  track.samples={pose(0,0),pose(20,.02f),pose(40,.04f),pose(60,.06f)};track.samples[2].q=track.samples[2].q*-1;track.prepareMotion();
  track.rotationAt(35,q);assert(yaw(q)>.025f&&yaw(q)<.04f);
  std::cout<<"Motion interpolation: short-hold repair, continuous velocity, irregular timing, pauses and quaternion signs passed\n";
}
