// Execute original and Anniversary code in isolated mappings. Rendering,
// Steam, live settings, player files and real score submission are not used.
#include "rehab-mode.hpp"
#include "verified-game.hpp"
#include "config.hpp"
#include "community-leaderboards.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

static uint8_t* gBase=nullptr;
static void* gEntry=nullptr;
static void* gObject=nullptr;
static int gEax=0,gEdx=2000,gResult=0,gPractice=0;
static float gFloat=0;
template<class T> static T& At(uint32_t rva) {return *reinterpret_cast<T*>(gBase+rva);}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* e) {
  if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
  std::fprintf(stderr,"Rehab test fault at RVA %08X\n",static_cast<uint32_t>(e->ContextRecord->Eip-reinterpret_cast<uintptr_t>(gBase)));
  TerminateProcess(GetCurrentProcess(),3);return EXCEPTION_CONTINUE_SEARCH;
}
static __declspec(naked) void Execute() {
  __asm {
    push ebp
    push ebx
    push esi
    push edi
    sub esp,400h
    lea ebp,[esp+200h]
    mov ebx,gObject
    mov esi,ebx
    mov edi,ebx
    mov eax,gEax
    xor ecx,ecx
    mov edx,gEdx
    call dword ptr [gEntry]
    mov gResult,eax
    add esp,400h
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
  }
}
static void Run(uint32_t rva) {gEntry=gBase+rva;Execute();}
static int __cdecl Practice() {return gPractice;}
static double __vectorcall Sqrt(double x) {return std::sqrt(x);}
static __declspec(naked) void LegacySqrt() {__asm {fsqrt} __asm {ret}}
static __declspec(naked) void CaptureFp() {__asm {fstp dword ptr [gFloat]} __asm {ret}}
static __declspec(naked) void CaptureXmm() {__asm {movss dword ptr [gFloat],xmm0} __asm {ret}}
static void Stop(uint32_t rva) {const uint8_t done=0xc3;WriteMemory(gBase+rva,&done,1);}
static void* Legacy(const wchar_t* path) {
  const uint8_t hash[]={0x98,0x1d,0x0c,0xdf,0x27,0x5f,0x60,0x17,0x5b,0xfd,0x67,0xd4,0x8f,0xd5,0x11,0x91,0x20,0xd7,0x62,0x1c,0xc4,0x69,0x45,0xaf,0x59,0x17,0x21,0x90,0xac,0x54,0x7c,0x2b};
  Require(VerifiedFileSha256(path,hash),"Unsupported original executable");
  std::ifstream f(path,std::ios::binary);std::vector<uint8_t> b(std::istreambuf_iterator<char>(f),{});
  auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(b.data()+reinterpret_cast<IMAGE_DOS_HEADER*>(b.data())->e_lfanew);
  auto* mapped=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
  Require(mapped!=nullptr,"Cannot map original");
  std::memcpy(mapped,b.data(),nt->OptionalHeader.SizeOfHeaders);
  const auto* section=IMAGE_FIRST_SECTION(nt);
  for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)std::memcpy(mapped+section[i].VirtualAddress,b.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
  // 1.3.2 has no relocation directory. Rebase the exact absolute-address
  // operands in the exercised snippets; leave opcodes/relative branches and
  // constants intact. The fingerprint above guards this explicit inventory.
  const auto delta=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mapped))-nt->OptionalHeader.ImageBase;
  for(unsigned i=0;i<10;++i)for(unsigned operand:{0x2a39u,0x2a3fu})*reinterpret_cast<uint32_t*>(mapped+operand+17*i)+=delta;
  for(unsigned operand:{0x46a6u,0x46b1u,0x46b9u,0x4a50u,0x8fd6u,0x8fdeu,0x8fe5u,0x8ffcu,0x9020u,0x9030u,0x903fu,0x9063u,0x9068u,0xf08au,0xf092u,0xf09au,0xf0a2u,0xf0a8u,0xf0b4u})
    *reinterpret_cast<uint32_t*>(mapped+operand)+=delta;
  return mapped;
}
static void Original(const wchar_t* path) {
  gBase=static_cast<uint8_t*>(Legacy(path));
  Require(At<float>(0x3d03c)==100,"Original default speed differs");
  Stop(0x2e7e);Stop(0x46bd);Stop(0xf0b8);Stop(0x4a64);
  JumpTo(gBase,0x1ad30,reinterpret_cast<void*>(&LegacySqrt));
  // Rebase the two operands in the eight-ball speed selector too.
  const auto delta=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gBase))-0x400000;
  for(uint32_t operand:{0x6bcbu,0x6bd4u,0x6bdcu})At<uint32_t>(operand)+=delta;
  JumpTo(gBase,0x6be0,reinterpret_cast<void*>(&CaptureFp));
  std::array<uint8_t,0x90> ball{};
  const float oldEightBallSpeed=At<float>(0x2f8e0);
  auto* velocity=reinterpret_cast<float*>(ball.data()+0x1c);
  for(int mode:{0,1}) {
    At<int>(0x50d28)=mode;
    gObject=nullptr;At<int>(0x3d048)=3;Run(0x46a4);
    Require(At<int>(0x3d048)==(mode?3:2),"Original death behavior differs");
    gEax=static_cast<int>(reinterpret_cast<uintptr_t>(ball.data()));Run(0x4a4e);gEax=0;
    Require(*reinterpret_cast<int*>(ball.data()+0x64)==(mode?120:220),"Original fresh-ball speed differs");
    for(int i=0;i<10;++i) {
      Run(0x2a37+17*i);const float speed=25.0f*(i+1);
      Require(At<float>(0x3d03c)==speed,"Original number-key speed differs");
      Run(0x6bc9);Require(gFloat==(mode?speed:oldEightBallSpeed),"Original eight-ball speed differs");
      velocity[0]=180;velocity[1]=-240;
      reinterpret_cast<void (__cdecl*)(void*,float)>(gBase+0x8f90)(ball.data(),speed);
      Require(std::abs(std::hypot(velocity[0],velocity[1])-(mode?speed:(std::max)(150.0f,speed)))<0.001f,"Original selected speed clamp differs");
    }
    // Original score gate: qualifying score, cheats off, no bonus board.
    gObject=nullptr;At<int>(0x15d1a8)=1000;At<int>(0x50d2c)=0;At<int>(0x50e20)=0;Run(0xf088);
    Require(At<int>(0x15d1d8)==!mode,"Original Rehab score exclusion differs");
  }
  VirtualFree(gBase,0,MEM_RELEASE);gBase=nullptr;
  std::puts("PASS: original 1.3.2 native death and high-score gates, ten numeric-key setters, velocity normalization and Rehab speed clamping (mode off/on).");
}
static void Config() {
  for(const char* text:{"","RehabMode=0","RehabMode=2","RehabMode=true","RehabMode=01","RehabMode=1x","rehabmode=1","# RehabMode=1","OtherRehabMode=1","RehabMode=1\nRehabMode=0"}) {
    std::istringstream f(text);Require(!ReadAexConfig(f).rehabMode,"RehabMode enabled without exact value 1");
  }
  std::istringstream on("\xef\xbb\xbf RehabMode = 1 # rehab\r\n");Require(ReadAexConfig(on).rehabMode,"RehabMode=1 not enabled");
  std::istringstream bad("RehabMode=1");bad.setstate(std::ios::badbit);Require(!ReadAexConfig(bad).rehabMode,"Unreadable config enabled RehabMode");
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=4){std::fprintf(stderr,"Usage: rehab-mode-tests.exe anniversary.exe original-1.3.2.exe 0|1\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    Config();Original(argv[2]);Require(VerifiedGame(argv[1]),"Unsupported Anniversary executable");
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));Require(gBase!=nullptr,"Cannot map Anniversary");
    rehab_mode::Verify(gBase);
    for(uint32_t site:{rehab_mode::kConsumers[0],rehab_mode::kConsumers[7],rehab_mode::kSaveEligibility,0x4e164u}) {
      const uint8_t original=gBase[site],wrong=0xcc;WriteMemory(gBase+site,&wrong,1);
      bool rejected=false;try{rehab_mode::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected&&At<int>(rehab_mode::kFlag)==0&&!rehab_mode::enabled,"Failed validation partially enabled RehabMode");
      WriteMemory(gBase+site,&original,1);
    }
    const bool enabled=argv[3][0]==L'1';
    if(enabled)rehab_mode::Install(gBase);
    Require((At<int>(rehab_mode::kFlag)!=0)==enabled&&rehab_mode::enabled==enabled,"Incorrect feature gating");
    community_leaderboards::Install(gBase);
    JumpTo(gBase,0x86f00,reinterpret_cast<void*>(&Practice));JumpTo(gBase,0x1132f0,reinterpret_cast<void*>(&Sqrt));
    for(uint32_t rva:{0x4e510u,0x62495u,0x960dfu,0x8f17au,0x90e7eu,0x5d6f3u})Stop(rva);
    JumpTo(gBase,0x83c90,reinterpret_cast<void*>(&CaptureXmm));
    // Native death handling still works with zero paddles and practice mode.
    for(int lives:{0,1,3,9})for(int practice:{0,1}) {
      gPractice=practice;At<int>(0x1a4080)=lives;Run(0x62473);
      Require(At<int>(0x1a4080)==lives-(!enabled&&!practice),"Anniversary death consumed a Rehab paddle");
    }
    std::array<uint8_t,0x90> ball{};gObject=ball.data();
    auto* velocity=reinterpret_cast<float*>(ball.data()+0x1c);
    At<float>(0x1bef7c)=225;At<float>(0x1bef78)=450;At<float>(0x1bef74)=650;
    for(int key=0;key<10;++key) {
      // Resolve actual keyboard dispatch, not just the known handler addresses.
      const int vk=key==9?'0':'1'+key;
      const auto index=gBase[0x4e6b8+vk-0x1b];
      const auto handler=At<uint32_t>(0x4e65c+index*4)-static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gBase));
      Run(handler);const float speed=25.0f*(key+1);Require(At<float>(rehab_mode::kSpeed)==speed,"Anniversary numeric dispatcher differs");
      Run(0x83c75);Require(gFloat==(enabled?speed:450.0f),"Anniversary eight-ball speed differs");
      velocity[0]=180;velocity[1]=-240;
      Run(0x5d6d3);
      Require(std::abs(std::hypot(velocity[0],velocity[1])-(enabled?speed:300))<0.001f,"Moving-ball speed control differs");
      Require(velocity[0]>0&&velocity[1]<0,"Speed change reversed direction");
      for(float request:{0.0f,25.0f,100.0f,300.0f,900.0f}) {
        velocity[0]=180;velocity[1]=-240;
        reinterpret_cast<void (__cdecl*)(void*,float)>(gBase+0x8b7d0)(ball.data(),request);
        const float normal=(std::max)(225.0f,request?request:302.25f);
        Require(std::abs(std::hypot(velocity[0],velocity[1])-(enabled?(std::min)(normal,speed):normal))<0.001f,"Native speed-adjustment cap differs");
      }
    }
    // Fresh ball creation on a new life, new board and player switch uses
    // half the selected game mode's starting speed in Rehab Mode.
    JumpTo(gBase,0xd6d9c,reinterpret_cast<void*>(&std::malloc));
    JumpTo(gBase,0xd6dcf,reinterpret_cast<void*>(&std::free));
    JumpTo(gBase,0xe6280,reinterpret_cast<void*>(&std::memcpy));
    for(uint32_t end:{0x62e8bu,0x8a6f5u,0x914d7u})Stop(end);
    for(uint32_t start:{0x62da0u,0x8a652u,0x9146bu})for(float speed:{265.0f,330.0f,400.0f}) {
      std::array<uint8_t,0x90> fresh{};int zero=0;uint8_t dummy=0;
      reinterpret_cast<void* (__thiscall*)(void*,int*,uint8_t*)>(gBase+0x48760)(fresh.data()+0x68,&zero,&dummy);
      At<void*>(0x1a4118)=nullptr;At<float>(0x1bef88)=speed;gObject=fresh.data();
      gEax=static_cast<int>(reinterpret_cast<uintptr_t>(gObject));Run(start);gEax=0;
      const auto* value=reinterpret_cast<float* (__thiscall*)(void*,int)>(gBase+0x61800)(fresh.data(),6);
      Require(*value==(enabled?speed/2:speed),"Fresh-ball Rehab launch speed differs");
    }
    // The same native gate owns local name-entry state 1, followed by online
    // post-score state 2. Source restrictions are patched exactly as in AEx.
    gObject=nullptr;
    for(int cheat:{0,1})for(int eligible:{0,1})for(int bonus:{0,1})for(int score:{500,1000,2000}) {
      At<int>(0x1b91d0)=cheat;At<int>(0x1b91d4)=eligible;At<int>(0x1a7170)=bonus;
      At<int>(0x1d2c70)=1000;gEdx=score;At<int>(0x1d2c34)=-1;Run(0x9609f);
      Require(At<int>(0x1d2c34)==(!enabled&&!cheat&&eligible&&!bonus&&score>1000),"Local/online score-entry exclusion differs");
    }
    // Execute the original save eligibility writer and restoration, then
    // simulate restarting with RehabMode disabled and check the score gate.
    for(int cheat:{0,1})for(int eligible:{0,1}) {
      std::array<uint8_t,0x80> record{};gObject=record.data()+0x28;
      At<int>(0x1b91d0)=cheat;At<int>(0x1b91d4)=eligible;Run(0x8f156);
      const bool saved=!enabled&&!cheat&&eligible;
      Require((record[0x50]!=0)==saved,"Rehab save retained score eligibility");
      At<int>(rehab_mode::kFlag)=0;gObject=record.data();Run(0x90e75);
      Require((At<int>(0x1b91d4)!=0)==saved,"Native resume lost eligibility marker");
      At<int>(0x1b91d0)=0;At<int>(0x1a7170)=0;gEdx=2000;Run(0x9609f);
      Require((At<int>(0x1d2c34)!=0)==saved,"Restart made a Rehab save eligible for scores");
      At<int>(rehab_mode::kFlag)=enabled;
    }
    std::printf("PASS: Anniversary RehabMode %s; strict config/guarded install, practice/death cases, native keyboard dispatch and moving-ball direction/speed, 50 speed-cap cases, 24 score-gate cases, native save eligibility and resume with mode disabled. No Steam calls or live files.\n",enabled?"on":"off");
    return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
