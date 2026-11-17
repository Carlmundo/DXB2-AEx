#pragma once
#include "geometry.hpp"
#include <algorithm>

namespace loaded_paddle {
inline uint8_t* game=nullptr;
template<class T> inline T& At(uint32_t rva) {return *reinterpret_cast<T*>(game+rva);}

inline void Initialize() {
  // The type-0 paddle is persistent and is not serialized with the moving
  // objects. Its normal update selects a sprite while paused, but returns
  // before positioning it (0045D145). Initialize it once, after save restore,
  // using the same frame and bounds as 0045D013..0045D378.
  const int powered=(At<int>(0x1a70e4)||At<int>(0x1a7104))?1:0;
  const int mode=(std::min)(2,powered+2*At<int>(0x1a70e8));
  const int frame=20*mode+4*(4-At<int>(0x1a70d8))+(At<int>(0x1a7180)/100)%4;
  auto* image=game+0x1cc7f0;
  if(frame>0&&frame<At<int>(0x1cd148)&&At<uint8_t*>(0x1cd140))
    image=At<uint8_t*>(0x1cd140)+(frame-1)*0x950;
  if(*reinterpret_cast<int*>(image+0x4c)>1&&*reinterpret_cast<uint8_t**>(image+0x48))
    image=*reinterpret_cast<uint8_t**>(image+0x48);
  const float width=static_cast<float>(*reinterpret_cast<int*>(image+0x1c));
  const float height=static_cast<float>(*reinterpret_cast<int*>(image+0x20));
  const float half=width*0.5f;
  const int columns=At<int>(0x1bedb4);
  using Margin=int (__thiscall*)(void*);
  const int margin=reinterpret_cast<Margin>(game+0x55a70)(game+0x1bc708);
  const float x=(std::min)((std::max)(static_cast<float>(At<int>(0x1b93a8)),margin+half),margin+PlayfieldWidth(columns)-half);
  const float y=At<float>(0x1616ac)-At<float>(0x1a7158);
  At<uint8_t*>(0x1a4144)=game+0x1cc7f0;
  At<int>(0x1a4148)=frame;
  At<float>(0x1a413c)=width;At<float>(0x1a4140)=height;
  At<float>(0x1a411c)=x;At<float>(0x1a4120)=y;
  At<float>(0x1a4124)=x;At<float>(0x1a4128)=y;
  At<float>(0x1a7140)=x;At<float>(0x1a7144)=y;At<float>(0x1a7148)=half;
}

inline void __cdecl PauseLoadedGame() {
  reinterpret_cast<void (__cdecl*)()>(game+0x89100)();
  Initialize();
}
inline void Verify(uint8_t* base) {
  const uint8_t call[]={0xe8,0xc5,0x06,0xff,0xff};
  if(std::memcmp(base+0x98a36,call,sizeof(call)))
    throw std::runtime_error("Unexpected loaded-game pause call");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&PauseLoadedGame)-reinterpret_cast<uintptr_t>(base+0x98a3b));
  WriteMemory(base+0x98a37,&relative,4);
}
}
