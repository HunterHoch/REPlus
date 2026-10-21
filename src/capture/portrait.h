#pragma once
#include <string>
namespace videoout {
// Apply the output rotation before a preset's existing scale/palette filters.
inline std::string portraitArgs(std::string args) {
  // The shipped GIF preset uses a complex split/palette graph instead of -vf.
  const auto complex=args.find("-filter_complex ");
  if(complex!=std::string::npos){
    size_t at=complex+16;while(at<args.size()&&args[at]==' ')++at;
    if(at<args.size()&&(args[at]=='"'||args[at]=='\''))++at;
    if(args.compare(at,5,"[0:v]")==0)at+=5;
    args.insert(at,"transpose=clock,");return args;
  }
  for(const char* flag:{"-vf ","-filter:v ","-filter:v:0 "}){
    size_t at=0;
    while((at=args.find(flag,at))!=std::string::npos){
      if(at && args[at-1]!=' '){++at;continue;}
      at+=std::char_traits<char>::length(flag);
      while(at<args.size()&&args[at]==' ')++at;
      if(at<args.size()&&(args[at]=='"'||args[at]=='\''))++at;
      args.insert(at,"transpose=clock,");return args;
    }
  }
  return "-vf transpose=clock "+args;
}
}
