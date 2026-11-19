#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#pragma comment(lib,"bcrypt.lib")

namespace editor_scripts {
inline constexpr int kBufferBytes=1024*1024;
inline constexpr int kMaxTextBytes=kBufferBytes-1;
inline constexpr uint32_t kEditBundledBoardsRva=0x2c312;
inline constexpr uint32_t kCommentAppendRvas[]={0x38cdb,0x38d1e,0x38dc2};
inline constexpr uint32_t kFinalParagraphRva=0x38fba;
struct CommentStart {uint32_t rva,original;};
inline constexpr CommentStart kCommentStarts[]={{0x38e81,0xe690},{0x38ebf,0x1b000}};
inline uint8_t* base=nullptr;
inline DLGPROC originalDialog=nullptr;
inline void (__cdecl* originalHighlight)(HWND,int)=nullptr;
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
inline void Call(uint8_t* at,const void* destination) {
  uint8_t bytes[5]={0xe8};
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
// These three formatter calls append comment substrings directly to an RTF
// document. Escape only the comment payload, leaving the formatter's own RTF
// and Lua tokenization intact. __fastcall's unused EDX bridges __thiscall.
inline void* __fastcall AppendComment(void* destination,void*,const void* source,size_t offset,size_t count) {
  const size_t length=Length(source);
  if(offset>length)throw std::out_of_range("Invalid editor comment range");
  if(count>length-offset)count=length-offset;
  const char* text=Text(source)+offset;
  std::string escaped;escaped.reserve(count);
  for(size_t i=0;i<count;i++) {
    const char c=text[i];
    if(c=='\\'||c=='{'||c=='}')escaped+='\\';
    escaped+=c;
  }
  return reinterpret_cast<void* (__thiscall*)(void*,const char*,size_t)>(base+0xe690)(destination,escaped.data(),escaped.size());
}
inline void* __fastcall AppendFinalParagraph(void* destination,void*,const char*,size_t) {
  // RTF consumes its final paragraph terminator. Add another terminator to
  // preserve an ending newline instead of the stock formatter's literal space.
  // With an unclosed block-comment group, Rich Edit already keeps that newline.
  const char* rtf=Text(destination);const size_t length=Length(destination);int depth=0;
  for(size_t i=0;i<length;i++) {
    if(rtf[i]=='\\'){if(i+1<length)i++;}
    else if(rtf[i]=='{')depth++;
    else if(rtf[i]=='}')depth--;
  }
  if(depth>1)return destination;
  return reinterpret_cast<void* (__thiscall*)(void*,const char*,size_t)>(base+0xe690)(destination,"\\par ",5);
}
// The formatter buffers identifiers/keywords in its stack-local token context.
// Flush that context before appending either comment's opening RTF, otherwise
// an adjacent token (identifier--comment) is emitted after the comment.
// Preserve the original destination in ECX and tail-call the original append
// routine with its untouched arguments and return address.
inline __declspec(naked) void BeginBlockComment() {
  __asm {
    push ecx
    lea ecx,[ebp-260h]
    mov eax,base
    add eax,380e0h
    call eax
    pop ecx
    mov eax,base
    add eax,0e690h
    jmp eax
  }
}
inline __declspec(naked) void BeginLineComment() {
  __asm {
    push ecx
    lea ecx,[ebp-260h]
    mov eax,base
    add eax,380e0h
    call eax
    pop ecx
    mov eax,base
    add eax,1b000h
    jmp eax
  }
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
struct SuspendedUndo {
  ITextDocument* document=nullptr;
  explicit SuspendedUndo(HWND control) {
    IRichEditOle* richOle=nullptr;
    SendMessageA(control,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&richOle));
    if(richOle) {
      richOle->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void**>(&document));richOle->Release();
      if(document&&FAILED(document->Undo(tomSuspend,nullptr))){document->Release();document=nullptr;}
    }
  }
  ~SuspendedUndo(){if(document){document->Undo(tomResume,nullptr);document->Release();}}
};

inline void Highlight(HWND control,int kind) {
  Updating guard;
  // Replacing the RTF document must not become an undoable user edit. Doing
  // so during EN_CHANGE also destroys redo and can leave the whole script
  // selected after Undo. Suspend recording without clearing the user's edits.
  SuspendedUndo undo(control);
  if(!undo.document)return;
  // The original formatter saves the caret using the packed 16-bit EM_GETSEL
  // return value. Preserve the full selection for long scripts.
  CHARRANGE selection{};POINT scroll{};
  SendMessageA(control,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
  const auto firstVisibleLine=SendMessageA(control,EM_GETFIRSTVISIBLELINE,0,0);
  SendMessageA(control,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  SendMessageA(control,WM_SETREDRAW,FALSE,0);
  originalHighlight(control,kind);
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
  constexpr uint8_t append[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x56,0x8b,0xf1};
  for(const auto rva:kCommentAppendRvas) {
    int32_t relative=0;std::memcpy(&relative,image+rva+1,sizeof(relative));
    if(image[rva]!=0xe8||static_cast<int64_t>(rva)+5+relative!=0xe4f0)
      throw std::runtime_error("Unsupported editor comment formatter");
  }
  for(const auto& hook:kCommentStarts) {
    int32_t relative=0;std::memcpy(&relative,image+hook.rva+1,sizeof(relative));
    if(image[hook.rva]!=0xe8||static_cast<int64_t>(hook.rva)+5+relative!=hook.original)
      throw std::runtime_error("Unsupported editor comment start");
  }
  int32_t paragraphRelative=0;std::memcpy(&paragraphRelative,image+kFinalParagraphRva+1,sizeof(paragraphRelative));
  if(image[kFinalParagraphRva]!=0xe8||static_cast<int64_t>(kFinalParagraphRva)+5+paragraphRelative!=0xe690)
    throw std::runtime_error("Unsupported editor final paragraph");
  constexpr uint8_t flushToken[]={0x8d,0x8d,0xa0,0xfd,0xff,0xff,0xe8,0x7b,0xf1,0xff,0xff};
  if(std::memcmp(image+0x38f5a,flushToken,sizeof(flushToken)))
    throw std::runtime_error("Unsupported editor token context");
  // The patched edtior may be used and differs only in this branch: JE +0x25 becomes JMP +0x25.
  // Accept its already-patched form as well as the original editor.
  constexpr uint8_t lockBranchTail[]={0x25,0x57,0x8b,0xcb,0xe8,0x64,0xe7,0xff,0xff,0x84,0xc0,0x75,0x19};
  const auto lockBranch=image[kEditBundledBoardsRva];
  if(std::memcmp(image+0x32f20,entry,sizeof(entry))||std::memcmp(image+0x3346f,oldRead,sizeof(oldRead))||
     std::memcmp(image+0x32480,getScript,sizeof(getScript))||std::memcmp(image+0xe7b0,assign,sizeof(assign))||
     std::memcmp(image+0x33b40,format,sizeof(format))||std::memcmp(image+0xe690,append,sizeof(append))||(lockBranch!=0x74&&lockBranch!=0xeb)||
     std::memcmp(image+kEditBundledBoardsRva+1,lockBranchTail,sizeof(lockBranchTail)))
    throw std::runtime_error("Unsupported editor instructions");
}
inline void Install(uint8_t* image) {
  Verify(image);
  auto* trampoline=static_cast<uint8_t*>(VirtualAlloc(nullptr,21,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  if(!trampoline)throw std::runtime_error("Cannot allocate editor script trampoline");
  std::memcpy(trampoline,image+0x32f20,5);
  trampoline[5]=0xe9;
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image+0x32f25)-reinterpret_cast<uintptr_t>(trampoline+10));
  std::memcpy(trampoline+6,&relative,4);
  uint8_t originalFormat[6];std::memcpy(originalFormat,image+0x33b40,sizeof(originalFormat));
  std::memcpy(trampoline+10,originalFormat,sizeof(originalFormat));trampoline[16]=0xe9;
  const auto formatRelative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image+0x33b46)-reinterpret_cast<uintptr_t>(trampoline+21));
  std::memcpy(trampoline+17,&formatRelative,4);
  DWORD old=0;
  if(!VirtualProtect(trampoline,21,PAGE_EXECUTE_READ,&old)){VirtualFree(trampoline,0,MEM_RELEASE);throw std::runtime_error("Cannot protect editor script trampoline");}
  FlushInstructionCache(GetCurrentProcess(),trampoline,21);
  base=image;originalDialog=reinterpret_cast<DLGPROC>(trampoline);
  originalHighlight=reinterpret_cast<void (__cdecl*)(HWND,int)>(trampoline+10);
  const auto originalBranch=image[kEditBundledBoardsRva];
  try {
    for(const auto rva:kCommentAppendRvas)Call(image+rva,reinterpret_cast<const void*>(&AppendComment));
    Call(image+kCommentStarts[0].rva,reinterpret_cast<const void*>(&BeginBlockComment));
    Call(image+kCommentStarts[1].rva,reinterpret_cast<const void*>(&BeginLineComment));
    Call(image+kFinalParagraphRva,reinterpret_cast<const void*>(&AppendFinalParagraph));
    if(originalBranch==0x74) {
      const uint8_t jump=0xeb;Write(image+kEditBundledBoardsRva,&jump,1);
    }
    Jump(image+0x33b40,reinterpret_cast<const void*>(&Highlight));
    Jump(image+0x32f20,reinterpret_cast<const void*>(&Dialog));
  }catch(...) {
    try{Write(image+0x33b40,originalFormat,sizeof(originalFormat));}catch(...){}
    try{Call(image+kFinalParagraphRva,image+0xe690);}catch(...){}
    for(const auto& hook:kCommentStarts)try{Call(image+hook.rva,image+hook.original);}catch(...){}
    for(const auto rva:kCommentAppendRvas)try{Call(image+rva,image+0xe4f0);}catch(...){}
    if(originalBranch==0x74)try{Write(image+kEditBundledBoardsRva,&originalBranch,1);}catch(...){}
    base=nullptr;originalDialog=nullptr;originalHighlight=nullptr;VirtualFree(trampoline,0,MEM_RELEASE);throw;
  }
}
}
