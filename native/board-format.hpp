#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

namespace dxb {
using Bytes=std::vector<uint8_t>;
struct FormatError:std::runtime_error {using std::runtime_error::runtime_error;};
inline uint32_t U32(const uint8_t* p) {uint32_t n;std::memcpy(&n,p,4);return n;}
inline void Put(Bytes& out,uint32_t n) {for(int i=0;i<4;i++)out.push_back(static_cast<uint8_t>(n>>(i*8)));}
inline void String(Bytes& out,const std::string& value) {Put(out,static_cast<uint32_t>(value.size()));out.insert(out.end(),value.begin(),value.end());}
inline uint32_t Checksum(const uint8_t* bytes,size_t length) {
  uint32_t s=static_cast<uint32_t>(length)^0x5e04a58c;
  for(size_t i=0;i<length;i++)s=((s<<1)|(s>>31))^bytes[i]^static_cast<uint32_t>(i);
  return s;
}
struct Reader {
  const Bytes& bytes;size_t at=0;
  const uint8_t* take(size_t n) {if(n>bytes.size()-at)throw FormatError("Truncated board payload");const auto* p=bytes.data()+at;at+=n;return p;}
  uint32_t u32(){return U32(take(4));}
  std::string string(bool legacy=false) {
    const auto n=u32();if(n>4*1024*1024)throw FormatError("Unreasonable string length");
    const auto* p=take(n);
    if(legacy) {
      if(n<1||n>1024||p[n-1]!=0||std::memchr(p,0,n-1))throw FormatError("Invalid legacy string");
      return std::string(reinterpret_cast<const char*>(p),n-1);
    }
    return std::string(reinterpret_cast<const char*>(p),n);
  }
};
inline Bytes Unwrap(const uint8_t* data,size_t length) {
  if(length<8||length>16*1024*1024)throw FormatError("Unsupported file length");
  if(std::memcmp(data,"BDST",4)==0)return Bytes(data,data+length);
  if((U32(data)^0xabbafad5)!=length)throw FormatError("Unknown envelope");
  Bytes out(data+4,data+length);size_t end=out.size();const auto passes=out.back()&15;
  if(passes<7||passes>15)throw FormatError("Invalid pass count");
  for(int pass=0;pass<passes;pass++) {
    if(end<1)throw FormatError("Truncated envelope");const auto n=out[--end]&15;
    if(n<7||n>15||static_cast<size_t>(n)>end)throw FormatError("Invalid envelope key");
    end-=n;uint8_t key[15];std::memcpy(key,out.data()+end,n);
    for(size_t i=0;i<end;i++)out[i]^=key[i%n];
  }
  out.resize(end);if(end<8||std::memcmp(out.data(),"BDST",4))throw FormatError("Missing BDST signature");return out;
}
struct Normalized {
  Bytes bytes;
  bool rival=false,mirrored=false,hasOriginalChecksum=false;
  uint32_t width=0,boards=0,mirroredBoards=0,originalChecksum=0,sourceVersion=0;
  std::string title;
};
inline std::string Basename(const std::string& path) {
  const auto p=path.find_last_of("/\\");auto name=path.substr(p==std::string::npos?0:p+1);
  if(name.empty()||name.find(':')!=std::string::npos||name.find('\0')!=std::string::npos)throw FormatError("Invalid asset filename");
  return name;
}
inline bool RivalNeedsMirror(const uint8_t* cells) {
  // Original Rival Ball routine 0x00418EF0 sums the upper ten rows using
  // 32-bit arithmetic, then reflects the lower half only if that sum is zero.
  uint32_t sum=0;
  for(size_t x=0;x<37;x++)for(size_t y=0;y<10;y++)sum+=U32(cells+(x*20+y)*4);
  return sum==0;
}
inline Normalized Rival(const Bytes& source) {
  Reader r{source};r.take(4);if(r.u32()!=7)throw FormatError("Not Rival v7");r.u32();
  r.u32(); // Network eligibility is independent of per-board reflection.
  const auto count=r.u32();if(count<1||count>128)throw FormatError("Invalid Rival board count");
  const auto brick=Basename(r.string(true)),background=Basename(r.string(true));
  const auto title=r.string(true),author=r.string(true),extra=r.string(true);
  if(source.size()-r.at!=static_cast<size_t>(count)*3220)throw FormatError("Not the Rival v7 layout");
  Normalized result;result.rival=true;result.sourceVersion=7;result.width=37;result.boards=count;result.title=title;
  auto& out=result.bytes;out.insert(out.end(),{'B','D','S','T'});
  // The selection menu requires version >= 7. Use the current v9 structure
  // with no lock, no artwork checksum restriction, and one layer per board.
  // Original files remain unchanged; this representation exists only in RAM.
  Put(out,9);Put(out,count);Put(out,37);String(out,"");String(out,"");Put(out,0);
  String(out,brick);Put(out,0);String(out,background);
  String(out,title);String(out,author);String(out,extra);String(out,"");Put(out,0);String(out,"");String(out,"");
  for(uint32_t i=0;i<count;i++) {
    Put(out,r.u32());const auto* cells=r.take(2960);const auto* name=r.take(256);
    const bool mirrored=RivalNeedsMirror(cells);
    if(mirrored){result.mirrored=true;result.mirroredBoards++;}
    const auto end=static_cast<const uint8_t*>(std::memchr(name,0,256));
    // v9's first board string is an initialization script, not a display name.
    // Retain the legacy label as a Lua line comment without executing it.
    auto label=std::string(reinterpret_cast<const char*>(name),end?end-name:256);
    std::replace(label.begin(),label.end(),'\r',' ');std::replace(label.begin(),label.end(),'\n',' ');
    String(out,"-- Rival Ball board: "+label);
    String(out,"");String(out,"");Put(out,1);
    // Rival Ball's full board reflects rows 10..19 into rows 9..0 when its
    // upper-half sum is zero; columns and brick IDs stay put.
    // Materialize that layout only in the in-memory Anniversary payload.
    for(size_t x=0;x<37;x++)for(size_t y=0;y<20;y++) {
      const auto sourceY=mirrored&&y<10?19-y:y;
      Put(out,U32(cells+(x*20+sourceY)*4));
    }
  }
  Put(out,Checksum(out.data(),out.size()));
  return result;
}
inline Normalized Modern(const Bytes& source) {
  Reader r{source};r.take(4);const auto version=r.u32();
  if(version<1||version>9)throw FormatError("Unsupported Anniversary version");
  const auto count=r.u32();if(count<1||count>1000)throw FormatError("Invalid Anniversary board count");
  const auto width=version>=5?r.u32():20;
  if(width<1||width>26)throw FormatError("Unsupported Anniversary width");
  if(version>=6){r.string();r.string();r.u32();}
  r.string();if(version>=7)r.u32();r.string();
  std::string title;
  if(version>=2){title=r.string();r.string();r.string();}
  if(version>=6)r.string();
  if(version>=7)r.u32();
  if(version>=8){r.string();r.string();}
  Normalized result;result.width=width;result.boards=count;result.sourceVersion=version;result.title=title;
  auto& out=result.bytes;out.insert(out.end(),source.begin(),source.begin()+r.at);
  const size_t cells=version>=5?520:400;
  for(uint32_t board=0;board<count;board++) {
    const auto begin=r.at;r.u32();
    if(version>=9)r.string();
    uint32_t layers=1;
    if(version>=8){r.string();r.string();layers=r.u32();if(layers>1000)throw FormatError("Invalid layer count");}
    out.insert(out.end(),source.begin()+begin,source.begin()+r.at);
    for(uint32_t layer=0;layer<layers;layer++) {
      const auto* grid=r.take(cells*4);out.insert(out.end(),grid,grid+cells*4);
      // Pre-v5 files retain their 400-cell disk read loop; the constructor
      // already zeros the remainder of the larger runtime layer.
      if(version>=5)out.insert(out.end(),(740-cells)*4,0);
    }
  }
  if(version>=6) {
    const auto beforeChecksum=r.at;const auto crc=r.u32();
    if(Checksum(source.data(),beforeChecksum)!=crc)throw FormatError("Anniversary checksum mismatch");
    result.hasOriginalChecksum=true;result.originalChecksum=crc;Put(out,crc);
  }
  if(r.at!=source.size())throw FormatError("Unexpected trailing board data");
  return result;
}
inline Normalized Normalize(const uint8_t* data,size_t length) {
  const auto payload=Unwrap(data,length);
  try {return Rival(payload);}catch(const FormatError&){}
  return Modern(payload);
}
}
