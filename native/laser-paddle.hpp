#pragma once
#include "patch.hpp"

namespace laser_paddle {
inline const int* screenHeight=nullptr;
inline void* expire=nullptr;
inline void* next=nullptr;
inline float half=0.5f;
inline uint8_t* game=nullptr;
inline void* clicked=nullptr;
inline void* noClick=nullptr;
inline void* fire=nullptr;
inline void* volley=nullptr;
inline uint32_t repeatDelayMs=200;
inline uint32_t lastShot=0;
inline bool hasShot=false;

// Intercept only the laser input gate. Actual clicks keep the native handler
// (including ball release); held left-click goes straight to laser firing.
// Native pause/transition guards and the six-shot limit remain in effect.
// Native clicks have no timed cooldown. Limit automatic repeats explicitly,
// using the native millisecond clock and recording actual firing opportunities.
inline __declspec(naked) void HeldFire() {
  __asm {
    jne click
    push eax
    mov eax,game
    cmp dword ptr [eax+1b93b0h],0
    je released
    cmp dword ptr [eax+1a40f4h],0
    jne skip
    cmp dword ptr [eax+1a714ch],0
    jne skip
    cmp dword ptr [eax+1a70cch],0
    jne skip
    cmp byte ptr [hasShot],0
    je ready
    mov eax,dword ptr [eax+1a70bch]
    sub eax,lastShot
    cmp eax,repeatDelayMs
    jb skip
  ready:
    pop eax
    jmp dword ptr [fire]
  released:
    mov byte ptr [hasShot],0
  skip:
    pop eax
    jmp dword ptr [noClick]
  click:
    jmp dword ptr [clicked]
  }
}

// Entered after the original powerup and capacity checks, for both manual and
// automatic shots. Preserve registers/flags and replay the displaced pushes.
inline __declspec(naked) void RecordVolley() {
  __asm {
    push eax
    mov eax,game
    mov eax,dword ptr [eax+1a70bch]
    mov lastShot,eax
    mov byte ptr [hasShot],1
    pop eax
    push 3fh
    push 0
    push ecx
    jmp dword ptr [volley]
  }
}

// Type-3 lasers are counted afresh each update (0045D525). With Thru Brick,
// permanent bricks reflect them, but native cleanup only checks y < 0.
// Expire shots below the playfield too, so normal object removal releases
// their firing slots. Keep reflections and the original top-edge check.
inline __declspec(naked) void CheckBottom() {
  __asm {
    jbe bottom
    jmp dword ptr [expire]
  bottom:
    push eax
    mov eax,screenHeight
    cvtsi2ss xmm6,dword ptr [eax]
    comiss xmm6,dword ptr [edi+10h]
    pop eax
    movss xmm6,half
    jb expired
    jmp dword ptr [next]
  expired:
    jmp dword ptr [expire]
  }
}
inline void Verify(uint8_t* base,bool holdClickFire=false) {
  const uint8_t branch[]={0x0f,0x86,0x3b,0x14,0x00,0x00,0xc7,0x47,0x5c,0x01,0x00,0x00,0x00};
  if(std::memcmp(base+0x5d6bb,branch,sizeof(branch)))
    throw std::runtime_error("Unexpected laser offscreen cleanup branch");
  const uint8_t inputBranch[]={0x0f,0x84,0xbf,0x01,0x00,0x00};
  if(holdClickFire&&std::memcmp(base+0x5cb45,inputBranch,sizeof(inputBranch)))
    throw std::runtime_error("Unexpected laser firing input branch");
  const uint8_t volleyStart[]={0x6a,0x3f,0x6a,0x00,0x51};
  if(holdClickFire&&std::memcmp(base+0x5cb8e,volleyStart,sizeof(volleyStart)))
    throw std::runtime_error("Unexpected laser volley entry");
}
inline void Install(uint8_t* base,bool holdClickFire=false) {
  Verify(base,holdClickFire);screenHeight=reinterpret_cast<const int*>(base+0x1ba630);
  expire=base+0x5d6c1;next=base+0x5eafc;
  JumpTo(base,0x5d6bb,reinterpret_cast<void*>(&CheckBottom));
  const uint8_t nop=0x90;WriteMemory(base+0x5d6c0,&nop,1);
  if(holdClickFire) {
    game=base;clicked=base+0x5cb4b;noClick=base+0x5cd0a;fire=base+0x5cb74;
    volley=base+0x5cb93;hasShot=false;
    JumpTo(base,0x5cb45,reinterpret_cast<void*>(&HeldFire));
    WriteMemory(base+0x5cb4a,&nop,1);
    JumpTo(base,0x5cb8e,reinterpret_cast<void*>(&RecordVolley));
  }
}
}
