#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "main.h"
#include "replay/gyro.h"
#include "replay/gyro_bake.h"
#include "replay/gyro_transport.h"
#include "replay/gyro_track.h"
#include "replay/quat.h"
#include "replay/marker.h"
#include "utils/paths.h"
#include "capture/render.h"
#include "capture/fxcapture.h"
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <map>

namespace gyro {
namespace {
  struct Packet { uint32_t magic, sequence, action; float x,y,z,w; };
  static_assert(sizeof(Packet)==28);
  SOCKET socketHandle=INVALID_SOCKET;
  bool initialized=false, havePhone=false, alignPending=false;
  ULONGLONG received=0, reported=0;
  rquat::Quat phone, alignment;
  uint64_t phoneVersion=0, recordedVersion=0;
  unsigned pending=0;
  enum Mode { Off, Live, Record, Replay };
  Mode mode=Off;
  std::vector<Sample> take;
  std::string scope, takeScope, savedPath;
  bool dirty=false;
  float previous=-1;
  PassTransport transport;
  bool startPending=false;
  bool vertical=false, takeVertical=false, streaming=false, streamPending=false;
  bool streamFailed=false;
  uint32_t streamSequence=0, streamSlot=0;
  ULONGLONG streamRequested=0;
  std::string streamPath;
  std::string orientationUndoScope;
  bool exportBeforeBake=false, orientationBakePending=false;
  std::map<std::string,Track> exportTracks;
  Track replayTrack;
  bool exportPrepared=false, exportVertical=false;

  std::string orientationFile(){return paths::baseDir()+"GyroTakes\\orientation.ini";}
  std::string projectKey(){
    const char* name=game::projectName();uint64_t hash=14695981039346656037ull;
    if(name)for(const unsigned char* p=(const unsigned char*)name;*p;++p){hash^=*p;hash*=1099511628211ull;}
    return std::to_string(hash);
  }
  void setVertical(bool value){
    vertical=value;const auto dir=paths::baseDir()+"GyroTakes\\";CreateDirectoryA(dir.c_str(),nullptr);
    if(!WritePrivateProfileStringA(projectKey().c_str(),"Vertical",value?"1":"0",orientationFile().c_str()))logger::write("error","gyro: could not save export orientation");
  }
  void setExportVertical(bool value){
    if(!WritePrivateProfileStringA(projectKey().c_str(),"ExportVertical",value?"1":"0",orientationFile().c_str()))logger::write("error","gyro: could not save baked export orientation");
  }
  void bakeTake(float at){
    const bool prior=portraitForExport();
    beginBake(take,takeScope,at);
    if(bakeActive()){exportBeforeBake=prior;orientationUndoScope=takeScope;orientationBakePending=true;}
  }
  void finishBakeOrientation(const std::string& activeScope){
    if(orientationBakePending&&!bakeActive()){
      if(activeScope==orientationUndoScope && bakeHasChanges())setExportVertical(takeVertical);
      orientationBakePending=false;
    }
  }

  bool findSaved(const std::string& wanted,Track& track,std::string& filePath) {
    try {
      const std::filesystem::path dir=paths::baseDir()+"GyroTakes\\";
      if(!std::filesystem::exists(dir))return false;
      std::vector<std::filesystem::directory_entry> files;
      for(const auto& file:std::filesystem::directory_iterator(dir))if(file.is_regular_file()&&file.path().extension()==".csv"&&file.file_size()<8*1024*1024)files.push_back(file);
      std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});
      for(const auto& file:files){
        std::ifstream in(file.path());
        if(readTrack(in,wanted,track)){filePath=file.path().string();return true;}
      }
    }catch(...){logger::write("error","gyro: saved-take lookup failed");}
    return false;
  }
  bool loadSaved(const std::string& wanted){
    if(!take.empty()&&takeScope==wanted)return true;
    Track track;std::string file;
    if(!findSaved(wanted,track,file))return false;
    take=std::move(track.samples);takeVertical=track.vertical;takeScope=wanted;savedPath=file;dirty=false;
    logger::write("info","gyro: loaded %zu samples from %s",take.size(),savedPath.c_str());return true;
  }

  std::string currentScope() {
    const char* name=game::projectName();
    const int clip=game::clipIndex();
    std::vector<uint64_t> ids;
    if(!name || clip<0 || !game::clipIdentities(ids) || clip>=(int)ids.size())return {};
    return std::string(name)+":"+std::to_string(ids[clip]);
  }
  void save() {
    if(!dirty || take.empty())return;
    const std::string dir=paths::baseDir()+"GyroTakes\\";
    CreateDirectoryA(dir.c_str(),nullptr);
    if(savedPath.empty()){
      SYSTEMTIME t;GetSystemTime(&t);char file[100];
      sprintf_s(file,"gyro-%04u%02u%02u-%02u%02u%02u-%03u.csv",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
      savedPath=dir+file;
    }
    std::ofstream out(savedPath,std::ios::trunc);
    out<<"# REPlus Gyro Pass v1; GTA world axes: X right, Y forward, Z up\n# scope "<<std::quoted(takeScope)<<"\n# vertical "<<(takeVertical?1:0)<<"\ntime_ms,x,y,z,qx,qy,qz,qw,fov\n"<<std::setprecision(9);
    for(const auto& s:take)out<<s.t<<','<<s.p[0]<<','<<s.p[1]<<','<<s.p[2]<<','<<s.q.x<<','<<s.q.y<<','<<s.q.z<<','<<s.q.w<<','<<s.fov<<'\n';
    out.flush();
    if(out.good()){dirty=false;logger::write("info","gyro: saved %zu samples to %s",take.size(),savedPath.c_str());}
    else logger::write("error","gyro: could not save take to %s; retained in memory",savedPath.c_str());
  }
  void stop() { if(mode==Record)save();if(transport.active)game::playbackPause();transport.clear();startPending=false;mode=Off;previous=-1; }
  void completePass(){
    const float end=transport.hi;
    if(mode==Record&&!take.empty()&&transport.hi-take.back().t>=0&&transport.hi-take.back().t<=50){
      if(transport.hi>take.back().t+.01f){auto endpoint=take.back();endpoint.t=transport.hi;take.push_back(endpoint);dirty=true;}
    }
    logger::write("info","gyro: pass finished at %.3f ms (clip/take span %.3f ms)",transport.hi,transport.hi-transport.lo);
    stop();
    // Park on the final instant rather than letting the next project clip run.
    game::jumpProjectTo(end-.01f,0);
  }
  void receive() {
    if(!initialized){
      initialized=true;WSADATA data;
      if(WSAStartup(MAKEWORD(2,2),&data)!=0)return;
      socketHandle=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
      sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(3211);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
      if(socketHandle==INVALID_SOCKET || bind(socketHandle,(sockaddr*)&addr,sizeof(addr))!=0){
        logger::write("error","gyro: loopback UDP 3211 unavailable (%d)",WSAGetLastError());
        if(socketHandle!=INVALID_SOCKET)closesocket(socketHandle);socketHandle=INVALID_SOCKET;return;
      }
      u_long nonblocking=1;ioctlsocket(socketHandle,FIONBIO,&nonblocking);
      logger::write("info","gyro: listening on 127.0.0.1:3211");
    }
    if(socketHandle==INVALID_SOCKET)return;
    // Bounded drain: network input can never stall the replay thread.
    for(int i=0;i<128;++i){
      Packet packet{};
      const int n=recv(socketHandle,(char*)&packet,sizeof(packet),0);
      if(n==SOCKET_ERROR)break;
      if(n!=sizeof(packet) || packet.magic!=0x31525947)continue;
      const float norm=packet.x*packet.x+packet.y*packet.y+packet.z*packet.z+packet.w*packet.w;
      if(!std::isfinite(norm) || fabsf(norm-1)>0.02f)continue;
      // Three.js Y-up, -Z forward -> GTA Z-up, +Y forward.
      if(packet.action==0){
        phone=rquat::Quat(packet.x,-packet.z,packet.y,packet.w).normalized();
        received=GetTickCount64();havePhone=true;
        ++phoneVersion;
      }
      // Settings and streaming must not be overwritten by a later command in this drain.
      if(packet.action==9 || packet.action==10){if(mode!=Record&&!bakeActive()&&!render::active())setVertical(packet.action==10);}
      else if(packet.action==11){streaming=true;streamFailed=false;if(mode==Off)pending=1;}
      else if(packet.action==12){streaming=false;}
      else if(packet.action>=1 && packet.action<=8)pending=packet.action;
    }
  }
  float heading(const rquat::Quat& q) {
    float a[3],b[3],c[3];rquat::toMatrixRows(q,a,b,c);
    return atan2f(-b[0],b[1]);
  }
  void report(float t) {
    const auto now=GetTickCount64();if(socketHandle==INVALID_SOCKET || now-reported<200)return;reported=now;
    float lo=0,hi=0,elapsed=0,duration=0,takeDuration=0;
    if(game::clipRange(lo,hi)){
      const float clamped=lo+elapsedWithin(t,lo,hi);
      const float base=game::nonDilatedToDilatedMs(lo);
      elapsed=game::nonDilatedToDilatedMs(clamped)-base;
      duration=game::nonDilatedToDilatedMs(hi)-base;
      if(take.size()>1)takeDuration=game::nonDilatedToDilatedMs(take.back().t)-game::nonDilatedToDilatedMs(take.front().t);
    }
    const char* streamError=streaming&&!fxcapture::addonPresent()?"Capture add-on unavailable":streaming&&streamPending&&now-streamRequested>5000?"Capture timed out":streaming&&render::active()?"Stream paused during export":streaming&&streamFailed?"Game frame capture failed":"";
    char text[1024];sprintf_s(text,"{\"type\":\"game\",\"mode\":%u,\"timeMs\":%.3f,\"elapsedMs\":%.3f,\"durationMs\":%.3f,\"takeDurationMs\":%.3f,\"samples\":%zu,\"phone\":%s,\"saved\":%s,\"baking\":%s,\"bakeStatus\":\"%s\",\"streaming\":%s,\"vertical\":%s,\"streamError\":\"%s\"}",(unsigned)mode,t,elapsed,duration,takeDuration,take.size(),havePhone && now-received<1000?"true":"false",!savedPath.empty()&&!dirty?"true":"false",bakeActive()?"true":"false",bakeStatus(),streaming?"true":"false",vertical?"true":"false",streamError);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(3212);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    sendto(socketHandle,text,(int)strlen(text),0,(sockaddr*)&addr,sizeof(addr));
  }
}
void command(unsigned action){pending=action;}
bool portraitForExport(){return exportPrepared?exportVertical:GetPrivateProfileIntA(projectKey().c_str(),"ExportVertical",0,orientationFile().c_str())!=0;}
bool recording(){return mode==Record;}
bool prepareExport(const char** reason){
  exportPrepared=false;exportTracks.clear();
  if(mode==Record||bakeActive()){if(reason)*reason="stop gyro recording or marker conversion before exporting";return false;}
  if(dirty)save();
  if(dirty){if(reason)*reason="gyro take could not be saved";return false;}
  const char* name=game::projectName();std::vector<uint64_t> ids;
  if(!name||!game::clipIdentities(ids)||ids.empty()){if(reason)*reason="could not identify project clips for gyro export; open the project first";return false;}
  bool first=true;
  exportVertical=GetPrivateProfileIntA(projectKey().c_str(),"ExportVertical",0,orientationFile().c_str())!=0;
  for(auto id:ids){
    const auto identity=std::string(name)+":"+std::to_string(id);Track track;std::string file;
    if(!findSaved(identity,track,file))continue;
    if(!first&&track.vertical!=exportVertical){exportTracks.clear();if(reason)*reason="gyro takes mix horizontal and vertical; use one orientation per exported project";return false;}
    exportVertical=track.vertical;first=false;
    logger::write("info","gyro: export track %s: %zu samples (%zu motion keys), %.3f..%.3f ms, %s (%s)",identity.c_str(),track.samples.size(),track.motion.size(),track.samples.front().t,track.samples.back().t,track.vertical?"vertical (roll already recorded)":"horizontal",file.c_str());
    exportTracks.emplace(identity,std::move(track));
  }
  exportPrepared=true;
  // Release phone/replay transport without issuing a pause or a seek.
  mode=Off;transport.clear();startPending=false;pending=0;previous=-1;
  logger::write("info","gyro: prepared %zu render track(s); sampled before shake/DOF at replay time",exportTracks.size());
  return true;
}
void streamTick(){
  const auto now=GetTickCount64();
  // Export/DOF owns this capture channel whenever it is active.
  if(render::active() || fxcapture::dofStatus()==1){streamPending=false;return;}
  if(streamPending && fxcapture::lastDone()){
    streamFailed=!fxcapture::lastSucceeded();
    if(fxcapture::lastSucceeded() && socketHandle!=INVALID_SOCKET){
      char event[160];sprintf_s(event,"{\"type\":\"streamFrame\",\"sequence\":%u,\"slot\":%u,\"vertical\":%s}",++streamSequence,streamSlot,vertical?"true":"false");
      sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(3212);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
      sendto(socketHandle,event,(int)strlen(event),0,(sockaddr*)&addr,sizeof(addr));
    }
    streamPending=false;
    fxcapture::setQuality(Config::get().renderQuality);
    fxcapture::setHighlightBoost(Config::get().renderHighlight);
  }
  if(!streaming || !game::isEditModeActive() || !havePhone || now-received>1500 || !fxcapture::addonPresent() || streamPending || !fxcapture::lastDone() || now-streamRequested<100)return;
  const auto dir=paths::baseDir()+"GyroStream\\";CreateDirectoryA(dir.c_str(),nullptr);
  streamSlot=(streamSlot+1)%3;streamPath=dir+"frame"+std::to_string(streamSlot)+".jpg";
  // Never carry preview quality into an export (render::begin sets its own).
  fxcapture::setQuality(65);
  fxcapture::setHighlightBoost(0);
  streamPending=fxcapture::requestSample(streamPath.c_str(),1,0);streamRequested=now;
}
void apply(void* director) {
  receive();
  if(!director || !game::addr_g_ReplayTimeMs)return;
  const float t=*(float*)game::addr_g_ReplayTimeMs;
  if(!std::isfinite(t))return;
  auto* b=(uint8_t*)director;
  const auto activeScope=currentScope();
  static std::string orientationProject;
  const auto project=projectKey();if(project!=orientationProject){orientationProject=project;vertical=GetPrivateProfileIntA(project.c_str(),"Vertical",0,orientationFile().c_str())!=0;}
  if(render::active()){
    // Export owns the transport. Releasing a preview must never pause or seek it.
    if(mode==Record)save();
    mode=Off;transport.clear();startPending=false;pending=0;previous=-1;
    if(exportPrepared){
      rquat::Quat q;const auto track=exportTracks.find(activeScope);
      if(track!=exportTracks.end()&&track->second.rotationAt(t,q)){
        // Recorded absolute aim already includes the portrait roll. Position/FOV
        // stay with the authored path; DOF translates along this rotated basis later.
        rquat::toMatrixRows(q,(float*)(b+rdirector::OFF_FrameMatrixA),(float*)(b+rdirector::OFF_FrameMatrixB),(float*)(b+rdirector::OFF_FrameMatrixC));
      }else if(exportVertical&&!exportTracks.empty()&&!activeScope.empty()){
        // Unrecorded clips in a portrait project need the same sideways framing.
        q=rquat::fromBasis(spline::Vec3((float*)(b+rdirector::OFF_FrameMatrixA)),spline::Vec3((float*)(b+rdirector::OFF_FrameMatrixB)),spline::Vec3((float*)(b+rdirector::OFF_FrameMatrixC)));
        q=(q*rquat::fromAxisAngle(0,1,0,1.57079632679f)).normalized();
        rquat::toMatrixRows(q,(float*)(b+rdirector::OFF_FrameMatrixA),(float*)(b+rdirector::OFF_FrameMatrixB),(float*)(b+rdirector::OFF_FrameMatrixC));
      }
    }
    report(t);return;
  }
  exportPrepared=false;
  if(bakeActive()){
    if(pending==2)streaming=false;
    if(pending==2||pending==5){pending=0;cancelBake(activeScope);}
    else {pending=0;pumpBake(activeScope,t);}
    finishBakeOrientation(activeScope);
    report(t);return;
  }
  if(!game::isEditModeActive() || activeScope.empty()){stop();report(t);return;}
  if(mode!=Off && scope!=activeScope){stop();logger::write("info","gyro: stopped on project/clip change");}
  const bool fresh=havePhone && GetTickCount64()-received<1000;
  const unsigned action=pending;pending=0;
  if(action==2 || action==5){stop();if(action==2)streaming=false;}
  if(action==1 || action==4){
    if(fresh){
      float clipLo=0,clipHi=0;
      if(action==4 && (!game::transportReady()||!game::clipRange(clipLo,clipHi))){logger::write("error","gyro: record refused; clip bounds or transport unavailable");report(t);return;}
      if(dirty)save();
      if(dirty){mode=Off;logger::write("error","gyro: new pass refused because the previous take could not be saved");report(t);return;}
      if(transport.active)game::playbackPause();transport.clear();
      mode=action==4?Record:Live;scope=activeScope;alignPending=true;previous=-1;
      if(mode==Record){take.clear();takeScope=scope;takeVertical=vertical;savedPath.clear();dirty=false;}
      logger::write("info","gyro: %s at replay %.3f ms",mode==Record?"record armed":"live",t);
      if(mode==Record){
        transport.begin(clipLo,clipHi,GetTickCount64());game::playbackPause();
        if(fabsf(t-clipLo)>.5f&&!game::jumpProjectTo(clipLo,0)){stop();logger::write("error","gyro: record seek failed");}
        report(t);return;
      }
    }else logger::write("info","gyro: no fresh phone samples; connect browser controller first");
  }
  if(action==3 && mode!=Off && mode!=Replay)alignPending=true;
  if(action==6){
    if(mode==Record)save();
    if(loadSaved(activeScope)&&game::transportReady()){
      setVertical(takeVertical);
      replayTrack.samples=take;replayTrack.vertical=takeVertical;replayTrack.prepareMotion();
      mode=Replay;scope=activeScope;previous=-1;game::playbackPause();
      transport.begin(take.front().t,take.back().t,GetTickCount64());
      if(fabsf(t-take.front().t)>.5f&&!game::jumpProjectTo(take.front().t,0)){stop();logger::write("error","gyro: take replay seek failed");}
      report(t);return;
    }
    else logger::write("info","gyro: no take for the current project/clip");
  }
  if(action==7){stop();if(loadSaved(activeScope)){setVertical(takeVertical);bakeTake(t);}else logger::write("info","gyro: no matching take to bake");report(t);return;}
  if(action==8){stop();undoBake(activeScope);if(activeScope==orientationUndoScope && strcmp(bakeStatus(),"Original markers and settings restored")==0)setExportVertical(exportBeforeBake);report(t);return;}
  if((mode==Live || mode==Record) && !fresh){stop();logger::write("info","gyro: phone stale; control released");}
  if(mode==Off){report(t);return;}
  bool endAfterSample=false;
  if(transport.active){
    const auto step=transport.tick(t,GetTickCount64());
    if(step==PassTransport::Step::Wait){report(t);return;}
    if(step==PassTransport::Step::TimedOut){stop();logger::write("error","gyro: clip-start seek timed out");report(t);return;}
    if(step==PassTransport::Step::End)endAfterSample=true;
    if(step==PassTransport::Step::Start){previous=-1;startPending=true;}
  }
  if(mode==Record && previous>=0 && (t<previous-.5f || t-previous>1000)){
    stop();logger::write("info","gyro: recording stopped on replay seek");report(t);return;
  }
  rquat::Quat q;
  if(mode==Replay){
    // Original path/aim stays in control outside the recorded interval.
    if(t<take.front().t || t>take.back().t){report(t);return;}
    if(!replayTrack.rotationAt(t,q)){report(t);return;}
  }else{
    if(alignPending){
      float a[3],forward[3],c[3];rquat::toMatrixRows(phone,a,forward,c);
      if(forward[0]*forward[0]+forward[1]*forward[1]<1e-5f){report(t);return;}
      const auto current=rquat::fromBasis(spline::Vec3((float*)(b+rdirector::OFF_FrameMatrixA)),spline::Vec3((float*)(b+rdirector::OFF_FrameMatrixB)),spline::Vec3((float*)(b+rdirector::OFF_FrameMatrixC)));
      if(!std::isfinite(current.dot(current))){report(t);return;}
      alignment=rquat::fromAxisAngle(0,0,1,heading(current)-heading(phone));alignPending=false;
    }
    q=(alignment*phone).normalized();
    // Camera local +Y is forward; this roll turns the rendered image left 90 degrees.
    if(vertical)q=(q*rquat::fromAxisAngle(0,1,0,1.57079632679f)).normalized();
  }
  rquat::toMatrixRows(q,(float*)(b+rdirector::OFF_FrameMatrixA),(float*)(b+rdirector::OFF_FrameMatrixB),(float*)(b+rdirector::OFF_FrameMatrixC));
  if(mode==Record && (take.empty() || (phoneVersion!=recordedVersion && t>take.back().t+.1f))){
    if(take.size()>=36000){stop();logger::write("info","gyro: sample limit reached; take saved");report(t);return;}
    const float* p=(float*)(b+rdirector::OFF_FramePosition);
    const float fov=*(float*)(b+rdirector::OFF_FrameFov);
    if(std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&std::isfinite(fov)){
      take.push_back({t,{p[0],p[1],p[2]},q,fov});dirty=true;recordedVersion=phoneVersion;
    }
  }
  previous=t;report(t);
  if(endAfterSample){completePass();reported=0;report(t);return;}
  if(startPending){startPending=false;game::playbackPlay();}
}
}
