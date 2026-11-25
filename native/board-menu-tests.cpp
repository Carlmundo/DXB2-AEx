// Exercise board ordering, original menu branches, the production adapter and failure cleanup.
// File I/O uses isolated fixtures; no game loop, Steam or live data is used.
#include "mod.cpp"
#include <algorithm>
#include <array>
#include <filesystem>

static uint8_t* gBase=nullptr;
static void* gEntry=nullptr;
static FILE* gFile=nullptr;
static int gResult=0,gDialogs=0,gOpenCalls=0,gLengthOverride=0,gReadCalls=0,gSizeCalls=0,gDecodeCalls=0,gDtors=0;
static bool gShortRead=false,gNativeFailure=false;
static int gCursorCount=-3;
static int gLoaderDtors=0;
extern "C" EXCEPTION_DISPOSITION __cdecl __CxxFrameHandler3(...);
static std::wstring gMessage,gCaption;
static HWND gDialogWindow=nullptr;
static std::wstring gWindowCaption=L"DX-Ball 2 / test game caption";
static std::vector<std::pair<std::wstring,std::wstring>> gShown;
struct Node {uint32_t links[2];board_menu::Wide path;};
static Node gNode{};
static std::array<uint8_t,0x180> gFrame{};
static uint8_t* gFramePointer=gFrame.data()+0x40;
static void* gLoadEntry=nullptr;
static void* gFinishEntry=nullptr;
struct Narrow {union {char local[16];char* heap;};uint32_t length,capacity;};
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
namespace order_tests {
using namespace board_menu;
static void Put(Row& row,size_t offset,int value){std::memcpy(row.bytes.data()+offset,&value,4);}
static void String(Row& row,size_t offset,const char* text) {
  const auto n=std::strlen(text);Put(row,offset+0x10,static_cast<int>(n));
  Put(row,offset+0x14,n>=16?static_cast<int>(n):15);
  if(n>=16)std::memcpy(row.bytes.data()+offset,&text,sizeof(text));
  else std::memcpy(row.bytes.data()+offset,text,n+1);
}
static Row Make(int id,int source,const char* group,const char* title,int ordinal=0) {
  Row row{};Put(row,0x94,id);Put(row,0x9c,source);Put(row,0x30,ordinal);
  String(row,0x34,group);String(row,0x4c,title);return row;
}
static void Run(uint8_t* base) {
  // The original comparator uses only a CRT case-insensitive string compare.
  JumpTo(base,0x117081,reinterpret_cast<void*>(&_stricmp));
  using NativeLess=bool (__stdcall*)(const Row*,const Row*);
  auto native=reinterpret_cast<NativeLess>(base+0x94df0);
  std::vector<Row> rows={
    Make(1,2,"Board Pack 1","30 Years - Bonus Set",1),
    Make(2,0,"Rival Ball","Wendy"),
    Make(3,2,"Board Pack 1","30 Years of DX-Ball"),
    Make(4,0,"Board Pack 1","Board Pack 1"),
    Make(5,0,"20th Anniversary","20th Anniversary"),
    Make(6,1,"Custom","Unrelated community"),
    Make(7,2,"Workshop","Other Workshop"),
    Make(8,0,"Board Pack 1","Board Pack 1 (Wide)",1),
    Make(9,2,"Workshop","Board Pack 1"),
    Make(10,2,"Board Pack 1 Extra","Different pack"),
    Make(11,1,"Board Pack 1","Local Documents source"),
    Make(12,1,"Board Pack 2","Second pack local"),
    Make(13,2,"Board Pack 2","Second pack Workshop",1),
    Make(14,1,"Board Pack 3","Third pack local"),
    Make(15,2,"Board Pack 3","Third pack Workshop",1),
    Make(16,1,"Memorial Pack","Memorial local"),
    Make(17,2,"Memorial Pack","Memorial Workshop",1)};
  const auto previousPack=Make(18,2,"30 Years of DX-Ball","30 Years of DX-Ball");
  Require(!Selected(previousPack),"Previous whitelist Pack still selected");
  Require(Selected(rows[0])&&Selected(rows[2])&&Selected(rows[10]),"Whitelisted Workshop and local Documents boardsets must be selected");
  const auto unknownSource=Make(19,3,"Board Pack 1","Unknown source");
  Require(!Selected(unknownSource),"Unknown source category matched the whitelist");
  for(size_t i=0;i<rows.size();i++)if(i!=0&&i!=2&&i<10)
    Require(!Selected(rows[i]),"Title, partial Pack name, or official source matched the whitelist");
  for(size_t i=10;i<rows.size();i++)Require(Selected(rows[i]),"Whitelisted Pack was not selected");
  for(const auto* pack:{"Board Pack 1 Extra","Board Pack 2 Extra","Board Pack 3 Extra","Memorial Pack Extra","board pack 1","Board Pack 2 ","Memorial"})
    for(int source:{1,2})Require(!Selected(Make(0,source,pack,"Unrelated title")),"Non-exact Pack matched the whitelist");
  for(const auto& a:rows)for(const auto& b:rows) {
    if(!Selected(a)&&!Selected(b))Require(Less(a,b)==native(&a,&b),"Unrelated native order changed");
    auto projectedA=a,projectedB=b;
    for(auto* row:{&projectedA,&projectedB})if(Selected(*row)) {
      Put(*row,0x9c,0);
    }
    Require(Less(a,b)==native(&projectedA,&projectedB),"Promotion differs from projected native order");
  }
  const auto original=rows;
  auto target=base+0x983fc+*reinterpret_cast<int32_t*>(base+0x983f8);
  Require(target==reinterpret_cast<uint8_t*>(&Sort),"Hook has incorrect destination");
  reinterpret_cast<void (__cdecl*)(Row*,Row*,int,void*)>(target)(rows.data(),rows.data()+rows.size(),static_cast<int>(rows.size()),nullptr);
  const int expected[]={5,3,4,11,1,8,12,13,14,15,16,17,2,6,10,7,9};
  for(size_t i=0;i<rows.size();i++) {
    Require(Number(rows[i],0x94)==expected[i],"Unexpected mixed order");
    const auto id=Number(rows[i],0x94);
    Require(std::memcmp(&rows[i],&original[id-1],sizeof(Row))==0,"Native row data or source identity changed");
  }
  std::puts("PASS: 289 native comparator pairs; all four Packs include Workshop and Documents entries; exact Pack matching; native row bytes, source flags and within-pack order preserved; guarded hook installation.");
  // Exercise a longer list with inline and heap strings, repeats, and no target.
  std::vector<Row> large;for(int i=0;i<150;i++)large.push_back(Make(i,i%3,i%2?"Short":"Long group title for heap storage",i%2?"Inline":"A longer unrelated board title",150-i));
  auto expectedLarge=large;
  std::stable_sort(expectedLarge.begin(),expectedLarge.end(),[&](const Row& a,const Row& b){return native(&a,&b);});
  SortRows(large.data(),large.data()+large.size());
  Require(std::memcmp(large.data(),expectedLarge.data(),large.size()*sizeof(Row))==0,"No-match list differs from native comparator order");
  SortRows(nullptr,nullptr);Row single=original[2];SortRows(&single,&single+1);
  Require(std::memcmp(&single,&original[2],sizeof(Row))==0,"Single entry changed");
  std::puts("PASS: 150-row no-match regression, empty and single-entry lists.");
}
}

static LONG CALLBACK NativeFault(EXCEPTION_POINTERS* error) {
  if(error->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
    std::fprintf(stderr,"Native test fault at game RVA %08X; EBP %08lX\n",static_cast<unsigned>(error->ContextRecord->Eip-reinterpret_cast<uintptr_t>(gBase)),error->ContextRecord->Ebp);
    std::fflush(stderr);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
static HWND DialogWindow(){return gDialogWindow;}
static int WINAPI Dialog(HWND owner,const wchar_t* message,const wchar_t* caption,UINT flags) {
  Require(owner==gDialogWindow&&caption==gWindowCaption,"Error prompt does not use the actual game window/title");
  Require(flags==(MB_OK|MB_ICONERROR|MB_SETFOREGROUND|MB_TOPMOST),"Wrong error dialog flags");
  Require(gCursorCount==0,"Cursor hidden during error dialog");
  ++gDialogs;gMessage=message;gCaption=caption;gShown.emplace_back(message,caption);return IDOK;
}
static int WINAPI Cursor(BOOL show){return gCursorCount+=show?1:-1;}
static HCURSOR WINAPI SetDialogCursor(HCURSOR){return nullptr;}
static BOOL WINAPI GetDialogClip(RECT*){return FALSE;}
static void* __fastcall FileOpen(void* files,void*,const board_menu::Wide* path) {
  Require(files==gBase+0x1beee4,"Native file object changed");++gOpenCalls;
  if(gFile)std::fclose(gFile);
  gFile=nullptr;_wfopen_s(&gFile,path->text(),L"rb");
  *static_cast<FILE**>(files)=gFile;
  if(gFile)std::memcpy(static_cast<uint8_t*>(files)+0x1c,path,sizeof(*path));
  return gFile;
}
static int __fastcall Size(void* files,void*) {
  Require(files==gBase+0x1beee4&&gFile,"Size called on wrong stream");
  ++gSizeCalls;
  if(gLengthOverride)return gLengthOverride;
  const auto at=std::ftell(gFile);std::fseek(gFile,0,SEEK_END);const auto size=std::ftell(gFile);std::fseek(gFile,at,SEEK_SET);return size;
}
static int __fastcall Read(void* files,void*,void* bytes,int size,int count) {
  Require(files==gBase+0x1beee4&&gFile&&count==1,"Read called on wrong stream");++gReadCalls;
  return static_cast<int>(std::fread(bytes,size,gShortRead?0:count,gFile));
}
static int __fastcall NativeLoad(void*,void*,void* files,int flags,void* script) {
  Require(flags==0&&script==&gNode,"Native loader arguments changed");
  if(gNativeFailure)throw 7; // Native failures need not derive from std::exception.
  const int length=Size(files,nullptr);
  if(length<=4||length>16*1024*1024)throw 7;
  dxb::Bytes bytes(static_cast<size_t>(length));board_menu::Read(files,nullptr,bytes.data(),length,1);
  void* output=nullptr;int outputLength=0;++gDecodeCalls;
  Require(DecodeBoard(bytes.data(),length,&output,&outputLength)==1,"Production adapter failed to allocate output");
  const bool invalid=outputLength==4&&std::memcmp(output,"FAIL",4)==0;std::free(output);
  if(invalid)throw 7;
  return 1;
}
static void __fastcall StringDtor(void* text,void*) {
  if(text==gFramePointer+0xa0) {
    Require(gFramePointer[-4]==0,"Wrong cleanup exception state");++gDtors;
  }else {
    auto* value=static_cast<Narrow*>(text);if(value->capacity>=16)std::free(value->heap);++gLoaderDtors;
  }
}
static void __fastcall ResizeString(Narrow* text,void*,uint32_t capacity,uint32_t length) {
  text->heap=static_cast<char*>(std::malloc(capacity+1));Require(text->heap!=nullptr,"Cannot resize native string");
  text->capacity=capacity;text->length=length;text->heap[length]=0;
}
static __declspec(naked) void ResetBoardset(){__asm {ret}}
static __declspec(naked) void Accepted(){__asm {mov eax,1} __asm {ret}}
static __declspec(naked) void Skipped(){__asm {xor eax,eax} __asm {ret}}
static __declspec(naked) void Execute() {
  __asm {
    push ebp
    push ebx
    push esi
    push edi
    lea esi,gNode
    call dword ptr [gEntry]
    mov gResult,eax
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
  }
}
static __declspec(naked) void Cleanup() {
  __asm {
    push ebp
    push ebx
    push esi
    push edi
    mov ebp,gFramePointer
    lea eax,gNode
    push eax
    push 0
    mov eax,gBase
    add eax,1beee4h
    push eax
    mov ecx,1234h
    mov edx,7fffffffh
    call dword ptr [gLoadEntry]
    call dword ptr [gEntry]
    mov gResult,eax
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
  }
}
static void Write(const std::wstring& path,const dxb::Bytes& bytes) {
  if(gFile){std::fclose(gFile);gFile=nullptr;}
  FILE* file=nullptr;_wfopen_s(&file,path.c_str(),L"wb");Require(file!=nullptr,"Cannot create fixture");
  const auto written=std::fwrite(bytes.data(),1,bytes.size(),file);std::fclose(file);
  Require(written==bytes.size(),"Cannot write fixture");
}
static dxb::Bytes Modern(uint32_t version) {
  dxb::Bytes out={'B','D','S','T'};dxb::Put(out,version);dxb::Put(out,1);
  if(version>=5)dxb::Put(out,20);
  if(version>=6){dxb::String(out,"");dxb::String(out,"");dxb::Put(out,0);}
  dxb::String(out,"bricks.img");if(version>=7)dxb::Put(out,0);dxb::String(out,"background.img");
  if(version>=2){dxb::String(out,"Title");dxb::String(out,"Author");dxb::String(out,"");}
  if(version>=6)dxb::String(out,"");
  if(version>=7)dxb::Put(out,0);
  if(version>=8){dxb::String(out,"");dxb::String(out,"");}
  dxb::Put(out,0);
  if(version>=9)dxb::String(out,"");
  if(version>=8){dxb::String(out,"");dxb::String(out,"");dxb::Put(out,1);}
  out.insert(out.end(),(version>=5?520:400)*4,0);
  if(version>=6)dxb::Put(out,dxb::Checksum(out.data(),out.size()));return out;
}
static dxb::Bytes Rival() {
  dxb::Bytes out={'B','D','S','T'};for(uint32_t n:{7u,0u,0u,1u})dxb::Put(out,n);
  for(const auto* s:{"bricks.img","background.img","Title","Author",""})dxb::String(out,std::string(s)+'\0');
  out.insert(out.end(),3220,0);return out;
}
static dxb::Bytes Encode(dxb::Bytes out) {
  for(int pass=0;pass<7;pass++) {
    for(size_t i=0;i<out.size();i++)out[i]^=static_cast<uint8_t>(0x70+i%7);
    for(int i=0;i<7;i++)out.push_back(static_cast<uint8_t>(0x70+i));
    out.push_back(7);
  }
  dxb::Bytes file;dxb::Put(file,static_cast<uint32_t>(out.size()+4)^0xabbafad5);file.insert(file.end(),out.begin(),out.end());return file;
}
static void CompleteScan() {
  using Sort=void (__cdecl*)(board_menu::Row*,board_menu::Row*,int,void*);
  reinterpret_cast<Sort>(gFinishEntry)(nullptr,nullptr,0,nullptr);
}
static void Check(const std::wstring& path,int source,bool valid,const wchar_t* reason=nullptr,bool finishScan=true) {
  gNode.path={};gNode.path.length=static_cast<uint32_t>(path.size());
  if(path.size()<8){gNode.path.capacity=7;std::memcpy(gNode.path.local,path.c_str(),(path.size()+1)*sizeof(wchar_t));}
  else {gNode.path.capacity=gNode.path.length;gNode.path.heap=path.c_str();}
  const uint32_t entries[]={0x97f02,0x980a0,0x982a1};gEntry=gBase+entries[source];
  const auto dialogs=gDialogs,opens=gOpenCalls,reads=gReadCalls,sizes=gSizeCalls,decodes=gDecodeCalls;Execute();
  Require(gOpenCalls==opens+1,"Native open not called exactly once");
  Require(gReadCalls==reads&&gSizeCalls==sizes&&gDecodeCalls==decodes,"Open performed extra I/O or validation");
  if(gResult) {
    std::memcpy(gFramePointer-0x24,&source,sizeof(source));
    const auto dtors=gDtors;gFramePointer[-4]=1;gEntry=gBase+board_menu::kAfterLoad;Cleanup();
    Require(gDtors==dtors+1,"Temporary string was not destroyed exactly once");
    Require(gReadCalls-reads<=1&&gSizeCalls-sizes<=1&&gDecodeCalls-decodes<=1,"Board read or validated more than once");
    if(valid)Require(gReadCalls==reads+1&&gDecodeCalls==decodes+1,"Valid board was not read and decoded exactly once");
  }
  Require(gResult==(valid?1:0),"Original menu cleanup did not accept/skip correctly");
  Require(gDialogs==dialogs,"Error displayed before the complete scan finished");
  Require(gCursorCount==-3,"Dialog did not restore cursor visibility");
  Require(board_menu::current==nullptr,"Loader context leaked after success or failure");
  if(!finishScan)return;
  CompleteScan();
  Require(gDialogs==dialogs+(valid?0:1),"Missing or duplicate error dialog");
  Require(gCursorCount==-3,"Grouped dialog did not restore cursor visibility");
  if(!valid) {
    Require(gMessage.find(path)!=std::wstring::npos,"Error does not identify the exact filename");
    Require(gMessage.find(L"This file will be skipped.")!=std::wstring::npos,"Missing recovery explanation");
    Require(gCaption==gWindowCaption,"Error title differs from the game window");
    Require(gMessage.find(L"Unknown envelope")==std::wstring::npos,"Technical envelope diagnostic is still displayed");
    const wchar_t* advice[]={L"\"Verify integrity of game files\" in Steam to fix the problem.",
      L"Consider removing the file.",L"Consider unsubscribing or re-subscribing to fix the problem."};
    Require(gMessage.find(advice[source])!=std::wstring::npos,"Advice does not match the native menu source");
    if(reason)Require(gMessage.find(reason)==std::wstring::npos,"Diagnostic reason is still displayed");
    Require(gMessage.find(L"Could not read board file:\n"+path+L"\n\nThis file will be skipped.")==0,"Unexpected spacing around the filename");
  }
}
static void CheckAdvice() {
  using board_menu::Source;
  struct Case {Source source;const wchar_t* advice;};
  const Case cases[]={
    {Source::Installed,L"\"Verify integrity of game files\" in Steam to fix the problem."},
    {Source::Documents,L"Consider removing the file."},
    {Source::Workshop,L"Consider unsubscribing or re-subscribing to fix the problem."},
    {static_cast<Source>(99),L""}
  };
  // The same arbitrary Unicode path must receive different advice solely
  // according to the source supplied by the game, without folder-name guesses.
  const wchar_t* filename=L"Z:\\arbitrary-location\\\u65e5\u672c.bdz";
  board_menu::Group group;group.files.push_back({filename});
  for(const auto& item:cases) {
    const auto message=board_menu::Message(group,item.source);
    Require(message.find(filename)!=std::wstring::npos&&message.find(L"Unknown envelope")==std::wstring::npos,"Filename missing or unwanted diagnostic displayed");
    if(*item.advice)Require(message.size()>=std::wcslen(item.advice)&&message.compare(message.size()-std::wcslen(item.advice),std::wcslen(item.advice),item.advice)==0,"Advice not appended verbatim");
    else Require(message.find(L"Consider ")==std::wstring::npos&&message.find(L"Verify integrity")==std::wstring::npos,"Unrelated path received inappropriate advice");
  }
  std::puts("PASS: advice follows native install/Documents/Workshop categories for an identical arbitrary Unicode filename; unknown sources receive no category-specific advice.");
}
static void CheckGroups(const std::filesystem::path& root) {
  gWindowCaption=L"DX-Ball 2 / \u65e5\u672c / "+std::wstring(300,L'x');
  Require(SetWindowTextW(gDialogWindow,gWindowCaption.c_str())!=FALSE,"Cannot change fixture title");
  const std::wstring files[]={(root/L"first.bdz").wstring(),(root/L"second.bdz").wstring(),(root/L"third.bdz").wstring()};
  for(const auto& path:files)Write(path,dxb::Bytes(8,0));
  const int before=gDialogs;const auto shown=gShown.size();
  for(int source=0;source<3;source++)for(const auto& path:files)Check(path,source,false,nullptr,false);
  Check(files[0],1,false,nullptr,false); // Duplicate discovery must not repeat a filename.
  Require(gDialogs==before,"Batch emitted a dialog while enumerating files");
  CompleteScan();
  Require(gDialogs==before+3&&gShown.size()==shown+3,"More than one message per source or a missing source group");
  const wchar_t* advice[]={L"\"Verify integrity of game files\" in Steam to fix the problem.",
    L"Consider removing the files.",L"Consider unsubscribing or re-subscribing to fix the problem."};
  for(size_t source=0;source<3;source++) {
    const auto& message=gShown[shown+source].first;
    Require(gShown[shown+source].second==gWindowCaption,"Grouped error title differs from the current game window");
    Require(message.find(L"Could not read board files:")==0&&message.find(L"These files will be skipped.")!=std::wstring::npos,"Group message not plural");
    Require(message.find(advice[source])!=std::wstring::npos&&message.find(L"problems")==std::wstring::npos,"Unexpected plural recovery advice");
    Require(message.find(L"Could not read board files:\n"+files[0]+L"\n"+files[1]+L"\n"+files[2]+L"\n\nThese files will be skipped.")==0,"Unexpected blank lines around grouped filenames");
    for(const auto& path:files) {
      const auto at=message.find(path);Require(at!=std::wstring::npos,"Grouped message omitted a filename");
      Require(message.find(path,at+path.size())==std::wstring::npos,"Grouped message repeated a filename");
    }
  }
  Require(gCursorCount==-3,"Batch did not restore cursor visibility");
  CompleteScan();Require(gDialogs==before+3,"Completed or clean scan repeated stale errors");
  gWindowCaption=L"DX-Ball 2: 20th Anniversary Edition";
  Require(SetWindowTextW(gDialogWindow,gWindowCaption.c_str())!=FALSE,"Cannot restore fixture title");
  Check(files[0],1,false);
  Require(gDialogs==before+4&&gMessage.find(files[1])==std::wstring::npos&&gMessage.find(files[2])==std::wstring::npos,"Next scan retained previous filenames");
  std::puts("PASS: ten failed discoveries produce exactly three grouped dialogs with all filenames, plural text and advice; actual window owner and changing Unicode titles, duplicates, empty scans, cursor restoration and singular next-scan reset.");
}
int wmain(int argc,wchar_t** argv) {
  try {
    Require((argc==3||argc==4)&&VerifiedGame(argv[1]),"Usage: board-menu-tests.exe verified-game.exe new-fixture-directory [corrupt-board-file]");
    CheckAdvice();
    const auto root=std::filesystem::absolute(argv[2]);Require(std::filesystem::create_directory(root),"Use a new fixture directory");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1,&NativeFault);
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));Require(gBase!=nullptr,"Cannot map game");
    board_menu::Verify(gBase);gGame=gBase;
    ApplyGridPatches(gBase);
    std::array<uint8_t,5> nativeLoader{};std::memcpy(nativeLoader.data(),gBase+board_menu::kNativeLoad,5);
    constexpr std::array<uint32_t,7> hooks={board_menu::kOpenCalls[0],board_menu::kOpenCalls[1],board_menu::kOpenCalls[2],board_menu::kLoadCall,board_menu::kReadCall,board_menu::kAfterLoad,board_menu::kSortCall};
    std::array<std::array<uint8_t,10>,hooks.size()> original{};
    for(size_t i=0;i<hooks.size();i++)std::memcpy(original[i].data(),gBase+hooks[i],10);
    for(size_t i=0;i<hooks.size();i++)for(int byte:{0,1}) {
      auto wrong=original[i];wrong[byte]^=1;WriteMemory(gBase+hooks[i],wrong.data(),hooks[i]==board_menu::kAfterLoad?10:5);
      bool rejected=false;try{board_menu::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Unexpected board-menu hook accepted");
      for(size_t j=0;j<hooks.size();j++)Require(std::memcmp(gBase+hooks[j],j==i?wrong.data():original[j].data(),hooks[j]==board_menu::kAfterLoad?10:5)==0,"Rejected installation partially patched game");
      WriteMemory(gBase+hooks[i],original[i].data(),hooks[i]==board_menu::kAfterLoad?10:5);
    }
    std::array<uint8_t,9> sourceRead{};std::memcpy(sourceRead.data(),gBase+0x94efa,sourceRead.size());
    for(int byte:{0,8}) {
      auto wrong=sourceRead;wrong[byte]^=1;WriteMemory(gBase+0x94efa,wrong.data(),wrong.size());
      bool rejected=false;try{board_menu::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Unexpected source argument layout accepted");
      for(size_t i=0;i<hooks.size();i++)Require(std::memcmp(gBase+hooks[i],original[i].data(),hooks[i]==board_menu::kAfterLoad?10:5)==0,"Source mismatch partially patched game");
      WriteMemory(gBase+0x94efa,sourceRead.data(),sourceRead.size());
    }
    board_menu::Install(gBase);board_menu::messageBox=&Dialog;
    gDialogWindow=CreateWindowExW(0,L"STATIC",gWindowCaption.c_str(),WS_OVERLAPPED,
      0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(gDialogWindow!=nullptr,"Cannot create hidden caption fixture");
    aex_update::gFindGameWindow=&DialogWindow;
    gFinishEntry=gBase+board_menu::kSortCall+5+*reinterpret_cast<int32_t*>(gBase+board_menu::kSortCall+1);
    Require(gFinishEntry==reinterpret_cast<void*>(&board_menu::Sort),"Native scan completion hook missing");
    aex_update::gShowCursor=&Cursor;aex_update::gSetCursor=&SetDialogCursor;aex_update::gGetClipCursor=&GetDialogClip;
    for(size_t i=0;i<3;i++) {
      const auto rva=board_menu::kOpenCalls[i];
      Require(gBase+rva+5+*reinterpret_cast<int32_t*>(gBase+rva+1)==board_menu::OpenTarget(i),"Wrong hook destination");
    }
    gLoadEntry=gBase+board_menu::kLoadCall+5+*reinterpret_cast<int32_t*>(gBase+board_menu::kLoadCall+1);
    Require(gLoadEntry==reinterpret_cast<void*>(&board_menu::LoadFromMenu),"Source-passing hook missing");
    order_tests::Run(gBase);
    JumpTo(gBase,board_menu::kNativeOpen,reinterpret_cast<void*>(&FileOpen));
    JumpTo(gBase,0xb1810,reinterpret_cast<void*>(&Size));JumpTo(gBase,0xb1500,reinterpret_cast<void*>(&Read));
    JumpTo(gBase,board_menu::kNativeLoad,reinterpret_cast<void*>(&NativeLoad));
    JumpTo(gBase,0x111f9a,reinterpret_cast<void*>(&std::malloc));
    JumpTo(gBase,0x39390,reinterpret_cast<void*>(&StringDtor));
    JumpTo(gBase,0x94f54,reinterpret_cast<void*>(&Accepted));JumpTo(gBase,0x95010,reinterpret_cast<void*>(&Skipped));
    for(const auto rva:{0x97f18u,0x980b6u,0x982b7u})JumpTo(gBase,rva,reinterpret_cast<void*>(&Accepted));
    for(const auto rva:{0x97fd3u,0x9816eu,0x98391u})JumpTo(gBase,rva,reinterpret_cast<void*>(&Skipped));
    const auto fixture=(root/L"board-\u65e5\u672c-\u00e9.bdz").wstring();
    const auto good=Modern(9);
    for(int source=0;source<3;source++) {
      Check((root/L"missing.bdz").wstring(),source,false,L"could not be opened");
      for(const auto& bytes:std::array<dxb::Bytes,7>{dxb::Bytes{},dxb::Bytes{'B','D','S','T'},dxb::Bytes(8,0),
          dxb::Bytes(good.begin(),good.begin()+30),dxb::Bytes(good.begin(),good.end()-8),dxb::Bytes{'B','D','S','T',10,0,0,0},Encode(dxb::Bytes(20,0))}) {
        Write(fixture,bytes);Check(fixture,source,false);
      }
      auto checksum=good;checksum.back()^=1;Write(fixture,checksum);Check(fixture,source,false,L"checksum mismatch");
      auto extra=Modern(5);extra.push_back(0);Write(fixture,extra);Check(fixture,source,false,L"trailing board data");
      for(const auto& bytes:{good,Encode(good),Modern(1),Modern(5),Rival(),Encode(Rival())}) {
        Write(fixture,bytes);Check(fixture,source,true);
      }
      Write(fixture,good);gShortRead=true;Check(fixture,source,false,L"could not be read completely");gShortRead=false;
      gNativeFailure=true;Check(fixture,source,false,L"corrupt or could not be read");gNativeFailure=false;
      for(int size:{-1,16*1024*1024+1}){gLengthOverride=size;Check(fixture,source,false);}gLengthOverride=0;
      Check(fixture,source,true);
    }
    // Now exercise the original loader and its real std::exception objects.
    // The EXE's uninitialized static CRT is replaced at its exception runtime
    // and allocation boundaries; its loader, throw sites and unwind map run.
    WriteMemory(gBase+board_menu::kNativeLoad,nativeLoader.data(),nativeLoader.size());
    JumpTo(gBase,0x572d0,reinterpret_cast<void*>(&ResetBoardset));
    JumpTo(gBase,0x3acd0,reinterpret_cast<void*>(&ResizeString));
    JumpTo(gBase,0x111f95,reinterpret_cast<void*>(&std::free));
    JumpTo(gBase,0xe5fe4,reinterpret_cast<void*>(&_CxxThrowException));
    JumpTo(gBase,0xe5a0b,reinterpret_cast<void*>(&__CxxFrameHandler3));
    const auto copyString=static_cast<errno_t (__cdecl*)(char*,size_t,const char*)>(&strcpy_s);
    JumpTo(gBase,0x126ea8,reinterpret_cast<void*>(copyString));
    ReplaceCall(0x55e56,reinterpret_cast<void*>(&DecodeBoard),0xab8c0);
    const uint8_t nops[2]={0x90,0x90};WriteMemory(gBase+0x55e44,nops,2);
    for(const auto& bytes:{dxb::Bytes{},dxb::Bytes{'B','D','S','T'},dxb::Bytes(8,0),
        dxb::Bytes(good.begin(),good.begin()+30),dxb::Bytes(good.begin(),good.end()-8)}) {
      Write(fixture,bytes);const int dtors=gLoaderDtors;Check(fixture,0,false);
      Require(gLoaderDtors==dtors+1,"Original loader did not unwind its temporary string");
    }
    Write(fixture,good);gShortRead=true;Check(fixture,0,false,L"could not be read completely");gShortRead=false;
    if(argc==4)Check(std::filesystem::absolute(argv[3]).wstring(),1,false,L"Unknown envelope");
    std::puts("PASS: original Anniversary loader throws, unwinds and skips corrupt/short-read files through the production exception boundary.");
    JumpTo(gBase,board_menu::kNativeLoad,reinterpret_cast<void*>(&NativeLoad));
    CheckGroups(root);
    // Also exercise the game's inline wstring representation.
    std::filesystem::current_path(root);
    Write(L"tiny.bd",good);Check(L"tiny.bd",0,true);std::fclose(gFile);gFile=nullptr;std::filesystem::remove(L"tiny.bd");
    std::printf("PASS: %d Play-menu cases across install, Documents and Workshop; production adapter diagnostics, native/non-std exceptions, original cleanup/skip instructions, corrupt/valid files, Unicode paths, cursor recovery and guarded hooks. Valid boards read and decoded exactly once; no preflight I/O.\n",gOpenCalls);
    DestroyWindow(gDialogWindow);return 0;
  }catch(const std::exception& error){if(gFile)std::fclose(gFile);std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
