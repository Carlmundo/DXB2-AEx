#include "version-proxy.hpp"
#include <cstring>
static const char* gHiddenExport=nullptr;
static FARPROC WINAPI TestGetProcAddress(HMODULE module,LPCSTR name) {
  if(gHiddenExport&&std::strcmp(name,gHiddenExport)==0)return nullptr;
  return GetProcAddress(module,name);
}
// Exercise the production DllMain and exported forwarding stub, hiding one
// system export at a time. The test EXE is unsupported, so no game is patched.
#define GetProcAddress TestGetProcAddress
#include "mod.cpp"
#undef GetProcAddress
#include <cstdio>
#include <cstring>
#include <stdexcept>
static void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static INT_PTR WINAPI Stub(){return 1;}
int main() {
  try {
    FARPROC exports[kVersionExportCount]={};
    const auto stub=&Stub;
    const auto all=[&](HMODULE,const char*){return stub;};
    Require(ResolveVersionExports(nullptr,exports,all),"Windows exports rejected");
    for(const auto proc:exports)Require(proc==stub,"Existing export replaced");
    const auto wine=[&](HMODULE,const char* name)->FARPROC {
      return std::strcmp(name,"GetFileVersionInfoByHandle")==0?nullptr:stub;
    };
    Require(ResolveVersionExports(nullptr,exports,wine),"Missing optional export prevented initialization");
    for(size_t i=0;i<kVersionExportCount;i++)if(i!=1)Require(exports[i]==stub,"Required export replaced");
    using ByHandle=BOOL (WINAPI*)(DWORD,HANDLE,LPVOID*,PDWORD);
    const auto fallback=reinterpret_cast<ByHandle>(exports[1]);
    for(int i=0;i<1000;i++) {
      LPVOID data=reinterpret_cast<void*>(1);DWORD length=123;
      SetLastError(ERROR_SUCCESS);
      Require(fallback(3,nullptr,&data,&length)==FALSE,"Fallback reported success");
      Require(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED,"Fallback error missing");
      Require(data==nullptr&&length==0,"Fallback output buffers not cleared");
    }
    Require(fallback(0,nullptr,nullptr,nullptr)==FALSE&&GetLastError()==ERROR_CALL_NOT_IMPLEMENTED,"Null output fallback failed");
    for(size_t missing=0;missing<kVersionExportCount;missing++)if(missing!=1) {
      const auto incomplete=[&](HMODULE,const char* name)->FARPROC {
        return std::strcmp(name,kVersionExports[missing])==0?nullptr:stub;
      };
      Require(!ResolveVersionExports(nullptr,exports,incomplete),"Missing required export accepted");
    }
    wchar_t path[MAX_PATH];const auto n=GetSystemDirectoryW(path,MAX_PATH);
    Require(n&&n+13<MAX_PATH,"Cannot locate system version.dll");wcscat_s(path,L"\\version.dll");
    const auto system=LoadLibraryW(path);Require(system!=nullptr,"Cannot load system version.dll");
    Require(ResolveVersionExports(system,exports,&GetProcAddress),"Real system exports rejected");
    const auto native=GetProcAddress(system,"GetFileVersionInfoByHandle");
    if(native)Require(exports[1]==native,"Real Windows API was replaced by fallback");
    FreeLibrary(system);
    const auto instance=GetModuleHandleW(nullptr);
    gHiddenExport=nullptr;
    Require(DllMain(instance,DLL_PROCESS_ATTACH,nullptr)==TRUE,"Normal DLL initialization failed");
    gHiddenExport="GetFileVersionInfoByHandle";
    Require(DllMain(instance,DLL_PROCESS_ATTACH,nullptr)==TRUE,"DLL initialization rejected missing optional export");
    const auto proxy=reinterpret_cast<ByHandle>(&ProxyGetFileVersionInfoByHandle);
    LPVOID data=reinterpret_cast<void*>(1);DWORD length=123;
    Require(proxy(0,nullptr,&data,&length)==FALSE&&GetLastError()==ERROR_CALL_NOT_IMPLEMENTED&&data==nullptr&&length==0,"Exported proxy did not reach safe fallback");
    gHiddenExport="GetFileVersionInfoW";
    Require(DllMain(instance,DLL_PROCESS_ATTACH,nullptr)==FALSE,"DLL initialization accepted missing required export");
    std::puts("PASS: production DLL initialization and exported forwarding stub with optional export hidden; required export failure preserved");
    std::puts("PASS: existing Windows exports forwarded; missing optional export accepted; stdcall fallback returns error 120; required exports remain mandatory");
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
