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
int wmain(int argc,wchar_t** argv) {
  if(argc!=4){std::fprintf(stderr,"Usage: rehab-startup-tests.exe game.exe fixture-directory missing|0|1|invalid\n");return 2;}
  try {
    wchar_t path[MAX_PATH];Require(GetFullPathNameW(argv[1],MAX_PATH,path,nullptr)!=0,"Cannot resolve game");gExePath=path;
    Require(GetFullPathNameW(argv[2],MAX_PATH,path,nullptr)!=0,"Cannot resolve fixture");const std::wstring directory=path;
    Require(CreateDirectoryW(directory.c_str(),nullptr)!=0,"Use a new fixture directory");
    gDllPath=directory+L"\\version.dll";
    if(std::wcscmp(argv[3],L"missing")!=0) {
      std::ofstream config(directory+L"\\aex.cfg");config<<"Log = 1\nRehabMode = ";
      config<<(std::wcscmp(argv[3],L"1")==0?"1":std::wcscmp(argv[3],L"0")==0?"0":"invalid");
      Require(config.good(),"Cannot write fixture config");
    }
    Require(VerifiedGame(gExePath.c_str()),"Unsupported game");
    gMapped=reinterpret_cast<uint8_t*>(LoadLibraryExW(gExePath.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES));Require(gMapped!=nullptr,"Cannot map game");
    Require(DllMain(GetModuleHandleW(nullptr),DLL_PROCESS_ATTACH,nullptr)!=FALSE,"Production DLL initialization failed");
    const bool on=std::wcscmp(argv[3],L"1")==0;
    Require((*reinterpret_cast<int*>(gMapped+rehab_mode::kFlag)!=0)==on&&rehab_mode::enabled==on,"Production config gating failed");
    const uint8_t original[]={0xb8,1,0,0,0},patched[]={0x33,0xc0,0x90,0x90,0x90};
    Require(std::memcmp(gMapped+rehab_mode::kSaveEligibility,on?patched:original,5)==0,"Production save-eligibility patch gating failed");
    JumpTo(gMapped,0xbd396,reinterpret_cast<void*>(&Version));
    void* value=nullptr;UINT length=0;
    Require(MenuVersion(nullptr,L"FileVersion",&value,&length)!=FALSE,"Menu version query failed");
    const std::wstring expected=std::wstring(L"2.3 / AEx ")+AEX_VERSION_WIDE+(on?L" / Rehab Mode":L"");
    Require(expected==static_cast<wchar_t*>(value)&&length==expected.size()+1,"Rehab menu indicator or length incorrect");
    std::printf("PASS: production DllMain with RehabMode %ls; all AEx patches installed, config and save protection gated, menu indicator verified. No game loop or Steam calls.\n",argv[3]);
    return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
