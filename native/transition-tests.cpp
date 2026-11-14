// Runs the original carpet renderer against memory surfaces. Only its display,
// clock, and audio dependencies are stubbed; no game or user files are opened.
#include "screen-transition.hpp"
#include "verified-game.hpp"
#include <array>
#include <algorithm>

static constexpr int kWidth=16,kHeight=80;
using Pixels=std::array<uint16_t,kWidth*kHeight>;
static Pixels gOld,gNew,gOutput;
static int gTime=0,gPresents=0,gUnlocks=0,gMusicStops=0,gSoundStops=0,gMusicVolume=-1;
static std::array<int,8> gSoundVolumes;
static void Require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
static int __fastcall Lock(void*,void*,uint32_t* out) {
  const uint32_t surface[]={2,kWidth,kHeight,kWidth*2,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gOutput.data()))};
  std::memcpy(out,surface,sizeof(surface));return 1;
}
static void __fastcall Unlock(void*,void*) {gUnlocks++;}
static void __fastcall Present(void*,void*,int) {gPresents++;}
static void __fastcall SetClip(void*,void*,const void*) {}
static int __fastcall Clock(void*,void*,int) {return gTime;}
static int __cdecl MusicVolume() {return 100;}
static int __cdecl SoundVolume(int) {return 100;}
static void __fastcall SetMusicVolume(void*,void*,int volume) {gMusicVolume=volume;}
static void __fastcall SetSoundVolume(void*,void*,int channel,int volume) {gSoundVolumes.at(channel)=volume;}
static void __cdecl StopMusic() {gMusicStops++;}
static void __fastcall StopSounds(void*,void*) {gSoundStops++;}

static void Reset(uint8_t* base,int direction,int audio) {
  const auto set=[&](size_t rva,int value){std::memcpy(base+rva,&value,4);};
  set(0x1a71a0,1);set(0x1a71a4,0);set(0x1a71a8,direction);
  set(0x1a7078,audio);set(0x1a707c,audio);
  std::memset(base+0x1c8248,0,16);
  gOutput.fill(0xdead);gPresents=0;gUnlocks=0;gMusicStops=0;gSoundStops=0;gMusicVolume=-1;gSoundVolumes.fill(-1);
}

int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: transition-tests.exe path-to-DXBall2.exe\n");return 2;}
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game executable");
    auto* base=reinterpret_cast<uint8_t*>(module);
    VerifyNoScreenTransition(base);
    JumpTo(base,0x35390,reinterpret_cast<void*>(&Lock));
    JumpTo(base,0x35910,reinterpret_cast<void*>(&Unlock));
    JumpTo(base,0x35940,reinterpret_cast<void*>(&Present));
    JumpTo(base,0x34f00,reinterpret_cast<void*>(&SetClip));
    JumpTo(base,0xbd2e0,reinterpret_cast<void*>(&Clock));
    JumpTo(base,0xe6280,reinterpret_cast<void*>(&std::memcpy));
    JumpTo(base,0x859d0,reinterpret_cast<void*>(&MusicVolume));
    JumpTo(base,0x85940,reinterpret_cast<void*>(&SoundVolume));
    JumpTo(base,0xbccf0,reinterpret_cast<void*>(&SetMusicVolume));
    JumpTo(base,0xbd040,reinterpret_cast<void*>(&SetSoundVolume));
    JumpTo(base,0x8b8e0,reinterpret_cast<void*>(&StopMusic));
    JumpTo(base,0xbd1f0,reinterpret_cast<void*>(&StopSounds));
    for(size_t i=0;i<gOld.size();i++){gOld[i]=static_cast<uint16_t>(i+1);gNew[i]=static_cast<uint16_t>(0x8000+i);}
    for(const auto rva:{0x1d29f0,0x1c9db8}) {
      const uint32_t surface[]={static_cast<uint32_t>(reinterpret_cast<uintptr_t>((rva==0x1d29f0?gNew:gOld).data())),kWidth,kHeight,kWidth*2,16};
      std::memcpy(base+rva+0x18,surface,sizeof(surface));
    }
    const auto update=reinterpret_cast<int (__cdecl*)()>(base+0x9ba70);
    std::array<Pixels,2> completed;
    for(int direction=0;direction<2;direction++) {
      Reset(base,direction,0);gTime=0;
      Require(update()==1&&*reinterpret_cast<int*>(base+0x1a71a0)==1,"Default transition should remain active at zero elapsed time");
      Reset(base,direction,0);gTime=2001;
      Require(update()==1&&*reinterpret_cast<int*>(base+0x1a71a0)==0,"Original transition did not complete");
      completed[direction]=gOutput;
      Require(std::find(gOutput.begin(),gOutput.end(),0xdead)==gOutput.end(),"Original completed screen was not fully drawn");
    }
    InstallNoScreenTransition(base);
    for(int direction=0;direction<2;direction++)for(int audio:{-1,0,1}) {
      Reset(base,direction,audio);gTime=0;
      Require(update()==1&&*reinterpret_cast<int*>(base+0x1a71a0)==0,"Enabled transition did not complete immediately");
      Require(gOutput==completed[direction],"Immediate transition differs from the original completed screen");
      Require(gPresents==1&&gUnlocks==1,"Immediate transition did not present/unlock the finished screen");
      Require(*reinterpret_cast<int*>(base+0x1a7078)==0&&*reinterpret_cast<int*>(base+0x1a707c)==0,"Audio transition flags were not cleared");
      Require(gMusicStops==(audio<0?1:0)&&gSoundStops==(audio<0?1:0),"Audio stop cleanup was lost");
      if(audio>0)Require(gMusicVolume==100&&std::all_of(gSoundVolumes.begin(),gSoundVolumes.end(),[](int v){return v==100;}),"Audio fade-in did not reach its final volume");
    }
    std::puts("PASS: default animation stays active; both carpet directions finish immediately with identical final pixels");
    std::puts("PASS: frame presentation, audio fade completion, stop cleanup, and transition flag reset preserved");
    FreeLibrary(module);return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
