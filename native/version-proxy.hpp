#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>

static constexpr const char* kVersionExports[]={"GetFileVersionInfoA","GetFileVersionInfoByHandle","GetFileVersionInfoExA","GetFileVersionInfoExW","GetFileVersionInfoSizeA","GetFileVersionInfoSizeExA","GetFileVersionInfoSizeExW","GetFileVersionInfoSizeW","GetFileVersionInfoW","VerFindFileA","VerFindFileW","VerInstallFileA","VerInstallFileW","VerLanguageNameA","VerLanguageNameW","VerQueryValueA","VerQueryValueW"};
static constexpr size_t kVersionExportCount=sizeof(kVersionExports)/sizeof(kVersionExports[0]);

// Wine does not export this API. Preserve its documented four-argument WINAPI
// ABI so an unexpected caller gets an ordinary failure instead of a null jump.
// https://learn.microsoft.com/windows/win32/menurc/getfileversioninfobyhandle
inline BOOL WINAPI MissingFileVersionInfoByHandle(DWORD,HANDLE,LPVOID* data,PDWORD length) noexcept {
  if(data)*data=nullptr;
  if(length)*length=0;
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

template<class Resolver>
inline bool ResolveVersionExports(HMODULE module,FARPROC (&exports)[kVersionExportCount],Resolver resolve) {
  for(size_t i=0;i<kVersionExportCount;i++) {
    exports[i]=resolve(module,kVersionExports[i]);
    // Slot 1 is optional; the remaining Windows/Wine exports stay mandatory.
    if(!exports[i]&&i==1)exports[i]=reinterpret_cast<FARPROC>(&MissingFileVersionInfoByHandle);
    if(!exports[i])return false;
  }
  return true;
}
