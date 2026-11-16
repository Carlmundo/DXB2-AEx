// Execute the original command-handler slice with fake sound and achievement
// interfaces. The executable is mapped without resolving imports or starting
// the game; no Steam API, user achievements, or game data are accessed.
#include "nostat.hpp"
#include "verified-game.hpp"
#include "config.hpp"
#include <array>
#include <fstream>
#include <sstream>

static uint8_t* gBase=nullptr;
static int gSounds=0,gResets=0,gSequence=0;
static int gSoundId=0,gSoundFlags=0,gSoundZero=0;
static float gSoundX=0;
struct FakeStats {void** vtable;bool integrated;unsigned achievements;};
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int __cdecl Match(const void* needle,const char* text) {
  Require(needle==gBase+0x16061c,"Original nostat string reference changed");
  return std::strcmp(text,"nostat")==0;
}
static void __cdecl Sound(int id,float x,int zero,int flags) {
  Require(gSequence++==0,"Sound was not played first");
  ++gSounds;gSoundId=id;gSoundX=x;gSoundZero=zero;gSoundFlags=flags;
}
static void __fastcall Reset(FakeStats* stats,void*,int achievements) {
  Require(gSequence++==1,"Reset did not follow sound in baseline");
  Require(achievements==1,"Baseline reset argument changed");
  ++gResets;if(stats->integrated)stats->achievements=0;
}
static void Execute(uint8_t* entry) {
  // The original slice uses EBX inside a larger function that saves it.
  // Preserve it here, and check that the slice leaves the stack balanced.
  uintptr_t before=0,after=0;
  __asm {
    mov before,esp
    push ebx
    mov eax,entry
    call eax
    pop ebx
    mov after,esp
  }
  Require(before==after,"nostat handler unbalanced the stack");
}
static void Run(bool matched,FakeStats* stats,bool patched) {
  const char* text=matched?"nostat":"ordinary input";
  WriteMemory(gBase+0x1d0038,&text,sizeof(text));
  WriteMemory(gBase+0x1d2a38,&stats,sizeof(stats));
  const int width=1280;WriteMemory(gBase+0x1a4094,&width,sizeof(width));
  gSounds=0;gResets=0;gSequence=0;
  if(stats)stats->achievements=0x5a5a;
  Execute(gBase+0x4de2a);
  Require(gSounds==(matched?1:0),"Wrong sound count");
  Require(gResets==((matched&&!patched)?1:0),"Unexpected achievement reset call");
  if(matched)Require(gSoundId==13&&gSoundX==640.0f&&gSoundZero==0&&gSoundFlags==63,"Original sound arguments changed");
  if(stats)Require(stats->achievements==((matched&&!patched&&stats->integrated)?0u:0x5a5au),"Achievement state changed unexpectedly");
}

int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: nostat-tests.exe path-to-DXBall2.exe\n");return 2;}
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game executable");
    gBase=reinterpret_cast<uint8_t*>(module);
    VerifyNostatSoundOnly(gBase);
    std::array<uint8_t,kNostatResetSize> original;
    std::memcpy(original.data(),gBase+kNostatResetRva,original.size());
    const uint8_t wrong=0xcc;WriteMemory(gBase+kNostatResetRva,&wrong,1);
    bool rejected=false;
    try{InstallNostatSoundOnly(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&gBase[kNostatResetRva]==wrong&&
      std::memcmp(gBase+kNostatResetRva+1,original.data()+1,original.size()-1)==0,"Unexpected reset bytes were not safely rejected");
    WriteMemory(gBase+kNostatResetRva,original.data(),original.size());
    const uint8_t originalSound=gBase[0x4de6a];WriteMemory(gBase+0x4de6a,&wrong,1);
    rejected=false;
    try{InstallNostatSoundOnly(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&std::memcmp(gBase+kNostatResetRva,original.data(),original.size())==0,"Unexpected sound bytes were not safely rejected");
    WriteMemory(gBase+0x4de6a,&originalSound,1);
    JumpTo(gBase,0xabfa0,reinterpret_cast<void*>(&Match));
    JumpTo(gBase,0x89490,reinterpret_cast<void*>(&Sound));
    const uint8_t done=0xc3;WriteMemory(gBase+0x4de7f,&done,1);
    std::array<void*,15> vtable{};vtable[14]=reinterpret_cast<void*>(&Reset);
    FakeStats offline{vtable.data(),false,0},online{vtable.data(),true,0};
    for(auto* stats:{&offline,&online})for(bool match:{false,true})Run(match,stats,false);
    std::array<uint8_t,0x4de72-0x4de2a> before;
    std::memcpy(before.data(),gBase+0x4de2a,before.size());
    InstallNostatSoundOnly(gBase);
    Require(std::memcmp(before.data(),gBase+0x4de2a,before.size())==0,"Command matching or sound code changed");
    for(auto* stats:{&offline,&online,static_cast<FakeStats*>(nullptr)})
      for(bool match:{false,true})for(int repeat=0;repeat<100;repeat++)Run(match,stats,true);
    const std::pair<const char*,bool> cases[]={
      {nullptr,true},{"",true},{"Other=0",true},{"DisableAchievementReset=1",true},
      {"DisableAchievementReset=0",false},{"\tDisableAchievementReset = 0 # comment\r\n",false},
      {"# DisableAchievementReset=0",true},{"DisableAchievementReset=0junk",true},
      {"DisableAchievementReset=0\nDisableAchievementReset=1",true},
      {"DisableAchievementReset=1\nDisableAchievementReset=0",false}
    };
    for(const auto& item:cases) {
      AexConfig config;
      if(item.first){std::istringstream input(item.first);config=ReadAexConfig(input);}
      else {std::ifstream missing;config=ReadAexConfig(missing);}
      Require(config.disableAchievementReset==item.second,"Incorrect achievement protection config");
      WriteMemory(gBase+kNostatResetRva,original.data(),original.size());
      if(config.disableAchievementReset)InstallNostatSoundOnly(gBase);
      for(auto* stats:{&offline,&online})for(bool match:{false,true})Run(match,stats,item.second);
      if(item.second)Run(true,nullptr,true);
    }
    std::puts("PASS: original nostat attempts reset(true) after sound; patched handler plays the same sound with zero reset calls");
    std::puts("PASS: fake achievements preserved online/offline; null interface safe; unrelated input unchanged; stack balanced over 600 patched runs");
    std::puts("PASS: unexpected sound/reset bytes rejected before patching; no Steam API or real achievements accessed");
    std::puts("PASS: 10 config scenarios execute the original handler; only explicit DisableAchievementReset=0 restores the reset call");
    FreeLibrary(module);return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
