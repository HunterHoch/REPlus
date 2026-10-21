#include "main.h"
#include "replay/gyro_bake.h"
#include "replay/blendprofile.h"
#include "replay/marker.h"
#include <cassert>
#include <array>
#include <map>
#include <vector>
#include <algorithm>

using Marker=std::array<uint8_t,rmarker::SIZE>;
struct Storage { void** table;std::vector<Marker> markers;int maximum=4; };
void* slots[46]{};Storage storage{slots};float clockMs=0,requested=-1;
std::map<float,rsettings::MarkerSettings> settings;
void* __fastcall get(void* self,uint32_t i){auto& s=*(Storage*)self;return i<s.markers.size()?s.markers[i].data():nullptr;}
int __fastcall count(void* self){return (int)((Storage*)self)->markers.size();}
int __fastcall maximum(void* self){return ((Storage*)self)->maximum;}
bool __fastcall canPlace(void* self,float at,bool){for(const auto& m:((Storage*)self)->markers)if(fabsf(rmarker::timeMs(m.data())-at)<35)return false;return true;}
bool __fastcall add(void* self,bool,bool,int* index){
  auto& s=*(Storage*)self;if(count(self)>=s.maximum || !canPlace(self,clockMs,false))return false;
  Marker m=s.markers.front();*(float*)(m.data()+rmarker::OFF_TimeMs)=clockMs;s.markers.push_back(m);
  std::sort(s.markers.begin(),s.markers.end(),[](const Marker& a,const Marker& b){return rmarker::timeMs(a.data())<rmarker::timeMs(b.data());});
  for(int i=0;i<count(self);++i)if(rmarker::timeMs(s.markers[i].data())==clockMs)*index=i;
  return true;
}
void __fastcall removeMarker(void* self,uint32_t i){auto& s=*(Storage*)self;s.markers.erase(s.markers.begin()+i);}
void __fastcall notify(void*,uint32_t){}
namespace game {
void* markerStorage(){return &storage;}
bool transportReady(){return true;}
bool isEditModeActive(){return true;}
void playbackPause(){}
bool jumpProjectTo(float t,unsigned){requested=t;return true;}
}
namespace rsettings {
MarkerSettings get(float t){auto found=settings.find(t);return found==settings.end()?MarkerSettings{}:found->second;}
void set(float t,const MarkerSettings& s){if(s.isDefault())settings.erase(t);else settings[t]=s;}
void save(){}
}
namespace logger { void write(const char*,const char*,...){} }
Marker marker(float t){
  Marker m{};*(float*)(m.data()+rmarker::OFF_TimeMs)=t;
  *(int32_t*)(m.data()+rmarker::OFF_AttachTargetId)=-1;*(int32_t*)(m.data()+rmarker::OFF_LookAtTargetId)=-1;
  m[rmarker::OFF_CamType]=rmarker::CAM_FREE;m[rmarker::OFF_CameraBlendType]=rmarker::BLEND_SMOOTH;
  rmarker::setCameraQuat(m.data(),rquat::encode({}));rmarker::setFov(m.data(),55);return m;
}
int main(){
  slots[rstorage::VT_TryGetMarker/8]=(void*)get;slots[rstorage::VT_GetMarkerCount/8]=(void*)count;
  slots[rstorage::VT_GetMaxMarkerCount/8]=(void*)maximum;slots[rstorage::VT_CanPlaceMarkerAt/8]=(void*)canPlace;
  slots[rstorage::VT_AddMarkerHere/8]=(void*)add;slots[rstorage::VT_RemoveMarker/8]=(void*)removeMarker;
  slots[rstorage::VT_BlendTypeUpdated/8]=(void*)notify;
  storage.markers={marker(0),marker(200)};const auto originals=storage.markers;
  rsettings::MarkerSettings source,target;
  source.path=rsettings::PathMode::Spline;source.orient=rsettings::OrientMode::Squad;
  source.v[rsettings::P_ALPHA]=.5f;source.v[rsettings::P_EASE_IN]=.1f;source.v[rsettings::P_EASE_OUT]=.2f;
  target.ourShake=true;target.v[rsettings::P_INTENSITY]=3;target.v[rsettings::P_DOF_DELTA]=.7f;
  blendprofile::copy(source,target);
  assert(target.path==source.path&&target.orient==source.orient&&target.v[rsettings::P_ALPHA]==.5f&&target.v[rsettings::P_EASE_OUT]==.2f);
  assert(target.ourShake&&target.v[rsettings::P_INTENSITY]==3&&target.v[rsettings::P_DOF_DELTA]==.7f);
  settings[0]=source;
  std::vector<gyro::Sample> samples;
  for(int t=0;t<=200;t+=20)samples.push_back({(float)t,{(float)t,2,3},rquat::fromAxisAngle(0,0,1,t*.002f),55});
  gyro::beginBake(samples,"fixture",75);assert(gyro::bakeActive());
  for(int i=0;i<200&&gyro::bakeActive();++i){gyro::pumpBake("fixture",clockMs);if(requested>=0){clockMs=requested;requested=-1;}}
  assert(!gyro::bakeActive());assert(storage.markers.size()==4);
  assert(strstr(gyro::bakeStatus(),"2 new, 2 updated"));assert(strstr(gyro::bakeStatus(),"spacing"));
  for(const auto& m:storage.markers){
    const float t=rmarker::timeMs(m.data());const auto pose=gyro::sampleAt(samples,t);
    assert(fabsf(rmarker::position(m.data())[0]-pose.p[0])<1e-5f);
    assert(fabsf(fabsf(rquat::decode(rmarker::camQuat(m.data())).dot(pose.q))-1)<1e-5f);
  }
  // Undo must refuse to overwrite edits made after baking.
  storage.markers[0][rmarker::OFF_Fov]=60;gyro::undoBake("fixture");assert(strstr(gyro::bakeStatus(),"refused"));
  storage.markers[0][rmarker::OFF_Fov]=55;gyro::undoBake("fixture");
  assert(storage.markers==originals);assert(strstr(gyro::bakeStatus(),"restored"));
  assert(settings.size()==1&&settings[0].path==source.path);
}
