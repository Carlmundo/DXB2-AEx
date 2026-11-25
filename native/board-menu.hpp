#pragma once
#include "patch.hpp"
#include "board-format.hpp"
#include "aex-update.hpp"
#include <array>

namespace board_menu {
inline uint8_t* game=nullptr;
inline constexpr uint32_t kNativeOpen=0xb0a10;
inline constexpr uint32_t kLoadCall=0x94f40,kNativeLoad=0x55ce0,kAfterLoad=0x94f45,kReadCall=0x55e06,kNativeRead=0xb1500;
// The native menu passes this source category to its metadata callback.
enum class Source:int {Installed=0,Documents=1,Workshop=2};
// The Play menu enumerates install folders, Documents and Workshop separately.
// A zero result here skips the entry before its metadata is added to the list.
inline constexpr uint32_t kOpenCalls[]={0x97f0b,0x980a9,0x982aa};
struct Wide {
  union {wchar_t local[8];const wchar_t* heap;};
  uint32_t length,capacity;
  const wchar_t* text() const noexcept {return capacity<8?local:heap;}
};
static_assert(sizeof(Wide)==24);
struct Context {bool readFailed=false;};
inline thread_local Context* current=nullptr;
inline bool ReadFailed() noexcept {return current&&current->readFailed;}
inline decltype(&MessageBoxW) messageBox=&MessageBoxW;
#pragma comment(lib,"user32.lib")

struct Failure {std::wstring path;};
struct Group {
  std::vector<Failure> files;
  bool incomplete=false;
  bool multiple() const noexcept {return incomplete||files.size()>1;}
};
inline thread_local std::array<Group,3> pending;

inline const wchar_t* Advice(Source source,bool multiple=false) noexcept {
  switch(source) {
    case Source::Installed:return L"Try \"Verify integrity of game files\" in Steam to fix the problem.";
    case Source::Documents:return multiple?L"Consider removing the files.":L"Consider removing the file.";
    case Source::Workshop:return L"Consider unsubscribing or re-subscribing to fix the problem.";
    default:return L"";
  }
}
inline std::wstring Message(const Group& group,Source source) {
  const bool multiple=group.multiple();
  std::wstring message=multiple?L"Could not read board files:\n":L"Could not read board file:\n";
  for(size_t i=0;i<group.files.size();i++) {
    const auto& file=group.files[i];
    if(i)message+=L"\n";
    message+=file.path;
  }
  if(group.incomplete)message+=L"\nSome unreadable filenames could not be listed.";
  message+=multiple?L"\n\nThese files will be skipped. Other boardsets can still be played.":
    L"\n\nThis file will be skipped. Other boardsets can still be played.";
  const auto* advice=Advice(source,multiple);
  if(*advice){message+=L"\n";message+=advice;}
  return message;
}

inline void Report(const Wide& path,Source source) noexcept {
  const auto index=static_cast<int>(source);
  if(index<0||index>=3)return;
  auto& group=pending[index];
  try {
    const std::wstring filename(path.text(),path.length);
    for(const auto& file:group.files)if(file.path==filename)return;
    group.files.push_back({filename});
  }catch(...) {group.incomplete=true;}
}
inline void Flush() noexcept {
  // Called once after the native scan, including when every entry was skipped.
  // Clear first so closing a dialog cannot cause a previous scan to reappear.
  auto groups=std::move(pending);pending={};
  bool any=false;for(const auto& group:groups)if(!group.files.empty()||group.incomplete)any=true;
  if(!any)return;
  const HWND owner=aex_update::gFindGameWindow();
  std::wstring caption;
  try {
    const int length=owner?GetWindowTextLengthW(owner):0;
    if(length>0) {
      caption.resize(static_cast<size_t>(length)+1);
      caption.resize(GetWindowTextW(owner,caption.data(),length+1));
    }
  }catch(...) {caption.clear();}
  const auto* title=caption.empty()?L"DX-Ball 2: 20th Anniversary Edition":caption.c_str();
  const aex_update::DialogCursor cursor;
  constexpr UINT flags=MB_OK|MB_ICONERROR|MB_SETFOREGROUND|MB_TOPMOST;
  for(size_t i=0;i<groups.size();i++) {
    const auto& group=groups[i];if(group.files.empty()&&!group.incomplete)continue;
    try {
      const auto message=Message(group,static_cast<Source>(i));
      messageBox(owner,message.c_str(),title,flags);
    }catch(...) {
      const auto* fallback=group.files.empty()?L"Could not read board files.":group.files.front().path.c_str();
      messageBox(owner,fallback,title,flags);
    }
  }
}
template<Source source> inline void* __fastcall Open(void* files,void*,const Wide* path) noexcept {
  using NativeOpen=void* (__thiscall*)(void*,const Wide*);
  try {
    auto* result=reinterpret_cast<NativeOpen>(game+kNativeOpen)(files,path);
    if(!result)Report(*path,source);
    return result;
  }catch(...) {Report(*path,source);}
  return nullptr;
}
inline void* OpenTarget(size_t index) noexcept {
  // Opening fails before the native callback is invoked, so use the category
  // of the native enumeration loop containing each open call.
  static void* const targets[]={reinterpret_cast<void*>(&Open<Source::Installed>),
    reinterpret_cast<void*>(&Open<Source::Documents>),reinterpret_cast<void*>(&Open<Source::Workshop>)};
  return targets[index];
}
inline int __fastcall Read(void* files,void*,void* bytes,int size,int count) {
  using Native=int (__thiscall*)(void*,void*,int,int);
  const int result=reinterpret_cast<Native>(game+kNativeRead)(files,bytes,size,count);
  if(current&&result!=count)current->readFailed=true;
  return result;
}
inline int __fastcall Load(void* boardset,Source source,void* files,int flags,void* script) noexcept {
  using Native=int (__thiscall*)(void*,void*,int,void*);
  const auto* path=reinterpret_cast<const Wide*>(static_cast<const uint8_t*>(files)+0x1c);
  Context context;auto* previous=current;current=&context;
  int result=0;
  try {result=reinterpret_cast<Native>(game+kNativeLoad)(boardset,files,flags,script);}
  catch(...) {
    // Catch at the existing loader boundary so its native exception unwinding
    // releases buffers and strings. Successful files are read/validated once.
    Report(*path,source);
  }
  current=previous;return result;
}
__declspec(naked) inline void LoadFromMenu() {
  __asm {
    // The native callback saves its source argument in this local. Pass it
    // through EDX, which the original thiscall loader does not use.
    mov edx,[ebp-24h]
    jmp Load
  }
}
inline void* stringDtor=nullptr;
inline void* continueLoad=nullptr;
inline void* skipEntry=nullptr;
__declspec(naked) inline void AfterLoad() {
  __asm {
    test eax,eax
    jz failed
    // Replay the displaced instructions before normal metadata processing.
    mov byte ptr [ebp-4],0
    lea ecx,[ebp+0a0h]
    jmp dword ptr [continueLoad]
  failed:
    // Destroy the caller's temporary string, then use its existing skip exit.
    mov byte ptr [ebp-4],0
    lea ecx,[ebp+0a0h]
    call dword ptr [stringDtor]
    jmp dword ptr [skipEntry]
  }
}
// Board-selector rows contain native strings and source metadata. Keep their
// storage opaque so moving a row cannot allocate or free strings in our CRT.
struct Row {std::array<uint8_t,0xa0> bytes;};
static_assert(sizeof(Row)==0xa0);
inline constexpr uint32_t kSortCall=0x983f7,kNativeSort=0x93dc0;
inline constexpr const char* kLocalOrderPacks[]={
  "Board Pack 1","Board Pack 2","Board Pack 3","Classic Pack","Memorial Pack"
};
inline int Number(const Row& row,size_t offset) noexcept {
  int value;std::memcpy(&value,row.bytes.data()+offset,4);return value;
}
inline const char* Text(const Row& row,size_t offset) noexcept {
  const auto* value=row.bytes.data()+offset;
  if(Number(row,offset+0x14)<16)return reinterpret_cast<const char*>(value);
  const char* pointer;std::memcpy(&pointer,value,sizeof(pointer));return pointer;
}
inline bool Selected(const Row& row) noexcept {
  // Match Pack (0x34), independently of each boardset's Title (0x4c).
  // Source 1 is Documents community content (computer); 2 is Workshop (cloud).
  const int source=Number(row,0x9c);
  if(source!=1&&source!=2)return false;
  for(const auto* pack:kLocalOrderPacks)
    if(std::strcmp(Text(row,0x34),pack)==0)return true;
  return false;
}
inline bool Less(const Row& left,const Row& right) noexcept {
  const int sourceA=Selected(left)?0:Number(left,0x9c);
  const int sourceB=Selected(right)?0:Number(right,0x9c);
  if(sourceA!=sourceB)return sourceA<sourceB;
  // The native order is source, case-insensitive Pack, then within-pack order.
  const int pack=_stricmp(Text(left,0x34),Text(right,0x34));
  return pack?pack<0:Number(left,0x30)<Number(right,0x30);
}
inline void SortRows(Row* first,Row* last) noexcept {
  if(first==last)return;
  // Stable whole-row moves preserve ownership, IDs, paths, and source icons.
  for(auto* row=first+1;row!=last;++row) {
    Row value;std::memcpy(&value,row,sizeof(value));auto* position=row;
    while(position!=first&&Less(value,position[-1])) {
      std::memcpy(position,position-1,sizeof(value));--position;
    }
    std::memcpy(position,&value,sizeof(value));
  }
}
inline void __cdecl Sort(Row* first,Row* last,int,void*) noexcept {
  SortRows(first,last);
  Flush();
}

inline void Verify(uint8_t* base) {
  if(base[kSortCall]!=0xe8||base+kSortCall+5+*reinterpret_cast<int32_t*>(base+kSortCall+1)!=base+kNativeSort)
    throw std::runtime_error("Unexpected board-selection sort call");
  for(const auto rva:kOpenCalls)
    if(base[rva]!=0xe8||base+rva+5+*reinterpret_cast<int32_t*>(base+rva+1)!=base+kNativeOpen)
      throw std::runtime_error("Unexpected board-selection open call");
  for(const auto rva:{kLoadCall,kReadCall}) {
    const auto target=rva==kLoadCall?kNativeLoad:kNativeRead;
    if(base[rva]!=0xe8||base+rva+5+*reinterpret_cast<int32_t*>(base+rva+1)!=base+target)
      throw std::runtime_error("Unexpected board-selection loader call");
  }
  const uint8_t after[]={0xc6,0x45,0xfc,0x00,0x8d,0x8d,0xa0,0x00,0x00,0x00};
  if(std::memcmp(base+kAfterLoad,after,sizeof(after)))throw std::runtime_error("Unexpected board-selection loader cleanup");
  const uint8_t source[]={0x8b,0x85,0xc8,0x00,0x00,0x00,0x89,0x45,0xdc};
  if(std::memcmp(base+0x94efa,source,sizeof(source)))throw std::runtime_error("Unexpected board-selection source argument");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;
  stringDtor=base+0x39390;continueLoad=base+0x94f4f;skipEntry=base+0x95010;
  for(size_t i=0;i<3;i++) {
    const auto rva=kOpenCalls[i];
    const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OpenTarget(i))-reinterpret_cast<uintptr_t>(base+rva+5));
    WriteMemory(base+rva+1,&relative,4);
  }
  for(const auto rva:{kLoadCall,kReadCall}) {
    const auto target=rva==kLoadCall?reinterpret_cast<void*>(&LoadFromMenu):reinterpret_cast<void*>(&Read);
    const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(base+rva+5));
    WriteMemory(base+rva+1,&relative,4);
  }
  JumpTo(base,kAfterLoad,reinterpret_cast<void*>(&AfterLoad));
  const auto sortRelative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Sort)-reinterpret_cast<uintptr_t>(base+kSortCall+5));
  WriteMemory(base+kSortCall+1,&sortRelative,4);
}
}
