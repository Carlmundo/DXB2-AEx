// Execute the native eligibility branches, name builder and Steam state machine.
// Steam is replaced with a fake interface; no real scores or saves are touched.
#include "online-leaderboards.hpp"
#include "verified-game.hpp"
#include <array>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

static uint8_t* gBase=nullptr;
static void* gEntry=nullptr;
static void* gRows=nullptr;
static int gCustom=0;
template<class T> static T& At(void* object,size_t offset) {return *reinterpret_cast<T*>(static_cast<uint8_t*>(object)+offset);}
template<class T> static T& Global(size_t offset) {return At<T>(gBase,offset);}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
  std::fprintf(stderr,"Leaderboard test fault at RVA %08X (EAX %08X ECX %08X EDX %08X ESI %08X)\n",static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gBase)),fault->ContextRecord->Eax,fault->ContextRecord->Ecx,fault->ContextRecord->Edx,fault->ContextRecord->Esi);
  TerminateProcess(GetCurrentProcess(),3);return EXCEPTION_CONTINUE_SEARCH;
}
static __declspec(naked) int Gate() {
  __asm {
    push esi
    xor eax,eax
    mov esi,gRows
    call dword ptr [gEntry]
    pop esi
    ret
  }
}
static int __cdecl Allowed() {return 1;}
static int __cdecl Blocked() {return 0;}
static int __fastcall CustomMode(void*,void*,const void*) {return gCustom;}
static int Run(uint32_t rva) {gEntry=gBase+rva;return Gate();}

static constexpr uint64_t kFindCall=0xfedcba9876543210ull,kBoard=0x12345678abcdef01ull;
static constexpr uint64_t kUploadCall=0xabcdef0123456789ull,kDownloadCall=0x92345678fedcba01ull;
static std::array<void*,40> gVtable{};
static void** gStats=gVtable.data();
static std::array<void*,6> gContext{};
static void* gCallback=nullptr;
static uint64_t gCallbackCall=0,gUsedBoard=0;
static int gFinds=0,gCreates=0,gUploads=0,gDownloads=0;
static int gExpectedType=0,gExpectedFirst=1,gExpectedLast=15;
static std::array<void*,40> gFriendsVtable{};
static void** gFriends=gFriendsVtable.data();
static int gEntries=0,gNotified=0;
static std::string gName;
static uint64_t __fastcall Find(void*,void*,const char* name) {++gFinds;gName=name;return kFindCall;}
static uint64_t __fastcall Create(void*,void*,const char*,int,int) {++gCreates;return 0;}
static uint64_t __fastcall Upload(void*,void*,uint64_t board,int method,int score,const int32_t* details,int count) {
  ++gUploads;gUsedBoard=board;Require(method==1&&score==123456&&!details&&!count,"Native score upload changed");return kUploadCall;
}
static uint64_t __fastcall Download(void*,void*,uint64_t board,int type,int first,int last) {
  ++gDownloads;gUsedBoard=board;Require(type==gExpectedType&&first==gExpectedFirst&&last==gExpectedLast,"Native download range changed");return kDownloadCall;
}
static bool __fastcall Entry(void*,void*,uint64_t,int index,void* entry,int32_t*,int) {
  ++gEntries;At<uint64_t>(entry,0)=123;At<int>(entry,8)=index+1;At<int>(entry,12)=987654;return true;
}
static const char* __fastcall Name(void*,void*,uint64_t) {return "Test player";}
static bool __fastcall UserInfo(void*,void*,uint64_t,bool) {return false;}
static void __fastcall Notify(void*,void*,const std::string*) {++gNotified;}
static void __fastcall ReleaseNotify(void*,void*,int) {}
static void* __cdecl Context(void*) {return gContext.data();}
static void __cdecl Register(void* callback,uint64_t call) {gCallback=callback;gCallbackCall=call;}
static void __cdecl Unregister(void*,uint64_t) {}
static void* gViewLeaderboard=nullptr;
static std::string gViewName="community_A29088_HC";
static void* __cdecl ViewName(void* output) {return new(output)std::string(gViewName);}
static void* __fastcall ViewLeaderboard(void*,void*,const std::string* name) {
  Require(*name==gViewName,"Tab changed selected boardset/mode identity");return gViewLeaderboard;
}
struct TitleDraw {std::string text;int x,y,width;};
static std::vector<TitleDraw> gTitles;
static void __fastcall Title(void*,void*,const char* text,int x,int y,int width) {gTitles.push_back({text,x,y,width});}
static void* gTitleSite=nullptr;
static void* gCharacterSite=nullptr;
static __declspec(naked) int __stdcall InvokeCharacter(unsigned) {
  __asm {
    push ebp
    sub esp,20
    mov ebp,esp
    mov eax,dword ptr [ebp+28]
    mov dword ptr [ebp+16],eax
    jmp dword ptr [gCharacterSite]
  }
}
static __declspec(naked) void __stdcall InvokeTitle(void*,const char*,int,int,int) {
  __asm {
    push ebp
    mov ebp,esp
    push offset finished
    push dword ptr [ebp+24]
    push dword ptr [ebp+20]
    push dword ptr [ebp+16]
    push dword ptr [ebp+12]
    mov ecx,dword ptr [ebp+8]
    jmp dword ptr [gTitleSite]
  finished:
    mov esp,ebp
    pop ebp
    ret 20
  }
}
struct Leaderboard {
  alignas(8) std::array<uint8_t,0xf0> bytes{};
  explicit Leaderboard(const std::string& name) {
    reinterpret_cast<void* (__thiscall*)(void*,void*,const std::string*)>(gBase+0x3e950)(bytes.data(),nullptr,&name);
  }
  ~Leaderboard() {reinterpret_cast<void (__thiscall*)(void*)>(gBase+0x3fca0)(bytes.data());}
  void Tick() {reinterpret_cast<void (__thiscall*)(void*,void*,float)>(gBase+0x41ef0)(nullptr,bytes.data(),0.016f);}
  void Found(bool found,bool ioFailure=false) {
    alignas(8) std::array<uint8_t,16> result{};
    At<uint64_t>(result.data(),0)=found?kBoard:0;result[8]=static_cast<uint8_t>(found);
    reinterpret_cast<void (__thiscall*)(void*,void*,bool)>(gBase+0x42280)(bytes.data(),result.data(),ioFailure);
  }
  void Queue(bool upload,int type=0) {
    if(upload) {
      auto* request=static_cast<uint8_t*>(std::calloc(1,0x2c));Require(request!=nullptr,"Allocation failed");
      At<int>(request,0x18)=123456;At<void*>(bytes.data(),0x58)=request;
    }else {
      auto* range=static_cast<int*>(std::calloc(3,sizeof(int)));Require(range!=nullptr,"Allocation failed");
      range[0]=type;range[1]=type==1?-7:1;range[2]=type==1?7:15;At<void*>(bytes.data(),0x60)=range;
    }
  }
  void Downloaded(int count,bool ioFailure=false,bool entriesHandle=true,bool original=false) {
    alignas(8) std::array<uint8_t,24> result{};
    At<uint64_t>(result.data(),0)=kBoard;At<uint64_t>(result.data(),8)=entriesHandle?0xabcdef0198765432ull:0;
    At<int>(result.data(),16)=count;
    const auto callback=original?static_cast<void*>(gBase+online_leaderboards::kNativeDownloadCallback):At<void*>(gCallback,0x1c);
    Require(original||At<void*>(gCallback,0x18)==bytes.data(),"Download callback owner changed");
    reinterpret_cast<void (__thiscall*)(void*,const void*,bool)>(callback)(bytes.data(),ioFailure?nullptr:result.data(),ioFailure);
  }
};
int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: online-leaderboards-tests.exe game.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));Require(gBase!=nullptr,"Cannot map game");
    online_leaderboards::Verify(gBase);
    // Verify every patch before writing any part of the feature.
    for(const auto site:{online_leaderboards::kViewGate,online_leaderboards::kPostGate,online_leaderboards::kTooltip,online_leaderboards::kDownloadCallback,online_leaderboards::kDownloadCallback+1,
                        online_leaderboards::kReadState,online_leaderboards::kReadState+1,online_leaderboards::kTitleCall,online_leaderboards::kTitleCall+1,
                        online_leaderboards::kCharacter,online_leaderboards::kCharacter+5}) {
      const uint8_t original=gBase[site],wrong=0xcc;WriteMemory(gBase+site,&wrong,1);
      bool rejected=false;try{online_leaderboards::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Unexpected instructions accepted");WriteMemory(gBase+site,&original,1);
      online_leaderboards::Verify(gBase);
    }
    std::array<uint8_t,0xa0> row{};gRows=row.data();
    Global<void*>(0x1d3d34)=row.data();Global<void*>(0x1d3d38)=row.data()+row.size();Global<int>(0x1d2c08)=0;
    JumpTo(gBase,0x96216,reinterpret_cast<void*>(&Allowed));JumpTo(gBase,0x9627e,reinterpret_cast<void*>(&Blocked));
    JumpTo(gBase,0x961ce,reinterpret_cast<void*>(&Blocked));
    JumpTo(gBase,0x71dd0,reinterpret_cast<void*>(&CustomMode));
    const uint8_t done=0xc3;WriteMemory(gBase+0x979d8,&done,1);
    // Demonstrate the original restriction, then execute the installed branches.
    for(int source:{0,1,2,3,-1}) {
      At<int>(row.data(),0x9c)=source;
      Require(Run(0x9620d)==(source==0),"Original view restriction differs");
      Global<int>(0x1d2c34)=-1;Run(0x96366);
      Require(Global<int>(0x1d2c34)==(source==0?2:0),"Original post restriction differs");
    }
    online_leaderboards::Install(gBase);
    Require(std::strcmp(reinterpret_cast<char*>(gBase+online_leaderboards::kTooltip),online_leaderboards::kNewTooltip)==0,"Tooltip not updated");
    for(int source:{0,1,2,3,255,256,-1})for(int custom:{0,1}) {
      At<int>(row.data(),0x9c)=source;const auto original=row;gCustom=custom;
      const bool supported=source>=0&&source<=2;
      Require(Run(0x961bb)==(supported&&!custom),"View gate changed custom-mode or source eligibility");
      Global<int>(0x1d2c34)=-1;Run(0x96366);
      Require(Global<int>(0x1d2c34)==(supported&&!custom?2:0),"Post gate changed custom-mode or source eligibility");
      Require(row==original,"Source metadata was modified");
      Require(reinterpret_cast<int (__cdecl*)()>(gBase+0x9d4f0)()==source,"Native source getter changed");
    }
    std::puts("PASS: native view/post branches for game-folder, Documents and Workshop sources; custom modes and unknown sources blocked; row metadata unchanged; guarded installation and tooltip.");

    JumpTo(gBase,0xd6d9c,reinterpret_cast<void*>(&std::malloc));JumpTo(gBase,0xd6dcf,reinterpret_cast<void*>(&std::free));
    JumpTo(gBase,0xe6280,reinterpret_cast<void*>(&std::memcpy));JumpTo(gBase,0xe5a70,reinterpret_cast<void*>(&std::memmove));JumpTo(gBase,0xe6060,reinterpret_cast<void*>(&std::memset));
    gVtable[22]=reinterpret_cast<void*>(&Create);gVtable[23]=reinterpret_cast<void*>(&Find);
    gVtable[28]=reinterpret_cast<void*>(&Download);gVtable[30]=reinterpret_cast<void*>(&Entry);gVtable[31]=reinterpret_cast<void*>(&Upload);gContext[5]=&gStats;
    gFriendsVtable[7]=reinterpret_cast<void*>(&Name);gFriendsVtable[37]=reinterpret_cast<void*>(&UserInfo);gContext[2]=&gFriends;
    const auto context=&Context;WriteMemory(gBase+0x15f400,&context,sizeof(context));
    const auto reg=&Register;WriteMemory(gBase+0x15f3f0,&reg,sizeof(reg));
    const auto unreg=&Unregister;WriteMemory(gBase+0x15f3f4,&unreg,sizeof(unreg));
    alignas(8) std::array<uint8_t,0x2780> board{};
    new(board.data()+0x2710)std::string("community");new(board.data()+0x2728)std::string("A29088");
    for(const char* mode:{"","C","HA","HC","KA","KC"}) {
      std::string name;const std::string extension(mode);
      reinterpret_cast<std::string* (__thiscall*)(void*,std::string*,const std::string*)>(gBase+0x549c0)(board.data(),&name,&extension);
      Require(name==std::string("community_A29088")+(*mode?"_":"")+mode,"Native leaderboard identity changed");
      for(bool upload:{false,true})for(int result:{0,1,2}) {
        const int finds=gFinds,uploads=gUploads,downloads=gDownloads;
        Leaderboard request(name);request.Queue(upload);request.Tick();
        Require(gFinds==finds+1&&gName==name&&!gCreates,"Lookup did not use find-only and the exact native name");
        Require(gUploads==uploads&&gDownloads==downloads,"Score operation started before Steam found the table");
        Require(gCallbackCall==kFindCall&&gCallback==request.bytes.data()+0x90,"64-bit lookup callback changed");
        // A failed API request has no usable result/leaderboard handle.
        request.Found(result==1,result==2);
        if(result==1) {
          Require(At<uint64_t>(request.bytes.data(),0x20)==kBoard,"Leaderboard handle lost high bits");
          request.Tick();Require(gUsedBoard==kBoard,"Wrong leaderboard used");
          Require(gUploads==uploads+upload&&gDownloads==downloads+!upload,"Native upload/download changed");
          Require(gCallbackCall==(upload?kUploadCall:kDownloadCall)&&gCallback==request.bytes.data()+(upload?0xb0:0xd0),"Score async callback changed");
        }else {
          Require(At<int>(request.bytes.data(),0x34)==4,"Lookup failure did not reach native error state");
          if(result==0)Require(At<std::string>(request.bytes.data(),0x40).find("Could not find a leaderboard")!=std::string::npos,"Missing-table error changed");
          request.Tick();Require(gUploads==uploads&&gDownloads==downloads&&!gCreates,"Failed lookup uploaded a score or created a table");
        }
      }
    }
    At<std::string>(board.data(),0x2710).~basic_string();At<std::string>(board.data(),0x2728).~basic_string();
    std::puts("PASS: six native mode names; find-only lookup; existing/missing/IO-failed tables; native upload/download and 64-bit callbacks. No Steam connection or real score upload.");
    std::array<void*,5> notifyVtable{};notifyVtable[2]=reinterpret_cast<void*>(&Notify);notifyVtable[4]=reinterpret_cast<void*>(&ReleaseNotify);
    void** notify=notifyVtable.data();
    {
      Leaderboard request("original_unplayed_board");request.Queue(false,1);
      gExpectedType=1;gExpectedFirst=-7;gExpectedLast=7;
      request.Tick();request.Found(true);request.Tick();const int downloads=gDownloads;
      request.Downloaded(0,false,true,true);request.Tick();
      Require(gDownloads==downloads&&!At<void*>(request.bytes.data(),0x60),"Original viewer unexpectedly retried an empty around-player result");
    }
    for(int type:{0,1,2})for(int count:{0,1,15})for(bool ioFailure:{false,true})for(bool entriesHandle:{false,true}) {
      if(count&&!entriesHandle)continue;
      const int downloads=gDownloads,uploads=gUploads,creates=gCreates,notified=gNotified,entries=gEntries;
      Leaderboard request("unplayed_board");request.Queue(false,type);
      gExpectedType=type;gExpectedFirst=type==1?-7:1;gExpectedLast=type==1?7:15;
      request.Tick();request.Found(true);request.Tick();
      Require(gDownloads==downloads+1&&gCallbackCall==kDownloadCall,"Initial download did not start");
      At<void*>(request.bytes.data(),0x8c)=&notify;
      request.Downloaded(count,ioFailure,entriesHandle);
      const bool fallback=type==1&&count==0&&!ioFailure;
      if(fallback) {
        Require(At<int>(request.bytes.data(),0x34)==0&&At<void*>(request.bytes.data(),0x60)&&gNotified==notified,"Fallback discarded pending UI request");
        gExpectedType=0;gExpectedFirst=1;gExpectedLast=15;
        request.Tick();Require(gDownloads==downloads+2&&gUsedBoard==kBoard,"Fallback did not request top 15 on the same board");
        request.Downloaded(entriesHandle?15:0,false,entriesHandle);
        Require(gEntries==entries+(entriesHandle?15:0),"Fallback did not decode native score rows");
      }else Require(gEntries==entries+(!ioFailure?count:0),"Existing score results changed");
      Require(At<int>(request.bytes.data(),0x34)==(!ioFailure?0:4),"Native completion/error state changed");
      Require(!At<void*>(request.bytes.data(),0x60)&&gNotified==notified+1,"Native completion did not release request and notify UI once");
      request.Tick();Require(gDownloads==downloads+1+fallback,"Empty/global/friends/error response retried indefinitely");
      Require(gUploads==uploads&&gCreates==creates,"Viewing scores uploaded a score or created a table");
    }
    // A valid but completely empty global table also completes without retrying.
    {
      Leaderboard request("empty_board");request.Queue(false,1);
      gExpectedType=1;gExpectedFirst=-7;gExpectedLast=7;
      request.Tick();request.Found(true);request.Tick();request.Downloaded(0);
      gExpectedType=0;gExpectedFirst=1;gExpectedLast=15;
      request.Tick();const int downloads=gDownloads;request.Downloaded(0);request.Tick();
      Require(gDownloads==downloads&&At<int>(request.bytes.data(),0x34)==0,"Empty table did not complete after one fallback");
    }
    {
      Leaderboard request("failed_retry");request.Queue(false,1);
      gExpectedType=1;gExpectedFirst=-7;gExpectedLast=7;
      request.Tick();request.Found(true);request.Tick();request.Downloaded(0);
      gExpectedType=0;gExpectedFirst=1;gExpectedLast=15;
      request.Tick();const int downloads=gDownloads;request.Downloaded(0,true);request.Tick();
      Require(gDownloads==downloads&&At<int>(request.bytes.data(),0x34)==4&&!At<void*>(request.bytes.data(),0x60),"Failed fallback did not preserve native error/cleanup");
    }
    std::puts("PASS: unranked-player fallback through registered native async callback; top-15 rows decoded; ranked/global/friends results, empty tables and IO failures; one UI completion; no uploads or table creation.");
    // Execute the installed state-read call and the game's real queueing method.
    // This also covers changing filters while a previous request is pending.
    // Return without touching EAX so the hook's native state result is visible.
    WriteMemory(gBase+online_leaderboards::kReadState+5,&done,1);
    JumpTo(gBase,0x9d360,reinterpret_cast<void*>(&ViewName));
    JumpTo(gBase,0x404c0,reinterpret_cast<void*>(&ViewLeaderboard));
    std::array<void*,23> managerVtable{};managerVtable[22]=gBase+0x40ab0;
    std::array<uint8_t,0x38> manager{};At<void*>(manager.data(),0)=managerVtable.data();At<uint8_t>(manager.data(),0x34)=1;
    Global<void*>(online_leaderboards::kManager)=manager.data();
    gCharacterSite=gBase+online_leaderboards::kCharacter;
    uint8_t characterReturn[]={0x8b,0xe5,0x83,0xc4,20,0x5d,0xb8,0,0,0,0,0xc2,4,0};
    WriteMemory(online_leaderboards::characterResume,characterReturn,sizeof(characterReturn));
    characterReturn[7]=1;WriteMemory(online_leaderboards::characterDone,characterReturn,sizeof(characterReturn));
    for(int scene:{2,4,5,6})for(int state:{0,1,7})for(unsigned key:{9u,static_cast<unsigned>('F'),static_cast<unsigned>('f'),static_cast<unsigned>('x')}) {
      Global<int>(0x1a7074)=scene;Global<int>(online_leaderboards::kState)=state;
      Global<uint8_t>(online_leaderboards::kKey)='?';Global<int>(0x1b93b8)=1;online_leaderboards::tabPending=false;
      const bool capture=scene==5&&state==7&&key==9;
      Require(InvokeCharacter(key)==static_cast<int>(capture)&&online_leaderboards::tabPending==capture,"Tab captured outside online scores or F handling changed");
      Require(Global<uint8_t>(online_leaderboards::kKey)==(capture?'?':key)&&Global<int>(0x1b93b8)==1,"Character hook changed key replay or consumed menu activation");
    }
    Global<int>(0x1a7074)=5;online_leaderboards::tabPending=false;
    const int uploads=gUploads,creates=gCreates;
    using View=online_leaderboards::View;
    using Rank=online_leaderboards::PersonalRank;
    for(bool ranked:{false,true})for(bool earlyTab:{false,true})for(const char* mode:{"","C","HA","HC","KA","KC"}) {
      gViewName=std::string("community_A29088")+(*mode?"_":"")+mode;
      Leaderboard request(gViewName);gViewLeaderboard=request.bytes.data();
      Global<int>(online_leaderboards::kState)=0;Global<uint8_t>(online_leaderboards::kKey)='F';
      Require(Run(online_leaderboards::kReadState)==0&&online_leaderboards::view==View::AroundUser&&Global<uint8_t>(online_leaderboards::kKey)=='F',"F affected local scores");
      request.Queue(false,1);Global<int>(online_leaderboards::kState)=7;Run(online_leaderboards::kReadState);
      Require(online_leaderboards::viewing==request.bytes.data()&&online_leaderboards::personalRank==Rank::Unknown,"Viewer did not bind the selected table with unknown rank");
      if(earlyTab) {
        auto* range=At<void*>(request.bytes.data(),0x60);InvokeCharacter(9);Run(online_leaderboards::kReadState);
        Require(online_leaderboards::tabPending&&online_leaderboards::view==View::AroundUser&&At<void*>(request.bytes.data(),0x60)==range,"Early Tab cancelled personal-rank detection");
      }
      // A different table's late callback must not change the current viewer.
      {
        Leaderboard other("other_board");other.Queue(false,1);
        gExpectedType=1;gExpectedFirst=-7;gExpectedLast=7;
        other.Tick();other.Found(true);other.Tick();other.Downloaded(0);
        Require(online_leaderboards::personalRank==Rank::Unknown&&online_leaderboards::view==View::AroundUser,"Other table changed viewer rank/view");
      }
      gExpectedType=1;gExpectedFirst=-7;gExpectedLast=7;
      request.Tick();request.Found(true);request.Tick();request.Downloaded(ranked?15:0);
      Require(online_leaderboards::personalRank==(ranked?Rank::Present:Rank::Absent)&&online_leaderboards::view==(ranked?View::AroundUser:View::Top),"Initial result selected the wrong view or rank availability");
      if(!ranked) {
        gExpectedType=0;gExpectedFirst=1;gExpectedLast=15;request.Tick();
        if(!earlyTab)request.Downloaded(15);
      }
      if(earlyTab) {
        Run(online_leaderboards::kReadState);
        Require(online_leaderboards::view==View::Friends&&!online_leaderboards::tabPending,"Deferred Tab did not enable friends after rank detection");
        gExpectedType=2;gExpectedFirst=1;gExpectedLast=15;request.Tick();request.Downloaded(0);
      }
      InvokeCharacter('F');Run(online_leaderboards::kReadState);
      Require(online_leaderboards::view==(earlyTab?View::Friends:(ranked?View::AroundUser:View::Top))&&Global<uint8_t>(online_leaderboards::kKey)=='F',"F changed view or was consumed");
      const View rankedCycle[]={earlyTab?View::Top:View::Friends,earlyTab?View::AroundUser:View::Top,earlyTab?View::Friends:View::AroundUser};
      const View unrankedCycle[]={earlyTab?View::Top:View::Friends,earlyTab?View::Friends:View::Top};
      for(int step=0;step<6;++step) {
        const View expected=ranked?rankedCycle[step%3]:unrankedCycle[step%2];
        InvokeCharacter(9);Require(Run(online_leaderboards::kReadState)==7,"Tab left online scores");
        Require(online_leaderboards::view==expected&&!online_leaderboards::tabPending,"Tab cycle included the wrong view");
        auto* range=At<int*>(request.bytes.data(),0x60);
        gExpectedType=expected==View::AroundUser?1:(expected==View::Friends?2:0);
        gExpectedFirst=expected==View::AroundUser?-7:1;gExpectedLast=expected==View::AroundUser?7:15;
        Require(range&&range[0]==gExpectedType&&range[1]==gExpectedFirst&&range[2]==gExpectedLast,"View queued the wrong native request/range");
        request.Tick();const int downloads=gDownloads,entries=gEntries;
        Run(online_leaderboards::kReadState);request.Tick();Require(gDownloads==downloads,"One Tab advanced the cycle twice");
        const int count=expected==View::AroundUser?15:(expected==View::Friends&&step%2==0?30:0);
        request.Downloaded(count);
        Require(gEntries==entries+(count?15:0),"Result violated native 15-row limit");
        Require(online_leaderboards::personalRank==(ranked?Rank::Present:Rank::Absent)&&online_leaderboards::view==expected,"Empty friends/top response changed rank availability or view");
      }
      Global<int>(online_leaderboards::kState)=0;Run(online_leaderboards::kReadState);
      Require(online_leaderboards::view==View::AroundUser&&online_leaderboards::personalRank==Rank::Unknown&&!online_leaderboards::viewing,"Leaving viewer retained view/rank availability");
      Global<int>(online_leaderboards::kState)=7;Run(online_leaderboards::kReadState);
      Require(online_leaderboards::view==View::AroundUser&&online_leaderboards::personalRank==Rank::Unknown,"New board/mode inherited a previous rank restriction");
    }
    // IO failure does not prove the player has no score; a callback after
    // closing the viewer must not carry a restriction into the next session.
    for(bool ioFailure:{false,true}) {
      Leaderboard request(gViewName);gViewLeaderboard=request.bytes.data();request.Queue(false,1);
      Global<int>(online_leaderboards::kState)=0;Run(online_leaderboards::kReadState);
      Global<int>(online_leaderboards::kState)=7;Run(online_leaderboards::kReadState);
      gExpectedType=1;gExpectedFirst=-7;gExpectedLast=7;request.Tick();request.Found(true);request.Tick();
      if(!ioFailure){Global<int>(online_leaderboards::kState)=0;Run(online_leaderboards::kReadState);}
      request.Downloaded(0,ioFailure);
      Require(online_leaderboards::personalRank==Rank::Unknown&&online_leaderboards::view==View::AroundUser,"IO-failed or closed-view callback imposed an unranked restriction");
    }
    Require(gUploads==uploads&&gCreates==creates,"Tab uploaded a score or created a leaderboard");
    JumpTo(gBase,online_leaderboards::kNativeTitle,reinterpret_cast<void*>(&Title));
    WriteMemory(gBase+online_leaderboards::kTitleCall+5,&done,1);
    gTitleSite=gBase+online_leaderboards::kTitleCall;
    std::array<uint8_t,0x24> font{};At<int>(font.data(),0x20)=20;
    for(View view:{View::AroundUser,View::Friends,View::Top})for(const char* mode:{"Normal","Classic","Hard","Hard Classic","Kid","Kid Classic"}) {
      online_leaderboards::view=view;gTitles.clear();const std::string title=std::string("Online Scores - ")+mode;
      InvokeTitle(font.data(),title.c_str(),0,200,1024);
      Require(gTitles.size()==(view==View::AroundUser?2u:3u)&&gTitles[0].text==title&&gTitles[0].y==200,"Native title changed or default subtitle was not blank");
      if(view!=View::AroundUser)Require(gTitles[1].text==(view==View::Friends?"Friends":"Top 15")&&gTitles[1].x==0&&gTitles[1].y==227&&gTitles[1].width==1024,"View subtitle text/placement differs");
      const auto& hint=gTitles.back();
      Require(hint.text=="Press Tab to change view"&&hint.x==0&&hint.y==590&&hint.width==1024,"Online view hint text/centering/placement differs");
    }
    std::puts("PASS: native Tab dispatch; ranked default/Friends/Top cycle; unranked Top/Friends cycle; early Tab during rank lookup; other-table/closed-view/IO-failed callbacks; per-view reset; all six mode names; blank/Friends/Top 15 subtitles; 15-row cap; no uploads or table creation.");
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
