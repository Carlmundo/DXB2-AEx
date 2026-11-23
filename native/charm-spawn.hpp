#pragma once
#include "patch.hpp"
#include <array>

namespace charm_spawn {
inline uint8_t* game=nullptr;
inline constexpr int columns=37,rows=20,layerBytes=0x2e54;
// Extra rows represent the open space above and below the brick grid. The
// native grid begins 40 pixels below the ceiling; row zero is not a top wall.
struct Space {
  int width=0;
  bool enabled=false;
  std::array<uint8_t,columns*(rows+2)> cells{};
  int Index(int x,int y) const {return (y+1)*width+x;}
  bool Reachable(int x,int y) const {
    return x>=0&&x<width&&y>=-1&&y<=rows&&cells[Index(x,y)]==2;
  }
  void Build() {
    width=*reinterpret_cast<int*>(game+0x1bedb4);
    if(width<1||width>columns)return;
    auto* board=*reinterpret_cast<uint8_t**>(game+0x1bc708)+
      *reinterpret_cast<int*>(game+0x1bedb0)*0x5c;
    auto* first=*reinterpret_cast<uint8_t**>(board+8);
    auto* end=*reinterpret_cast<uint8_t**>(board+12);
    using Type=int (__thiscall*)(void*,int);
    const auto type=reinterpret_cast<Type>(game+0x858f0);
    const auto permanent=[&](const uint32_t* cell) {
      return cell[1]&&type(game+0x1bc708,cell[1])==7;
    };
    // Count actual bricks, including inactive layers, without counting wrapped
    // layer positions twice. Definition type 7 identifies permanent bricks;
    // their IDs vary between brick artwork sets.
    int count=0;
    for(auto* layer=first;layer<end&&count<4;layer+=layerBytes)
      for(int x=0;x<width&&count<4;x++)for(int y=0;y<rows&&count<4;y++)
        if(permanent(reinterpret_cast<uint32_t*>(layer+4+(x*rows+y)*16)))++count;
    if(count<4)return;
    enabled=true;
    using Cell=uint32_t* (__thiscall*)(void*,int,int,bool);
    const auto cell=reinterpret_cast<Cell>(game+0x54560);
    for(auto* layer=first;layer<end;layer+=layerBytes) {
      if(!layer[layerBytes-0x10])continue;
      for(int x=0;x<width;x++)for(int y=0;y<rows;y++)
        // The native accessor applies current offsets and wrapping. Permanent
        // bricks obstruct the ball even when hidden or covered by other layers.
        if(permanent(cell(layer,x,y,false)))cells[Index(x,y)]=1;
    }
    // Ordinary destructible bricks are future paths. Four-way movement closes
    // diagonal corner contacts; a ball cannot pass through a zero-width gap.
    std::array<int,columns*(rows+2)> queue{};
    int begin=0,size=0;
    const auto visit=[&](int x,int y) {
      if(x<0||x>=width||y< -1||y>rows)return;
      const int index=Index(x,y);
      if(cells[index])return;
      cells[index]=2;queue[size++]=index;
    };
    for(int x=0;x<width;x++)visit(x,rows);
    while(begin<size) {
      const int index=queue[begin++],x=index%width,y=index/width-1;
      visit(x-1,y);visit(x+1,y);visit(x,y-1);visit(x,y+1);
    }
  }
};
inline const Space* active=nullptr;
inline void __cdecl Spawn() {
  Space space;space.Build();
  const auto* previous=active;active=&space;
  reinterpret_cast<void (__cdecl*)()>(game+0x81840)();
  active=previous;
}
inline bool __fastcall Suitable(void* boardset,void*,int x,int lowerRow) {
  using Empty=bool (__thiscall*)(void*,int,int);
  if(!reinterpret_cast<Empty>(game+0x559d0)(boardset,x,lowerRow))return false;
  // This replaces only the second of the native two-cell checks. Rejecting a
  // pocket continues the existing search, preserving RNG, rarity and retries.
  return !active||!active->enabled||
    (active->Reachable(x,lowerRow-1)&&active->Reachable(x,lowerRow));
}
inline void Verify(uint8_t* base) {
  const uint8_t spawn[]={0xe8,0x95,0x4a,0x02,0x00};
  const uint8_t space[]={0xe8,0xee,0x40,0xfd,0xff};
  if(std::memcmp(base+0x5cda6,spawn,sizeof(spawn))||
     std::memcmp(base+0x818dd,space,sizeof(space)))
    throw std::runtime_error("Unexpected Charm spawn calls");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;
  const auto replace=[&](uint32_t rva,void* destination) {
    const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(destination)-reinterpret_cast<uintptr_t>(base+rva+5));
    WriteMemory(base+rva+1,&relative,4);
  };
  replace(0x5cda6,reinterpret_cast<void*>(&Spawn));
  replace(0x818dd,reinterpret_cast<void*>(&Suitable));
}
}
