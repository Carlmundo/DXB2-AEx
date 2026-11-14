#pragma once
#include "board-format.hpp"

namespace dxb {
struct SavedRecord {std::string key;uint32_t players=0,layers=0;std::vector<uint32_t> layerChecksums;};
struct NormalizedSave {
  Bytes bytes;uint32_t version=0,checksum=0,storedCells=0;
  std::vector<SavedRecord> records;
};
inline Bytes UnwrapSave(const uint8_t* data,size_t length) {
  if(length<12||length>256*1024*1024)throw FormatError("Unsupported save length");
  if(U32(data)==0x53415645)return Bytes(data,data+length);
  if((U32(data)^0xabbafad5)!=length)throw FormatError("Unknown save envelope");
  Bytes out(data+4,data+length);size_t end=out.size();const auto passes=out.back()&15;
  if(passes<7||passes>15)throw FormatError("Invalid save pass count");
  for(int pass=0;pass<passes;pass++) {
    if(end<1)throw FormatError("Truncated save envelope");const auto n=out[--end]&15;
    if(n<7||n>15||static_cast<size_t>(n)>end)throw FormatError("Invalid save envelope key");
    end-=n;uint8_t key[15];std::memcpy(key,out.data()+end,n);
    for(size_t i=0;i<end;i++)out[i]^=key[i%n];
  }
  out.resize(end);return out;
}
inline uint32_t SaveCount(Reader& r,uint32_t limit) {
  const auto value=r.u32();if(value>limit)throw FormatError("Invalid save count");return value;
}
inline NormalizedSave ParseSave(const Bytes& source,uint32_t cells) {
  NormalizedSave result;Reader r{source};
  if(r.u32()!=0x53415645)throw FormatError("Missing SAVE signature");
  result.version=r.u32();result.checksum=U32(source.data()+source.size()-4);result.storedCells=cells;
  if(result.version<5||result.version>6)throw FormatError("Unsupported save layout");
  r.take(16*4);const auto count=SaveCount(r,10000);size_t copied=0;
  const auto flush=[&](){result.bytes.insert(result.bytes.end(),source.begin()+copied,source.begin()+r.at);copied=r.at;};
  for(uint32_t record=0;record<count;record++) {
    SavedRecord info;info.key=r.string();if(info.key.find('\0')!=std::string::npos)throw FormatError("Invalid save key");
    r.u32();r.string();r.u32();r.string();r.string();r.string();r.take(3*4);
    info.players=SaveCount(r,4);
    for(uint32_t player=0;player<info.players;player++) {
      r.take(5*4);const auto crates=SaveCount(r,4096);r.take(crates*8);r.string();
      const auto layers=SaveCount(r,1024);info.layers+=layers;
      for(uint32_t layer=0;layer<layers;layer++) {
        r.take(cells*16);flush();
        if(cells==520)result.bytes.insert(result.bytes.end(),220*16,0);
        info.layerChecksums.push_back(Checksum(result.bytes.data()+result.bytes.size()-740*16,740*16));
      }
    }
    r.take(9*4);r.string();r.take(2*4);
    const auto objects=SaveCount(r,4096);
    for(uint32_t object=0;object<objects;object++) {
      r.take(11*4);const auto variables=SaveCount(r,4096);r.take(variables*8);
    }
    const auto variables=SaveCount(r,4096);r.take(variables*8);
    result.records.push_back(info);
  }
  r.u32();if(r.at!=source.size())throw FormatError("Unexpected trailing save data");flush();
  // Preserve the validated source checksum/identity. The native checksum hook
  // supplies it for this in-memory padding, just as the board adapter does.
  return result;
}
inline NormalizedSave NormalizeSave(const uint8_t* data,size_t length) {
  const auto source=UnwrapSave(data,length);
  if(source.size()<12||U32(source.data())!=0x53415645)throw FormatError("Missing SAVE signature");
  if(Checksum(source.data(),source.size()-4)!=U32(source.data()+source.size()-4))throw FormatError("Save checksum mismatch");
  const auto version=U32(source.data()+4);
  if(version>=1&&version<5) {
    NormalizedSave result;result.bytes=source;result.version=version;result.storedCells=520;
    result.checksum=U32(source.data()+source.size()-4);return result;
  }
  if(version==6)return ParseSave(source,740);
  if(version==5) {
    // Earlier experimental builds wrote expanded layers but retained version 5.
    // Require a complete structural parse to distinguish them from stock saves.
    try{return ParseSave(source,740);}catch(const FormatError&){}
    return ParseSave(source,520);
  }
  throw FormatError("Unsupported save version");
}
}
