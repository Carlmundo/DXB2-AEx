#pragma once
#include "patch.hpp"

namespace online_leaderboards {
inline uint8_t* game=nullptr;
inline constexpr uint32_t kViewGate=0x9620d,kPostGate=0x96366,kTooltip=0x16a700;
inline constexpr uint8_t kViewBytes[]={0xe8,0xde,0x72,0,0,0x85,0xc0,0x75,0x68};
inline constexpr uint8_t kPostBytes[]={0x83,0xbc,0x30,0x9c,0,0,0,0,0x75,0x21};
inline constexpr uint32_t kDownloadCallback=0x42040,kNativeDownloadCallback=0x42420;
inline constexpr char kOldTooltip[]="View the online high scores for this boardset. (Online scores are not available for community content)";
inline constexpr char kNewTooltip[]="View the online high scores for this boardset (if available).";
static_assert(sizeof(kNewTooltip)<=sizeof(kOldTooltip));

inline int __cdecl LookupSource() noexcept {
  // Project only this eligibility check. The selector's actual source, paths,
  // icons and saved-game verification remain owned by the original game.
  const int source=reinterpret_cast<int (__cdecl*)()>(game+0x9d4f0)();
  return static_cast<uint32_t>(source)<=2?0:source;
}
inline void __fastcall Downloaded(uint8_t* leaderboard,void*,const uint8_t* result,bool ioFailure) {
  auto* range=*reinterpret_cast<int**>(leaderboard+0x60);
  // Steam returns no entries around a player who has never posted a score.
  // Retain the pending request and UI callback, and let the next native tick
  // fetch the top 15 instead. Changing the range to global makes this a single
  // retry, including when the entire leaderboard is empty.
  if(!ioFailure&&result&&*reinterpret_cast<int*>(leaderboard+0x34)==3&&range&&range[0]==1&&
     *reinterpret_cast<const uint64_t*>(result)==*reinterpret_cast<uint64_t*>(leaderboard+0x20)&&
     *reinterpret_cast<const int*>(result+0x10)==0) {
    range[0]=0;range[1]=1;range[2]=15;
    *reinterpret_cast<int*>(leaderboard+0x3c)=3;
    *reinterpret_cast<int*>(leaderboard+0x34)=0;
    *reinterpret_cast<float*>(leaderboard+0x38)=0;
    return;
  }
  reinterpret_cast<void (__thiscall*)(void*,const void*,bool)>(game+kNativeDownloadCallback)(leaderboard,result,ioFailure);
}
inline void Verify(uint8_t* base) {
  const auto callback=reinterpret_cast<uintptr_t>(base+kNativeDownloadCallback);
  if(std::memcmp(base+kViewGate,kViewBytes,sizeof(kViewBytes))||
     std::memcmp(base+kPostGate,kPostBytes,sizeof(kPostBytes))||
     base[kDownloadCallback]!=0x68||std::memcmp(base+kDownloadCallback+1,&callback,4)||
     std::memcmp(base+kTooltip,kOldTooltip,sizeof(kOldTooltip)))
    throw std::runtime_error("Unexpected online leaderboard instructions");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LookupSource)-reinterpret_cast<uintptr_t>(base+kViewGate+5));
  WriteMemory(base+kViewGate+1,&relative,4);
  // The post-score prompt also compares the selected row's source inline.
  // Accept source 0 (game folder), 1 (Documents), and 2 (Workshop); reject
  // unknown/negative sources with an unsigned comparison. Custom-mode checks
  // still run. Steam's existing FindLeaderboard and async callbacks decide
  // whether a table exists before downloading or uploading any score.
  const uint8_t maximumSource=2,above=0x77;
  WriteMemory(base+kPostGate+7,&maximumSource,1);
  WriteMemory(base+kPostGate+8,&above,1);
  WriteMemory(base+kTooltip,kNewTooltip,sizeof(kNewTooltip));
  const auto callback=&Downloaded;
  WriteMemory(base+kDownloadCallback+1,&callback,sizeof(callback));
}
}
