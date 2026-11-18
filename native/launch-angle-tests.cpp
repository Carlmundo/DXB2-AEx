// Exercise native fresh-ball setup, attached positioning and release in a
// mapped executable. No game session, settings or player saves are opened.
#include "launch-angle.hpp"
#include "verified-game.hpp"
#include "config.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <sstream>

static uint8_t* gBase=nullptr;
static void* gObject=nullptr;
static void* gEntry=nullptr;
template<class T> static T& At(uint32_t rva) {return *reinterpret_cast<T*>(gBase+rva);}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
  std::fprintf(stderr,"Native launch fault %08lX at RVA %08X\n",fault->ExceptionRecord->ExceptionCode,
    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gBase)));
  TerminateProcess(GetCurrentProcess(),3);return EXCEPTION_CONTINUE_SEARCH;
}
static double __vectorcall Sin(double angle) {return std::sin(angle);}
static double __vectorcall Cos(double angle) {return std::cos(angle);}
static __declspec(naked) void Execute() {
  __asm {
    push ebp
    mov ebp,esp
    sub esp,200h
    push ebx
    push esi
    push edi
    mov eax,gObject
    mov edi,eax
    call dword ptr [gEntry]
    pop edi
    pop esi
    pop ebx
    mov esp,ebp
    pop ebp
    ret
  }
}
static void Run(uint32_t rva) {gEntry=gBase+rva;Execute();}
static void Set(void* ball,int key,uint32_t bits) {
  reinterpret_cast<void (__thiscall*)(void*,int,uint32_t)>(gBase+0x4db00)(ball,key,bits);
}
static uint32_t Get(void* ball,int key) {
  return *reinterpret_cast<uint32_t* (__thiscall*)(void*,int)>(gBase+0x61800)(ball,key);
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=3){std::fprintf(stderr,"Usage: launch-angle-tests.exe game.exe 0|1\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    for(const char* value:{"","LaunchAngle = 0","LaunchAngle = 2","LaunchAngle = true","# LaunchAngle = 1","LaunchAngle = 01","LaunchAngle = 1x"}) {
      std::istringstream input(value);Require(!ReadAexConfig(input).launchAngle,"LaunchAngle enabled without exact value 1");
    }
    std::istringstream enabledConfig("\xef\xbb\xbfLaunchAngle = 1 # restore launch\n");
    Require(ReadAexConfig(enabledConfig).launchAngle,"LaunchAngle = 1 was not enabled");
    std::istringstream duplicate("LaunchAngle = 1\nLaunchAngle = 0\n");
    Require(!ReadAexConfig(duplicate).launchAngle,"Last config setting did not win");
    std::istringstream bad("LaunchAngle = 1");bad.setstate(std::ios::badbit);
    Require(!ReadAexConfig(bad).launchAngle,"Read failure enabled LaunchAngle");
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Require(gBase!=nullptr,"Could not map game");
    launch_angle::Verify(gBase);
    const uint8_t wrong=0xcc,original=gBase[0x91471];WriteMemory(gBase+0x91471,&wrong,1);
    bool rejected=false;try{launch_angle::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&gBase[0x62db5]==0x8d,"Invalid callsite was accepted or verification partially patched code");
    WriteMemory(gBase+0x91471,&original,1);
    const bool enabled=argv[2][0]==L'1';if(enabled)launch_angle::Install(gBase);
    // Supply the uninitialized EXE CRT's allocation/math dependencies only.
    JumpTo(gBase,0xd6d9c,reinterpret_cast<void*>(&std::malloc));
    JumpTo(gBase,0xd6dcf,reinterpret_cast<void*>(&std::free));
    JumpTo(gBase,0xe6280,reinterpret_cast<void*>(&std::memcpy));
    JumpTo(gBase,0x113120,reinterpret_cast<void*>(&Sin));
    JumpTo(gBase,0x112410,reinterpret_cast<void*>(&Cos));
    const uint8_t done=0xc3;
    for(uint32_t rva:{0x62e8bu,0x8a6f5u,0x914d7u,0x5ead4u})WriteMemory(gBase+rva,&done,1);
    int cases=0;
    for(uint32_t start:{0x62da0u,0x8a652u,0x9146bu})for(bool slow:{false,true})for(float width:{54.0f,109.0f,219.0f,438.0f}) {
      std::array<uint8_t,0x88> ball{};
      int zero=0;uint8_t dummy=0;
      reinterpret_cast<void* (__thiscall*)(void*,int*,uint8_t*)>(gBase+0x48760)(ball.data()+0x68,&zero,&dummy);
      *reinterpret_cast<int*>(ball.data()+0x40)=1;*reinterpret_cast<int*>(ball.data()+0x44)=1;
      *reinterpret_cast<int*>(ball.data()+0x48)=1;Set(ball.data(),0,123456);
      Set(ball.data(),5,0x123456);At<void*>(0x1a4118)=nullptr;
      At<int>(0x1b91dc)=slow;At<float>(0x1bef88)=330.0f;
      gObject=ball.data();Run(start);
      Require(At<void*>(0x1a4118)==ball.data(),"Native append lost the new ball");
      Require(Get(ball.data(),8)==(enabled?0x40c00000u:0u),"Incorrect launch offset bits");
      Require(Get(ball.data(),0)==(!enabled&&start==0x62da0?6u:123456u),"Launch setup corrupted the timestamp");
      Require(Get(ball.data(),5)==0x123456,"Launch setup changed another variable");
      At<float>(0x1a7140)=640;At<float>(0x1a7144)=680;At<float>(0x1a7148)=width*0.5f;
      At<int>(0x1a70bc)=123500;Run(0x5e1a0);
      const float x=*reinterpret_cast<float*>(ball.data()+0x0c);
      Require(x==(enabled?646.0f:640.0f),"Native attached positioning did not use the expected offset");
      At<uint8_t>(0x1bef94)=1;At<float>(0x1bef74)=450;At<float>(0x1bef7c)=1;
      reinterpret_cast<void (__cdecl*)()>(gBase+0x62930)();
      const float vx=*reinterpret_cast<float*>(ball.data()+0x1c),vy=*reinterpret_cast<float*>(ball.data()+0x20);
      const float speed=slow?165.0f:330.0f;
      const double angle=enabled?6.0/(1.3f*width*0.5f):0;
      Require(std::abs(vx-std::sin(angle)*speed)<0.001&&std::abs(vy+std::cos(angle)*speed)<0.001,"Native release velocity did not match the original angle");
      Require((*reinterpret_cast<int*>(ball.data()+0x48)&1)==0,"Native release left ball attached");
      ++cases;
    }
    // Allocation failure remains a harmless null append at every spawn site.
    for(uint32_t start:{0x62da0u,0x8a652u,0x9146bu}) {
      At<void*>(0x1a4118)=nullptr;gObject=nullptr;Run(start);
      Require(At<void*>(0x1a4118)==nullptr,"Null append changed the object list");
    }
    std::printf("PASS: LaunchAngle %s; strict config gating, guarded installation, %d native spawn/position/release cases, timestamp and other-variable preservation, null spawns\n",enabled?"enabled":"disabled",cases);
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
