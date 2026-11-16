#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv) {
  if(argc!=3){std::fprintf(stderr,"Usage: editor-proxy-tests.exe Tools/steam_api.dll original-steam_api.dll\n");return 2;}
  try {
    const auto proxy=LoadLibraryW(argv[1]);Require(proxy!=nullptr,"Cannot load editor proxy");
    const auto steam=LoadLibraryW(argv[2]);Require(steam!=nullptr&&steam!=proxy,"Proxy did not load distinct original Steam DLL");
    const auto* bytes=reinterpret_cast<const unsigned char*>(steam);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(bytes+reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes)->e_lfanew);
    const auto* table=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    const auto* functions=reinterpret_cast<const DWORD*>(bytes+table->AddressOfFunctions);
    const auto* names=reinterpret_cast<const DWORD*>(bytes+table->AddressOfNames);
    const auto* ordinals=reinterpret_cast<const WORD*>(bytes+table->AddressOfNameOrdinals);
    unsigned forwarded=0;
    for(DWORD i=0;i<table->NumberOfFunctions;i++)if(functions[i]) {
      const auto ordinal=MAKEINTRESOURCEA(table->Base+i);
      const auto expected=GetProcAddress(steam,ordinal),actual=GetProcAddress(proxy,ordinal);
      Require(expected!=nullptr&&actual!=nullptr,"Missing ordinal export");
      const auto* thunk=reinterpret_cast<const unsigned char*>(actual);
      Require(thunk[0]==0xff&&thunk[1]==0x25,"Steam forwarding thunk changes calling convention");
      FARPROC* slot=nullptr;std::memcpy(&slot,thunk+2,sizeof(slot));
      Require(*slot==expected,"Steam export does not forward to the original function");forwarded++;
    }
    for(DWORD i=0;i<table->NumberOfNames;i++) {
      const auto* name=reinterpret_cast<const char*>(bytes+names[i]);
      Require(GetProcAddress(proxy,name)==GetProcAddress(proxy,MAKEINTRESOURCEA(table->Base+ordinals[i])),"Steam named export/ordinal mismatch");
    }
    using GetUser=int (__cdecl*)();
    const auto actual=reinterpret_cast<GetUser>(GetProcAddress(proxy,"SteamAPI_GetHSteamUser"));
    const auto expected=reinterpret_cast<GetUser>(GetProcAddress(steam,"SteamAPI_GetHSteamUser"));
    Require(actual&&expected&&actual()==expected(),"Forwarded Steam call returned a different result");
    std::printf("PASS: all %u Steam exports forward with original names, ordinals and ABI; real Steam call returned unchanged\n",forwarded);
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s (Windows error %lu)\n",error.what(),GetLastError());return 1;}
}
