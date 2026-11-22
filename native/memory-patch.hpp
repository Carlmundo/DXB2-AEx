#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace memory_patch {
inline void Write(void* destination,const void* bytes,size_t size) {
  DWORD old=0;
  if(!VirtualProtect(destination,size,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("Cannot protect executable hook");
  std::memcpy(destination,bytes,size);
  DWORD ignored=0;VirtualProtect(destination,size,old,&ignored);
  FlushInstructionCache(GetCurrentProcess(),destination,size);
}
}
