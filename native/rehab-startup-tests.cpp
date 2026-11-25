// Run the production DLL initialization against a mapped verified game.
// Substitute only module discovery; no actual game loop or Steam is started.
#include "patch.hpp"
#include <fstream>
#include <string>
static uint8_t* gMapped=nullptr;
static std::wstring gExePath,gDllPath;
static DWORD WINAPI TestModulePath(HMODULE module,wchar_t* output,DWORD size) {
  const auto& path=module?gDllPath:gExePath;
  if(path.size()>=size)return size;
  std::memcpy(output,path.c_str(),(path.size()+1)*sizeof(wchar_t));return static_cast<DWORD>(path.size());
}
static HMODULE WINAPI TestModule(HMODULE) {return reinterpret_cast<HMODULE>(gMapped);}
static HMODULE WINAPI TestModuleByName(const wchar_t* name) {return name?nullptr:TestModule(nullptr);}
#define GetModuleFileNameW TestModulePath
#define GetModuleHandleW TestModuleByName
#include "mod.cpp"
#undef GetModuleFileNameW
#undef GetModuleHandleW
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static BOOL WINAPI Version(const void*,const wchar_t*,void** output,UINT* length) {
  static wchar_t value[]=L"2.3";*output=value;*length=4;return TRUE;
}
static int gCheckLaunches=0;
static bool NoBlocklistDownload(std::string& text) {text.clear();return true;}
static BOOL WINAPI NoCheckProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,
    LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION) {
  ++gCheckLaunches;return FALSE;
}
int wmain(int argc,wchar_t** argv) {
  if(argc<4||argc>6){std::fprintf(stderr,"Usage: rehab-startup-tests.exe game.exe fixture-directory missing|0|1|invalid [CheckForUpdates: 0|1|invalid] [FilterScores: missing|0|1|invalid]\n");return 2;}
  try {
    wchar_t path[MAX_PATH];Require(GetFullPathNameW(argv[1],MAX_PATH,path,nullptr)!=0,"Cannot resolve game");gExePath=path;
    Require(GetFullPathNameW(argv[2],MAX_PATH,path,nullptr)!=0,"Cannot resolve fixture");const std::wstring directory=path;
    Require(CreateDirectoryW(directory.c_str(),nullptr)!=0,"Use a new fixture directory");
    gDllPath=directory+L"\\version.dll";
    if(std::wcscmp(argv[3],L"missing")!=0) {
      std::ofstream config(directory+L"\\aex.cfg");config<<"Log = 1\nRehabMode = ";
      config<<(std::wcscmp(argv[3],L"1")==0?"1":std::wcscmp(argv[3],L"0")==0?"0":"invalid");
      if(argc>=5)config<<"\nCheckForUpdates = "<<(std::wcscmp(argv[4],L"1")==0?"1":std::wcscmp(argv[4],L"0")==0?"0":"invalid");
      if(argc==6&&std::wcscmp(argv[5],L"missing")!=0)config<<"\nFilterScores = "<<(std::wcscmp(argv[5],L"1")==0?"1":std::wcscmp(argv[5],L"0")==0?"0":"invalid");
      Require(config.good(),"Cannot write fixture config");
    }
    Require(VerifiedGame(gExePath.c_str()),"Unsupported game");
    gMapped=reinterpret_cast<uint8_t*>(LoadLibraryExW(gExePath.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES));Require(gMapped!=nullptr,"Cannot map game");
    aex_update::gCreateProcess=&NoCheckProcess;
    score_blocklist::fetch=&NoBlocklistDownload;
    Require(DllMain(GetModuleHandleW(nullptr),DLL_PROCESS_ATTACH,nullptr)!=FALSE,"Production DLL initialization failed");
    for(const auto rva:community_assets::kFindCalls)
      Require(gMapped+rva+5+*reinterpret_cast<int32_t*>(gMapped+rva+1)==reinterpret_cast<uint8_t*>(&community_assets::Find),"Production community asset hook missing");
    Require(gCheckLaunches==0,"Update checker created a process while holding loader lock");
    const bool filter=std::wcscmp(argv[3],L"missing")==0||argc<6||std::wcscmp(argv[5],L"0")!=0;
    Require(score_blocklist::enabled==filter,"Production FilterScores config gating failed");
    Require(score_blocklist::started==0&&aex_update::gAfterUpdateCheck==(filter?&score_blocklist::StartOnce:nullptr),
      "Blocklist downloaded under loader lock or production startup callback missing");
    const bool on=std::wcscmp(argv[3],L"1")==0;
    Require((*reinterpret_cast<int*>(gMapped+rehab_mode::kFlag)!=0)==on&&rehab_mode::enabled==on,"Production config gating failed");
    const uint8_t original[]={0xb8,1,0,0,0},patched[]={0x33,0xc0,0x90,0x90,0x90};
    Require(std::memcmp(gMapped+rehab_mode::kSaveEligibility,on?patched:original,5)==0,"Production save-eligibility patch gating failed");
    JumpTo(gMapped,0xbd396,reinterpret_cast<void*>(&Version));
    void* value=nullptr;UINT length=0;
    Require(MenuVersion(nullptr,L"FileVersion",&value,&length)!=FALSE,"Menu version query failed");
    const std::wstring expected=std::wstring(L"2.3 / AEx ")+AEX_VERSION_WIDE+(on?L" / Rehab Mode":L"");
    Require(expected==static_cast<wchar_t*>(value)&&length==expected.size()+1,"Rehab menu indicator or length incorrect");
    const bool check=std::wcscmp(argv[3],L"missing")==0||argc<5||std::wcscmp(argv[4],L"1")==0;
    for(int query=0;query<3;query++)MenuVersion(nullptr,L"FileVersion",&value,&length);
    Require(aex_update::gCheckForUpdates==check&&gCheckLaunches==(check?1:0),"Startup check was not gated or launched more than once");
    const auto deadline=GetTickCount64()+5000;
    while(filter&&!InterlockedCompareExchange(&score_blocklist::finished,0,0)&&GetTickCount64()<deadline)Sleep(1);
    Require(filter?(score_blocklist::started&&score_blocklist::finished):(!score_blocklist::started&&!score_blocklist::finished),
      "Production blocklist task did not follow FilterScores");
    std::ifstream configFile(directory+L"\\aex.cfg");AexConfigPresence present{};
    ReadAexConfig(configFile,&present);
    for(bool found:present)Require(found,"Production startup did not create or complete aex.cfg");
    std::printf("PASS: FilterScores %s; missing key appended with default 1; explicit 0 skips blocklist startup.\n",filter?"on":"off");
    std::printf("PASS: production config creation/completion; update check %s, launch after loader initialization, once across repeated menu queries; no real processes.\n",check?"on":"off");
    std::printf("PASS: production DllMain with RehabMode %ls; all AEx patches installed, config and save protection gated, menu indicator verified. No game loop or Steam calls.\n",argv[3]);
    return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
