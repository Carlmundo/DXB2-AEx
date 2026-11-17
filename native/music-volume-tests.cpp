// Execute the shipped tracker, resamplers, mixers and game fade code offline.
// Only the audio device and display are replaced; no game settings are opened.
#include "music-volume.hpp"
#include "screen-transition.hpp"
#include <vector>
#include <string>

static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class T> static T Function(uint32_t rva) {return reinterpret_cast<T>(music_volume::audio+rva);}
static unsigned __stdcall IgnoreCallback(void*) {return 0;}
static void Driver(uint32_t slot,uint32_t rva) {music_volume::Audio<void*>(slot)=music_volume::audio+rva;}
static void Settings(int master,int music) {
  *reinterpret_cast<int*>(music_volume::game+0x1a40c8)=master;
  *reinterpret_cast<int*>(music_volume::game+0x1a40d0)=music;
}

static void OpenMixer(unsigned format,unsigned rate) {
  // Connect the library's own software driver directly to a memory sink.
  // This exercises the mixer used by both WaveOut and software DirectSound.
  Driver(0xe5ac,0x65d0);Driver(0xe5b0,0x6610);
  Driver(0xe5b4,0x6a80);Driver(0xe5b8,0x6a90);Driver(0xe5bc,0x6500);
  Driver(0xe5c4,0x6660);Driver(0xe5c8,0x6680);Driver(0xe5cc,0x66a0);
  Driver(0xe5d0,0x66f0);Driver(0xe5d4,0x67b0);Driver(0xe5d8,0x67e0);
  Driver(0xe5dc,0x6810);Driver(0xe5e0,0x6850);Driver(0xe5e4,0x68b0);
  Driver(0xe5e8,0x68f0);Driver(0xe5ec,0x6930);Driver(0xe5f0,0x6970);
  Driver(0xe5f4,0x69c0);Driver(0xe5f8,0x6a00);Driver(0xe5fc,0x6a40);
  music_volume::Audio<void*>(0xe514)=reinterpret_cast<void*>(&IgnoreCallback);
  struct Info {unsigned device;uint16_t format,rate;} info{0,static_cast<uint16_t>(format),static_cast<uint16_t>(rate)};
  Require(Function<unsigned (__stdcall*)(void*)>(0x63e0)(&info)==0,"Software mixer initialization failed");
  Require(Function<unsigned (__stdcall*)(unsigned,unsigned)>(0x6500)(1,192)==0,"Mixer attenuation initialization failed");
  Require(Function<unsigned (__stdcall*)(unsigned)>(0x1470)(32)==0,"Voice allocation failed");
}
static void CloseMixer() {Function<void (__cdecl*)()>(0x64d0)();}
static void* LoadTrack(const char* path) {
  void* track=nullptr;
  Require(Function<unsigned (__stdcall*)(const char*,void**,unsigned)>(0x5eb0)(path,&track,0)==0&&track,"Could not load music fixture");
  return track;
}
static void Start(void* track,int units) {
  music_volume::Audio<unsigned>(0x10cb0)=0;
  Require(Function<unsigned (__stdcall*)(void*)>(0x4ea0)(track)==0,"Tracker start failed");
  Require(music_volume::SetVolume(nullptr,nullptr,units)==1,"Music volume update failed");
}
static void Stop() {Require(Function<unsigned (__cdecl*)()>(0x5bb0)()==0,"Tracker stop failed");}
static std::vector<int16_t> Capture(unsigned values) {
  std::vector<int16_t> pcm(values);
  Function<void (__stdcall*)(void*,unsigned)>(0x6ad0)(pcm.data(),values*2);
  return pcm;
}
static std::vector<int32_t> CaptureUnclipped(unsigned values) {
  std::vector<int32_t> pcm(values);
  for(unsigned offset=0;offset<values;) {
    const auto count=(std::min)(512u,values-offset);
    Function<void (__fastcall*)(int32_t*,unsigned)>(0x6b80)(pcm.data()+offset,count);
    offset+=count;
  }
  return pcm;
}
static void CheckPcmGain(const std::vector<int16_t>& pcm,const std::vector<int32_t>& reference,double ratio) {
  Require(pcm.size()==reference.size(),"Different audio capture lengths");
  for(size_t i=0;i<pcm.size();i++) {
    // Apply gain before final PCM clipping, as the production hook does.
    const auto expected=std::clamp(std::lround(reference[i]*ratio),-32768L,32767L);
    // At most 32 voices each round by half a sample before accumulation.
    Require(std::abs(pcm[i]-expected)<=17,"Music PCM does not match the requested gain before clipping");
  }
}
static double Energy(const std::vector<int16_t>& pcm) {
  double sum=0;for(const auto value:pcm)sum+=static_cast<double>(value)*value;return sum;
}

static void TestGrid(void* track) {
  Start(track,music_volume::kUnity);const auto reference=Capture(44100*2);Stop();
  Start(track,music_volume::kUnity);const auto wide=CaptureUnclipped(44100*2);Stop();
  const double referenceEnergy=Energy(reference);Require(referenceEnergy>0,"Music fixture contains no audio");
  for(int master=0;master<=100;master+=10)for(int music=0;music<=100;music+=10) {
    Settings(master,music);Start(track,music_volume::SettingsVolume());
    const auto pcm=Capture(44100*2);Stop();
    const double energy=Energy(pcm),ratio=static_cast<double>(master*music)/10000;
    if(!master||!music)Require(energy==0,"Zero slider setting did not mute music");
    else {
      Require(energy>0,"Positive slider settings produced silent PCM");
      CheckPcmGain(pcm,wide,ratio);
    }
    if(master==100&&music==100)Require(pcm==reference,"Maximum music output changed");
    Settings(music,master);Start(track,music_volume::SettingsVolume());
    const auto reversed=Capture(44100*2);Stop();
    Require(reversed==pcm,"Swapping Master and Music changed output");
  }
  std::puts("PASS: all 121 slider combinations; positive PCM at 10/10 and 10/40; exact mute, symmetry and gain");
}

static void TestLegacyPlayback(void* track) {
  auto* game=music_volume::game;
  // Exercise the original game audio object and its public virtual setter,
  // as native in-game music mods do. The original module-start routine calls
  // SEAL through the game's stop import; its play call uses our adapter.
  auto* stop=music_volume::audio+0x5bb0;
  WriteMemory(game+0x15f324,&stop,sizeof(stop));
  alignas(4) std::array<uint8_t,0x100> object{};
  void* tracks[]={track};
  *reinterpret_cast<void**>(object.data())=game+0x16b2b0;
  *reinterpret_cast<int*>(object.data()+0x40)=1;
  *reinterpret_cast<void***>(object.data()+0x54)=tracks;
  const auto play=reinterpret_cast<int (__thiscall*)(void*,int)>(game+0xbc270);
  const auto set=reinterpret_cast<int (__thiscall*)(void*,int)>((*reinterpret_cast<void***>(object.data()))[10]);
  const auto get=reinterpret_cast<int (__cdecl*)()>(game+0x859d0);
  for(int master=0;master<=100;master+=10)for(int music=0;music<=100;music+=10) {
    Settings(master,music);
    const auto legacy=get();
    Require(legacy>=0&&legacy<=40,"Public volume getter no longer uses legacy units");
    music_volume::Audio<unsigned>(0x10cb0)=0;
    Require(play(object.data(),0)==1,"Original game module start failed");
    Require(music_volume::startingTrack.load(),"Game module start did not mark its initial volume update");
    Require(set(object.data(),legacy)==1,"Public game volume setter failed");
    Require(music_volume::gain.load()==master*music*100,"Native music mod's legacy volume was mistaken for fixed-point gain");
    const auto pcm=Capture(44100*2);
    Require(master&&music?Energy(pcm)>0:Energy(pcm)==0,"Native music mod path produced incorrect silence");
    Require(set(object.data(),0)==1&&music_volume::gain.load()==0,"Explicit legacy mute after track startup was lost");
    Require(Energy(Capture(512))==0,"Explicit legacy mute produced audio");
    Require(set(object.data(),64)==0&&set(object.data(),-1)==0,"Invalid legacy volume was accepted");
    Stop();
  }
  Settings(100,100);
  std::puts("PASS: original game module start and public volume ABI; 121 native-mod slider combinations, low gains and explicit mute");
}

static void TestEffects() {
  auto& flags=music_volume::Audio<uint8_t>(0x10950);flags=0x80;
  music_volume::Audio<unsigned>(0x10954)=1;music_volume::Audio<unsigned>(0x108d0)=1;
  music_volume::Audio<unsigned>(0x10ca0)=2;music_volume::Audio<void*>(0x10cb8)=nullptr;
  std::array<int16_t,1028> wave{};
  for(size_t i=0;i<wave.size();i++)wave[i]=(i%32<16)?24000:-24000;
  auto prepare=[&](unsigned index) {
    auto* voice=music_volume::audio+0x109a0+index*24;
    std::memset(voice,0,24);
    *reinterpret_cast<void**>(voice)=wave.data();
    *reinterpret_cast<unsigned*>(voice+8)=4096;
    *reinterpret_cast<unsigned*>(voice+12)=1024*4096;
    *reinterpret_cast<unsigned*>(voice+16)=1024*4096;
    voice[20]=20;voice[21]=128;voice[22]=6;
  };
  auto render=[&](int units,bool music,bool effect) {
    prepare(0);prepare(1);
    if(!music)music_volume::Audio<uint8_t>(0x109a0+22)|=1;
    if(!effect)music_volume::Audio<uint8_t>(0x109a0+24+22)|=1;
    music_volume::gain.store(units);
    std::array<int32_t,512> output{};
    Function<void (__fastcall*)(int32_t*,unsigned)>(0x6b80)(output.data(),512);
    return output;
  };
  const auto effects=render(music_volume::kUnity,false,true);
  const auto original=render(music_volume::kUnity,true,true);
  const auto music=render(music_volume::kUnity,true,false);
  Require(render(0,true,true)==effects,"Music mute changed simultaneous sound effects");
  Require(render(10000,false,true)==effects,"Music gain changed sound effects");
  const auto combined=render(10000,true,true);
  for(unsigned i=0;i<512;i++) {
    Require(original[i]==effects[i]+music[i],"Original voice contributions were not additive");
    Require(std::abs((combined[i]-effects[i])-music[i]*0.01)<=0.5,"Music attenuation affected the effects bus");
  }
  render(0,true,false);const auto mutedPosition=music_volume::Audio<unsigned>(0x109a4);
  render(music_volume::kUnity,true,false);
  Require(mutedPosition>0&&music_volume::Audio<unsigned>(0x109a4)==mutedPosition,"Muting changed music playback timing");
  // Unhooked SEAL still discards low volumes; show the regression is real.
  music_volume::Audio<void*>(0x10cb8)=nullptr;prepare(0);
  music_volume::Audio<uint8_t>(0x109a0+24+22)|=1;
  Require(Function<unsigned (__stdcall*)(unsigned,unsigned)>(0x1770)(1,1)==0,"Voice update failed");
  Require(Energy(Capture(512))==0,"Original internal volume 1 should reproduce silence");
  flags=0;
  std::puts("PASS: original silence reproduced; simultaneous effects unchanged and music phase retained");
}

static void TestEightBitOutput(const char* path) {
  for(const unsigned format:{0u,0x100u,0x8000u,0x8100u})for(const unsigned rate:{22050u,44100u}) {
    OpenMixer(format,rate);void* track=LoadTrack(path);
    Start(track,music_volume::kUnity);const auto wide=CaptureUnclipped(rate*2);Stop();
    Start(track,10000);std::vector<uint8_t> low(rate*2);
    Function<void (__stdcall*)(void*,unsigned)>(0x6ad0)(low.data(),static_cast<unsigned>(low.size()));Stop();
    Require(std::any_of(low.begin(),low.end(),[](uint8_t value){return value!=128;}),"8-bit output became silent at 10/10");
    for(size_t i=0;i<low.size();i++) {
      const auto expected=(std::clamp(std::lround(wide[i]*0.01),-32768L,32767L)+32768)/256;
      Require(std::abs(low[i]-expected)<=1,"8-bit output has incorrect low gain");
    }
    Start(track,0);std::vector<uint8_t> muted(rate*2);
    Function<void (__stdcall*)(void*,unsigned)>(0x6ad0)(muted.data(),static_cast<unsigned>(muted.size()));Stop();
    Require(std::all_of(muted.begin(),muted.end(),[](uint8_t value){return value==128;}),"8-bit mute is not digital silence");
    Function<void (__stdcall*)(void*)>(0x5dc0)(track);CloseMixer();
  }
  std::puts("PASS: 8-bit output retains low-volume music and exact mute across all mixer modes");
}

static LONG gAttenuation=0;
static HRESULT WINAPI BufferVolume(void*,LONG value) {gAttenuation=value;return S_OK;}
static void TestDirectSound() {
  std::array<void*,16> methods{};methods[15]=reinterpret_cast<void*>(&BufferVolume);
  auto* table=methods.data();auto* buffer=&table;
  music_volume::Audio<void*>(0x11568)=buffer;
  music_volume::Audio<void*>(0x1156c)=buffer;
  music_volume::Audio<uint8_t>(0x10950)=0x80;
  music_volume::Audio<unsigned>(0x10954)=1;music_volume::Audio<unsigned>(0x108d0)=1;
  Driver(0xe5e4,0xb410);
  Function<unsigned (__stdcall*)(unsigned,unsigned)>(0xaea0)(1,192);
  music_volume::gain.store(music_volume::kUnity);
  Require(music_volume::TrackerVolume(1,20)==0,"DirectSound tracker volume failed");
  const LONG baseline=gAttenuation;
  for(const auto units:{10000,40000,100000,500000,1000000,0}) {
    Require(music_volume::SetVolume(nullptr,nullptr,units)==1,"DirectSound slider update failed");
    const LONG expected=units?baseline+static_cast<LONG>(std::lround(2000*std::log10(units/1000000.0))):-10000;
    Require(gAttenuation==expected,"Existing DirectSound notes did not update with slider gain");
    music_volume::TrackerVolume(1,20);
    Require(gAttenuation==expected,"New DirectSound notes lost slider gain");
    Function<unsigned (__stdcall*)(unsigned,unsigned)>(0x1770)(2,20);
    Require(gAttenuation==baseline,"DirectSound effects were attenuated by Music volume");
  }
  music_volume::TrackerVolume(1,0);Require(gAttenuation==-10000,"Silent tracker note became audible");
  music_volume::Audio<uint8_t>(0x10950)=0;
  music_volume::Audio<void*>(0x11568)=nullptr;music_volume::Audio<void*>(0x1156c)=nullptr;
  std::puts("PASS: DirectSound notes, live slider updates, exact mute and unaffected effects");
}

static int gTime=0,gStops=0;
static std::array<uint16_t,16*80> gPixels{};
static int __fastcall Lock(void*,void*,uint32_t* out) {
  const uint32_t surface[]={2,16,80,32,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gPixels.data()))};
  std::memcpy(out,surface,sizeof(surface));return 1;
}
static void __fastcall Nothing(void*,void*) {}
static void __fastcall Present(void*,void*,int) {}
static void __fastcall Clip(void*,void*,const void*) {}
static int __fastcall Clock(void*,void*,int) {return gTime;}
static int __cdecl SoundVolume(int) {return 0;}
static void __fastcall SetSoundVolume(void*,void*,int,int) {}
static void __cdecl StopMusic() {gStops++;}
static void TestFades() {
  auto* base=music_volume::game;
  JumpTo(base,0x35390,reinterpret_cast<void*>(&Lock));JumpTo(base,0x35910,reinterpret_cast<void*>(&Nothing));
  JumpTo(base,0x35940,reinterpret_cast<void*>(&Present));JumpTo(base,0x34f00,reinterpret_cast<void*>(&Clip));
  JumpTo(base,0xbd2e0,reinterpret_cast<void*>(&Clock));JumpTo(base,0xe6280,reinterpret_cast<void*>(&std::memcpy));
  JumpTo(base,0x85940,reinterpret_cast<void*>(&SoundVolume));JumpTo(base,0xbd040,reinterpret_cast<void*>(&SetSoundVolume));
  JumpTo(base,0x8b8e0,reinterpret_cast<void*>(&StopMusic));JumpTo(base,0xbd1f0,reinterpret_cast<void*>(&Nothing));
  for(const auto rva:{0x1d29f0,0x1c9db8}) {
    const uint32_t surface[]={static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gPixels.data())),16,80,32,16};
    std::memcpy(base+rva+0x18,surface,sizeof(surface));
  }
  Settings(10,10);
  const auto update=reinterpret_cast<int (__cdecl*)()>(base+0x9ba70);
  for(const int direction:{-1,1}) {
    int previous=direction>0?-1:10001;gStops=0;
    for(const int elapsed:{0,500,1000,1500,2001}) {
      *reinterpret_cast<int*>(base+0x1a71a0)=1;*reinterpret_cast<int*>(base+0x1a71a4)=0;
      *reinterpret_cast<int*>(base+0x1a71a8)=0;*reinterpret_cast<int*>(base+0x1a7078)=direction;
      *reinterpret_cast<int*>(base+0x1a707c)=0;std::memset(base+0x1c8248,0,16);
      gTime=elapsed;Require(update()==1,"Original carpet update failed");
      const int units=music_volume::gain.load();
      Require(direction>0?units>=previous:units<=previous,"Low-volume fade is not monotonic");
      if(elapsed==1000)Require(units>0&&units<10000,"Low-volume fade lost its intermediate gain");
      previous=units;
    }
    Require(direction>0?previous==10000:gStops==1,"Fade completion or stop cleanup changed");
  }
  InstallNoScreenTransition(base);
  for(const int direction:{-1,1}) {
    gStops=0;gTime=0;
    *reinterpret_cast<int*>(base+0x1a71a0)=1;*reinterpret_cast<int*>(base+0x1a71a4)=0;
    *reinterpret_cast<int*>(base+0x1a71a8)=0;*reinterpret_cast<int*>(base+0x1a7078)=direction;
    *reinterpret_cast<int*>(base+0x1a707c)=0;std::memset(base+0x1c8248,0,16);
    Require(update()==1&&*reinterpret_cast<int*>(base+0x1a71a0)==0,"Immediate transition did not complete");
    Require(direction>0?music_volume::gain.load()==10000:gStops==1,"Immediate transition lost music fade cleanup");
  }
  std::puts("PASS: original game fade-in/out retains intermediate gain at 10/10 and completes cleanup");
}

int wmain(int argc,wchar_t** argv) {
  std::setvbuf(stdout,nullptr,_IONBF,0);
  if(argc<4){std::fprintf(stderr,"Usage: music-volume-tests.exe DXBall2.exe Audiow32.dll track.mod [track.mod ...]\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported game hash");Require(music_volume::VerifiedAudio(argv[2]),"Unsupported audio hash");
    const auto gameModule=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    const auto audioModule=LoadLibraryW(argv[2]);Require(gameModule&&audioModule,"Could not map game/audio library");
    auto* game=reinterpret_cast<uint8_t*>(gameModule);auto* audio=reinterpret_cast<uint8_t*>(audioModule);
    music_volume::game=game;music_volume::audio=audio;
    Settings(100,100);
    // Capture the unpatched library, then require identical output at unity.
    OpenMixer(0x101,44100);
    char path[MAX_PATH];Require(WideCharToMultiByte(CP_ACP,0,argv[3],-1,path,MAX_PATH,nullptr,nullptr)>0,"Invalid track path");
    void* track=LoadTrack(path);Start(track,music_volume::kUnity);const auto before=Capture(44100*2);Stop();
    music_volume::Install(game,audio);
    Start(track,music_volume::kUnity);Require(Capture(44100*2)==before,"Unity output differs from unpatched library");Stop();
    std::puts("PASS: maximum output is byte-for-byte identical to the original tracker and mixer");
    TestGrid(track);
    TestLegacyPlayback(track);
    Function<void (__stdcall*)(void*)>(0x5dc0)(track);
    TestEffects();CloseMixer();
    for(int arg=3;arg<argc;arg++)for(const unsigned format:{1u,0x101u,0x8001u,0x8101u})for(const unsigned rate:{22050u,44100u}) {
      OpenMixer(format,rate);
      Require(WideCharToMultiByte(CP_ACP,0,argv[arg],-1,path,MAX_PATH,nullptr,nullptr)>0,"Invalid track path");
      track=LoadTrack(path);
      Start(track,music_volume::kUnity);const auto full=Capture(rate*2);Stop();
      Start(track,music_volume::kUnity);const auto wide=CaptureUnclipped(rate*2);Stop();
      Start(track,10000);const auto low=Capture(rate*2);Stop();
      Require(Energy(full)>0&&Energy(low)>0,"Real music track became silent at 10/10");
      CheckPcmGain(low,wide,0.01);
      Function<void (__stdcall*)(void*)>(0x5dc0)(track);CloseMixer();
    }
    std::puts("PASS: real music at 10/10 across mono/stereo, interpolation on/off and 22050/44100 Hz");
    Require(WideCharToMultiByte(CP_ACP,0,argv[3],-1,path,MAX_PATH,nullptr,nullptr)>0,"Invalid track path");
    TestEightBitOutput(path);
    TestDirectSound();TestFades();
    // This process connected SEAL's private software driver to a memory sink,
    // without opening a physical device. Leave module teardown to process exit
    // instead of asking its normal device-close path to close that test sink.
    FreeLibrary(gameModule);return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
