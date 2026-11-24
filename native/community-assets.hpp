#pragma once
#include "patch.hpp"

namespace community_assets {
// These offsets locate the setting and search code inside the loaded game.
inline uint8_t* game=nullptr;
inline constexpr uint32_t kEnabled=0x1a4078,kNativeFind=0xb0f10;
inline constexpr uint32_t kFindCalls[]={0x5a992,0x5a9f7};

// The game looks for assets in Documents, then Workshop, then its install folders.
// We redirect only the Documents and Workshop searches through this function.
inline void* __fastcall Find(void* files,void*,void* output,const void* root,const void* name,bool recursive) {
  if(*reinterpret_cast<const int*>(game+kEnabled)) {
    // Community Content is on: let the game search this community folder as usual.
    using Native=void* (__thiscall*)(void*,void*,const void*,const void*,bool);
    return reinterpret_cast<Native>(game+kNativeFind)(files,output,root,name,recursive);
  }
  // Community Content is off: tell the game no community content was found
  // The game will continue looking in its install folders.
  // Build an empty string in the layout the game expects: length 0, capacity 7.
  // Fill the return storage directly; our DLL's C++ strings may use a different layout.
  const uint32_t empty[6]={0,0,0,0,0,7};
  std::memcpy(output,empty,sizeof(empty));return output;
}
inline void Verify(uint8_t* base) {
  // Check both calls still point to the expected game function before changing either.
  for(const auto rva:kFindCalls)
    if(base[rva]!=0xe8||base+rva+5+*reinterpret_cast<int32_t*>(base+rva+1)!=base+kNativeFind)
      throw std::runtime_error("Unexpected community asset search call");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;
  for(const auto rva:kFindCalls) {
    // Redirect this call to Find(). A call stores the distance from the next instruction.
    const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Find)-reinterpret_cast<uintptr_t>(base+rva+5));
    WriteMemory(base+rva+1,&relative,4);
  }
}
}
