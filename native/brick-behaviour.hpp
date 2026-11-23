#pragma once
#include "patch.hpp"

namespace brick_behaviour {
inline uint8_t* game=nullptr;
inline void* reveal=nullptr;
inline void* visible=nullptr;
inline void* zapNext=nullptr;
inline constexpr uint32_t multiplyCalls[]={0x83788,0x837ba,0x837e6};

// Inside HitBrick (00485B90), the native locals already distinguish explosion
// objects (-38h) and fireball damage (-3Ch). Let those hits use the same path
// as revealed bricks, retaining native removal, scoring and chain reactions.
// Normal balls and lasers must still clear the hidden flag on their first hit.
inline __declspec(naked) void HiddenHit() {
  __asm {
    test al,1
    jz destroy
    cmp dword ptr [ebp-38h],0
    jne destroy
    cmp dword ptr [ebp-3Ch],0
    jne destroy
    jmp dword ptr [reveal]
  destroy:
    jmp dword ptr [visible]
  }
}
inline void __cdecl MoveBrick(int x,int y,int id,int layer) {
  using GetCell=uint32_t* (__thiscall*)(void*,int,int,int);
  const auto cell=reinterpret_cast<GetCell>(game+0x85890);
  auto* source=cell(game+0x1bc708,x,y-1,layer);
  auto* destination=cell(game+0x1bc708,x,y,layer);
  // Native Falling Bricks moves only the ID. Transfer the hidden bit before
  // its destination redraw, and clear it before the vacated cell is reused.
  // Other cell flags and metadata retain their existing native behavior.
  destination[2]=(destination[2]&~2u)|(source[2]&2u);
  source[2]&=~2u;
  using SetBrick=void (__cdecl*)(int,int,int,int);
  reinterpret_cast<SetBrick>(game+0x8c1a0)(x,y,id,layer);
}
inline bool __fastcall MultiplyBrick(void* boardset,void*,int x,int y,int id,int layer) {
  using GetCell=uint32_t* (__thiscall*)(void*,int,int,int);
  auto* cell=reinterpret_cast<GetCell>(game+0x85890)(boardset,x,y,layer);
  using BrickType=int (__thiscall*)(void*,int);
  // Preserve switch bricks, including their flags and script metadata. The
  // definition's type identifies a switch; its board-specific ID can vary.
  if(cell[1]&&reinterpret_cast<BrickType>(game+0x858f0)(boardset,cell[1])==9)return false;
  // Multiply Exploding replaces the brick ID but leaves the destination's
  // flags in place. Its new exploding brick must not inherit invisibility.
  cell[2]&=~2u;
  using SetBrick=bool (__thiscall*)(void*,int,int,int,int);
  return reinterpret_cast<SetBrick>(game+0x8adf0)(boardset,x,y,id,layer);
}
// Zap Bricks already visits every cell on visible layers. Reveal each cell
// and set its native changed flag so even a reveal-only zap redraws the board.
// Replay the displaced brick-definition pointer load before continuing.
inline __declspec(naked) void ZapReveal() {
  __asm {
    test byte ptr [eax+8],2
    jz unchanged
    and dword ptr [eax+8],0fffffffdh
    mov byte ptr [esp+13h],1
  unchanged:
    mov ebp,game
    mov ebp,dword ptr [ebp+1bee6ch]
    jmp dword ptr [zapNext]
  }
}
inline void Verify(uint8_t* base) {
  const uint8_t branch[]={0xa8,0x01,0x0f,0x84,0xae,0x01,0x00,0x00};
  if(std::memcmp(base+0x85f40,branch,sizeof(branch)))
    throw std::runtime_error("Unexpected invisible-brick hit branch");
  const uint8_t call[]={0xe8,0x76,0xfa,0x02,0x00};
  if(std::memcmp(base+0x5c725,call,sizeof(call)))
    throw std::runtime_error("Unexpected Falling Bricks destination call");
  uint8_t zapLoad[]={0x8b,0x2d,0,0,0,0};
  const auto definitions=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base+0x1bee6c));
  std::memcpy(zapLoad+2,&definitions,sizeof(definitions));
  if(std::memcmp(base+0x8c27b,zapLoad,sizeof(zapLoad)))
    throw std::runtime_error("Unexpected Zap Bricks cell update");
  for(const auto rva:multiplyCalls) {
    uint8_t expected[]={0xe8,0,0,0,0};
    const uint32_t relative=0x8adf0-(rva+5);
    std::memcpy(expected+1,&relative,sizeof(relative));
    if(std::memcmp(base+rva,expected,sizeof(expected)))
      throw std::runtime_error("Unexpected Multiply Exploding brick update");
  }
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;reveal=base+0x85f48;visible=base+0x860f6;
  JumpTo(base,0x85f40,reinterpret_cast<void*>(&HiddenHit));
  const uint8_t nops[]={0x90,0x90,0x90};WriteMemory(base+0x85f45,nops,sizeof(nops));
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&MoveBrick)-reinterpret_cast<uintptr_t>(base+0x5c72a));
  WriteMemory(base+0x5c726,&relative,sizeof(relative));
  zapNext=base+0x8c281;
  JumpTo(base,0x8c27b,reinterpret_cast<void*>(&ZapReveal));
  const uint8_t nop=0x90;WriteMemory(base+0x8c280,&nop,1);
  for(const auto rva:multiplyCalls) {
    const auto target=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&MultiplyBrick)-reinterpret_cast<uintptr_t>(base+rva+5));
    WriteMemory(base+rva+1,&target,sizeof(target));
  }
}
}
