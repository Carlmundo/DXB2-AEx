#pragma once
#include "patch.hpp"

inline constexpr uint32_t kNostatResetRva=0x4de72;
inline constexpr size_t kNostatResetSize=13;

inline void VerifyNostatSoundOnly(uint8_t* base) {
  // The sound call and its stack cleanup immediately precede the reset block.
  const uint8_t sound[]={0xe8,0x21,0xb6,0x03,0x00,0x83,0xc4,0x10};
  uint8_t reset[]={0x8b,0x0d,0,0,0,0,0x8b,0x01,0x6a,0x01,0xff,0x50,0x38};
  const auto wrapper=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base+0x1d2a38));
  std::memcpy(reset+2,&wrapper,4);
  if(std::memcmp(base+0x4de6a,sound,sizeof(sound))!=0||
     std::memcmp(base+kNostatResetRva,reset,sizeof(reset))!=0)
    throw std::runtime_error("Unexpected nostat command handler");
}

inline void InstallNostatSoundOnly(uint8_t* base) {
  VerifyNostatSoundOnly(base);
  // Remove the entire wrapper lookup, argument push, and virtual reset call.
  // Removing just the call would leave its callee-cleaned argument on the stack.
  uint8_t nops[kNostatResetSize];std::memset(nops,0x90,sizeof(nops));
  WriteMemory(base+kNostatResetRva,nops,sizeof(nops));
}
