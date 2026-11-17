#pragma once
#include "verified-game.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// The supported SEAL 1.07 DLL has two playback paths: a software mixer shared
// by WaveOut/DirectSound output, and a DirectSound driver with separate voices.
// Keep the tracker at the game's normal module volume (40), and attenuate only
// its voices after the tracker/voice integer volume conversions.
namespace music_volume {
inline constexpr int kUnity=1000000;
inline uint8_t* game=nullptr;
inline uint8_t* audio=nullptr;
inline std::atomic<int> gain{kUnity};
inline std::atomic<bool> startingTrack{false};
inline constexpr uint32_t kSettingsCalls[]={0x893e7,0x8945f,0x99cd1,0x9a7aa,0x9a817};
inline constexpr uint32_t kFadeGetCalls[]={0x9bf23,0x9bf59};
inline constexpr uint32_t kFadeSetCalls[]={0x951bb,0x9a9ec,0x9bf4b,0x9bf75};

template<class T> inline T& Audio(uint32_t rva) {return *reinterpret_cast<T*>(audio+rva);}

inline bool VerifiedAudio(const wchar_t* path) {
  static const unsigned char expected[32]={0x95,0x82,0x96,0xd0,0xc0,0x80,0xe8,0xcb,0xa8,0xe8,0xe7,0x12,0xd3,0x45,0x28,0x23,0x28,0xb0,0x9f,0xc7,0x17,0xe8,0x89,0xf2,0xa9,0x7a,0x8d,0xc9,0x42,0xf1,0xc0,0x71};
  return VerifiedFileSha256(path,expected);
}

inline bool IsMusicVoice(unsigned handle) noexcept {
  if(!handle||handle>32||!(Audio<uint8_t>(0x10950)&0x80))return false;
  const auto count=(std::min)(Audio<unsigned>(0x10954),32u);
  for(unsigned i=0;i<count;i++)if(Audio<unsigned>(0x108d0+i*4)==handle)return true;
  return false;
}

inline bool DirectSoundVoices() noexcept {
  return Audio<void*>(0xe5e4)==audio+0xb410;
}

inline void ApplyDirectSoundGain(unsigned handle,unsigned volume,int units) noexcept {
  if(!handle||handle>32||volume>=64)return;
  auto* buffer=Audio<void*>(0x11568+(handle-1)*4);
  if(!buffer)return;
  // Preserve SEAL's existing mixer attenuation at unity. DirectSound accepts
  // hundredths of dB, so the slider gain need never pass through a 0..63 int.
  LONG attenuation=-10000;
  if(units>0&&volume>0) {
    const double extra=2000.0*std::log10(static_cast<double>(units)/kUnity);
    attenuation=std::clamp(Audio<LONG>(0x115ec+volume*4)+static_cast<LONG>(std::lround(extra)),-10000L,0L);
  }
  const auto table=*static_cast<void***>(buffer);
  reinterpret_cast<HRESULT (WINAPI*)(void*,LONG)>(table[15])(buffer,attenuation);
}

inline unsigned __stdcall TrackerVolume(unsigned handle,unsigned volume) noexcept {
  const auto result=reinterpret_cast<unsigned (__stdcall*)(unsigned,unsigned)>(audio+0x1770)(handle,volume);
  if(result==0&&DirectSoundVoices()&&IsMusicVoice(handle))
    ApplyDirectSoundGain(handle,volume,gain.load(std::memory_order_relaxed));
  return result;
}

inline int ScaleSample(int sample,int units) noexcept {
  const int64_t product=static_cast<int64_t>(sample)*units;
  // Symmetric rounding avoids adding a DC offset to quiet music.
  return static_cast<int>(product>=0?(product+kUnity/2)/kUnity:-((-product+kUnity/2)/kUnity));
}

inline void __fastcall MixVoice(int32_t* destination,unsigned samples,void* voice) noexcept {
  using Mix=void (__fastcall*)(int32_t*,unsigned,void*);
  const auto original=reinterpret_cast<Mix>(audio+0x6c20);
  const auto offset=reinterpret_cast<uintptr_t>(voice)-reinterpret_cast<uintptr_t>(audio+0x109a0);
  const auto units=gain.load(std::memory_order_relaxed);
  if(units==kUnity||offset>=32*24||offset%24||!IsMusicVoice(static_cast<unsigned>(offset/24+1))) {
    original(destination,samples,voice);return;
  }
  // The original outer callback splits output into at most 512 sample values.
  // Still chunk here so the scratch buffer has an explicit bound of its own.
  while(samples) {
    const auto count=(std::min)(samples,512u);
    std::array<int32_t,512> contribution{};
    original(contribution.data(),count,voice);
    for(unsigned i=0;i<count;i++)destination[i]+=ScaleSample(contribution[i],units);
    destination+=count;samples-=count;
  }
}

inline int __cdecl SettingsVolume() noexcept {
  const auto master=std::clamp(*reinterpret_cast<int*>(game+0x1a40c8),0,100);
  const auto music=std::clamp(*reinterpret_cast<int*>(game+0x1a40d0),0,100);
  // Preserve six decimal places through the original integer fade code.
  return master*music*100;
}

inline int __fastcall SetVolume(void*,void*,int units) noexcept {
  // The carpet renderer finishes just past 2000 ms; cap that final fade step
  // at the selected gain rather than allowing a small overshoot.
  units=std::clamp(units,0,SettingsVolume());
  startingTrack.store(false,std::memory_order_relaxed);
  gain.store(units,std::memory_order_relaxed);
  const auto result=reinterpret_cast<unsigned (__stdcall*)(unsigned)>(audio+0x5c90)(40);
  if(result==0&&DirectSoundVoices()) {
    for(unsigned handle=1;handle<=32;handle++)if(IsMusicVoice(handle))
      ApplyDirectSoundGain(handle,Audio<unsigned>(0x11bf0+(handle-1)*4),units);
  }
  return result==0?1:0;
}

inline int __fastcall SetFromSettings(void* self,void*,int) noexcept {
  return SetVolume(self,nullptr,SettingsVolume());
}

inline unsigned __stdcall PlayModule(void* track) noexcept {
  const auto result=reinterpret_cast<unsigned (__stdcall*)(void*)>(audio+0x4ea0)(track);
  startingTrack.store(result==0,std::memory_order_relaxed);
  return result;
}

inline int __fastcall SetLegacyVolume(void* self,void*,int volume) noexcept {
  // Other native mods call the game's audio interface with its original
  // 0..63 units. Never interpret those arguments as our fixed-point fade gain.
  if(volume<0||volume>63)return 0;
  const auto selected=SettingsVolume();
  const auto rounded=static_cast<int>(static_cast<int64_t>(selected)*40/kUnity);
  // A mod starting an in-game track may calculate the original rounded slider
  // volume itself (including zero at 10/10). Recover the precise slider gain
  // for that initial update; an explicit later volume=0 must still mute.
  const bool initial=startingTrack.exchange(false,std::memory_order_relaxed);
  const auto units=initial&&volume==rounded?selected:volume*(kUnity/40);
  return SetVolume(self,nullptr,units);
}

inline void VerifyCall(uint8_t* base,uint32_t rva,uint32_t target) {
  if(base[rva]!=0xe8||base+rva+5+*reinterpret_cast<int32_t*>(base+rva+1)!=base+target)
    throw std::runtime_error("Unexpected music volume call");
}
inline void RedirectCall(uint8_t* base,uint32_t rva,void* target) {
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(base+rva+5));
  WriteMemory(base+rva+1,&relative,4);
}

inline void Verify(uint8_t* gameBase,uint8_t* audioBase) {
  for(const auto rva:kSettingsCalls)VerifyCall(gameBase,rva,0xbccf0);
  for(const auto rva:kFadeSetCalls)VerifyCall(gameBase,rva,0xbccf0);
  for(const auto rva:kFadeGetCalls)VerifyCall(gameBase,rva,0x859d0);
  VerifyCall(gameBase,0xbc299,0xbd4c8);
  VerifyCall(audioBase,0x57d3,0x1770);
  VerifyCall(audioBase,0x6bef,0x6c20);
  const uint8_t setter[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
  const uint8_t getter[]={0x66,0x0f,0x6e,0x15};
  if(std::memcmp(gameBase+0xbccf0,setter,sizeof(setter))||std::memcmp(gameBase+0x859d0,getter,sizeof(getter))||
     *reinterpret_cast<int*>(gameBase+0x1a40d4)!=40)
    throw std::runtime_error("Unexpected music volume routines");
}

inline void Install(uint8_t* gameBase,uint8_t* audioBase) {
  Verify(gameBase,audioBase);
  game=gameBase;audio=audioBase;gain.store(kUnity,std::memory_order_relaxed);
  startingTrack.store(false,std::memory_order_relaxed);
  // Preserve the public getter/setter's original units. Only the verified
  // carpet fade call sites exchange the new fixed-point representation.
  JumpTo(game,0xbccf0,reinterpret_cast<void*>(&SetLegacyVolume));
  for(const auto rva:kSettingsCalls)RedirectCall(game,rva,reinterpret_cast<void*>(&SetFromSettings));
  for(const auto rva:kFadeGetCalls)RedirectCall(game,rva,reinterpret_cast<void*>(&SettingsVolume));
  for(const auto rva:kFadeSetCalls)RedirectCall(game,rva,reinterpret_cast<void*>(&SetVolume));
  RedirectCall(game,0xbc299,reinterpret_cast<void*>(&PlayModule));
  RedirectCall(audio,0x57d3,reinterpret_cast<void*>(&TrackerVolume));
  RedirectCall(audio,0x6bef,reinterpret_cast<void*>(&MixVoice));
}
} // namespace music_volume
