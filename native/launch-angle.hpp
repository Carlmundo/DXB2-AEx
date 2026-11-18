#pragma once
#include "patch.hpp"

namespace launch_angle {
inline uint8_t* game=nullptr;
// These three sites create fresh attached balls: a new life, a new board,
// and a player switch without a saved ball. Do not alter scripted balls or
// balls caught by Sticky Paddle, whose offsets already have a meaning.
inline constexpr uint32_t kAppendCalls[]={0x62da6,0x8a658,0x91471};
inline void* __fastcall Append(void* list,void*,void* ball) {
  using NativeAppend=void* (__thiscall*)(void*,void*);
  auto* added=reinterpret_cast<NativeAppend>(game+0x5ba30)(list,ball);
  if(added) {
    using SetFloat=void (__thiscall*)(void*,int,float);
    reinterpret_cast<SetFloat>(game+0x4db00)(added,8,6.0f);
  }
  return added;
}
inline void Verify(uint8_t* base) {
  for(auto rva:kAppendCalls)
    if(base[rva]!=0xe8||base+rva+5+*reinterpret_cast<int32_t*>(base+rva+1)!=base+0x5ba30)
      throw std::runtime_error("Unexpected launch-angle append call");
  const uint8_t key[]={0x8d,0x45,0xc8,0xc7,0x45,0xc8,0,0,0,0};
  const uint8_t value[]={0xc7,0x40,0x0c,6,0,0,0};
  if(std::memcmp(base+0x62db5,key,sizeof(key))||std::memcmp(base+0x62df6,value,sizeof(value)))
    throw std::runtime_error("Unexpected launch-angle initialization");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;
  for(auto rva:kAppendCalls) {
    const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Append)-reinterpret_cast<uintptr_t>(base+rva+5));
    WriteMemory(base+rva+1,&relative,4);
  }
  // Skip the obsolete integer assignment to key 0. The hook initializes
  // key 8 with float 6.0 instead; execution resumes at the speed setup.
  const uint8_t skip[]={0xeb,0x46};WriteMemory(base+0x62db5,skip,sizeof(skip));
}
}
