#include <cassert>
#include <cmath>
#include "capture/portrait.h"
#include "replay/quat.h"
int main(){
  assert(videoout::portraitArgs("-c:v libx264")=="-vf transpose=clock -c:v libx264");
  assert(videoout::portraitArgs("-vf \"scale=640:360,fps=30\" -c:v libx264")=="-vf \"transpose=clock,scale=640:360,fps=30\" -c:v libx264");
  assert(videoout::portraitArgs("-filter:v scale=iw/2:ih/2")=="-filter:v transpose=clock,scale=iw/2:ih/2");
  assert(videoout::portraitArgs("-filter_complex split[a][b];[a]palettegen[p];[b][p]paletteuse[v] -map [v]")=="-filter_complex transpose=clock,split[a][b];[a]palettegen[p];[b][p]paletteuse[v] -map [v]");
  float a[3],b[3],c[3];
  rquat::toMatrixRows(rquat::fromAxisAngle(0,1,0,1.57079632679f),a,b,c);
  // World up projects onto image left; the forward/path direction is unchanged.
  assert(a[2]<-.999f && c[0]>.999f && b[1]>.999f);
}
