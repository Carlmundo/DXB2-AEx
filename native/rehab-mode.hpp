#pragma once
#include "patch.hpp"

namespace rehab_mode {
inline bool enabled=false;
inline constexpr uint32_t kFlag=0x1b91dc,kSpeed=0x1a40c4,kSaveEligibility=0x8f16e;
// All eight consumers of the retained 1.3.2 mode: moving-ball speed, life
// loss, new-life speed, eight-ball speed, new-board speed, speed clamping,
// player-switch speed, and entry into the local/online score state machine.
inline constexpr uint32_t kConsumers[]={0x5d6d3,0x62473,0x62e0a,0x83c75,0x8a67b,0x8b85d,0x91494,0x960b9};
inline void Verify(uint8_t* base) {
  for(size_t i=0;i<sizeof(kConsumers)/sizeof(*kConsumers);++i) {
    const auto rva=kConsumers[i];
    const uint8_t prefix=i==2?0x05:(i==4||i==6?0x0d:0x3d);
    const uint8_t opcode=prefix==0x3d?0x83:0x39;
    const auto address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base+kFlag));
    if(base[rva]!=opcode||base[rva+1]!=prefix||std::memcmp(base+rva+2,&address,4)||
       (opcode==0x83&&base[rva+6]!=0))
      throw std::runtime_error("Unexpected RehabMode consumer");
  }
  for(uint32_t key=0;key<10;++key) {
    const auto rva=0x4e164+15*key;
    const auto address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base+kSpeed));
    const float speed=25.0f*(key+1);
    if(base[rva]!=0xc7||base[rva+1]!=0x05||std::memcmp(base+rva+2,&address,4)||
       std::memcmp(base+rva+6,&speed,4))throw std::runtime_error("Unexpected RehabMode number-key handler");
  }
  const uint8_t eligible[]={0xb8,1,0,0,0};
  const float defaultSpeed=100.0f;
  if(*reinterpret_cast<const int*>(base+kFlag)!=0||std::memcmp(base+kSpeed,&defaultSpeed,4)||
     std::memcmp(base+kSaveEligibility,eligible,sizeof(eligible)))
    throw std::runtime_error("Unexpected RehabMode initialization or save eligibility");
}
inline void Install(uint8_t* base) {
  Verify(base);
  // The original saved-game eligibility bit did not know about the dormant
  // mode. Save Rehab sessions as ineligible so restarting with RehabMode=0
  // cannot turn their scores into local records or Steam submissions.
  const uint8_t ineligible[]={0x33,0xc0,0x90,0x90,0x90};
  WriteMemory(base+kSaveEligibility,ineligible,sizeof(ineligible));
  const int on=1;WriteMemory(base+kFlag,&on,sizeof(on));
  enabled=true;
}
}
