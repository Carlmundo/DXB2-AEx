// Use the verified game's real Lua VM. Process creation is intercepted: these
// tests cannot download an update, close a game, or launch PowerShell.
#include "aex-update.hpp"
#include "dangerous-lua.hpp"
#include "verified-game.hpp"
#include <wincrypt.h>
#include <cstdlib>
#pragma comment(lib,"crypt32.lib")

static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int gLaunches=0,gOpens=0,gSplashCalls=0;
static int __cdecl OriginalSplash(){++gSplashCalls;return 37;}
static int gMenuNextState=-1,gMenuCalls=0,gPromptCalls=0,gAnswer=IDNO;
static HWND gDialogWindow=nullptr;
static int gCursorCount=-3,gShows=0,gHides=0;
static HCURSOR gCursor=reinterpret_cast<HCURSOR>(0x1234);
static RECT gClip={10,20,300,400};
static bool gClipped=true,gSaveComplete=false,gExpectSave=false;
static int gSaveCalls=0,gCleanupCalls=0;
static int WINAPI ShowTestCursor(BOOL show){if(show){++gShows;return ++gCursorCount;}++gHides;return --gCursorCount;}
static HCURSOR WINAPI SetTestCursor(HCURSOR cursor){const auto previous=gCursor;gCursor=cursor;return previous;}
static BOOL WINAPI GetTestClip(RECT* clip) {
  if(gClipped)*clip=gClip;
  else {
    const int left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
    *clip={left,top,left+GetSystemMetrics(SM_CXVIRTUALSCREEN),top+GetSystemMetrics(SM_CYVIRTUALSCREEN)};
  }
  return TRUE;
}
static BOOL WINAPI SetTestClip(const RECT* clip){gClipped=clip!=nullptr;if(clip)gClip=*clip;return TRUE;}
static void __cdecl SaveGame() {
  ++gSaveCalls;
  Require(*reinterpret_cast<int*>(aex_update::gBase+0x1a7074)==2,"Unexpected save-and-quit transition");
  Require(gPromptCalls==2&&!gSaveComplete,"Prompt appeared before the save completed");
  SetEvent(aex_update::gReady); // Availability can arrive in the middle of saving.
  gSaveComplete=true;
}
static void __cdecl AfterSave() {++gCleanupCalls;Require(gSaveComplete&&gPromptCalls==2,"Quit cleanup ran before save or prompt interrupted it");}
static int __cdecl OriginalMenu() {
  ++gMenuCalls;
  if(gMenuNextState>=0)*reinterpret_cast<int*>(aex_update::gBase+0x1a7074)=gMenuNextState;
  return 83;
}
static HWND DialogWindow(){return gDialogWindow;}
static int WINAPI TestUpdatePrompt(HWND owner,LPCWSTR text,LPCWSTR caption,UINT flags) {
  ++gPromptCalls;
  Require(*reinterpret_cast<int*>(aex_update::gBase+0x1a7074)==2,"Prompt appeared outside the main menu");
  Require(!gExpectSave||gSaveComplete,"Prompt appeared before save-and-quit completed");
  Require(gCursorCount>=0&&gCursor==LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))&&!gClipped,"Dialog cursor is hidden, not an arrow, or confined");
  Require(owner==gDialogWindow&&std::wcscmp(caption,L"DX-Ball 2 / test game caption")==0,"Prompt does not use the actual game window/title");
  Require(std::wcscmp(text,L"An update is available for AEx. Do you want to install it now?")==0&&flags==0x50124,"Prompt text/options changed");
  Require(!aex_update::LaunchUpdate(),"A second updater started during the modal prompt");
  return gAnswer;
}
static bool gFailLaunch=false;
static std::wstring gCommand;
static void* __cdecl Allocate(void*,void* pointer,size_t,size_t size) {
  if(!size){std::free(pointer);return nullptr;}return std::realloc(pointer,size);
}
static BOOL WINAPI CreateUpdater(LPCWSTR application,LPWSTR command,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,
    BOOL inherit,DWORD flags,LPVOID,LPCWSTR directory,LPSTARTUPINFOW,LPPROCESS_INFORMATION child) {
  ++gLaunches;gCommand=command;
  wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
  Require(std::wstring(application)==std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe","Updater executable is not the system PowerShell");
  Require(!inherit&&(flags&CREATE_NO_WINDOW)&&(flags&CREATE_NEW_PROCESS_GROUP),"Updater is not detached/hidden or inherits handles");
  wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);
  const std::wstring game(executable);
  Require(std::wstring(directory)==game.substr(0,game.find_last_of(L'\\')),"Updater uses the current directory instead of the game directory");
  if(gFailLaunch)return FALSE;
  child->hProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  child->hThread=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  Require(child->hProcess&&child->hThread,"Cannot create fake updater handles");return TRUE;
}
static int __cdecl OpenOs(void* state) {
  using namespace aex_update;
  reinterpret_cast<void (__cdecl*)(void*,int,int)>(gBase+0x6cd0)(state,0,1);
  const LuaReg functions[]={{"execute",reinterpret_cast<LuaFunction>(gBase+0x20560)},{nullptr,nullptr}};
  SetFunctions(state,functions,0);
  return 1;
}
static void __cdecl OriginalLibraries(void* state) {
  using namespace aex_update;++gOpens;
  RequireLibrary(state,"os",&OpenOs,1);
  reinterpret_cast<void (__cdecl*)(void*,int)>(gBase+0x5660)(state,-2);
}
static void KernelImports(uint8_t* image) {
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
  auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
  for(;descriptor->Name;descriptor++) {
    const auto* name=reinterpret_cast<const char*>(image+descriptor->Name);
    if(_stricmp(name,"KERNEL32.dll")!=0)continue;
    const auto module=LoadLibraryA(name);Require(module!=nullptr,"Cannot resolve kernel imports");
    const auto* source=reinterpret_cast<const IMAGE_THUNK_DATA*>(image+descriptor->OriginalFirstThunk);
    auto* target=reinterpret_cast<IMAGE_THUNK_DATA*>(image+descriptor->FirstThunk);
    for(;source->u1.AddressOfData;source++,target++) {
      const auto importName=IMAGE_SNAP_BY_ORDINAL(source->u1.Ordinal)?MAKEINTRESOURCEA(IMAGE_ORDINAL(source->u1.Ordinal)):
        reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+source->u1.AddressOfData)->Name);
      const auto address=GetProcAddress(module,importName);Require(address!=nullptr,"Missing kernel import");
      WriteMemory(&target->u1.Function,&address,sizeof(address));
    }
  }
}
static void CheckLua(void* state,const char* source,const char* expected) {
  auto* base=aex_update::gBase;
  const auto load=reinterpret_cast<int (__cdecl*)(void*,const char*)>(base+0x9f60);
  const auto call=reinterpret_cast<int (__cdecl*)(void*,int,int,int,int,void*)>(base+0x7720);
  const auto text=reinterpret_cast<const char* (__cdecl*)(void*,int,size_t*)>(base+0x6170);
  Require(load(state,source)==0,"Test Lua did not compile");
  const auto status=call(state,0,1,0,0,nullptr);
  if(status)std::fprintf(stderr,"Lua error: %s\n",text(state,-1,nullptr));
  Require(status==0,"Test Lua failed");
  const char* actual=text(state,-1,nullptr);
  Require(actual&&std::strcmp(actual,expected)==0,"Unexpected Lua result");
  reinterpret_cast<void (__cdecl*)(void*,int)>(base+0x5660)(state,0);
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: aex-update-tests.exe game.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try {
    for(const bool logging:{false,true}) {
      aex_update::gLoggingEnabled=logging;
      for(const bool check:{false,true}) {
        const auto script=aex_update::BuildScript(L"C:\\Game\\DXBall2.exe",4242,check);
        Require(script.find(logging?L" -LogEnabled:$true":L" -LogEnabled:$false")!=std::wstring::npos,
          "Updater did not receive the configured logging setting");
      }
    }
    aex_update::gLoggingEnabled=false;
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto* base=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Require(base!=nullptr,"Cannot map game");KernelImports(base);
    aex_update::Verify(base);
    for(const auto rva:{0x4ba14,0x4ba4e,0x9ff05,0x5640,0x5660,0x6790,0x6cd0,0xa3b0,0xa5c0}) {
      const uint8_t original=base[rva],wrong=0xcc;WriteMemory(base+rva,&wrong,1);
      bool rejected=false;try{aex_update::Install(base);}catch(...){rejected=true;}
      Require(rejected&&aex_update::gBase==nullptr,"Mismatch allowed partial update installation");
      WriteMemory(base+rva,&original,1);
    }
    std::puts("PASS: guarded Lua registration rejects every mismatched hook/API before installation");std::fflush(stdout);
    aex_update::Install(base);
    InstallDisableDangerousLua(base);
    // Only the library-opening boundary is replaced; the registration APIs,
    // Lua parser/VM, and protected os.execute callback are the game's own.
    JumpTo(base,0xb400,reinterpret_cast<void*>(&OriginalLibraries));
    JumpTo(base,0x12b4e4,reinterpret_cast<void*>(&GetSystemTimeAsFileTime));
    aex_update::gCreateProcess=&CreateUpdater;
    const auto initialize=reinterpret_cast<void (__cdecl*)(void*)>(base+0x9ff0a+*reinterpret_cast<int32_t*>(base+0x9ff06));
    const auto close=reinterpret_cast<void (__cdecl*)(void*)>(base+0xb3e0);
    auto* state=reinterpret_cast<void* (__cdecl*)(decltype(&Allocate),void*)>(base+0xb200)(&Allocate,nullptr);
    Require(state!=nullptr,"Cannot create game Lua state");initialize(state);
    Require(gOpens==1,"Original Lua library initialization was bypassed");
    CheckLua(state,"return os.execute('must never execute') == nil and 'blocked' or 'unsafe'","blocked");
    CheckLua(state,"return aex.update('custom URL') == false and 'rejected' or 'unsafe'","rejected");
    Require(gLaunches==0,"Lua arguments reached updater process creation");
    gFailLaunch=true;CheckLua(state,"return aex.update() == false and 'failed' or 'wrong'","failed");
    gFailLaunch=false;CheckLua(state,"return aex.update() and 'started' or 'failed'","started");
    Require(gLaunches==2,"Launch failure cannot be retried");
    CheckLua(state,"return aex.update() == false and 'busy' or 'wrong'","busy");
    Require(gLaunches==2,"Repeated call launched a second concurrent updater");
    DWORD bytes=0;const auto at=gCommand.find(L"-EncodedCommand ");
    Require(at!=std::wstring::npos,"Updater command is not encoded");
    const auto encoded=gCommand.substr(at+16);
    Require(CryptStringToBinaryW(encoded.c_str(),0,CRYPT_STRING_BASE64,nullptr,&bytes,nullptr,nullptr)!=0,"Invalid command base64");
    std::wstring decoded(bytes/sizeof(wchar_t),L'\0');
    Require(CryptStringToBinaryW(encoded.c_str(),0,CRYPT_STRING_BASE64,reinterpret_cast<BYTE*>(decoded.data()),&bytes,nullptr,nullptr)!=0,"Cannot decode command");
    wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);
    Require(decoded==aex_update::BuildScript(executable,GetCurrentProcessId()),"Updater encoding or current PID/path changed");
    Require(aex_update::QuotePowerShell(L"C:\\Game's & % ! λ\\DXBall2.exe")==L"'C:\\Game''s & % ! λ\\DXBall2.exe'","Path quoting allows shell interpretation");
    SetEvent(aex_update::gUpdater);
    CheckLua(state,"return aex.update() and 'retry' or 'failed'","retry");
    close(state);
    state=reinterpret_cast<void* (__cdecl*)(decltype(&Allocate),void*)>(base+0xb200)(&Allocate,nullptr);
    Require(state!=nullptr,"Cannot create second Lua state");initialize(state);
    CheckLua(state,"return aex.update() == false and 'busy' or 'wrong'","busy");
    close(state);CloseHandle(aex_update::gUpdater);aex_update::gUpdater=nullptr;
    std::puts("PASS: real game Lua calls aex.update with dangerous Lua blocked; arguments rejected, launch failure/retry, shared duplicate prevention, hidden detached helper, fixed paths/PID, independent base64 decode, and multiple states");
    const int beforeCheck=gLaunches;
    JumpTo(base,0x99a00,reinterpret_cast<void*>(&OriginalSplash));
    const auto startup=reinterpret_cast<int (__cdecl*)()>(base+0x4ba19+*reinterpret_cast<int32_t*>(base+0x4ba15));
    Require(startup()==37&&gSplashCalls==1,"Startup hook did not preserve the original intro update");
    aex_update::CheckForUpdatesOnce();
    Require(gLaunches==beforeCheck&&aex_update::gCheckStarted==0,"Disabled automatic check launched a helper");
    aex_update::gCheckForUpdates=true;
    Require(startup()==37&&gSplashCalls==2,"Enabled check did not preserve the intro update");
    aex_update::CheckForUpdatesOnce();
    Require(gLaunches==beforeCheck+1&&aex_update::gUpdater,"Enabled automatic check did not launch");
    const auto encodedCheck=gCommand.substr(gCommand.find(L"-EncodedCommand ")+16);
    bytes=0;Require(CryptStringToBinaryW(encodedCheck.c_str(),0,CRYPT_STRING_BASE64,nullptr,&bytes,nullptr,nullptr)!=0,"Invalid checker base64");
    std::wstring checkScript(bytes/sizeof(wchar_t),L'\0');
    Require(CryptStringToBinaryW(encodedCheck.c_str(),0,CRYPT_STRING_BASE64,reinterpret_cast<BYTE*>(checkScript.data()),&bytes,nullptr,nullptr)!=0,"Cannot decode checker");
    Require(checkScript==aex_update::BuildScript(executable,GetCurrentProcessId(),true),"Checker version, target or encoding changed");
    Require(checkScript.find(aex_update::kUpdateScript)!=std::wstring::npos&&checkScript.find(AEX_VERSION_WIDE)!=std::wstring::npos,"Checker does not share installer/compiled version");
    Require(checkScript.find(aex_update::gSignalName)!=std::wstring::npos&&!aex_update::gSignalName.empty(),"Checker does not receive its private readiness/decision signals");
    JumpTo(base,0x99eb0,reinterpret_cast<void*>(&OriginalMenu));
    const auto menu=reinterpret_cast<int (__cdecl*)()>(base+0x4ba53+*reinterpret_cast<int32_t*>(base+0x4ba4f));
    auto& gameState=*reinterpret_cast<int*>(base+0x1a7074);
    aex_update::gFindGameWindow=&DialogWindow;aex_update::gMessageBox=&TestUpdatePrompt;
    aex_update::gShowCursor=&ShowTestCursor;aex_update::gSetCursor=&SetTestCursor;
    aex_update::gGetClipCursor=&GetTestClip;aex_update::gClipCursor=&SetTestClip;
    SetEvent(aex_update::gReady);
    for(const int mode:{0,1,3,4,5,6,7,8,9}) {
      gameState=mode;aex_update::PromptAtMainMenu();
      Require(gPromptCalls==0&&WaitForSingleObject(aex_update::gReady,0)==WAIT_OBJECT_0,"Non-menu state displayed or discarded a pending update");
    }
    // Simulate selecting a board in the same menu frame that notice is ready.
    gameState=2;gMenuNextState=4;
    Require(menu()==83&&gMenuCalls==1&&gameState==4&&gPromptCalls==0,"Board loading raced the update prompt");
    Require(WaitForSingleObject(aex_update::gReady,0)==WAIT_OBJECT_0,"Board loading lost the pending notice");
    gameState=2;gMenuNextState=-1;
    Require(menu()==83&&gPromptCalls==0,"Prompt appeared without a game window");
    Require(WaitForSingleObject(aex_update::gReady,0)==WAIT_OBJECT_0,"Missing window lost the pending notice");
    gDialogWindow=CreateWindowExW(0,L"STATIC",L"DX-Ball 2 / test game caption",WS_OVERLAPPED,
      0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(gDialogWindow!=nullptr,"Cannot create hidden caption fixture");
    Require(menu()==83&&gPromptCalls==1&&WaitForSingleObject(aex_update::gCancel,0)==WAIT_OBJECT_0,"Returning to menu did not show/decline the pending notice");
    Require(WaitForSingleObject(aex_update::gReady,0)==WAIT_TIMEOUT,"Notice was not consumed");
    menu();Require(gPromptCalls==1,"Declined notice repeated on the next menu frame");
    ResetEvent(aex_update::gCancel);SetEvent(aex_update::gReady);gAnswer=IDYES;
    Require(menu()==83&&gPromptCalls==2&&WaitForSingleObject(aex_update::gInstall,0)==WAIT_OBJECT_0,"Yes did not signal the shared installer");
    Require(gCursorCount==-3&&gShows==gHides&&gCursor==reinterpret_cast<HCURSOR>(0x1234)&&gClipped,"Prompt did not restore hidden game cursor state");
    // Execute the actual save-and-quit branch, intercepting only file writing
    // and cleanup. The game changes state to 2 before its synchronous save.
    JumpTo(base,0x8ab80,reinterpret_cast<void*>(&SaveGame));
    JumpTo(base,0x8a8f0,reinterpret_cast<void*>(&AfterSave));
    Require(base[0x5c89a]==0xc7&&base[0x5c8ae]==0xe9,"Unexpected save-and-quit branch");
    const uint8_t ret=0xc3;WriteMemory(base+0x5c8ae,&ret,1);
    ResetEvent(aex_update::gInstall);ResetEvent(aex_update::gReady);
    gameState=4;gExpectSave=true;
    reinterpret_cast<void (__cdecl*)()>(base+0x5c89a)();
    Require(gSaveCalls==1&&gCleanupCalls==1&&gSaveComplete&&gPromptCalls==2,"Save-and-quit was interrupted");
    gAnswer=IDNO;
    Require(menu()==83&&gPromptCalls==3,"Saved game did not show its pending notice after returning to menu");
    gExpectSave=false;
    // Preserve both negative visibility counters and already-visible cursors,
    // including No, Yes and a failed/dismissed dialog result.
    for(const int count:{-5,-1,0,3})for(const int answer:{IDNO,IDYES,0})for(const bool clipped:{false,true}) {
      gCursorCount=count;gShows=gHides=0;gCursor=nullptr;gClipped=clipped;
      const auto previousClip=gClip;gAnswer=answer;
      SetEvent(aex_update::gReady);menu();
      Require(gCursorCount==count&&gShows==gHides&&gCursor==nullptr&&gClipped==clipped,"Cursor state leaked after dialog");
      Require(!clipped||EqualRect(&gClip,&previousClip),"Original mouse confinement was not restored");
    }
    std::puts("PASS: real save-and-quit branch completes before pending prompt; arrow visible/unconfined during dialog; balanced cursor count, shape and confinement restored for 24 visibility/answer combinations");
    DestroyWindow(gDialogWindow);gDialogWindow=nullptr;
    std::puts("PASS: UI-thread main-menu prompt only; every non-menu state retains notice, same-frame board load defers, returning to menu shows once, actual window caption, No/Yes handoff, and no concurrent updater");
    SetEvent(aex_update::gUpdater);
    aex_update::CheckForUpdatesOnce();
    Require(gLaunches==beforeCheck+1,"Automatic check repeated after its helper exited");
    CloseHandle(aex_update::gUpdater);aex_update::gUpdater=nullptr;
    aex_update::CloseSignals();
    std::puts("PASS: automatic checks default off, launch once outside DLL initialization, embed compiled version and the identical installer, and fit the Windows command line");
    FreeLibrary(reinterpret_cast<HMODULE>(base));
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
  return 0;
}
