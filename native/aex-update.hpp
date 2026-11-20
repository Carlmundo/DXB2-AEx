#pragma once
#include "patch.hpp"
#include "aex-update-script.hpp"
#include "aex-update-check-script.hpp"
#include "aex-version.h"
#include <string>
#pragma comment(lib,"user32.lib")

namespace aex_update {
using LuaFunction=int (__cdecl*)(void*);
struct LuaReg { const char* name; LuaFunction function; };
inline uint8_t* gBase=nullptr;
inline HANDLE gUpdater=nullptr;
inline SRWLOCK gLock=SRWLOCK_INIT;
inline bool gCheckForUpdates=false;
inline LONG gCheckStarted=0;
inline decltype(&CreateProcessW) gCreateProcess=&CreateProcessW;
inline HANDLE gReady=nullptr,gInstall=nullptr,gCancel=nullptr;
inline std::wstring gSignalName;
inline unsigned gSignalSequence=0;
inline bool gPromptOpen=false;

inline void CloseSignals() noexcept {
  for(auto* signal:{&gReady,&gInstall,&gCancel})if(*signal){CloseHandle(*signal);*signal=nullptr;}
  gSignalName.clear();
}
inline void CreateSignals() {
  gSignalName=L"Local\\DXB2-AEx-update-"+std::to_wstring(GetCurrentProcessId())+L"-"+
    std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++gSignalSequence);
  const wchar_t* suffixes[]={L".ready",L".install",L".cancel"};
  HANDLE* signals[]={&gReady,&gInstall,&gCancel};
  for(size_t i=0;i<3;i++) {
    *signals[i]=CreateEventW(nullptr,TRUE,FALSE,(gSignalName+suffixes[i]).c_str());
    if(!*signals[i]||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Cannot create update signals");
  }
}

inline std::wstring QuotePowerShell(const std::wstring& value) {
  std::wstring quoted=L"'";
  for(const auto c:value){quoted+=c;if(c==L'\'')quoted+=c;}
  return quoted+L"'";
}
inline std::wstring EncodeCommand(const std::wstring& script) {
  static constexpr wchar_t alphabet[]=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto* bytes=reinterpret_cast<const uint8_t*>(script.data());
  const auto length=script.size()*sizeof(wchar_t);
  std::wstring encoded;encoded.reserve((length+2)/3*4);
  for(size_t i=0;i<length;i+=3) {
    const auto n=(static_cast<uint32_t>(bytes[i])<<16)|
      (i+1<length?static_cast<uint32_t>(bytes[i+1])<<8:0)|(i+2<length?bytes[i+2]:0);
    encoded+=alphabet[(n>>18)&63];encoded+=alphabet[(n>>12)&63];
    encoded+=i+1<length?alphabet[(n>>6)&63]:L'=';encoded+=i+2<length?alphabet[n&63]:L'=';
  }
  return encoded;
}
inline std::wstring BuildScript(const std::wstring& executable,DWORD process,bool check=false) {
  if(check) {
    return std::wstring(L"& {\n")+kUpdateCheckScript+L"\n} -GamePath "+QuotePowerShell(executable)+
      L" -GameId "+std::to_wstring(process)+L" -InstalledVersion "+QuotePowerShell(AEX_VERSION_WIDE)+
      L" -SignalName "+QuotePowerShell(gSignalName)+
      L" -InstallUpdate {\n"+kUpdateScript+L"\n}";
  }
  return std::wstring(L"& {\n")+kUpdateScript+L"\n} -GamePath "+QuotePowerShell(executable)+L" -GameId "+std::to_wstring(process);
}
inline bool LaunchUpdate(bool check=false) noexcept {
  AcquireSRWLockExclusive(&gLock);
  bool launched=false;
  try {
    if(gPromptOpen||(gUpdater&&WaitForSingleObject(gUpdater,0)==WAIT_TIMEOUT)) {
      ReleaseSRWLockExclusive(&gLock);return false;
    }
    if(gUpdater){CloseHandle(gUpdater);gUpdater=nullptr;}
    CloseSignals();
    if(check)CreateSignals();
    wchar_t executable[MAX_PATH],system[MAX_PATH];
    const auto count=GetModuleFileNameW(nullptr,executable,MAX_PATH);
    const auto systemCount=GetSystemDirectoryW(system,MAX_PATH);
    if(!count||count>=MAX_PATH||!systemCount||systemCount>=MAX_PATH)throw std::runtime_error("Cannot locate updater paths");
    const std::wstring game(executable);
    const auto slash=game.find_last_of(L"\\/");
    if(slash==std::wstring::npos)throw std::runtime_error("Cannot locate game directory");
    const auto directory=game.substr(0,slash);
    const auto powershell=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    auto command=L"\""+powershell+L"\" -NoProfile -NonInteractive -EncodedCommand "+EncodeCommand(BuildScript(game,GetCurrentProcessId(),check));
    if(command.size()>=32767)throw std::runtime_error("Updater command is too long");
    STARTUPINFOW startup={};startup.cb=sizeof(startup);
    PROCESS_INFORMATION child={};
    if(gCreateProcess(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW|CREATE_NEW_PROCESS_GROUP,nullptr,directory.c_str(),&startup,&child)) {
      CloseHandle(child.hThread);gUpdater=child.hProcess;launched=true;
    }
  }catch(...){launched=false;}
  if(check&&!launched)CloseSignals();
  ReleaseSRWLockExclusive(&gLock);
  return launched;
}

// Called at the first intro update; the main-menu query is a fallback. No
// HTTP, dialog, process creation, or waiting occurs while holding loader lock.
inline void CheckForUpdatesOnce() noexcept {
  if(gCheckForUpdates&&InterlockedCompareExchange(&gCheckStarted,1,0)==0)LaunchUpdate(true);
}
// Preserve the original intro update arguments and integer return value.
// Start the helper before its normal five-second splash/input wait.
inline int __cdecl StartupUpdate() {
  CheckForUpdatesOnce();
  return reinterpret_cast<int (__cdecl*)()>(gBase+0x99a00)();
}

inline BOOL CALLBACK PickGameWindow(HWND window,LPARAM output) {
  if(!IsWindowVisible(window)||!GetWindowTextLengthW(window))return TRUE;
  *reinterpret_cast<HWND*>(output)=window;return FALSE;
}
inline HWND FindGameWindow() {
  HWND window=nullptr;
  EnumThreadWindows(GetCurrentThreadId(),&PickGameWindow,reinterpret_cast<LPARAM>(&window));
  return window;
}
inline decltype(&FindGameWindow) gFindGameWindow=&FindGameWindow;
inline decltype(&MessageBoxW) gMessageBox=&MessageBoxW;
inline decltype(&ShowCursor) gShowCursor=&ShowCursor;
inline decltype(&SetCursor) gSetCursor=&SetCursor;
inline decltype(&GetClipCursor) gGetClipCursor=&GetClipCursor;
inline decltype(&ClipCursor) gClipCursor=&ClipCursor;

struct DialogCursor {
  HCURSOR previous=nullptr;
  int increments=0;
  RECT clip={};
  bool restoreClip=false,wasUnclipped=false;
  DialogCursor() noexcept {
    // The game's drawn cursor stops rendering inside a modal Windows dialog.
    // Show the OS arrow even if the game hid it with several ShowCursor calls.
    previous=gSetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))); // IDC_ARROW
    do { ++increments; } while(gShowCursor(TRUE)<0);
    if(gGetClipCursor(&clip)) {
      const int left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
      const RECT desktop={left,top,left+GetSystemMetrics(SM_CXVIRTUALSCREEN),top+GetSystemMetrics(SM_CYVIRTUALSCREEN)};
      wasUnclipped=EqualRect(&clip,&desktop)!=FALSE;
      restoreClip=true;gClipCursor(nullptr);
    }
  }
  ~DialogCursor() {
    if(restoreClip)gClipCursor(wasUnclipped?nullptr:&clip);
    while(increments-- > 0)gShowCursor(FALSE);
    gSetCursor(previous);
  }
  DialogCursor(const DialogCursor&)=delete;
  DialogCursor& operator=(const DialogCursor&)=delete;
};

inline void PromptAtMainMenu() noexcept {
  HWND owner=nullptr;std::wstring caption;
  AcquireSRWLockExclusive(&gLock);
  try {
    // State 2 is the main menu; gameplay, pause, selection and intro states
    // must leave the ready signal pending. This runs on the game UI thread.
    if(!gBase||*reinterpret_cast<int*>(gBase+0x1a7074)!=2||gPromptOpen||!gReady||
        !gUpdater||WaitForSingleObject(gUpdater,0)!=WAIT_TIMEOUT||WaitForSingleObject(gReady,0)!=WAIT_OBJECT_0) {
      ReleaseSRWLockExclusive(&gLock);return;
    }
    owner=gFindGameWindow();
    const int length=owner?GetWindowTextLengthW(owner):0;
    if(length<=0){ReleaseSRWLockExclusive(&gLock);return;}
    caption.resize(static_cast<size_t>(length)+1);
    if(!GetWindowTextW(owner,caption.data(),length+1)){ReleaseSRWLockExclusive(&gLock);return;}
    ResetEvent(gReady);gPromptOpen=true;
  }catch(...){ReleaseSRWLockExclusive(&gLock);return;}
  ReleaseSRWLockExclusive(&gLock);
  // The main loop cannot load a board while its UI thread owns this modal
  // dialog. The helper never displays UI or independently samples game state.
  int answer;
  {
    const DialogCursor cursor;
    answer=gMessageBox(owner,L"An update is available for AEx. Do you want to install it now?",
      caption.c_str(),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2|MB_SETFOREGROUND|MB_TOPMOST);
  }
  AcquireSRWLockExclusive(&gLock);
  SetEvent(answer==IDYES?gInstall:gCancel);gPromptOpen=false;
  ReleaseSRWLockExclusive(&gLock);
}
inline int __cdecl MenuUpdate() {
  CheckForUpdatesOnce();
  const int result=reinterpret_cast<int (__cdecl*)()>(gBase+0x99eb0)();
  // Original menu input may just have loaded a board. Recheck afterwards.
  PromptAtMainMenu();return result;
}
// LTCG kept ECX/EDX parameters but uses caller cleanup for stack arguments.
// MSVC __fastcall would expect the callee to pop them, corrupting ESP.
__declspec(naked) inline void __cdecl SetFunctions(void*,const LuaReg*,int) {
  __asm {
    mov ecx,[esp+4]
    mov edx,[esp+8]
    mov eax,[esp+12]
    push eax
    mov eax,gBase
    add eax,0a3b0h
    call eax
    add esp,4
    ret
  }
}
__declspec(naked) inline void __cdecl RequireLibrary(void*,const char*,LuaFunction,int) {
  __asm {
    mov ecx,[esp+4]
    mov edx,[esp+8]
    mov eax,[esp+16]
    push eax
    push dword ptr [esp+16]
    mov eax,gBase
    add eax,0a5c0h
    call eax
    add esp,8
    ret
  }
}
inline int __cdecl Update(void* state) {
  // No Lua arguments can specify a URL, process, path, or shell command.
  const auto getTop=reinterpret_cast<int (__cdecl*)(void*)>(gBase+0x5640);
  const bool started=getTop(state)==0&&LaunchUpdate();
  reinterpret_cast<void (__cdecl*)(void*,int)>(gBase+0x6790)(state,started);
  return 1;
}
inline int __cdecl OpenAex(void* state) {
  reinterpret_cast<void (__cdecl*)(void*,int,int)>(gBase+0x6cd0)(state,0,1);
  static const LuaReg functions[]={{"update",&Update},{nullptr,nullptr}};
  SetFunctions(state,functions,0);
  return 1;
}
inline void __cdecl OpenLibraries(void* state) {
  reinterpret_cast<void (__cdecl*)(void*)>(gBase+0xb400)(state);
  RequireLibrary(state,"aex",&OpenAex,1);
  reinterpret_cast<void (__cdecl*)(void*,int)>(gBase+0x5660)(state,-2);
}
inline void Verify(uint8_t* base) {
  const uint8_t menuCall[]={0xe8,0x5d,0xe4,0x04,0x00};
  if(std::memcmp(base+0x4ba4e,menuCall,sizeof(menuCall)))throw std::runtime_error("Unexpected main-menu update call");
  const uint8_t startupCall[]={0xe8,0xe7,0xdf,0x04,0x00};
  if(std::memcmp(base+0x4ba14,startupCall,sizeof(startupCall)))throw std::runtime_error("Unexpected intro update call");
  const uint8_t call[]={0xe8,0xf6,0xb4,0xf6,0xff};
  if(std::memcmp(base+0x9ff05,call,sizeof(call)))throw std::runtime_error("Unexpected Lua library initialization call");
  struct Api {uint32_t rva;uint8_t expected[6];};
  static constexpr Api apis[]={
    {0x5640,{0x55,0x8b,0xec,0x8b,0x45,0x08}}, {0x5660,{0x55,0x8b,0xec,0x8b,0x45,0x08}},
    {0x6790,{0x55,0x8b,0xec,0x8b,0x55,0x08}}, {0x6cd0,{0x55,0x8b,0xec,0x83,0xe4,0xf8}},
    {0xa3b0,{0x55,0x8b,0xec,0x83,0xec,0x10}}, {0xa5c0,{0x55,0x8b,0xec,0x83,0xe4,0xf8}}
  };
  for(const auto& api:apis)if(std::memcmp(base+api.rva,api.expected,sizeof(api.expected)))
    throw std::runtime_error("Unexpected Lua update API");
}
inline void Install(uint8_t* base) {
  Verify(base);gBase=base;
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&OpenLibraries)-reinterpret_cast<uintptr_t>(base+0x9ff0a));
  WriteMemory(base+0x9ff06,&relative,sizeof(relative));
  const auto startupRelative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&StartupUpdate)-reinterpret_cast<uintptr_t>(base+0x4ba19));
  WriteMemory(base+0x4ba15,&startupRelative,sizeof(startupRelative));
  const auto menuRelative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&MenuUpdate)-reinterpret_cast<uintptr_t>(base+0x4ba53));
  WriteMemory(base+0x4ba4f,&menuRelative,sizeof(menuRelative));
}
} // namespace aex_update
