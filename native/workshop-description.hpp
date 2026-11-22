#pragma once
#include "executable-hash.hpp"
#include "memory-patch.hpp"
#include <cstdint>

namespace workshop_description {
inline constexpr int kMaxBytes=8000;
inline constexpr uint32_t kReadRva=0x11305,kResumeRva=0x1132a;
inline uint8_t* base=nullptr;

inline bool VerifiedExecutable(const wchar_t* path) noexcept {
  constexpr unsigned char expected[][32]={
    {0x76,0xe6,0x06,0xe2,0xb6,0x80,0xd3,0xa6,0x84,0x5f,0x25,0x66,0x19,0xf8,0xa6,0xb7,0x74,0xee,0x88,0xc9,0x38,0x8a,0x4b,0x8e,0xe1,0x51,0x8f,0x35,0x43,0x87,0x3d,0x4b}
  };
  return executable_hash::Matches(path,expected);
}

inline void __stdcall Capture(HWND dialog) {
  // The original dialog has only 256 bytes on its stack. Use our own buffer,
  // with a separate byte for the terminator, and the EXE's own string allocator.
  char text[kMaxBytes+1]={};
  GetDlgItemTextA(dialog,0x3ee,text,sizeof(text));
  reinterpret_cast<void* (__thiscall*)(void*,const char*)>(base+0x2b60)(base+0x20ab0,text);
}

inline void Verify(uint8_t* image) {
  // This block has an absolute string address, which Windows relocates.
  uint8_t expected[]={
    0x68,0x00,0x01,0x00,0x00,0x8d,0x85,0xf0,0xfe,0xff,0xff,0x50,
    0x68,0xee,0x03,0x00,0x00,0x57,0xff,0xd6,0x8d,0x85,0xf0,0xfe,0xff,0xff,
    0xb9,0xb0,0x0a,0x42,0x00,0x50,0xe8,0x36,0x18,0xff,0xff
  };
  const auto destination=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image+0x20ab0));
  std::memcpy(expected+27,&destination,sizeof(destination));
  constexpr uint8_t assign[]={0x55,0x8b,0xec,0x56,0x8b,0x75,0x08};
  if(std::memcmp(image+kReadRva,expected,sizeof(expected))||std::memcmp(image+0x2b60,assign,sizeof(assign)))
    throw std::runtime_error("Unsupported Workshop description instructions");
}

inline void Install(uint8_t* image) {
  Verify(image);
  uint8_t patch[kResumeRva-kReadRva];std::memset(patch,0x90,sizeof(patch));
  patch[0]=0x57; // push edi (dialog HWND)
  patch[1]=0xe8; // call Capture; __stdcall removes its argument
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Capture)-reinterpret_cast<uintptr_t>(image+kReadRva+6));
  std::memcpy(patch+2,&relative,sizeof(relative));
  base=image;memory_patch::Write(image+kReadRva,patch,sizeof(patch));
}
}
