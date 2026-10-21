#include <cassert>
#include <cmath>
#include <initializer_list>
#include "replay/quat.h"
#include "replay/gyro_transport.h"
int main(){
  gyro::PassTransport pass;pass.begin(4317.99707f,20669.8262f,100);
  assert(pass.tick(15000,101)==gyro::PassTransport::Step::Wait);
  assert(pass.tick(4317.99707f,102)==gyro::PassTransport::Step::Start);
  assert(pass.tick(4500,120)==gyro::PassTransport::Step::Run);
  assert(fabsf(gyro::elapsedWithin(20669.8262f,4317.99707f,20669.8262f)-16351.8291f)<.01f);
  assert(gyro::elapsedWithin(4000,4317.99707f,20669.8262f)==0);
  assert(pass.tick(20669.8262f,150)==gyro::PassTransport::Step::End);
  pass.begin(100,200,100);assert(pass.tick(150,10200)==gyro::PassTransport::Step::TimedOut);
  pass.begin(0,1000,0);assert(pass.tick(0,0)==gyro::PassTransport::Step::Start);
  assert(pass.tick(980,1)==gyro::PassTransport::Step::Run);
  assert(pass.tick(992,2)==gyro::PassTransport::Step::End);
  pass.clear();assert(!pass.active);
  const float pi=3.14159265358979323846f;
  for(const auto q:{rquat::Quat(),rquat::fromAxisAngle(1,0,0,pi/2),rquat::fromAxisAngle(0,1,0,-pi/2),rquat::fromAxisAngle(0,0,1,pi)}){
    float a[3],b[3],c[3];rquat::toMatrixRows(q,a,b,c);
    const auto roundtrip=rquat::fromBasis(spline::Vec3(a),spline::Vec3(b),spline::Vec3(c));
    assert(fabsf(fabsf(q.dot(roundtrip))-1)<1e-5f);
    assert(fabsf(fabsf(q.dot(rquat::decode(rquat::encode(q))))-1)<1e-5f);
  }
  // Phone yaw around Three Y becomes GTA yaw around Z, not pitch or roll.
  auto browser=rquat::fromAxisAngle(0,1,0,pi/2);
  rquat::Quat game(browser.x,-browser.z,browser.y,browser.w);
  float a[3],b[3],c[3];rquat::toMatrixRows(game,a,b,c);
  assert(fabsf(b[0]+1)<1e-5f && fabsf(b[1])<1e-5f && fabsf(b[2])<1e-5f);
  assert(fabsf(c[2]-1)<1e-5f);
}
