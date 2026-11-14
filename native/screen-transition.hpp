#pragma once
#include "patch.hpp"

inline void VerifyNoScreenTransition(uint8_t* base) {
  // sub eax,[transition start time]; the absolute operand is relocated.
  uint8_t expected[]={0x2b,0x05,0,0,0,0};
  const auto start=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base+0x1a71a4));
  std::memcpy(expected+2,&start,4);
  const float duration=2000.0f;
  if(std::memcmp(base+0x9bad8,expected,sizeof(expected))!=0||
     std::memcmp(base+0x1616b0,&duration,sizeof(duration))!=0)
    throw std::runtime_error("Unexpected screen transition timing");
}

inline void InstallNoScreenTransition(uint8_t* base) {
  VerifyNoScreenTransition(base);
  // Force elapsed time to 2001 ms, just past the 2000 ms carpet effect.
  // The original renderer draws the finished screen and completes its audio
  // fades/cleanup in one update, including the inline life-loss transition.
  const uint8_t elapsed[]={0xb8,0xd1,0x07,0x00,0x00,0x90};
  WriteMemory(base+0x9bad8,elapsed,sizeof(elapsed));
}
