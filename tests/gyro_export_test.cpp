// Exercise the production export branch with game/capture boundaries stubbed.
#include "../src/replay/gyro.cpp"
#include <cassert>
#include <iostream>
static float testClock=71410.2266f;
static int testClip=0,seekCalls=0,pauseCalls=0;
static bool testRendering=true;
static bool testEditing=false;
namespace game {
uintptr_t addr_g_ReplayTimeMs=(uintptr_t)&testClock;
const char* projectName(){return "Project 15";}
int clipIndex(){return testClip;}
bool clipIdentities(std::vector<uint64_t>& out){out={7869990332145647615ull,1234};return true;}
bool clipRange(float& lo,float& hi){lo=71410.2266f;hi=79060.3203f;return true;}
float nonDilatedToDilatedMs(float t){return t;}
bool isEditModeActive(){return testEditing;}
bool transportReady(){return true;}
void playbackPause(){++pauseCalls;}
void playbackPlay(){assert(false);}
bool jumpProjectTo(float,unsigned){++seekCalls;return true;}
}
namespace render {bool active(){return testRendering;}}
namespace gyro {
bool bakeActive(){return false;}
bool bakeHasChanges(){return false;}
const char* bakeStatus(){return "fixture";}
void beginBake(const std::vector<Sample>&,const std::string&,float){assert(false);}
void undoBake(const std::string&) {assert(false);}
void cancelBake(const std::string&) {assert(false);}
void pumpBake(const std::string&,float){assert(false);}
}
namespace fxcapture {
bool addonPresent(){return false;}
bool lastDone(){return true;}
bool lastSucceeded(){return true;}
uint32_t dofStatus(){return 0;}
void setQuality(int){}
void setHighlightBoost(float){}
bool requestSample(const char*,int,int){assert(false);return false;}
}
namespace logger {void write(const char*,const char*,...) {}}
int main(int argc,char** argv){
  assert(argc==2);
  // No UDP socket is opened by this harness.
  gyro::initialized=true;
  const std::filesystem::path fixture=paths::sub("GyroTakes")+"export-fixture.csv";
  assert(!std::filesystem::exists(fixture));
  std::filesystem::copy_file(argv[1],fixture);
  gyro::Track expected;std::ifstream source(argv[1]);assert(gyro::readTrack(source,"Project 15:7869990332145647615",expected));
  const char* reason=nullptr;assert(gyro::prepareExport(&reason));assert(gyro::portraitForExport());
  // Freeze the snapshot: subsequent disk changes cannot alter a running render.
  {std::ofstream changed(fixture);changed<<"invalid replacement";}
  alignas(16) uint8_t director[1024]{};
  auto a=(float*)(director+rdirector::OFF_FrameMatrixA),b=(float*)(director+rdirector::OFF_FrameMatrixB),c=(float*)(director+rdirector::OFF_FrameMatrixC);
  auto pos=(float*)(director+rdirector::OFF_FramePosition),fov=(float*)(director+rdirector::OFF_FrameFov);
  pos[0]=11;pos[1]=12;pos[2]=13;*fov=47;
  for(float clock:{71410.2266f,75000.f,75000.1f,75000.f,79060.3203f,71410.2266f}){
    testClock=clock;rquat::toMatrixRows({},a,b,c);
    gyro::command(4);gyro::apply(director); // live commands cannot take over export
    const auto actual=rquat::fromBasis(spline::Vec3(a),spline::Vec3(b),spline::Vec3(c));
    rquat::Quat q;assert(expected.rotationAt(clock,q));assert(fabsf(fabsf(actual.dot(q))-1)<1e-5f);
    assert(pos[0]==11&&pos[1]==12&&pos[2]==13&&*fov==47);assert(!gyro::recording());
    // The existing DOF operation uses the current rotated right/up axes.
    float aperture[3];for(int i=0;i<3;++i)aperture[i]=pos[i]+.1f*a[i]+.2f*c[i];
    float ea[3],eb[3],ec[3];rquat::toMatrixRows(q,ea,eb,ec);
    for(int i=0;i<3;++i)assert(fabsf(aperture[i]-(pos[i]+.1f*ea[i]+.2f*ec[i]))<1e-5f);
  }
  // A clip without a take keeps its own aim/path and receives portrait framing.
  testClip=1;rquat::toMatrixRows({},a,b,c);gyro::apply(director);assert(a[2]<-.999f&&b[1]>.999f);
  assert(seekCalls==0&&pauseCalls==0);
  // Recording samples fresh sensor packets, not repeated game-frame holds.
  testRendering=false;testEditing=true;testClip=0;
  gyro::mode=gyro::Record;gyro::scope="Project 15:7869990332145647615";gyro::takeScope=gyro::scope;
  gyro::take.clear();gyro::pending=0;gyro::previous=-1;gyro::alignPending=false;
  gyro::havePhone=true;gyro::received=GetTickCount64();gyro::phoneVersion=1;gyro::recordedVersion=0;
  testClock=100;gyro::apply(director);assert(gyro::take.size()==1);
  testClock=110;gyro::apply(director);assert(gyro::take.size()==1);
  ++gyro::phoneVersion;testClock=120;gyro::apply(director);assert(gyro::take.size()==2);
  ++gyro::phoneVersion;testClock=130;gyro::apply(director);assert(gyro::take.size()==3);
  gyro::mode=gyro::Off;gyro::dirty=false;
  std::filesystem::remove(fixture);
  std::cout<<"Production gyro export: walking/sliding sample times, DOF basis, portrait, frozen track, no transport writes passed\n";
}
