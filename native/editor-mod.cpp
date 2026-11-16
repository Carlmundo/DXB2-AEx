#include "editor-scripts.hpp"
#include "editor-steam.h"

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
  if(reason!=DLL_PROCESS_ATTACH)return TRUE;
  DisableThreadLibraryCalls(instance);
  // Install in Tools only. Forward every export to the untouched Steam API
  // in the game directory, using its absolute path to avoid proxy recursion.
  wchar_t path[MAX_PATH];const auto n=GetModuleFileNameW(instance,path,MAX_PATH);
  if(!n||n>=MAX_PATH)return FALSE;
  auto slash=wcsrchr(path,L'\\');if(!slash)return FALSE;*slash=0;
  slash=wcsrchr(path,L'\\');if(!slash)return FALSE;
  if(static_cast<size_t>(slash-path)+15>=MAX_PATH)return FALSE;
  wcscpy_s(slash,MAX_PATH-static_cast<size_t>(slash-path),L"\\steam_api.dll");
  const auto steam=LoadLibraryW(path);if(!steam||steam==instance)return FALSE;
  for(size_t i=0;i<sizeof(gSteam)/sizeof(gSteam[0]);i++) {
    gSteam[i]=GetProcAddress(steam,kSteamNames[i]?kSteamNames[i]:MAKEINTRESOURCEA(kSteamOrdinals[i]));
    if(!gSteam[i])return FALSE;
  }
  if(!GetModuleFileNameW(nullptr,path,MAX_PATH)||!editor_scripts::VerifiedExecutable(path))return TRUE;
  try {
    editor_scripts::Install(reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)));
    OutputDebugStringA("DX-Ball 2 AEx: editor 1 MiB script buffer installed.\n");
  }catch(const std::exception& error){OutputDebugStringA(error.what());}
  return TRUE;
}
