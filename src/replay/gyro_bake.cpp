#include "main.h"
#include "replay/gyro_bake.h"
#include "replay/marker.h"
#include "utils/paths.h"
#include <array>
#include <fstream>
#include <iomanip>

namespace gyro {
namespace {
struct Snapshot { std::array<uint8_t,rmarker::SIZE> bytes; rsettings::MarkerSettings settings; };
std::vector<Snapshot> before,after;
std::vector<Sample> points;
std::vector<float> added;
std::string bakeScope;
char status[180]="No bake yet";
size_t cursor=0;
int written=0,updated=0,spacing=0,capacity=0;
bool active=false,waiting=false,undoReady=false;
ULONGLONG seekStarted=0;
float returnTime=0;

bool sameSettings(const rsettings::MarkerSettings& a,const rsettings::MarkerSettings& b){
  if(a.path!=b.path || a.orient!=b.orient || a.ourShake!=b.ourShake)return false;
  for(int i=0;i<rsettings::P_COUNT;++i)if(a.v[i]!=b.v[i])return false;
  return true;
}
int find(void* storage,float t){
  for(int i=0;i<rstorage::markerCount(storage);++i){void* m=rstorage::tryGetMarker(storage,i);if(m&&fabsf(rmarker::timeMs(m)-t)<.25f)return i;}
  return -1;
}
std::vector<Snapshot> snapshot(void* storage){
  std::vector<Snapshot> result;
  for(int i=0;i<rstorage::markerCount(storage);++i){
    void* marker=rstorage::tryGetMarker(storage,i);if(!marker)return {};
    Snapshot s;memcpy(s.bytes.data(),marker,rmarker::SIZE);s.settings=rsettings::get(rmarker::timeMs(marker));result.push_back(s);
  }
  return result;
}
const Snapshot& sourceAt(float t){
  size_t selected=0;
  for(size_t i=0;i<before.size();++i)if(rmarker::markerType(before[i].bytes.data())!=rmarker::MARKERTYPE_ANCHOR){selected=i;break;}
  for(size_t i=0;i<before.size();++i){
    const void* m=before[i].bytes.data();
    if(rmarker::markerType(m)==rmarker::MARKERTYPE_ANCHOR)continue;
    if(rmarker::timeMs(m)<=t)selected=i;
  }
  return before[selected];
}
void writePose(void* storage,int index,const Sample& s,bool isNew){
  void* marker=rstorage::tryGetMarker(storage,index);if(!marker)return;
  rmarker::setCameraPosition(marker,s.p);rmarker::setCameraQuat(marker,rquat::encode(s.q));
  rmarker::setFov(marker,(uint8_t)(s.fov<1?1:s.fov>130?130:s.fov+.5f));
  if(isNew){
    const auto& source=sourceAt(s.t);const void* m=source.bytes.data();
    rmarker::setField<uint8_t>(marker,rmarker::OFF_CameraBlendType,rmarker::blendType(m),rmarker::BIT_CameraBlendType);
    rmarker::setField<uint8_t>(marker,rmarker::OFF_CameraEasing,rmarker::easing(m)?1:0,rmarker::BIT_CameraEasing);
    rsettings::set(rmarker::timeMs(marker),source.settings);
  }
  rstorage::blendTypeUpdated(storage,index);
}
void finish(const char* prefix){
  active=false;waiting=false;
  void* storage=game::markerStorage();after=storage?snapshot(storage):std::vector<Snapshot>{};
  undoReady=!before.empty()&&!after.empty()&&(written+updated>0);
  rsettings::save();game::jumpProjectTo(returnTime,0);game::playbackPause();
  sprintf_s(status,"%s: %d new, %d updated; skipped %d spacing, %d capacity",prefix,written,updated,spacing,capacity);
  logger::write("info","gyro: %s",status);
}
}
bool bakeActive(){return active;}
bool bakeHasChanges(){return written+updated>0;}
const char* bakeStatus(){return status;}
void beginBake(const std::vector<Sample>& samples,const std::string& scope,float at){
  if(active || samples.size()<2){strcpy_s(status,"Bake needs a stopped take with at least two samples");return;}
  void* storage=game::markerStorage();
  if(!storage || !game::transportReady()){strcpy_s(status,"Bake unavailable: no marker storage or replay transport");return;}
  auto original=snapshot(storage);
  if(original.empty()){strcpy_s(status,"Bake unavailable: no camera markers");return;}
  bool hasCamera=false;
  for(const auto& entry:original){
    const void* m=entry.bytes.data();if(rmarker::markerType(m)==rmarker::MARKERTYPE_ANCHOR)continue;
    hasCamera=true;
    if(rmarker::camType(m)!=rmarker::CAM_FREE || rmarker::attachId(m)!=-1 || rmarker::lookAtId(m)!=-1){
      strcpy_s(status,"Bake supports world-space free-camera clips without attach/look-at targets");return;
    }
  }
  if(!hasCamera){strcpy_s(status,"Bake needs a free-camera marker");return;}
  // Complete original marker and sidecar snapshot BEFORE the first mutation.
  const std::string dir=paths::baseDir()+"GyroTakes\\";CreateDirectoryA(dir.c_str(),nullptr);
  SYSTEMTIME stamp;GetSystemTime(&stamp);char filename[100];
  sprintf_s(filename,"before-bake-%04u%02u%02u-%02u%02u%02u-%03u.txt",stamp.wYear,stamp.wMonth,stamp.wDay,stamp.wHour,stamp.wMinute,stamp.wSecond,stamp.wMilliseconds);
  std::ofstream backup(dir+filename);
  backup<<"REPlus marker snapshot v1\nscope "<<scope<<'\n';
  const char hex[]="0123456789abcdef";
  for(const auto& entry:original){
    for(uint8_t byte:entry.bytes)backup<<hex[byte>>4]<<hex[byte&15];
    backup<<'\n'<<(int)entry.settings.path<<' '<<(int)entry.settings.orient<<' '<<entry.settings.ourShake;
    for(float value:entry.settings.v)backup<<' '<<std::setprecision(9)<<value;
    backup<<'\n';
  }
  backup.flush();if(!backup.good()){strcpy_s(status,"Bake refused: could not write the original marker backup");return;}
  before=std::move(original);after.clear();added.clear();undoReady=false;
  points=samples;
  // Existing camera markers must also receive their precise take-time pose.
  for(const auto& entry:before){
    const void* m=entry.bytes.data();const float t=rmarker::timeMs(m);
    if(rmarker::markerType(m)!=rmarker::MARKERTYPE_ANCHOR && t>=samples.front().t && t<=samples.back().t)points.push_back(sampleAt(samples,t));
  }
  std::sort(points.begin(),points.end(),[](const Sample& a,const Sample& b){return a.t<b.t;});
  points.erase(std::unique(points.begin(),points.end(),[](const Sample& a,const Sample& b){return fabsf(a.t-b.t)<.1f;}),points.end());
  bakeScope=scope;returnTime=at;cursor=0;written=updated=spacing=capacity=0;waiting=false;active=true;
  game::playbackPause();strcpy_s(status,"Baking markers: keep this clip open");
  logger::write("info","gyro: baking %zu poses; original snapshot %s",points.size(),(dir+filename).c_str());
}
void cancelBake(const std::string& scope){
  if(!active)return;
  if(scope==bakeScope)finish("Bake stopped (Undo available)");
  else {active=false;undoReady=false;strcpy_s(status,"Bake interrupted by clip change; original disk backup retained");}
}
void pumpBake(const std::string& scope,float clock){
  if(!active)return;
  if(!scope.empty() && scope!=bakeScope){cancelBake(scope);return;}
  void* storage=game::markerStorage();
  if(scope.empty() || !storage || !game::isEditModeActive()){
    if(waiting && GetTickCount64()-seekStarted>10000){active=false;undoReady=false;strcpy_s(status,"Bake stopped: clip unavailable; original disk backup retained");}
    return;
  }
  game::playbackPause();
  // Rejected close-spaced samples can be skipped cheaply without extra seeks.
  for(int budget=0;budget<128 && cursor<points.size();++budget){
    const auto point=points[cursor];
    const int existing=find(storage,point.t);
    if(existing>=0){
      void* m=rstorage::tryGetMarker(storage,existing);
      if(rmarker::markerType(m)!=rmarker::MARKERTYPE_ANCHOR){writePose(storage,existing,point,false);++updated;}
      else ++spacing;
      ++cursor;waiting=false;break;
    }
    const int max=rstorage::maxMarkerCount(storage);
    if(max<=0 || rstorage::markerCount(storage)>=max){++capacity;++cursor;waiting=false;continue;}
    if(!rstorage::canPlaceMarkerAt(storage,point.t)){++spacing;++cursor;waiting=false;continue;}
    if(!waiting){
      if(!game::jumpProjectTo(point.t,0)){finish("Bake seek failed (Undo available)");return;}
      waiting=true;seekStarted=GetTickCount64();return;
    }
    if(fabsf(clock-point.t)>.5f){
      if(GetTickCount64()-seekStarted>10000)finish("Bake seek timed out (Undo available)");
      return;
    }
    const int index=rstorage::addMarkerHere(storage);
    if(index<0){finish("Bake marker creation failed (Undo available)");return;}
    void* marker=rstorage::tryGetMarker(storage,index);
    if(!marker){finish("Bake lost created marker (backup retained)");return;}
    const float actual=rmarker::timeMs(marker);added.push_back(actual);
    Sample pose=point;pose.t=actual;
    writePose(storage,index,pose,true);++written;++cursor;waiting=false;break;
  }
  if(cursor>=points.size()){finish("Bake complete");return;}
  sprintf_s(status,"Baking %zu/%zu: %d new, %d updated",cursor,points.size(),written,updated);
}
void undoBake(const std::string& scope){
  if(active){strcpy_s(status,"Stop the bake before undoing it");return;}
  void* storage=game::markerStorage();
  if(!undoReady || scope!=bakeScope || !storage){strcpy_s(status,"No undo snapshot for this clip/session");return;}
  const auto current=snapshot(storage);
  if(current.size()!=after.size()){strcpy_s(status,"Undo refused: markers changed after baking; original disk backup retained");return;}
  for(size_t i=0;i<current.size();++i)if(current[i].bytes!=after[i].bytes || !sameSettings(current[i].settings,after[i].settings)){
    strcpy_s(status,"Undo refused: marker/settings edits after baking; original disk backup retained");return;
  }
  game::playbackPause();
  std::sort(added.begin(),added.end(),std::greater<float>());
  for(float time:added){const int i=find(storage,time);if(i<0 || !rstorage::removeMarker(storage,i)){strcpy_s(status,"Undo could not remove a baked marker; disk backup retained");return;}rsettings::set(time,rsettings::MarkerSettings{});}
  for(const auto& entry:before){
    const float time=rmarker::timeMs(entry.bytes.data());const int i=find(storage,time);
    if(i<0){strcpy_s(status,"Undo could not find an original marker; disk backup retained");return;}
    memcpy(rstorage::tryGetMarker(storage,i),entry.bytes.data(),rmarker::SIZE);
    rsettings::set(time,entry.settings);rstorage::blendTypeUpdated(storage,i);
  }
  rsettings::save();game::jumpProjectTo(returnTime,0);undoReady=false;strcpy_s(status,"Original markers and settings restored");
  logger::write("info","gyro: %s",status);
}
}
