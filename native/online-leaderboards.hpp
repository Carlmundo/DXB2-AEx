#pragma once
#include "patch.hpp"
#include <utility>

namespace online_leaderboards {
inline uint8_t* game=nullptr;
enum class View {AroundUser,Friends,Top};
enum class PersonalRank {Unknown,Present,Absent};
inline View view=View::AroundUser;
inline PersonalRank personalRank=PersonalRank::Unknown;
inline uint8_t* viewing=nullptr;
inline bool tabPending=false;
inline void* characterResume=nullptr;
inline void* characterDone=nullptr;
inline constexpr uint32_t kViewGate=0x9620d,kPostGate=0x96366,kTooltip=0x16a700;
inline constexpr uint8_t kViewBytes[]={0xe8,0xde,0x72,0,0,0x85,0xc0,0x75,0x68};
inline constexpr uint8_t kPostBytes[]={0x83,0xbc,0x30,0x9c,0,0,0,0,0x75,0x21};
inline constexpr uint32_t kDownloadCallback=0x42040,kNativeDownloadCallback=0x42420;
inline constexpr uint32_t kReadState=0x96119,kTitleCall=0x973a9,kNativeTitle=0xaa9b0;
inline constexpr uint32_t kState=0x1d2c34,kKey=0x1a719d,kManager=0x1d2a38,kCharacter=0x4ddaf;
inline constexpr uint8_t kTitleBytes[]={0xe8,0x02,0x36,0x01,0x00};
inline constexpr char kOldTooltip[]="View the online high scores for this boardset. (Online scores are not available for community content)";
inline constexpr char kNewTooltip[]="View the online high scores for this boardset (if available).";
static_assert(sizeof(kNewTooltip)<=sizeof(kOldTooltip));

// The native download method takes its optional UI callback by value. An
// empty callback is exactly what the original online-score viewer supplies.
struct ViewCallback {uint8_t bytes[0x28]{};};
struct ViewName {
  alignas(4) uint8_t bytes[0x18]{};
  ViewName() {reinterpret_cast<void* (__cdecl*)(void*)>(game+0x9d360)(bytes);}
  ~ViewName() {reinterpret_cast<void (__thiscall*)(void*)>(game+0x39390)(bytes);}
};
// Capture Tab before the native WM_CHAR handler turns it into a menu click.
// Every other character, including F, keeps its original handling.
inline __declspec(naked) void Character() {
  __asm {
    mov ecx,dword ptr [ebp+10h]
    push eax
    mov eax,game
    cmp ecx,9
    jne original
    cmp dword ptr [eax+1a7074h],5
    jne original
    cmp dword ptr [eax+1d2c34h],7
    jne original
    mov byte ptr [tabPending],1
    pop eax
    jmp dword ptr [characterDone]
  original:
    mov byte ptr [eax+1a719dh],cl
    pop eax
    jmp dword ptr [characterResume]
  }
}
inline int __cdecl ReadState() {
  const int state=*reinterpret_cast<int*>(game+kState);
  if(state!=7){view=View::AroundUser;personalRank=PersonalRank::Unknown;viewing=nullptr;tabPending=false;}
  else {
    auto* manager=*reinterpret_cast<void**>(game+kManager);
    if(manager&&!viewing) {
      // The original open-view path already queued this table. Resolve the
      // same cached native object so other tables' callbacks cannot change
      // this viewer's rank availability or subtitle.
      ViewName name;
      viewing=reinterpret_cast<uint8_t* (__thiscall*)(void*,const void*)>(game+0x404c0)(manager,name.bytes);
    }
    // Finish the initial around-user request before cycling: only its result
    // tells us whether the personal-rank view exists. Keep an early Tab queued.
    if(manager&&viewing&&tabPending&&personalRank!=PersonalRank::Unknown) {
      tabPending=false;
      if(view==View::Friends)view=View::Top;
      else if(view==View::AroundUser||personalRank==PersonalRank::Absent)view=View::Friends;
      else view=View::AroundUser;
      const bool around=view==View::AroundUser;
      const int range[]={around?1:(view==View::Friends?2:0),around?-7:1,around?7:15};
      ViewName name;
      using Request=void (__thiscall*)(void*,const void*,const int*,ViewCallback);
      const auto request=reinterpret_cast<Request>((*reinterpret_cast<void***>(manager))[22]);
      request(manager,name.bytes,range,ViewCallback{});
    }
  }
  return state;
}
inline void __fastcall DrawTitle(uint8_t* font,void*,const char* title,int x,int y,int width) {
  const auto draw=reinterpret_cast<void (__thiscall*)(void*,const char*,int,int,int)>(game+kNativeTitle);
  draw(font,title,x,y,width);
  const char* label=view==View::Friends?"Friends":(view==View::Top?"Top 15":nullptr);
  if(label)draw(font,label,x,y+*reinterpret_cast<int*>(font+0x20)+7,width);
  // Below all 15 score rows, above the existing menu-button tooltip.
  draw(font,"Press Tab to change view",x,590,width);
}
inline int __cdecl LookupSource() noexcept {
  // Project only this eligibility check. The selector's actual source, paths,
  // icons and saved-game verification remain owned by the original game.
  const int source=reinterpret_cast<int (__cdecl*)()>(game+0x9d4f0)();
  return static_cast<uint32_t>(source)<=2?0:source;
}
inline void __fastcall Downloaded(uint8_t* leaderboard,void*,const uint8_t* result,bool ioFailure) {
  auto* range=*reinterpret_cast<int**>(leaderboard+0x60);
  // Steam returns no entries around a player who has never posted a score.
  // Retain the pending request and UI callback, and let the next native tick
  // fetch the top 15 instead. Changing the range to global makes this a single
  // retry, including when the entire leaderboard is empty.
  if(!ioFailure&&result&&*reinterpret_cast<int*>(leaderboard+0x34)==3&&range&&range[0]==1&&
     *reinterpret_cast<const uint64_t*>(result)==*reinterpret_cast<uint64_t*>(leaderboard+0x20)) {
    const int count=*reinterpret_cast<const int*>(result+0x10);
    const bool current=leaderboard==viewing&&*reinterpret_cast<int*>(game+kState)==7;
    if(current&&count>=0)personalRank=count?PersonalRank::Present:PersonalRank::Absent;
    if(count==0) {
      if(current)view=View::Top;
      range[0]=0;range[1]=1;range[2]=15;
      *reinterpret_cast<int*>(leaderboard+0x3c)=3;
      *reinterpret_cast<int*>(leaderboard+0x34)=0;
      *reinterpret_cast<float*>(leaderboard+0x38)=0;
      return;
    }
  }
  reinterpret_cast<void (__thiscall*)(void*,const void*,bool)>(game+kNativeDownloadCallback)(leaderboard,result,ioFailure);
}
inline void Verify(uint8_t* base) {
  const auto callback=reinterpret_cast<uintptr_t>(base+kNativeDownloadCallback);
  const auto state=reinterpret_cast<uintptr_t>(base+kState);
  const auto key=reinterpret_cast<uintptr_t>(base+kKey);
  const uint8_t character[]={0x8b,0x4d,0x10,0x88,0x0d};
  if(std::memcmp(base+kViewGate,kViewBytes,sizeof(kViewBytes))||
     std::memcmp(base+kPostGate,kPostBytes,sizeof(kPostBytes))||
     base[kDownloadCallback]!=0x68||std::memcmp(base+kDownloadCallback+1,&callback,4)||
     base[kReadState]!=0xa1||std::memcmp(base+kReadState+1,&state,4)||
     std::memcmp(base+kTitleCall,kTitleBytes,sizeof(kTitleBytes))||
     std::memcmp(base+kCharacter,character,sizeof(character))||std::memcmp(base+kCharacter+5,&key,4)||
     std::memcmp(base+kTooltip,kOldTooltip,sizeof(kOldTooltip)))
    throw std::runtime_error("Unexpected online leaderboard instructions");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;view=View::AroundUser;personalRank=PersonalRank::Unknown;viewing=nullptr;tabPending=false;
  characterResume=base+kCharacter+9;characterDone=base+0x4e510;
  JumpTo(base,kCharacter,reinterpret_cast<void*>(&Character));
  const uint8_t padding[]={0x90,0x90,0x90,0x90};WriteMemory(base+kCharacter+5,padding,sizeof(padding));
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LookupSource)-reinterpret_cast<uintptr_t>(base+kViewGate+5));
  WriteMemory(base+kViewGate+1,&relative,4);
  // The post-score prompt also compares the selected row's source inline.
  // Accept source 0 (game folder), 1 (Documents), and 2 (Workshop); reject
  // unknown/negative sources with an unsigned comparison. Custom-mode checks
  // still run. Steam's existing FindLeaderboard and async callbacks decide
  // whether a table exists before downloading or uploading any score.
  const uint8_t maximumSource=2,above=0x77;
  WriteMemory(base+kPostGate+7,&maximumSource,1);
  WriteMemory(base+kPostGate+8,&above,1);
  WriteMemory(base+kTooltip,kNewTooltip,sizeof(kNewTooltip));
  const auto callback=&Downloaded;
  WriteMemory(base+kDownloadCallback+1,&callback,sizeof(callback));
  for(const auto& hook:{std::pair<uint32_t,void*>{kReadState,reinterpret_cast<void*>(&ReadState)},
                       std::pair<uint32_t,void*>{kTitleCall,reinterpret_cast<void*>(&DrawTitle)}}) {
    uint8_t call[5]={0xe8};
    const auto displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hook.second)-reinterpret_cast<uintptr_t>(base+hook.first+5));
    std::memcpy(call+1,&displacement,4);WriteMemory(base+hook.first,call,sizeof(call));
  }
}
}
