#pragma once
#include "patch.hpp"

namespace community_leaderboards {
inline uint8_t* game=nullptr;
inline constexpr uint32_t kViewGate=0x9620d,kPostGate=0x96366,kTooltip=0x16a700;
inline constexpr uint8_t kViewBytes[]={0xe8,0xde,0x72,0,0,0x85,0xc0,0x75,0x68};
inline constexpr uint8_t kPostBytes[]={0x83,0xbc,0x30,0x9c,0,0,0,0,0x75,0x21};
inline constexpr char kOldTooltip[]="View the online high scores for this boardset. (Online scores are not available for community content)";
inline constexpr char kNewTooltip[]="View the online high scores for this boardset (if available).";
static_assert(sizeof(kNewTooltip)<=sizeof(kOldTooltip));

inline int __cdecl LookupSource() noexcept {
  // Project only this eligibility check. The selector's actual source, paths,
  // icons and saved-game verification remain owned by the original game.
  const int source=reinterpret_cast<int (__cdecl*)()>(game+0x9d4f0)();
  return static_cast<uint32_t>(source)<=2?0:source;
}
inline void Verify(uint8_t* base) {
  if(std::memcmp(base+kViewGate,kViewBytes,sizeof(kViewBytes))||
     std::memcmp(base+kPostGate,kPostBytes,sizeof(kPostBytes))||
     std::memcmp(base+kTooltip,kOldTooltip,sizeof(kOldTooltip)))
    throw std::runtime_error("Unexpected community leaderboard eligibility instructions");
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
}
}
