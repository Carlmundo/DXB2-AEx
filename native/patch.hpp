#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

struct OperandPatch { uint32_t rva, before, after; };
#include "patches.h"

inline void WriteMemory(void* where, const void* bytes, size_t size) {
  DWORD old;
  if(!VirtualProtect(where,size,PAGE_EXECUTE_READWRITE,&old)) throw std::runtime_error("VirtualProtect failed");
  std::memcpy(where,bytes,size);
  DWORD ignored;
  VirtualProtect(where,size,old,&ignored);
  FlushInstructionCache(GetCurrentProcess(),where,size);
}

inline void ApplyGridPatches(uint8_t* base) {
  // Check the complete manifest before writing any bytes.
  for(const auto& patch:kGridPatches) {
    uint32_t value;std::memcpy(&value,base+patch.rva,4);
    if(value!=patch.before) {
      char message[128];sprintf_s(message,"Unexpected instruction operand at RVA %08X",patch.rva);
      throw std::runtime_error(message);
    }
  }
  for(const auto& patch:kGridPatches)WriteMemory(base+patch.rva,&patch.after,4);
}

inline void JumpTo(uint8_t* base,uint32_t rva,void* destination) {
  uint8_t bytes[5]={0xe9};
  const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(destination)-reinterpret_cast<uintptr_t>(base+rva+5));
  std::memcpy(bytes+1,&relative,4);WriteMemory(base+rva,bytes,5);
}
