#pragma once
#include "patch.hpp"
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#pragma comment(lib,"winhttp.lib")

// Blocklist download and SteamID parsing.
namespace score_blocklist {
inline bool enabled=true;
inline constexpr wchar_t kHost[]=L"raw.githubusercontent.com";
inline constexpr wchar_t kPath[]=L"/Carlmundo/DXB2AE-Blocklist/refs/heads/main/blocklist.txt";
inline constexpr size_t kMaxBytes=1024*1024;
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::vector<uint64_t> ids;
inline LONG started=0,finished=0;
inline LONG generation=0;
inline void (*log)(const char*)=nullptr;

inline std::vector<uint64_t> Parse(std::string_view text) {
  if(text.substr(0,3)=="\xef\xbb\xbf")text.remove_prefix(3);
  std::vector<uint64_t> parsed;
  while(!text.empty()) {
    const auto end=text.find('\n');
    auto line=text.substr(0,end);
    if(end==std::string_view::npos)text={};else text.remove_prefix(end+1);
    const auto first=line.find_first_not_of(" \t\r");
    if(first==std::string_view::npos)continue;
    line=line.substr(first,line.find_last_not_of(" \t\r")-first+1);
    // Each line starts with a SteamID64. Descriptions, scores and any other
    // trailing text belong to that entry and must not become additional IDs.
    line=line.substr(0,line.find_first_not_of("0123456789"));
    if(line.empty())continue;
    uint64_t id=0;bool valid=true;
    for(const char c:line) {
      if(c<'0'||c>'9'||id>((std::numeric_limits<uint64_t>::max)()-static_cast<unsigned>(c-'0'))/10) {
        valid=false;break;
      }
      id=id*10+static_cast<unsigned>(c-'0');
    }
    if(valid&&id)parsed.push_back(id);
  }
  std::sort(parsed.begin(),parsed.end());
  parsed.erase(std::unique(parsed.begin(),parsed.end()),parsed.end());
  return parsed;
}
inline void Publish(std::vector<uint64_t> parsed) noexcept {
  AcquireSRWLockExclusive(&lock);ids.swap(parsed);InterlockedIncrement(&generation);ReleaseSRWLockExclusive(&lock);
}

struct InternetHandle {
  HINTERNET value;
  explicit InternetHandle(HINTERNET handle):value(handle){}
  ~InternetHandle(){if(value)WinHttpCloseHandle(value);}
  InternetHandle(const InternetHandle&)=delete;
  InternetHandle& operator=(const InternetHandle&)=delete;
};
inline bool Download(std::string& text) {
  InternetHandle session(WinHttpOpen(L"DX-Ball 2 AEx",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
    WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
  if(!session.value||!WinHttpSetTimeouts(session.value,5000,5000,5000,5000))return false;
  InternetHandle connection(WinHttpConnect(session.value,kHost,INTERNET_DEFAULT_HTTPS_PORT,0));
  if(!connection.value)return false;
  InternetHandle request(WinHttpOpenRequest(connection.value,L"GET",kPath,nullptr,
    WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
  if(!request.value)return false;
  // Never follow a redirect to a different host or downgrade to HTTP.
  DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if(!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))||
     !WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||
     !WinHttpReceiveResponse(request.value,nullptr))return false;
  DWORD status=0,size=sizeof(status);
  if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200)return false;
  text.clear();const auto deadline=GetTickCount64()+15000;
  for(;;) {
    char buffer[4096];DWORD count=0;
    if(GetTickCount64()>deadline||!WinHttpReadData(request.value,buffer,sizeof(buffer),&count))return false;
    if(!count)return true;
    if(count>kMaxBytes-text.size())return false;
    text.append(buffer,count);
  }
}
inline decltype(&Download) fetch=&Download;
inline DWORD WINAPI Load(void* context) noexcept {
  const auto update=reinterpret_cast<HANDLE>(context);
  if(update){WaitForSingleObject(update,INFINITE);CloseHandle(update);}
  try {
    std::string text;
    if(fetch(text)&&text.size()<=kMaxBytes) {
      auto parsed=Parse(text);const auto count=parsed.size();Publish(std::move(parsed));
      if(log){char message[128];sprintf_s(message,"Online score blocklist loaded: %zu SteamIDs.",count);log(message);}
    }else if(log)log("Online score blocklist unavailable; online scores left unfiltered.");
  }catch(...){if(log)log("Online score blocklist could not be loaded.");}
  InterlockedExchange(&finished,1);return 0;
}
// Called outside loader lock after the update checker has been launched. The
// worker owns its duplicate, so a later manual update cannot invalidate it.
inline void StartOnce(HANDLE update) noexcept {
  if(!enabled)return;
  if(InterlockedCompareExchange(&started,1,0)!=0)return;
  HANDLE duplicate=nullptr;
  if(update&&!DuplicateHandle(GetCurrentProcess(),update,GetCurrentProcess(),&duplicate,SYNCHRONIZE,FALSE,0)) {
    InterlockedExchange(&finished,1);return;
  }
  const auto thread=CreateThread(nullptr,0,&Load,duplicate,0,nullptr);
  if(thread)CloseHandle(thread);
  else {if(duplicate)CloseHandle(duplicate);InterlockedExchange(&finished,1);}
}
} // namespace score_blocklist

// Filtering, replacement batches and adjusted ranks.
namespace leaderboard_filter {
inline constexpr int kReplacementPageSize=15;
inline constexpr uint32_t kStats=0x43730,kSetCallback=0x43690,kAssignName=0x45d90;
inline void Verify(uint8_t* game) {
  const auto context=reinterpret_cast<uintptr_t>(game+0x1a4000);
  const uint8_t callback[]={0x55,0x8b,0xec,0x56,0x8b,0xf1};
  const uint8_t name[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
  if(game[kStats]!=0x68||std::memcmp(game+kStats+1,&context,4)||std::memcmp(game+kSetCallback,callback,sizeof(callback))||
      std::memcmp(game+kAssignName,name,sizeof(name)))throw std::runtime_error("Unexpected leaderboard paging helpers");
}
// Match the game's Steamworks ABI, including the padding before its UGC handle.
struct Entry {uint64_t user;int rank,score,details,padding;uint64_t ugc;};
static_assert(sizeof(Entry)==32);
struct Row {Entry entry{};int board=0;std::string name;};
struct Pending {
  uint8_t* owner=nullptr;
  uint64_t board=0;
  int* range=nullptr;
  std::array<int,3> expected{};
  int originalType=0,firstRank=INT_MAX,lastRank=0;
  bool exhausted=false,before=false,lookup=false,ranksReady=false;
  size_t nextUser=0;
  std::vector<uint64_t> blocked;
  std::vector<int> blockedRanks;
  std::vector<Row> rows;
};
inline std::unique_ptr<Pending> pending;
inline bool applied=false;
inline void Reset() {pending.reset();applied=false;}
inline std::vector<uint64_t> BlockedIDs() {
  if(!score_blocklist::enabled)return {};
  AcquireSRWLockShared(&score_blocklist::lock);
  try {
    auto ids=score_blocklist::ids;ReleaseSRWLockShared(&score_blocklist::lock);return ids;
  }catch(...){ReleaseSRWLockShared(&score_blocklist::lock);throw;}
}
inline bool HasBlocklist() noexcept {
  if(!score_blocklist::enabled)return false;
  AcquireSRWLockShared(&score_blocklist::lock);const bool any=!score_blocklist::ids.empty();
  ReleaseSRWLockShared(&score_blocklist::lock);return any;
}
inline void* Stats(uint8_t* game) {return reinterpret_cast<void* (__cdecl*)()>(game+kStats)();}
template<class Function> inline Function Method(void* object,size_t index) {
  return reinterpret_cast<Function>((*reinterpret_cast<void***>(object))[index]);
}
inline bool ReadEntry(void* stats,uint64_t entries,int index,Entry& entry,int& board) {
  return Method<bool (__thiscall*)(void*,uint64_t,int,void*,int*,int)>(stats,30)(stats,entries,index,&entry,&board,1);
}
inline void ReadRows(uint8_t* game,const uint8_t* result) {
  auto& p=*pending;
  const auto entries=*reinterpret_cast<const uint64_t*>(result+8);
  const int count=*reinterpret_cast<const int*>(result+16);
  if(count<0||(count&&!entries))throw std::runtime_error("Invalid leaderboard page");
  void* stats=Stats(game);
  const auto context=reinterpret_cast<void* (__cdecl*)(void*)>(*reinterpret_cast<void**>(game+0x15f400));
  void* friends=static_cast<void**>(context(game+0x1a4000))[2];
  const int previousFirst=p.firstRank,previousLast=p.lastRank;
  for(int i=0;i<count;++i) {
    Row row;
    if(!ReadEntry(stats,entries,i,row.entry,row.board)||row.entry.rank<=0)
      throw std::runtime_error("Cannot read leaderboard entry");
    p.firstRank=(std::min)(p.firstRank,row.entry.rank);p.lastRank=(std::max)(p.lastRank,row.entry.rank);
    if(std::binary_search(p.blocked.begin(),p.blocked.end(),row.entry.user)) {
      p.blockedRanks.push_back(row.entry.rank);continue;
    }
    if((!p.before&&p.rows.size()==15)||std::any_of(p.rows.begin(),p.rows.end(),[&](const Row& saved){return saved.entry.user==row.entry.user;}))continue;
    const char* name=Method<const char* (__thiscall*)(void*,uint64_t)>(friends,7)(friends,row.entry.user);
    row.name=name?name:"";
    Method<bool (__thiscall*)(void*,uint64_t,bool)>(friends,37)(friends,row.entry.user,true);
    p.rows.push_back(std::move(row));
  }
  // An upward refill can return more than the remaining gap. Keep the entries
  // closest to the existing personal-view window, rather than the start of the batch.
  if(p.before&&p.rows.size()>15) {
    std::sort(p.rows.begin(),p.rows.end(),[](const Row& a,const Row& b){return a.entry.rank<b.entry.rank;});
    p.rows.erase(p.rows.begin(),p.rows.end()-15);
  }
  // Steam returns the complete friends list regardless of the requested range.
  // For global pages, a short result or no rank progress means the end was reached.
  if(p.originalType==2)p.exhausted=true;
  else if(p.expected[0]==0) {
    const int requested=p.expected[2]-p.expected[1]+1;
    p.exhausted=count<requested||(p.before?p.firstRank>=previousFirst:p.lastRank<=previousLast);
  }else p.exhausted=count==0;
}
inline bool MoreRows(uint8_t* leaderboard) {
  auto& p=*pending;
  if(p.rows.size()==15||p.originalType==2)return false;
  if(p.exhausted) {
    if(p.originalType!=1||p.before||p.firstRank<=1||p.firstRank==INT_MAX)return false;
    p.before=true;p.exhausted=false;
  }
  if(p.before) {
    if(p.firstRank<=1)return false;
    p.range[1]=(std::max)(1,p.firstRank-kReplacementPageSize);p.range[2]=p.firstRank-1;
  }else {
    if(p.lastRank==INT_MAX)return false;
    p.range[1]=p.lastRank+1;p.range[2]=p.lastRank>INT_MAX-kReplacementPageSize?INT_MAX:p.lastRank+kReplacementPageSize;
  }
  p.range[0]=0;std::copy_n(p.range,3,p.expected.begin());
  *reinterpret_cast<int*>(leaderboard+0x34)=0;
  *reinterpret_cast<float*>(leaderboard+0x38)=0;
  return true;
}
inline bool LookupRanks(uint8_t* game,void* callback) {
  auto& p=*pending;
  if(p.ranksReady||p.rows.empty()||p.nextUser==p.blocked.size()){p.ranksReady=true;return false;}
  const auto count=(std::min)(size_t{100},p.blocked.size()-p.nextUser);
  void* stats=Stats(game);
  const auto call=Method<uint64_t (__thiscall*)(void*,uint64_t,const uint64_t*,int)>(stats,29)
    (stats,p.board,p.blocked.data()+p.nextUser,static_cast<int>(count));
  if(!call){p.ranksReady=true;return false;}
  p.nextUser+=count;p.lookup=true;
  *reinterpret_cast<int*>(p.owner+0x34)=3;*reinterpret_cast<float*>(p.owner+0x38)=0;
  // Reuse the native Steam call-result object, including its 64-bit call ID
  // matching and cancellation when another view replaces this request.
  reinterpret_cast<void (__thiscall*)(void*,uint64_t,void*,void*)>(game+kSetCallback)
    (p.owner+0xd0,call,p.owner,callback);
  return true;
}
inline void ReadRanks(uint8_t* game,const uint8_t* result) {
  auto& p=*pending;
  const int count=*reinterpret_cast<const int*>(result+16);
  const auto entries=*reinterpret_cast<const uint64_t*>(result+8);
  if(count<0||count>100||(count&&!entries))throw std::runtime_error("Invalid blocked rank lookup");
  void* stats=Stats(game);
  for(int i=0;i<count;++i) {
    Entry entry{};int board=0;
    if(!ReadEntry(stats,entries,i,entry,board))throw std::runtime_error("Cannot read blocked rank");
    if(entry.rank>0&&std::binary_search(p.blocked.begin(),p.blocked.end(),entry.user))p.blockedRanks.push_back(entry.rank);
  }
  p.lookup=false;
}
inline std::vector<Row> CaptureRows(const uint8_t* leaderboard) {
  const auto* first=*reinterpret_cast<uint8_t* const*>(leaderboard+0x28);
  const auto* last=*reinterpret_cast<uint8_t* const*>(leaderboard+0x2c);
  std::vector<Row> rows;
  for(auto* source=first;source!=last;source+=0x40) {
    Row row;std::memcpy(&row.entry,source,sizeof(row.entry));std::memcpy(&row.board,source+0x38,sizeof(row.board));
    // Read the verified native string layout; keep cache strings in AEx's own heap.
    const auto size=*reinterpret_cast<const uint32_t*>(source+0x30);
    const auto capacity=*reinterpret_cast<const uint32_t*>(source+0x34);
    const char* name=capacity>=16?*reinterpret_cast<const char* const*>(source+0x20):reinterpret_cast<const char*>(source+0x20);
    row.name.assign(name,size);rows.push_back(std::move(row));
  }
  return rows;
}
inline void StoreRows(uint8_t* game,uint8_t* leaderboard,const std::vector<Row>& rows) {
  reinterpret_cast<void (__thiscall*)(void*,size_t)>(game+0x46f90)(leaderboard+0x28,rows.size());
  auto* first=*reinterpret_cast<uint8_t**>(leaderboard+0x28);
  for(size_t i=0;i<rows.size();++i) {
    const auto& row=rows[i];auto* destination=first+i*0x40;
    std::memcpy(destination,&row.entry,sizeof(row.entry));std::memcpy(destination+0x38,&row.board,sizeof(row.board));
    reinterpret_cast<void* (__thiscall*)(void*,const char*,size_t)>(game+kAssignName)
      (destination+0x20,row.name.data(),row.name.size());
  }
}
inline void WriteRows(uint8_t* game,uint8_t* leaderboard,Pending& p) {
  std::sort(p.rows.begin(),p.rows.end(),[](const Row& a,const Row& b){return a.entry.rank<b.entry.rank;});
  std::sort(p.blockedRanks.begin(),p.blockedRanks.end());
  p.blockedRanks.erase(std::unique(p.blockedRanks.begin(),p.blockedRanks.end()),p.blockedRanks.end());
  for(size_t i=0;i<p.rows.size();++i) {
    auto& row=p.rows[i];
    row.entry.rank=p.originalType==0?static_cast<int>(i)+1:
      row.entry.rank-static_cast<int>(std::lower_bound(p.blockedRanks.begin(),p.blockedRanks.end(),row.entry.rank)-p.blockedRanks.begin());
  }
  StoreRows(game,leaderboard,p.rows);
}
inline void Complete(uint8_t* game,uint8_t* leaderboard,const uint8_t* result) {
  // Let the original callback perform its state/error/request cleanup once,
  // while keeping the assembled rows alive and deferring UI notification.
  std::array<uint8_t,24> emptyResult{};std::memcpy(emptyResult.data(),result,emptyResult.size());
  const int zero=0;std::memcpy(emptyResult.data()+16,&zero,sizeof(zero));
  std::array<uint8_t,12> rows;std::memcpy(rows.data(),leaderboard+0x28,rows.size());
  std::memset(leaderboard+0x28,0,rows.size());
  auto*& notify=*reinterpret_cast<void**>(leaderboard+0x8c);void* savedNotify=notify;notify=nullptr;
  reinterpret_cast<void (__thiscall*)(void*,const void*,bool)>(game+0x42420)(leaderboard,emptyResult.data(),false);
  std::memcpy(leaderboard+0x28,rows.data(),rows.size());notify=savedNotify;
  if(notify) {
    Method<void (__thiscall*)(void*,void*)>(notify,2)(notify,leaderboard+4);
    if(notify) {Method<void (__thiscall*)(void*,bool)>(notify,4)(notify,notify!=leaderboard+0x68);notify=nullptr;}
  }
}
inline bool Process(uint8_t* game,uint8_t* leaderboard,const uint8_t* result,void* callback) {
  auto* range=*reinterpret_cast<int**>(leaderboard+0x60);
  if(!range)return false;
  if(pending&&(pending->owner!=leaderboard||pending->range!=range||
      !std::equal(pending->expected.begin(),pending->expected.end(),range)))pending.reset();
  if(!pending) {
    auto blocked=BlockedIDs();if(blocked.empty())return false;
    pending=std::make_unique<Pending>();auto& p=*pending;
    p.owner=leaderboard;p.board=*reinterpret_cast<uint64_t*>(leaderboard+0x20);p.range=range;
    std::copy_n(range,3,p.expected.begin());p.originalType=range[0];p.ranksReady=p.originalType==0;p.blocked=std::move(blocked);
  }
  if(pending->lookup)ReadRanks(game,result);else ReadRows(game,result);
  if(MoreRows(leaderboard)||LookupRanks(game,callback))return true;
  auto complete=std::move(pending);WriteRows(game,leaderboard,*complete);applied=true;
  Complete(game,leaderboard,result);return true;
}
} // namespace leaderboard_filter

// Native score screen, view switching and session cache.
namespace online_leaderboards {
inline uint8_t* game=nullptr;
enum class View {AroundUser,Friends,Top};
enum class PersonalRank {Unknown,Present,Absent};
inline View view=View::AroundUser;
inline PersonalRank personalRank=PersonalRank::Unknown;
struct CachedView {bool ready=false,filtered=false;std::vector<leaderboard_filter::Row> rows;};
inline std::array<CachedView,3> cachedViews;
inline LONG cacheGeneration=0;
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
inline constexpr uint32_t kResizeRows=0x46f90;
inline constexpr uint32_t kSetDownloadState=0x436e0;
inline constexpr uint8_t kResizeRowsBytes[]={0x55,0x8b,0xec,0x6a,0xff,0x68};
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
inline void ClearCache() {
  for(auto& cached:cachedViews){cached.ready=false;cached.filtered=false;cached.rows.clear();}
  cacheGeneration=InterlockedCompareExchange(&score_blocklist::generation,0,0);
}
inline void SyncCache() {
  if(cacheGeneration!=InterlockedCompareExchange(&score_blocklist::generation,0,0)) {
    ClearCache();
    if(!leaderboard_filter::pending)leaderboard_filter::applied=false;
  }
}
inline void CacheCompletedView(uint8_t* leaderboard) noexcept {
  if(leaderboard!=viewing||*reinterpret_cast<int*>(game+kState)!=7||
      *reinterpret_cast<int*>(leaderboard+0x34)!=0||*reinterpret_cast<void**>(leaderboard+0x60)||
      cacheGeneration!=InterlockedCompareExchange(&score_blocklist::generation,0,0))return;
  auto& cached=cachedViews[static_cast<size_t>(view)];
  try {
    auto rows=leaderboard_filter::CaptureRows(leaderboard);
    cached.rows=std::move(rows);cached.filtered=leaderboard_filter::applied;cached.ready=true;
  }catch(...){cached.ready=false;cached.rows.clear();}
}
inline bool RestoreCachedView() {
  const auto& cached=cachedViews[static_cast<size_t>(view)];
  if(!cached.ready)return false;
  // Cancel the old download's registered call ID before showing cached rows.
  // A response already in flight must not overwrite the newly selected view.
  reinterpret_cast<void (__thiscall*)(void*,uint64_t,void*,void*)>(game+leaderboard_filter::kSetCallback)
    (viewing+0xd0,0,viewing,nullptr);
  if(*reinterpret_cast<void**>(viewing+0x60)) {
    auto*& notify=*reinterpret_cast<void**>(viewing+0x8c);void* savedNotify=notify;notify=nullptr;
    *reinterpret_cast<int*>(viewing+0x34)=3;
    reinterpret_cast<void (__thiscall*)(void*,const void*,bool)>(game+kNativeDownloadCallback)(viewing,nullptr,true);
    if(savedNotify)leaderboard_filter::Method<void (__thiscall*)(void*,bool)>(savedNotify,4)
      (savedNotify,savedNotify!=viewing+0x68);
  }
  leaderboard_filter::StoreRows(game,viewing,cached.rows);
  reinterpret_cast<void (__thiscall*)(void*,int)>(game+kSetDownloadState)(viewing,0);
  leaderboard_filter::applied=cached.filtered;
  return true;
}
inline void FilterRows(uint8_t* leaderboard) {
  if(!score_blocklist::enabled||!leaderboard)return;
  auto* first=*reinterpret_cast<uint8_t**>(leaderboard+0x28);
  auto* last=*reinterpret_cast<uint8_t**>(leaderboard+0x2c);
  if(first==last)return;
  size_t kept=0;
  AcquireSRWLockShared(&score_blocklist::lock);
  if(score_blocklist::ids.empty()){ReleaseSRWLockShared(&score_blocklist::lock);return;}
  // Native rows are 64 bytes: SteamID64 at +0, rank at +8, score at +12,
  // and an owning native string at +32. Swap complete rows so that the game
  // remains responsible for destroying every string with its own allocator.
  for(auto* row=first;row!=last;row+=0x40) {
    uint64_t id;std::memcpy(&id,row,sizeof(id));
    if(std::binary_search(score_blocklist::ids.begin(),score_blocklist::ids.end(),id))continue;
    auto* destination=first+kept*0x40;
    if(destination!=row) {
      uint8_t temporary[0x40];std::memcpy(temporary,destination,sizeof(temporary));
      std::memcpy(destination,row,sizeof(temporary));std::memcpy(row,temporary,sizeof(temporary));
    }
    ++kept;
  }
  ReleaseSRWLockShared(&score_blocklist::lock);
  if(first+kept*0x40!=last)
    reinterpret_cast<void (__thiscall*)(void*,size_t)>(game+kResizeRows)(leaderboard+0x28,kept);
}
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
  if(state!=7){view=View::AroundUser;personalRank=PersonalRank::Unknown;viewing=nullptr;tabPending=false;leaderboard_filter::Reset();ClearCache();}
  else {
    SyncCache();
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
      leaderboard_filter::Reset();
      if(!RestoreCachedView()) {
      const bool around=view==View::AroundUser;
      const int range[]={around?1:(view==View::Friends?2:0),around?-7:1,around?7:15};
      ViewName name;
      using Request=void (__thiscall*)(void*,const void*,const int*,ViewCallback);
      const auto request=reinterpret_cast<Request>((*reinterpret_cast<void***>(manager))[22]);
      request(manager,name.bytes,range,ViewCallback{});
      }
    }
    // A blocklist arriving after the original response needs one fresh request
    // so its missing rows and ranks can be assembled through the same pipeline.
    if(manager&&viewing&&!leaderboard_filter::applied&&!leaderboard_filter::pending&&
        !*reinterpret_cast<void**>(viewing+0x60)&&*reinterpret_cast<int*>(viewing+0x34)==0&&leaderboard_filter::HasBlocklist()) {
      const bool around=view==View::AroundUser;
      const int range[]={around?1:(view==View::Friends?2:0),around?-7:1,around?7:15};
      ViewName name;
      reinterpret_cast<void (__thiscall*)(void*,const void*,const int*,ViewCallback)>
        ((*reinterpret_cast<void***>(manager))[22])(manager,name.bytes,range,ViewCallback{});
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
  // The native find callback leaves the 64-bit handle zero if no table exists.
  if(viewing&&*reinterpret_cast<const uint64_t*>(viewing+0x20))
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
  const bool current=leaderboard==viewing&&*reinterpret_cast<int*>(game+kState)==7;
  const bool valid=!ioFailure&&result&&*reinterpret_cast<int*>(leaderboard+0x34)==3&&
    *reinterpret_cast<const uint64_t*>(result)==*reinterpret_cast<uint64_t*>(leaderboard+0x20);
  const bool rankLookup=leaderboard_filter::pending&&leaderboard_filter::pending->owner==leaderboard&&leaderboard_filter::pending->lookup;
  // Steam returns no entries around a player who has never posted a score.
  // Retain the pending request and UI callback, and let the next native tick
  // fetch the top 15 instead. Changing the range to global makes this a single
  // retry, including when the entire leaderboard is empty.
  if(valid&&!rankLookup&&range&&range[0]==1) {
    const int count=*reinterpret_cast<const int*>(result+0x10);
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
  if(current&&valid) {
    try {if(leaderboard_filter::Process(game,leaderboard,result,reinterpret_cast<void*>(&Downloaded))){CacheCompletedView(leaderboard);return;}}
    catch(...) {leaderboard_filter::pending.reset();leaderboard_filter::applied=true;}
  }else if(leaderboard_filter::pending&&leaderboard_filter::pending->owner==leaderboard)leaderboard_filter::pending.reset();
  // A failed replacement request must finish once, rather than requeueing on
  // every UI tick. The native callback retains its normal error presentation.
  if(current&&ioFailure)leaderboard_filter::applied=true;
  reinterpret_cast<void (__thiscall*)(void*,const void*,bool)>(game+kNativeDownloadCallback)(leaderboard,result,ioFailure);
  FilterRows(leaderboard);
  if(current&&valid)CacheCompletedView(leaderboard);
}
inline void Verify(uint8_t* base) {
  leaderboard_filter::Verify(base);
  const auto callback=reinterpret_cast<uintptr_t>(base+kNativeDownloadCallback);
  const auto state=reinterpret_cast<uintptr_t>(base+kState);
  const auto key=reinterpret_cast<uintptr_t>(base+kKey);
  const uint8_t character[]={0x8b,0x4d,0x10,0x88,0x0d};
  const uint8_t setState[]={0x55,0x8b,0xec,0x56,0x8b,0xf1};
  if(std::memcmp(base+kViewGate,kViewBytes,sizeof(kViewBytes))||
     std::memcmp(base+kPostGate,kPostBytes,sizeof(kPostBytes))||
     base[kDownloadCallback]!=0x68||std::memcmp(base+kDownloadCallback+1,&callback,4)||
     base[kReadState]!=0xa1||std::memcmp(base+kReadState+1,&state,4)||
     std::memcmp(base+kTitleCall,kTitleBytes,sizeof(kTitleBytes))||
     std::memcmp(base+kResizeRows,kResizeRowsBytes,sizeof(kResizeRowsBytes))||
     std::memcmp(base+kSetDownloadState,setState,sizeof(setState))||
     std::memcmp(base+kCharacter,character,sizeof(character))||std::memcmp(base+kCharacter+5,&key,4)||
     std::memcmp(base+kTooltip,kOldTooltip,sizeof(kOldTooltip)))
    throw std::runtime_error("Unexpected online leaderboard instructions");
}
inline void Install(uint8_t* base) {
  Verify(base);game=base;view=View::AroundUser;personalRank=PersonalRank::Unknown;viewing=nullptr;tabPending=false;leaderboard_filter::Reset();ClearCache();
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
