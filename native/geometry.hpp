#pragma once
#include "patch.hpp"
#include <array>
struct GeometryPatch { uint32_t rva;size_t size;uint8_t original[8],rival[8];bool halfCellAddress; };
struct CoordinateHook { uint32_t rva;size_t size,skip;int reg,kind;uint8_t original[16];int absolute,relative; };
#include "geometry.h"
inline bool gRivalGeometry=false;
inline int __cdecl ScaleCoordinate(int triple,int kind) {
  if(!gRivalGeometry)return kind==0?triple*16:triple*2;
  if(kind==1)return kRivalFieldWidth/8; // Followed by a scale of 8.
  if(kind==2)return (triple-kRivalColumns*24)*2+kRivalFieldWidth; // Background clip including margin.
  return (triple*(kRivalFieldWidth/3)+(triple>=0?kRivalColumns/2:-kRivalColumns/2))/kRivalColumns;
}
inline int PlayfieldWidth(int columns) {return columns==kRivalColumns?kRivalFieldWidth:columns*48;}
struct Geometry {
  uint8_t* base=nullptr;bool rival=false;
  uint8_t* operands[sizeof(kGeometryPatches)/sizeof(kGeometryPatches[0])]={};
  std::array<uint8_t,8> Value(const GeometryPatch& patch,bool next) const {
    std::array<uint8_t,8> value{};std::memcpy(value.data(),next?patch.rival:patch.original,patch.size);
    if(patch.halfCellAddress) {
      uint32_t address;
      if(next)address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&kRivalHalfCell));
      else {std::memcpy(&address,patch.original,4);address+=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)-0x400000);}
      std::memcpy(value.data(),&address,4);
    }
    return value;
  }
  void Verify() const {
    for(const auto& patch:kGeometryPatches) {
      const auto expected=Value(patch,false);
      if(std::memcmp(base+patch.rva,expected.data(),patch.size))throw std::runtime_error("Unexpected geometry operand");
    }
    for(const auto& hook:kCoordinateHooks) {
      uint8_t expected[16];std::memcpy(expected,hook.original,hook.size);
      if(hook.absolute>=0)*reinterpret_cast<uint32_t*>(expected+hook.absolute)+=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)-0x400000);
      if(std::memcmp(base+hook.rva,expected,hook.size))throw std::runtime_error("Unexpected coordinate hook");
    }
  }
  void Install() {
    Verify();
    for(size_t i=0;i<sizeof(kGeometryPatches)/sizeof(kGeometryPatches[0]);i++)operands[i]=base+kGeometryPatches[i].rva;
    auto* thunks=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!thunks)throw std::runtime_error("Cannot allocate coordinate hooks");
    size_t used=0;
    for(const auto& hook:kCoordinateHooks) {
      auto* out=thunks+used;size_t n=0;
      const auto byte=[&](uint8_t b){out[n++]=b;};
      const auto word=[&](uint32_t w){std::memcpy(out+n,&w,4);n+=4;};
      const auto branch=[&](uint8_t op,uintptr_t target){byte(op);word(static_cast<uint32_t>(target-reinterpret_cast<uintptr_t>(out+n+4)));};
      // Preserve all integer registers; the integer-only callback leaves SIMD
      // registers untouched. Store its result in the saved target register.
      byte(0x9c);byte(0x60);
      byte(0x6a);byte(static_cast<uint8_t>(hook.kind));
      byte(0xff);byte(0x74);byte(0x24);byte(static_cast<uint8_t>((7-hook.reg)*4+4));
      branch(0xe8,reinterpret_cast<uintptr_t>(&ScaleCoordinate));
      byte(0x83);byte(0xc4);byte(8);
      byte(0x89);byte(0x44);byte(0x24);byte(static_cast<uint8_t>((7-hook.reg)*4));
      byte(0x61);byte(0x9d);
      const auto tail=n;
      std::memcpy(out+n,base+hook.rva+hook.skip,hook.size-hook.skip);n+=hook.size-hook.skip;
      for(size_t i=0;i<sizeof(kGeometryPatches)/sizeof(kGeometryPatches[0]);i++) {
        const auto& patch=kGeometryPatches[i];
        if(patch.rva>=hook.rva+hook.skip&&patch.rva+patch.size<=hook.rva+hook.size)
          operands[i]=out+tail+patch.rva-hook.rva-hook.skip;
      }
      if(hook.relative>=0) {
        auto* original=base+hook.rva+hook.relative;
        const auto target=reinterpret_cast<uintptr_t>(original+4)+*reinterpret_cast<int32_t*>(original);
        *reinterpret_cast<uint32_t*>(out+tail+hook.relative-hook.skip)=static_cast<uint32_t>(target-reinterpret_cast<uintptr_t>(out+tail+hook.relative-hook.skip+4));
      }
      branch(0xe9,reinterpret_cast<uintptr_t>(base+hook.rva+hook.size));
      used+=64;
    }
    DWORD old;if(!VirtualProtect(thunks,4096,PAGE_EXECUTE_READ,&old))throw std::runtime_error("Cannot protect coordinate hooks");
    FlushInstructionCache(GetCurrentProcess(),thunks,4096);
    used=0;
    for(const auto& hook:kCoordinateHooks) {
      JumpTo(base,hook.rva,thunks+used);
      if(hook.size>5){const uint8_t nops[16]={0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};WriteMemory(base+hook.rva+5,nops,hook.size-5);}
      used+=64;
    }
  }
  void Set(bool next) {
    if(next==rival)return;
    for(size_t i=0;i<sizeof(kGeometryPatches)/sizeof(kGeometryPatches[0]);i++) {
      const auto& patch=kGeometryPatches[i];const auto value=Value(patch,next);WriteMemory(operands[i],value.data(),patch.size);
    }
    rival=next;
    gRivalGeometry=next;
  }
};
