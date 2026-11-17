#pragma once
#include "patch.hpp"

inline void VerifyLivesCounter(uint8_t* base) {
  const uint8_t reservedSpacing[]={0x83,0xc1,0x04}; // add ecx,4
  const uint8_t drawnSpacing[]={0x83,0xc0,0x06}; // add eax,6
  if(std::memcmp(base+0x5fb78,reservedSpacing,sizeof(reservedSpacing))||
     std::memcmp(base+0x5fc6b,drawnSpacing,sizeof(drawnSpacing)))
    throw std::runtime_error("Unexpected lives counter spacing");
}

inline void InstallLivesCounter(uint8_t* base) {
  VerifyLivesCounter(base);
  // The background restore and screen update share this width. Match the
  // actual icon stride so the leftmost paddle is included at higher counts.
  const uint8_t spacing=6;WriteMemory(base+0x5fb7a,&spacing,1);
}
