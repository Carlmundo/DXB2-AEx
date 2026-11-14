#pragma once
#include "patch.hpp"

// The splash update at RVA 0x99a00 normally waits for input or five seconds.
// Take its existing exit to title state 2 before it draws the Longbow logo.
inline void VerifySkipLongbow(uint8_t* base) {
  const uint8_t expected[]={0x0f,0x85,0x09,0x01,0x00,0x00};
  if(std::memcmp(base+0x99a22,expected,sizeof(expected))!=0)
    throw std::runtime_error("Unexpected Longbow splash branch");
}

inline void InstallSkipLongbow(uint8_t* base) {
  VerifySkipLongbow(base);
  const uint8_t jump[]={0xe9,0x0a,0x01,0x00,0x00,0x90};
  WriteMemory(base+0x99a22,jump,sizeof(jump));
}
