#include "patch.hpp"
#include "verified-game.hpp"
#include "board-format.hpp"
#include "geometry.hpp"
#include "save-format.hpp"
#include <intrin.h>
#include <fstream>

static uint8_t* gGame=nullptr;
static Geometry gGeometry;
static wchar_t gLogPath[MAX_PATH]={};
static bool gLoggingEnabled=false;
static FARPROC gVersion[17]={};
static const char* kVersionExports[]={"GetFileVersionInfoA","GetFileVersionInfoByHandle","GetFileVersionInfoExA","GetFileVersionInfoExW","GetFileVersionInfoSizeA","GetFileVersionInfoSizeExA","GetFileVersionInfoSizeExW","GetFileVersionInfoSizeW","GetFileVersionInfoW","VerFindFileA","VerFindFileW","VerInstallFileA","VerInstallFileW","VerLanguageNameA","VerLanguageNameW","VerQueryValueA","VerQueryValueW"};
static thread_local const void* gNormalized=nullptr;
static thread_local int gNormalizedLength=0;
static thread_local uint32_t gOriginalChecksum=0;
static thread_local const void* gSaveNormalized=nullptr;
static thread_local int gSaveNormalizedLength=0;
static thread_local uint32_t gSaveChecksum=0;

static bool ReadLogSetting(const wchar_t* path) noexcept {
  try {
    std::ifstream config(path);
    bool enabled=false;
    std::string line;
    bool firstLine=true;
    while(std::getline(config,line)) {
      if(firstLine&&line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
      firstLine=false;
      const auto comment=line.find('#');
      if(comment!=std::string::npos)line.erase(comment);
      const auto equals=line.find('=');
      if(equals==std::string::npos)continue;
      const auto trim=[](const std::string& text) {
        const auto first=text.find_first_not_of(" \t\r\n");
        if(first==std::string::npos)return std::string{};
        return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
      };
      if(trim(line.substr(0,equals))=="Log")enabled=trim(line.substr(equals+1))=="1";
    }
    return !config.bad()&&enabled;
  }catch(...){return false;}
}

static void Log(const char* message) noexcept {
  if(!gLoggingEnabled)return;
  HANDLE file=CreateFileW(gLogPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return;
  DWORD written;WriteFile(file,message,static_cast<DWORD>(std::strlen(message)),&written,nullptr);
  WriteFile(file,"\r\n",2,&written,nullptr);CloseHandle(file);
}
static void* GameAlloc(size_t size) {return reinterpret_cast<void* (__cdecl*)(size_t)>(gGame+0x111f9a)(size);}

static int __cdecl DecodeBoard(const void* input,int length,void** output,int* outputLength) noexcept {
  gNormalized=nullptr;
  try {
    const auto result=dxb::Normalize(static_cast<const uint8_t*>(input),static_cast<size_t>(length));
    auto* data=GameAlloc(result.bytes.size());if(!data)return 0;
    std::memcpy(data,result.bytes.data(),result.bytes.size());*output=data;*outputLength=static_cast<int>(result.bytes.size());
    if(result.hasOriginalChecksum) {
      gNormalized=data;gNormalizedLength=*outputLength-4;gOriginalChecksum=result.originalChecksum;
    }
    char message[512];sprintf_s(message,"Loaded %s boardset: %s (%u boards, %u columns)%s",result.rival?"Rival":"Anniversary",result.title.c_str(),result.boards,result.width,result.mirrored?"; mirrored upper half restored":"");Log(message);
    return 1;
  } catch(const std::exception& error) {
    Log(error.what());
    // Returning a short invalid payload lets the original loader display its
    // normal error path even for malformed raw-BDST inputs.
    auto* invalid=GameAlloc(4);if(!invalid)return 0;
    std::memcpy(invalid,"FAIL",4);*output=invalid;*outputLength=4;return 1;
  }
}
static uint32_t __cdecl BoardChecksum(const void* data,int length) noexcept {
  if(data==gNormalized&&length==gNormalizedLength) {gNormalized=nullptr;return gOriginalChecksum;}
  return dxb::Checksum(static_cast<const uint8_t*>(data),static_cast<size_t>(length));
}
static int __cdecl DecodeSave(const void* input,int length,void** output,int* outputLength) noexcept {
  gSaveNormalized=nullptr;
  try {
    const auto result=dxb::NormalizeSave(static_cast<const uint8_t*>(input),static_cast<size_t>(length));
    auto* data=GameAlloc(result.bytes.size());if(!data)return 0;
    std::memcpy(data,result.bytes.data(),result.bytes.size());*output=data;*outputLength=static_cast<int>(result.bytes.size());
    const uint32_t columns=result.version>=5?37:26,cellBytes=result.version>=5?740*16:520*4;
    WriteMemory(gGame+0x90598,&columns,4);WriteMemory(gGame+0x90540,&cellBytes,4);
    gSaveNormalized=data;gSaveNormalizedLength=*outputLength-4;gSaveChecksum=result.checksum;
    char message[256];sprintf_s(message,"Loaded save version %u: %zu records, %u stored cells per layer",result.version,result.records.size(),result.storedCells);Log(message);
    return 1;
  }catch(const std::exception& error){Log(error.what());return 0;}
}
static uint32_t __cdecl SaveChecksum(const void* data,int length) noexcept {
  if(data==gSaveNormalized&&length==gSaveNormalizedLength){gSaveNormalized=nullptr;return gSaveChecksum;}
  return dxb::Checksum(static_cast<const uint8_t*>(data),static_cast<size_t>(length));
}
static void ReplaceCall(uint32_t rva,void* target,uint32_t oldTarget) {
  auto* where=gGame+rva;
  if(where[0]!=0xe8 || where+5+*reinterpret_cast<int32_t*>(where+1)!=gGame+oldTarget)throw std::runtime_error("Unexpected loader call");
  auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(where+5));
  WriteMemory(where+1,&relative,4);
}
static int __fastcall BoardMargin(void* boardset,void*) {
  const auto width=*reinterpret_cast<int*>(static_cast<uint8_t*>(boardset)+0x26ac);
  return (*reinterpret_cast<int*>(gGame+0x1a4094)-PlayfieldWidth(width))/2;
}
static int __fastcall BoardBorder(void* boardset,void* unused) {return (std::min)(30,BoardMargin(boardset,unused));}
static int __fastcall ResizeBoardBricks(void* images,void*,int width,int height,int flags) {
  const auto* boardset=static_cast<uint8_t*>(images)-0x128;
  const bool rival=*reinterpret_cast<const int*>(boardset+0x26ac)==37;
  gGeometry.Set(rival);
  using Resize=int (__thiscall*)(void*,int,int,int);
  return reinterpret_cast<Resize>(gGame+0x58330)(images,rival?kRivalRasterWidth:width,height,flags);
}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION || fault->ExceptionRecord->ExceptionCode==EXCEPTION_STACK_OVERFLOW) {
    char message[256];sprintf_s(message,"FAULT %08lX at %p (game RVA %08X)",fault->ExceptionRecord->ExceptionCode,fault->ExceptionRecord->ExceptionAddress,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gGame)));Log(message);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#define FORWARD_VERSION(name,offset) extern "C" __declspec(naked) void Proxy##name() {__asm jmp dword ptr [gVersion+offset]}
FORWARD_VERSION(GetFileVersionInfoA,0)
FORWARD_VERSION(GetFileVersionInfoByHandle,4)
FORWARD_VERSION(GetFileVersionInfoExA,8)
FORWARD_VERSION(GetFileVersionInfoExW,12)
FORWARD_VERSION(GetFileVersionInfoSizeA,16)
FORWARD_VERSION(GetFileVersionInfoSizeExA,20)
FORWARD_VERSION(GetFileVersionInfoSizeExW,24)
FORWARD_VERSION(GetFileVersionInfoSizeW,28)
FORWARD_VERSION(GetFileVersionInfoW,32)
FORWARD_VERSION(VerFindFileA,36)
FORWARD_VERSION(VerFindFileW,40)
FORWARD_VERSION(VerInstallFileA,44)
FORWARD_VERSION(VerInstallFileW,48)
FORWARD_VERSION(VerLanguageNameA,52)
FORWARD_VERSION(VerLanguageNameW,56)
FORWARD_VERSION(VerQueryValueA,60)
FORWARD_VERSION(VerQueryValueW,64)

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
  if(reason!=DLL_PROCESS_ATTACH)return TRUE;
  DisableThreadLibraryCalls(instance);
  wchar_t system[MAX_PATH];const auto n=GetSystemDirectoryW(system,MAX_PATH);
  if(!n||n+13>=MAX_PATH)return FALSE;wcscat_s(system,L"\\version.dll");
  const auto version=LoadLibraryW(system);if(!version)return FALSE;
  for(size_t i=0;i<17;i++) {gVersion[i]=GetProcAddress(version,kVersionExports[i]);if(!gVersion[i])return FALSE;}
  const auto pathLength=GetModuleFileNameW(instance,gLogPath,MAX_PATH);
  if(!pathLength||pathLength>=MAX_PATH)return TRUE;
  auto slash=wcsrchr(gLogPath,L'\\');
  if(!slash)return TRUE;
  const auto remaining=MAX_PATH-static_cast<size_t>(slash+1-gLogPath);
  wcscpy_s(slash+1,remaining,L"aex.cfg");gLoggingEnabled=ReadLogSetting(gLogPath);
  wcscpy_s(slash+1,remaining,L"aex.log");
  wchar_t executable[MAX_PATH];if(!GetModuleFileNameW(nullptr,executable,MAX_PATH))return TRUE;
  if(!VerifiedGame(executable)) {Log("Unsupported executable: native mod disabled; original game left unchanged.");return TRUE;}
  gGame=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  try {
    gGeometry.base=gGame;gGeometry.Verify();
    if(gGame[0x90060]!=5||gGame[0x91655]!=5||*reinterpret_cast<uint32_t*>(gGame+0x90598)!=26||*reinterpret_cast<uint32_t*>(gGame+0x90540)!=0x820)
      throw std::runtime_error("Unexpected save format operands");
    if(gGame[0x55e44]!=0x74||gGame[0x55e45]!=0x50)throw std::runtime_error("Unexpected plain-BDST branch");
    ApplyGridPatches(gGame);
    ReplaceCall(0x55e56,reinterpret_cast<void*>(&DecodeBoard),0xab8c0);
    ReplaceCall(0x55ef4,reinterpret_cast<void*>(&BoardChecksum),0xab700);
    ReplaceCall(0x8ff2e,reinterpret_cast<void*>(&DecodeSave),0xab8c0);
    ReplaceCall(0x8ffbf,reinterpret_cast<void*>(&SaveChecksum),0xab700);
    // Version 6 identifies the expanded save layout. Existing official and
    // experimental v5 files are adapted on read, without rewriting the file.
    const uint8_t saveVersion=6;WriteMemory(gGame+0x90060,&saveVersion,1);WriteMemory(gGame+0x91655,&saveVersion,1);
    ReplaceCall(0x566bf,reinterpret_cast<void*>(&ResizeBoardBricks),0x58330);
    JumpTo(gGame,0x55a70,reinterpret_cast<void*>(&BoardMargin));
    JumpTo(gGame,0x53720,reinterpret_cast<void*>(&BoardBorder));
    gGeometry.Install();
    // Route raw and encoded board files through the same validated adapter.
    const uint8_t nops[2]={0x90,0x90};WriteMemory(gGame+0x55e44,nops,2);
    AddVectoredExceptionHandler(1,Fault);
    Log("DX-Ball 2 AEx experimental native grid and board adapter installed.");
  } catch(const std::exception& error) {Log(error.what());return FALSE;}
  return TRUE;
}
