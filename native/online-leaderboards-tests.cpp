// Execute the native eligibility branches, name builder and Steam state machine.
// Steam is replaced with a fake interface; no real scores or saves are touched.
#include "online-leaderboards.hpp"
#include "aex-update.hpp"
#include <atomic>
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
// Parser and startup-download tests use synthetic identifiers and fictional board data.
namespace blocklist_tests {
static std::atomic<int> gFetches{0};
static int gLaunches=0;
static bool gFailLaunch=false,gFailFetch=false,gThrowFetch=false;
static std::string gText;
static HANDLE gCheckComplete=nullptr;
static bool Fetch(std::string& text) {
  ++gFetches;
  if(gThrowFetch)throw std::runtime_error("Simulated HTTP exception");
  text=gText;return !gFailFetch;
}
static BOOL WINAPI CreateChecker(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,
    LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION child) {
  ++gLaunches;
  if(gFailLaunch)return FALSE;
  child->hProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  child->hThread=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  Require(child->hProcess&&child->hThread,"Cannot create fake checker");
  Require(DuplicateHandle(GetCurrentProcess(),child->hProcess,GetCurrentProcess(),&gCheckComplete,
    EVENT_MODIFY_STATE,FALSE,0)!=FALSE,"Cannot retain checker completion signal");
  return TRUE;
}
static void AwaitFinished() {
  const auto deadline=GetTickCount64()+5000;
  while(!InterlockedCompareExchange(&score_blocklist::finished,0,0)&&GetTickCount64()<deadline)Sleep(1);
  Require(score_blocklist::finished!=0,"Blocklist worker did not finish");
}
static void Run() {
    const uint64_t blocked=90000000000000001ull,other=90000000000000007ull;
    Require(score_blocklist::Parse("").empty(),"Empty blocklist is not empty");
    Require(score_blocklist::Parse("\xef\xbb\xbf 90000000000000007 \r\n\n90000000000000001\r\n90000000000000001\n")
      ==std::vector<uint64_t>({blocked,other}),"BOM, CRLF, whitespace, precision or duplicates mishandled");
    Require(score_blocklist::Parse("0\n-1\n+1\n18446744073709551616 # overflow\n<html>\n# 90000000000000001\n").empty(),
      "Malformed lines or overflowing IDs accepted");
    Require(score_blocklist::Parse("18446744073709551615")==std::vector<uint64_t>({UINT64_MAX}),"uint64 maximum rejected");
    std::puts("PASS: exact 64-bit SteamIDs, BOM/CRLF/LF, final line, whitespace, duplicates, empty and malformed lines, overflow");
    const char* annotated=
      "90000000000000001 # Test Set Alpha (Normal) - 111111111\\\r\n"
      "90000000000000002 # Test Set Alpha (Wide) (Normal) - 222222222\\\r\n"
      "90000000000000003 # Test Set Beta (Normal) - 3333333\\\r\n"
      "90000000000000004 # Test Set Beta (Normal) - 4444444\\\r\n"
      "90000000000000005 # Test Set Gamma (Normal) - 555555\\\r\n"
      "90000000000000006 # Test Set Alpha (Wide) (Casual) - 666666666";
    std::vector<uint64_t> expected={90000000000000001ull,90000000000000002ull,90000000000000003ull,
      90000000000000004ull,90000000000000005ull,90000000000000006ull};
    std::sort(expected.begin(),expected.end());
    Require(score_blocklist::Parse(annotated)==expected,"Annotated six-user blocklist was not parsed correctly");
    Require(score_blocklist::Parse(" \t90000000000000001#comment 90000000000000007\n90000000000000001 trailing text\n")
      ==std::vector<uint64_t>{blocked},"Trailing text was not ignored or more than one ID was read from a line");
    std::puts("PASS: six synthetic annotated entries; leading whitespace; comments/arbitrary suffixes; trailing backslashes; one ID per line; duplicates removed");
    score_blocklist::fetch=&Fetch;aex_update::gCreateProcess=&CreateChecker;
    aex_update::gAfterUpdateCheck=&score_blocklist::StartOnce;
    for(int scenario=0;scenario<6;++scenario) {
      score_blocklist::started=score_blocklist::finished=0;score_blocklist::Publish({});
      aex_update::gCheckStarted=aex_update::gStartupTaskStarted=0;
      aex_update::gCheckForUpdates=scenario!=1;
      gFailLaunch=scenario==2;gFailFetch=scenario==3;gThrowFetch=scenario==4;
      gText=scenario==5?std::string(score_blocklist::kMaxBytes+1,'1'):"90000000000000001 # Example board - 111111111\r\n";
      gFetches=0;const int launches=gLaunches;
      aex_update::CheckForUpdatesOnce();
      Require(gLaunches==launches+(scenario!=1),"Wrong update-check launch count");
      if(aex_update::gUpdater) {
        // Close the original before completion: the worker must own a duplicate.
        CloseHandle(aex_update::gUpdater);aex_update::gUpdater=nullptr;
        Sleep(25);Require(gFetches==0,"Blocklist downloaded before the update check finished");
        SetEvent(gCheckComplete);CloseHandle(gCheckComplete);gCheckComplete=nullptr;
      }
      AwaitFinished();
      Require(gFetches==1,"Blocklist was not fetched exactly once");
      Require(score_blocklist::ids==(scenario<3?std::vector<uint64_t>{blocked}:std::vector<uint64_t>{}),
        "Failed/oversized download published IDs, or successful download was lost");
      for(int repeat=0;repeat<3;++repeat)aex_update::CheckForUpdatesOnce();
      Require(gFetches==1&&gLaunches==launches+(scenario!=1),"Startup downloads repeated");
      aex_update::CloseSignals();
    }
    std::puts("PASS: asynchronous fetch after update-check completion; disabled/failed checks still fetch; duplicate handle ownership; once per startup; offline/exception/oversized responses fail open");
    score_blocklist::started=score_blocklist::finished=0;score_blocklist::enabled=false;
    const int fetches=gFetches;score_blocklist::StartOnce(nullptr);
    Require(!score_blocklist::started&&!score_blocklist::finished&&gFetches==fetches,"Disabled score filtering started a download");
    score_blocklist::enabled=true;
    std::puts("PASS: disabled FilterScores starts no worker or blocklist download");
    score_blocklist::fetch=&score_blocklist::Download;
    score_blocklist::Publish({});
    aex_update::gCreateProcess=&CreateProcessW;
    aex_update::gAfterUpdateCheck=nullptr;
    aex_update::gCheckForUpdates=false;
    aex_update::gCheckStarted=aex_update::gStartupTaskStarted=0;
}
} // namespace blocklist_tests

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
static int gNotifyExpectedRows=-1,gReleased=0;
static std::vector<uint64_t> gEntryIDs;
static std::vector<int> gEntryRanks;
static std::vector<uint64_t> gLookupIDs;
static int gLookups=0;
static bool gFailLookup=false;
static bool gLongNames=false;
static std::string gName;
static uint64_t __fastcall Find(void*,void*,const char* name) {++gFinds;gName=name;return kFindCall;}
static uint64_t __fastcall Create(void*,void*,const char*,int,int) {++gCreates;return 0;}
static uint64_t __fastcall Upload(void*,void*,uint64_t board,int method,int score,const int32_t* details,int count) {
  ++gUploads;gUsedBoard=board;Require(method==1&&score==123456&&!details&&!count,"Native score upload changed");return kUploadCall;
}
static uint64_t __fastcall Download(void*,void*,uint64_t board,int type,int first,int last) {
  ++gDownloads;gUsedBoard=board;Require(type==gExpectedType&&first==gExpectedFirst&&last==gExpectedLast,"Native download range changed");return kDownloadCall;
}
static uint64_t __fastcall DownloadUsers(void*,void*,uint64_t board,const uint64_t* users,int count) {
  Require(board==kBoard&&count>0&&count<=100,"Invalid blocked-rank lookup");
  ++gLookups;gLookupIDs.assign(users,users+count);return gFailLookup?0:kDownloadCall+1;
}
static bool __fastcall Entry(void*,void*,uint64_t,int index,void* entry,int32_t* details,int count) {
  ++gEntries;At<uint64_t>(entry,0)=gEntryIDs.empty()?123:gEntryIDs.at(index);
  const int rank=gEntryRanks.empty()?index+1:gEntryRanks.at(index);
  At<int>(entry,8)=rank;At<int>(entry,12)=987655-rank;
  if(count&&details)*details=1000+rank;return true;
}
static const char* __fastcall Name(void*,void*,uint64_t) {return gLongNames?"Test player with a name exceeding native small string storage":"Test player";}
static bool __fastcall UserInfo(void*,void*,uint64_t,bool) {return false;}
static void __fastcall Notify(void*,void*,const std::string* name) {
  ++gNotified;
  if(gNotifyExpectedRows>=0) {
    auto* owner=reinterpret_cast<const uint8_t*>(name)-4;
    const auto* first=At<uint8_t*>(const_cast<uint8_t*>(owner),0x28);
    const auto* last=At<uint8_t*>(const_cast<uint8_t*>(owner),0x2c);
    Require((first?static_cast<int>((last-first)/0x40):0)==gNotifyExpectedRows,"UI notified before replacement rows were complete");
  }
}
static void __fastcall ReleaseNotify(void*,void*,int) {++gReleased;}
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
    blocklist_tests::Run();
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));Require(gBase!=nullptr,"Cannot map game");
    online_leaderboards::Verify(gBase);
    // Verify every patch before writing any part of the feature.
    for(const auto site:{online_leaderboards::kViewGate,online_leaderboards::kPostGate,online_leaderboards::kTooltip,online_leaderboards::kDownloadCallback,online_leaderboards::kDownloadCallback+1,
                        online_leaderboards::kReadState,online_leaderboards::kReadState+1,online_leaderboards::kTitleCall,online_leaderboards::kTitleCall+1,
                        online_leaderboards::kCharacter,online_leaderboards::kCharacter+5,online_leaderboards::kResizeRows,
                        leaderboard_filter::kStats,leaderboard_filter::kStats+1,leaderboard_filter::kSetCallback,leaderboard_filter::kAssignName,
                        online_leaderboards::kSetDownloadState}) {
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
    gVtable[29]=reinterpret_cast<void*>(&DownloadUsers);
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
        const bool cached=online_leaderboards::cachedViews[static_cast<size_t>(expected)].ready;
        const int previousDownloads=gDownloads,previousEntries=gEntries;
        InvokeCharacter(9);Require(Run(online_leaderboards::kReadState)==7,"Tab left online scores");
        Require(online_leaderboards::view==expected&&!online_leaderboards::tabPending,"Tab cycle included the wrong view");
        auto* range=At<int*>(request.bytes.data(),0x60);
        if(cached) {
          request.Tick();
          Require(!range&&gDownloads==previousDownloads&&gEntries==previousEntries&&At<int>(request.bytes.data(),0x34)==0,
            "Cached Tab view issued a new request or retained loading state");
          continue;
        }
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
      for(const auto& cached:online_leaderboards::cachedViews)Require(!cached.ready&&cached.rows.empty(),"Leaving online scores retained cached rows");
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
    for(int lookup:{0,1,2,3}) {
      Leaderboard table("hint_lookup");online_leaderboards::viewing=table.bytes.data();table.Tick();
      if(lookup!=3)table.Found(lookup==1,lookup==2);
      const bool found=lookup==1;
      for(View view:{View::AroundUser,View::Friends,View::Top})for(const char* mode:{"Normal","Classic","Hard","Hard Classic","Kid","Kid Classic"}) {
      online_leaderboards::view=view;gTitles.clear();const std::string title=std::string("Online Scores - ")+mode;
      InvokeTitle(font.data(),title.c_str(),0,200,1024);
      Require(gTitles.size()==(view==View::AroundUser?1u:2u)+(found?1u:0u)&&gTitles[0].text==title&&gTitles[0].y==200,"Native title changed or lookup showed an unavailable view hint");
      if(view!=View::AroundUser)Require(gTitles[1].text==(view==View::Friends?"Friends":"Top 15")&&gTitles[1].x==0&&gTitles[1].y==227&&gTitles[1].width==1024,"View subtitle text/placement differs");
      if(found) {
        const auto& hint=gTitles.back();
        Require(hint.text=="Press Tab to change view"&&hint.x==0&&hint.y==590&&hint.width==1024,"Online view hint text/centering/placement differs");
      }
      }
    }
    online_leaderboards::viewing=nullptr;
    // Decode real native rows, filter before the viewer draws them, and let
    // the native resize/destructor release both short and heap-backed names.
    const uint64_t blocked=90000000000000001ull,other=90000000000000007ull;
    gEntryIDs={blocked,other,blocked,blocked+(1ull<<32),other+1,blocked};
    for(bool longNames:{false,true})for(bool allBlocked:{false,true})for(int type:{0,1,2})
      for(const char* mode:{"","C","HA","HC","KA","KC"}) {
      gLongNames=longNames;
      auto blockedIDs=allBlocked?gEntryIDs:std::vector<uint64_t>{blocked};
      std::sort(blockedIDs.begin(),blockedIDs.end());score_blocklist::Publish(std::move(blockedIDs));
      gViewName=std::string("community_A29088")+(*mode?"_":"")+mode;
      Leaderboard request(gViewName);request.Queue(false,type);
      gExpectedType=type;gExpectedFirst=type==1?-7:1;gExpectedLast=type==1?7:15;
      request.Tick();request.Found(true);request.Tick();request.Downloaded(static_cast<int>(gEntryIDs.size()));
      auto* first=At<uint8_t*>(request.bytes.data(),0x28);auto* last=At<uint8_t*>(request.bytes.data(),0x2c);
      Require(last-first==(allBlocked?0:3*0x40),"Blocked score rows remained or allowed rows were lost");
      if(!allBlocked)for(int i=0;i<3;++i) {
        const int original[]={1,3,4};auto* entry=first+i*0x40;
        Require(At<uint64_t>(entry,0)==gEntryIDs[original[i]]&&At<int>(entry,8)==original[i]+1&&
          At<int>(entry,12)==987654-original[i],"Filtering changed SteamID high bits, order, native rank or score");
        Require(At<std::string>(entry,0x20)==Name(nullptr,nullptr,0),"Filtering corrupted a native player name");
      }
      const int downloads=gDownloads;request.Tick();
      Require(gDownloads==downloads,"Filtering all rows triggered an unranked fallback or another download");
    }
    score_blocklist::Publish({});gLongNames=false;
    {
      Leaderboard request(gViewName);gViewLeaderboard=request.bytes.data();request.Queue(false);
      gExpectedType=0;gExpectedFirst=1;gExpectedLast=15;
      request.Tick();request.Found(true);request.Tick();request.Downloaded(static_cast<int>(gEntryIDs.size()));
      auto count=[&](){return (At<uint8_t*>(request.bytes.data(),0x2c)-At<uint8_t*>(request.bytes.data(),0x28))/0x40;};
      Require(count()==6,"Empty blocklist filtered a score");
      score_blocklist::Publish({blocked});online_leaderboards::viewing=request.bytes.data();leaderboard_filter::Reset();
      Global<int>(online_leaderboards::kState)=7;Run(online_leaderboards::kReadState);
      Require(At<void*>(request.bytes.data(),0x60)!=nullptr,"Late blocklist did not queue a fresh filtered view");
      auto* queued=At<int*>(request.bytes.data(),0x60);Run(online_leaderboards::kReadState);
      Require(At<int*>(request.bytes.data(),0x60)==queued,"Late blocklist queued more than one refresh");
      online_leaderboards::viewing=nullptr;
    }
    score_blocklist::Publish({});gEntryIDs.clear();
    leaderboard_filter::Reset();
    Require(gUploads==uploads&&gCreates==creates,"Blocklist filtering uploaded a score or created a table");
    std::puts("PASS: blocked SteamID64 rows removed in all six modes and all views; short/long names; all blocked; empty list; late download refresh queued once; no uploads");
    const auto userForRank=[](int rank){return 90000000100000000ull+static_cast<uint64_t>(rank);};
    const auto publishRanks=[&](const std::vector<int>& ranks) {
      std::vector<uint64_t> ids;for(int rank:ranks)ids.push_back(userForRank(rank));
      std::sort(ids.begin(),ids.end());score_blocklist::Publish(std::move(ids));
    };
    const auto page=[&](const std::vector<int>& ranks) {
      gEntryRanks=ranks;gEntryIDs.clear();for(int rank:ranks)gEntryIDs.push_back(userForRank(rank));
    };
    const auto interval=[&](int first,int last) {
      std::vector<int> ranks;for(int rank=first;rank<=last;++rank)ranks.push_back(rank);page(ranks);
    };
    const auto startView=[&](Leaderboard& request,int type) {
      leaderboard_filter::Reset();online_leaderboards::ClearCache();online_leaderboards::viewing=request.bytes.data();gViewLeaderboard=request.bytes.data();
      online_leaderboards::view=type==0?View::Top:(type==1?View::AroundUser:View::Friends);
      Global<int>(online_leaderboards::kState)=7;request.Queue(false,type);
      gExpectedType=type;gExpectedFirst=type==1?-7:1;gExpectedLast=type==1?7:15;
      request.Tick();request.Found(true);request.Tick();At<void*>(request.bytes.data(),0x8c)=&notify;
    };
    const auto assertRows=[&](Leaderboard& request,const std::vector<int>& originals,const std::vector<int>& blockedRanks,bool top) {
      auto* first=At<uint8_t*>(request.bytes.data(),0x28);auto* last=At<uint8_t*>(request.bytes.data(),0x2c);
      Require((first?static_cast<size_t>((last-first)/0x40):0)==originals.size(),"Wrong replacement row count");
      for(size_t i=0;i<originals.size();++i) {
        auto* row=first+i*0x40;const int original=originals[i];
        const int rank=top?static_cast<int>(i)+1:original-static_cast<int>(std::count_if(blockedRanks.begin(),blockedRanks.end(),[&](int blockedRank){return blockedRank<original;}));
        Require(At<uint64_t>(row,0)==userForRank(original)&&At<int>(row,8)==rank&&At<int>(row,12)==987655-original&&
          At<int>(row,0x38)==1000+original,"Replacement corrupted identity, filtered rank, score, or reached board");
        Require(At<std::string>(row,0x20)==Name(nullptr,nullptr,0),"Replacement corrupted native player name");
      }
      Require(!At<void*>(request.bytes.data(),0x60)&&At<int>(request.bytes.data(),0x34)==0&&!leaderboard_filter::pending,
        "Completed replacement retained a request or pending state");
    };
    std::vector<int> firstTwenty,firstThirty,sparseRun{1};
    for(int rank=1;rank<=30;++rank) {
      if(rank<=20)firstTwenty.push_back(rank);firstThirty.push_back(rank);
      if(rank>=16)sparseRun.push_back(rank);
    }
    for(bool longNames:{false,true})for(const std::vector<int>& excluded:std::vector<std::vector<int>>{{1},{5},{15},{1,5,15,16,18},firstTwenty,firstThirty,sparseRun})
      for(const char* mode:{"","C","HA","HC","KA","KC"}) {
      publishRanks(excluded);gLongNames=longNames;
      gViewName=std::string("community_A29088")+(*mode?"_":"")+mode;
      Leaderboard request(gViewName);startView(request,0);
      const int notified=gNotified,released=gReleased,lookups=gLookups;
      gNotifyExpectedRows=15;interval(1,15);request.Downloaded(15);
      int attempts=0;
      while(At<void*>(request.bytes.data(),0x60)) {
        Require(++attempts<10&&gNotified==notified,"Replacement loop failed to converge or notified UI too early");
        auto* range=At<int*>(request.bytes.data(),0x60);
        Require(range[0]==0&&range[1]==1+attempts*15&&range[2]==15+attempts*15,"Replacement did not request the next batch of 15");
        gExpectedType=range[0];gExpectedFirst=range[1];gExpectedLast=range[2];
        request.Tick();interval(gExpectedFirst,gExpectedLast);request.Downloaded(static_cast<int>(gEntryRanks.size()));
      }
      std::vector<int> expected;
      for(int rank=1;expected.size()<15;++rank)if(std::find(excluded.begin(),excluded.end(),rank)==excluded.end())expected.push_back(rank);
      assertRows(request,expected,excluded,true);
      if(excluded==sparseRun||excluded==firstThirty)Require(attempts==2,"Blocked runs should finish in three score downloads including the initial page");
      Require(gNotified==notified+1&&gReleased==released+1&&gLookups==lookups,"Replacement notification/release repeated or Top requested unrelated users");
      const int downloads=gDownloads;Run(online_leaderboards::kReadState);request.Tick();
      Require(gDownloads==downloads,"Completed filtered view downloaded again on UI tick");
      online_leaderboards::viewing=nullptr;
    }
    std::puts("PASS: replacement batches of 15; exclusions at 1, 5, or 15; blocked 1-30 and 1 plus 16-30 finish in three score downloads; first 15 allowed entries retained; six modes; long names; single final UI notification");
    for(int total:{0,8,15}) {
      publishRanks({5});Leaderboard request(gViewName);startView(request,0);
      const int expected=total-(total>=5);gNotifyExpectedRows=expected;
      interval(1,(std::min)(15,total));request.Downloaded(static_cast<int>(gEntryRanks.size()));
      if(total==15) {
        Require(At<int*>(request.bytes.data(),0x60)[1]==16,"Full last page did not check whether rank 16 exists");
        gExpectedType=0;gExpectedFirst=16;gExpectedLast=30;request.Tick();page({});request.Downloaded(0);
      }
      std::vector<int> originals;for(int rank=1;rank<=total;++rank)if(rank!=5)originals.push_back(rank);
      assertRows(request,originals,{5},true);online_leaderboards::viewing=nullptr;
    }
    std::puts("PASS: empty/short/exactly-15 tables stop at actual end and show every remaining valid score");
    // Friends contains more than the native 15-row cap. Consume its already
    // downloaded rows before requesting the blocked users' global positions.
    {
      const std::vector<int> excluded={1,6,42,61};publishRanks(excluded);
      Leaderboard request(gViewName);startView(request,2);gNotifyExpectedRows=15;
      std::vector<int> friends;for(int rank=3;rank<=60;rank+=3)friends.push_back(rank);page(friends);
      const int downloads=gDownloads,lookups=gLookups,notified=gNotified;
      request.Downloaded(static_cast<int>(friends.size()));
      Require(gLookups==lookups+1&&gCallbackCall==kDownloadCall+1&&gNotified==notified,"Friends did not start one blocked-global-rank lookup");
      page(excluded);request.Downloaded(static_cast<int>(excluded.size()));
      std::vector<int> originals;for(int rank:friends)if(rank!=6&&rank!=42&&originals.size()<15)originals.push_back(rank);
      assertRows(request,originals,excluded,false);Require(gDownloads==downloads,"Friends redownloaded rows already in its result");
      online_leaderboards::viewing=nullptr;
    }
    // Personal view can need several replacements and blocked global ranks
    // above its window. At the table's end, extend upward to keep 15 entries.
    for(bool bottom:{false,true}) {
      const std::vector<int> excluded=bottom?std::vector<int>{1,47,49}:std::vector<int>{1,45,55,56};publishRanks(excluded);
      Leaderboard request(gViewName);startView(request,1);gNotifyExpectedRows=15;
      interval(bottom?44:40,bottom?50:54);request.Downloaded(static_cast<int>(gEntryRanks.size()));
      int attempts=0;
      while(leaderboard_filter::pending&&!leaderboard_filter::pending->lookup) {
        Require(++attempts<10,"Personal replacement failed to converge");
        auto* range=At<int*>(request.bytes.data(),0x60);gExpectedType=range[0];gExpectedFirst=range[1];gExpectedLast=range[2];request.Tick();
        interval(gExpectedFirst,bottom?(std::min)(gExpectedLast,50):gExpectedLast);request.Downloaded(static_cast<int>(gEntryRanks.size()));
      }
      Require(leaderboard_filter::pending&&leaderboard_filter::pending->lookup,"Personal view did not request blocked ranks");
      page(excluded);request.Downloaded(static_cast<int>(excluded.size()));
      std::vector<int> originals;for(int rank=bottom?34:40;rank<=(bottom?50:57);++rank)
        if(std::find(excluded.begin(),excluded.end(),rank)==excluded.end())originals.push_back(rank);
      assertRows(request,originals,excluded,false);online_leaderboards::viewing=nullptr;
    }
    std::puts("PASS: Friends uses all downloaded entries; global rank correction includes blocked nonfriends; personal replacements skip blocked replacements, extend upward at table end, and include blocked ranks above the window");
    // Large blocklists use Steam's 100-users-per-request limit and reuse its
    // registered 64-bit callback, including successful empty lookup results.
    {
      std::vector<int> excluded;for(int rank=200;rank<325;++rank)excluded.push_back(rank);publishRanks(excluded);
      Leaderboard request(gViewName);startView(request,1);gNotifyExpectedRows=15;
      const int lookups=gLookups;interval(40,54);request.Downloaded(15);
      Require(gLookupIDs.size()==100,"First blocked-rank batch did not contain 100 users");
      page({});request.Downloaded(0);Require(gLookupIDs.size()==25,"Second blocked-rank batch was not the remaining 25 users");
      request.Downloaded(0);std::vector<int> originals;for(int rank=40;rank<=54;++rank)originals.push_back(rank);
      assertRows(request,originals,{},false);Require(gLookups==lookups+2,"Blocked rank batches repeated");online_leaderboards::viewing=nullptr;
    }
    for(bool lookupFailure:{false,true}) {
      publishRanks({1});Leaderboard request(gViewName);startView(request,lookupFailure?1:0);
      gNotifyExpectedRows=-1;const int notified=gNotified;interval(lookupFailure?40:1,lookupFailure?54:15);request.Downloaded(15);
      if(!lookupFailure) {gExpectedType=0;gExpectedFirst=16;gExpectedLast=30;request.Tick();}
      request.Downloaded(0,true);const int downloads=gDownloads;
      Run(online_leaderboards::kReadState);request.Tick();
      Require(gNotified==notified+1&&!leaderboard_filter::pending&&!At<void*>(request.bytes.data(),0x60)&&
        At<int>(request.bytes.data(),0x34)==4&&gDownloads==downloads,"Failed refill/lookup retried forever or did not finish once");
      online_leaderboards::viewing=nullptr;
    }
    {
      publishRanks({1});Leaderboard request(gViewName);startView(request,0);gNotifyExpectedRows=14;
      interval(1,15);request.Downloaded(15);gExpectedType=0;gExpectedFirst=16;gExpectedLast=30;request.Tick();
      interval(1,1);request.Downloaded(1);
      std::vector<int> originals;for(int rank=2;rank<=15;++rank)originals.push_back(rank);
      assertRows(request,originals,{1},true);online_leaderboards::viewing=nullptr;
    }
    {
      publishRanks({1});Leaderboard request(gViewName);startView(request,1);gNotifyExpectedRows=15;
      interval(40,54);request.Downloaded(15);
      Require(leaderboard_filter::pending&&leaderboard_filter::pending->lookup,"Expected in-flight blocked-rank lookup");
      online_leaderboards::personalRank=Rank::Present;
      InvokeCharacter(9);Run(online_leaderboards::kReadState);
      Require(online_leaderboards::view==View::Friends&&!leaderboard_filter::pending,"Tab did not cancel the previous filtered page");
      // A late callback before the next native tick must not consume its newly
      // queued request or revive rows from the previous personal view.
      interval(1,1);request.Downloaded(1);
      Require(At<int>(request.bytes.data(),0x34)==0&&At<int*>(request.bytes.data(),0x60)[0]==2,"Late callback consumed the new view request");
      gExpectedType=2;gExpectedFirst=1;gExpectedLast=15;request.Tick();interval(2,16);request.Downloaded(15);
      interval(1,1);request.Downloaded(1);
      std::vector<int> originals;for(int rank=2;rank<=16;++rank)originals.push_back(rank);
      assertRows(request,originals,{1},false);online_leaderboards::viewing=nullptr;
    }
    {
      publishRanks({1});gFailLookup=true;Leaderboard request(gViewName);startView(request,1);gNotifyExpectedRows=15;
      interval(40,54);request.Downloaded(15);
      std::vector<int> originals;for(int rank=40;rank<=54;++rank)originals.push_back(rank);
      assertRows(request,originals,{},false);gFailLookup=false;online_leaderboards::viewing=nullptr;
    }
    // Cached rows retain their already adjusted ranks and owning player names.
    // Returning to one while another download is in flight cancels that request
    // without issuing another score download or blocked-user lookup.
    for(bool longNames:{false,true}) {
      publishRanks({5});gLongNames=longNames;Leaderboard request(gViewName);startView(request,0);gNotifyExpectedRows=15;
      interval(1,15);request.Downloaded(15);gExpectedType=0;gExpectedFirst=16;gExpectedLast=30;
      request.Tick();interval(16,30);request.Downloaded(15);
      Require(online_leaderboards::cachedViews[static_cast<size_t>(View::Top)].ready,"Completed filtered Top view was not cached");
      online_leaderboards::personalRank=Rank::Absent;
      InvokeCharacter(9);Run(online_leaderboards::kReadState);
      Require(online_leaderboards::view==View::Friends,"Top did not switch to Friends");
      gExpectedType=2;gExpectedFirst=1;gExpectedLast=15;request.Tick();
      const int downloads=gDownloads,lookups=gLookups;
      InvokeCharacter(9);Run(online_leaderboards::kReadState);request.Tick();
      Require(online_leaderboards::view==View::Top&&gDownloads==downloads&&gLookups==lookups,"Cached Top made another Steam request");
      std::vector<int> originals;for(int rank=1;rank<=16;++rank)if(rank!=5)originals.push_back(rank);
      assertRows(request,originals,{5},true);
      alignas(8) std::array<uint8_t,24> late{};At<uint64_t>(late.data(),0)=kBoard;
      At<uint64_t>(late.data(),8)=0xabcdef0198765432ull;At<int>(late.data(),16)=1;
      online_leaderboards::Downloaded(request.bytes.data(),nullptr,late.data(),false);
      assertRows(request,originals,{5},true);
      // Empty results are successful completed views too.
      InvokeCharacter(9);Run(online_leaderboards::kReadState);gExpectedType=2;gExpectedFirst=1;gExpectedLast=15;request.Tick();
      gNotifyExpectedRows=-1;page({});request.Downloaded(0);
      const int cachedDownloads=gDownloads,cachedLookups=gLookups;
      for(int cycle=0;cycle<4;++cycle){InvokeCharacter(9);Run(online_leaderboards::kReadState);request.Tick();}
      Require(gDownloads==cachedDownloads&&gLookups==cachedLookups,"Completed Top/empty Friends cycle requested data again");
      Require(online_leaderboards::cachedViews[static_cast<size_t>(View::Friends)].ready,"Empty Friends result was not cached");
      // A new blocklist invalidates every cached view and refreshes the active one.
      publishRanks({5,16});Run(online_leaderboards::kReadState);
      for(const auto& cached:online_leaderboards::cachedViews)Require(!cached.ready,"Blocklist change retained a stale cached view");
      Require(At<void*>(request.bytes.data(),0x60)!=nullptr,"Blocklist change did not queue a refresh");
      Global<int>(online_leaderboards::kState)=0;Run(online_leaderboards::kReadState);
      for(const auto& cached:online_leaderboards::cachedViews)Require(!cached.ready&&cached.rows.empty(),"Leaving retained a completed view");
      // Simulate the game's normal fresh around-user request on reopening.
      gNotifyExpectedRows=-1;
      auto* oldRange=At<void*>(request.bytes.data(),0x60);At<void*>(request.bytes.data(),0x60)=nullptr;std::free(oldRange);
      request.Queue(false,1);Global<int>(online_leaderboards::kState)=7;Run(online_leaderboards::kReadState);
      gExpectedType=1;gExpectedFirst=-7;gExpectedLast=7;const int beforeReopen=gDownloads;request.Tick();
      Require(gDownloads==beforeReopen+1,"Reopening online scores reused cached results");
      online_leaderboards::viewing=nullptr;
    }
    {
      publishRanks({5});score_blocklist::enabled=false;Leaderboard request(gViewName);startView(request,0);gNotifyExpectedRows=15;
      const int lookups=gLookups;interval(1,15);request.Downloaded(15);
      std::vector<int> originals;for(int rank=1;rank<=15;++rank)originals.push_back(rank);
      assertRows(request,originals,{},true);
      const int downloads=gDownloads;Run(online_leaderboards::kReadState);request.Tick();
      Require(gDownloads==downloads&&gLookups==lookups&&!leaderboard_filter::HasBlocklist()&&leaderboard_filter::BlockedIDs().empty(),
        "FilterScores=0 still filtered, refilled, or looked up blocked users");
      score_blocklist::enabled=true;online_leaderboards::viewing=nullptr;
    }
    gNotifyExpectedRows=-1;gLongNames=false;gEntryIDs.clear();gEntryRanks.clear();score_blocklist::Publish({});leaderboard_filter::Reset();online_leaderboards::ClearCache();
    Require(gUploads==uploads&&gCreates==creates,"Replacement flow uploaded scores or created tables");
    std::puts("PASS: 100-user blocked-rank batching; failed/invalid replacement/lookup stops; no-rank-progress guard; Tab cancels old assembly and ignores late callback; no uploads or table creation");
    std::puts("PASS: completed per-view cache; empty results cached; short/long player names and adjusted ranks restored; in-flight request cancelled; late callback ignored; no repeated downloads/lookups; blocklist invalidation; leaving clears cache and reopening downloads afresh");
    std::puts("PASS: FilterScores=0 preserves blocked rows and native ranks; no replacement downloads, blocked-user lookups, or automatic filter refresh");
    std::puts("PASS: view hint shown only for a found leaderboard; hidden for missing, IO-failed and pending lookups across all modes/views.");
    std::puts("PASS: native Tab dispatch; ranked default/Friends/Top cycle; unranked Top/Friends cycle; early Tab during rank lookup; other-table/closed-view/IO-failed callbacks; per-view reset; all six mode names; blank/Friends/Top 15 subtitles; 15-row cap; no uploads or table creation.");
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
