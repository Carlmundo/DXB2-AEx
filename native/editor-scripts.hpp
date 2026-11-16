#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <richedit.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#pragma comment(lib,"bcrypt.lib")

namespace editor_scripts {
inline constexpr int kBufferBytes=1024*1024;
inline constexpr int kMaxTextBytes=kBufferBytes-1;
inline uint8_t* base=nullptr;
inline DLGPROC originalDialog=nullptr;
inline thread_local bool updating=false;

inline bool VerifiedExecutable(const wchar_t* path) noexcept {
  constexpr unsigned char expected[][32]={
    {0x53,0xce,0x81,0x49,0x38,0x9b,0xe5,0xef,0x31,0x87,0xa4,0xe2,0xc8,0x81,0xaf,0x81,0x6e,0x72,0x03,0xa5,0xd8,0x2b,0xb6,0x61,0x4f,0x02,0xc2,0xd4,0x5e,0xb9,0x88,0x0e},
    {0x3c,0x2d,0x51,0x1e,0x72,0xcd,0x51,0xe8,0x9a,0x9d,0x75,0x16,0xbd,0x8a,0xd0,0x05,0x36,0x24,0x75,0xf5,0x92,0xed,0x8b,0x5a,0x46,0xb7,0x0c,0xf3,0xb8,0xc3,0xaf,0x3f}
  };
  HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  unsigned char digest[32];bool ok=false;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&
     BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
    unsigned char bytes[65536];DWORD count=0;bool readOk=true;
    for(;;) {
      if(!ReadFile(file,bytes,sizeof(bytes),&count,nullptr)){readOk=false;break;}
      if(!count)break;
      if(BCryptHashData(hash,bytes,count,0)<0){readOk=false;break;}
    }
    if(readOk&&BCryptFinishHash(hash,digest,sizeof(digest),0)>=0)
      for(const auto& candidate:expected)if(std::memcmp(candidate,digest,sizeof(digest))==0)ok=true;
  }
  if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
  CloseHandle(file);return ok;
}

inline void Write(void* destination,const void* bytes,size_t size) {
  DWORD old=0;
  if(!VirtualProtect(destination,size,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("Cannot protect editor script hook");
  std::memcpy(destination,bytes,size);
  DWORD ignored=0;VirtualProtect(destination,size,old,&ignored);
  FlushInstructionCache(GetCurrentProcess(),destination,size);
}
inline void Jump(uint8_t* at,const void* destination) {
  uint8_t bytes[5]={0xe9};
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(destination)-reinterpret_cast<uintptr_t>(at+5));
  std::memcpy(bytes+1,&relative,4);Write(at,bytes,sizeof(bytes));
}

// Use the editor's own string allocator/assignment routine. Passing a DLL's
// std::string across CRT boundaries would give the editor ownership of foreign memory.
inline void* Script(int kind) {
  return reinterpret_cast<void* (__thiscall*)(void*,int)>(base+0x32480)(base+0x555f0,kind);
}
inline size_t Length(const void* script) {return *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(script)+0x10);}
inline const char* Text(const void* script) {
  const auto* bytes=static_cast<const uint8_t*>(script);
  return *reinterpret_cast<const uint32_t*>(bytes+0x14)>=16?*reinterpret_cast<const char* const*>(bytes):reinterpret_cast<const char*>(bytes);
}
inline int Kind(HWND dialog) {
  const auto* dialogs=reinterpret_cast<const HWND*>(base+0x51e3c);
  for(int i=0;i<5;i++)if(dialogs[i]==dialog)return i;
  return -1;
}
struct Updating {
  const bool previous=updating;
  Updating(){updating=true;}
  ~Updating(){updating=previous;}
};

inline void Highlight(HWND control,int kind) {
  // The original formatter saves the caret using the packed 16-bit EM_GETSEL
  // return value. Preserve the full selection for long scripts.
  CHARRANGE selection{};POINT scroll{};
  SendMessageA(control,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
  const auto firstVisibleLine=SendMessageA(control,EM_GETFIRSTVISIBLELINE,0,0);
  SendMessageA(control,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  SendMessageA(control,WM_SETREDRAW,FALSE,0);
  reinterpret_cast<void (__cdecl*)(HWND,int)>(base+0x33b40)(control,kind);
  SendMessageA(control,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
  // EM_GETSCROLLPOS returns 16-bit pixel coordinates even in POINT's 32-bit
  // fields. Restore horizontal scrolling, then recover the vertical viewport
  // using line indices so it cannot wrap at 65,536 pixels.
  SendMessageA(control,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  // A long scroll from a rounded pixel offset can land one line away; check
  // the resulting line and correct that rounding before repainting.
  for(int attempt=0;attempt<2;attempt++) {
    const auto currentFirstLine=SendMessageA(control,EM_GETFIRSTVISIBLELINE,0,0);
    if(currentFirstLine==firstVisibleLine)break;
    SendMessageA(control,EM_LINESCROLL,0,firstVisibleLine-currentFirstLine);
  }
  SendMessageA(control,WM_SETREDRAW,TRUE,0);
  InvalidateRect(control,nullptr,FALSE);
}

inline void Changed(HWND dialog,HWND control,int kind) {
  Updating guard;
  void* script=Script(kind);
  try {
    const int length=GetWindowTextLengthA(control);
    if(length>kMaxTextBytes) {
      // Rich Edit counts characters, while the boardset stores ANSI bytes.
      // CRLF/DBCS input can exceed the byte budget before the control's own
      // limit is reached. Reject that edit in full, preserving the last script.
      SetWindowTextA(control,Text(script));MessageBeep(MB_ICONWARNING);return;
    }
    std::vector<char> buffer(static_cast<size_t>(length)+1);
    const int copied=GetWindowTextA(control,buffer.data(),static_cast<int>(buffer.size()));
    if(copied!=length)throw std::runtime_error("Cannot read the complete script. The previous script has been restored.");
    if(Length(script)!=static_cast<size_t>(copied)||std::memcmp(Text(script),buffer.data(),copied)!=0) {
      reinterpret_cast<void* (__thiscall*)(void*,const char*,size_t)>(base+0xe7b0)(script,buffer.data(),copied);
      if(!base[0x51dd6]) {
        base[0x51dd6]=1;
        reinterpret_cast<void (__cdecl*)()>(base+0x39500)();
      }
    }
    Highlight(control,kind);
  }catch(const std::exception& error) {
    SetWindowTextA(control,Text(script));
    MessageBoxA(dialog,error.what(),"DX-Ball 2 editor script buffer",MB_OK|MB_ICONERROR);
  }
}

inline INT_PTR CALLBACK Dialog(HWND dialog,UINT message,WPARAM wparam,LPARAM lparam) {
  if(message==WM_COMMAND&&LOWORD(wparam)==0x40a&&HIWORD(wparam)==EN_CHANGE) {
    if(updating)return TRUE;
    const int kind=Kind(dialog);
    if(kind>=0){Changed(dialog,reinterpret_cast<HWND>(lparam),kind);return TRUE;}
  }
  if(message==WM_INITDIALOG) {
    Updating guard;
    const auto control=GetDlgItem(dialog,0x40a);
    SendMessageA(control,EM_EXLIMITTEXT,0,kMaxTextBytes);
    return originalDialog(dialog,message,wparam,lparam);
  }
  return originalDialog(dialog,message,wparam,lparam);
}

inline void Verify(uint8_t* image) {
  constexpr uint8_t entry[]={0x55,0x8b,0xec,0x6a,0xff};
  constexpr uint8_t oldRead[]={0x68,0x00,0x40,0x00,0x00,0x8d,0x85,0xf0,0xbf,0xff,0xff,0x50,0x53,0xff,0x15};
  constexpr uint8_t getScript[]={0x55,0x8b,0xec,0x8b,0xc1,0x83,0xec,0x0c};
  constexpr uint8_t assign[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x56,0x8b,0xf1};
  constexpr uint8_t format[]={0x55,0x8b,0xec,0x83,0xec,0x30};
  if(std::memcmp(image+0x32f20,entry,sizeof(entry))||std::memcmp(image+0x3346f,oldRead,sizeof(oldRead))||
     std::memcmp(image+0x32480,getScript,sizeof(getScript))||std::memcmp(image+0xe7b0,assign,sizeof(assign))||
     std::memcmp(image+0x33b40,format,sizeof(format)))throw std::runtime_error("Unsupported editor script instructions");
}
inline void Install(uint8_t* image) {
  Verify(image);
  auto* trampoline=static_cast<uint8_t*>(VirtualAlloc(nullptr,10,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  if(!trampoline)throw std::runtime_error("Cannot allocate editor script trampoline");
  std::memcpy(trampoline,image+0x32f20,5);
  trampoline[5]=0xe9;
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image+0x32f25)-reinterpret_cast<uintptr_t>(trampoline+10));
  std::memcpy(trampoline+6,&relative,4);
  DWORD old=0;
  if(!VirtualProtect(trampoline,10,PAGE_EXECUTE_READ,&old)){VirtualFree(trampoline,0,MEM_RELEASE);throw std::runtime_error("Cannot protect editor script trampoline");}
  FlushInstructionCache(GetCurrentProcess(),trampoline,10);
  base=image;originalDialog=reinterpret_cast<DLGPROC>(trampoline);
  try {Jump(image+0x32f20,reinterpret_cast<const void*>(&Dialog));}
  catch(...) {base=nullptr;originalDialog=nullptr;VirtualFree(trampoline,0,MEM_RELEASE);throw;}
}
}
