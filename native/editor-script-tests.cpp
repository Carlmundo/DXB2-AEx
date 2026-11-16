// Native regression tests in a separate process: real Rich Edit controls,
// original editor string/format/save/load routines, and workspace fixtures.
#include "editor-scripts.hpp"
#include <cstdio>
#include <string>
#include <filesystem>
#include <array>
#include <fstream>

static uint8_t* gImage=nullptr;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
    std::fprintf(stderr,"Native editor fault at RVA %08lX; access %lu address %p\n",
      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gImage)),
      static_cast<unsigned long>(fault->ExceptionRecord->ExceptionInformation[0]),reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]));
    auto* frame=reinterpret_cast<uint32_t*>(fault->ContextRecord->Ebp);
    for(int i=0;i<7&&frame;i++) {
      std::fprintf(stderr,"Frame %p return RVA %08lX arg1 %08lX\n",frame,static_cast<unsigned long>(frame[1]-reinterpret_cast<uintptr_t>(gImage)),static_cast<unsigned long>(frame[2]));
      frame=reinterpret_cast<uint32_t*>(frame[0]);
    }
    std::fflush(stderr);TerminateProcess(GetCurrentProcess(),3);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
static void ResolveImports(uint8_t* image,const std::filesystem::path& executable) {
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
  auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
  for(;descriptor->Name;descriptor++) {
    const auto* name=reinterpret_cast<const char*>(image+descriptor->Name);
    HMODULE dll=nullptr;
    if(_stricmp(name,"steam_api.dll")==0)dll=LoadLibraryW((executable.parent_path().parent_path()/L"steam_api.dll").c_str());
    else dll=LoadLibraryA(name);
    if(!dll){std::fprintf(stderr,"Dependency %s: Windows error %lu\n",name,GetLastError());throw std::runtime_error("Cannot resolve editor dependency");}
    const auto* source=reinterpret_cast<const IMAGE_THUNK_DATA*>(image+descriptor->OriginalFirstThunk);
    auto* target=reinterpret_cast<IMAGE_THUNK_DATA*>(image+descriptor->FirstThunk);
    for(;source->u1.AddressOfData;source++,target++) {
      const auto nameOrOrdinal=IMAGE_SNAP_BY_ORDINAL(source->u1.Ordinal)?MAKEINTRESOURCEA(IMAGE_ORDINAL(source->u1.Ordinal)):
        reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+source->u1.AddressOfData)->Name);
      const auto address=GetProcAddress(dll,nameOrOrdinal);Require(address!=nullptr,"Missing editor import");
      editor_scripts::Write(&target->u1.Function,&address,sizeof(address));
    }
  }
}
static void __cdecl RefreshTitle(){}
static int __cdecl NoExitRegistration(void (*)()){return 0;}
static INT_PTR CALLBACK EmptyDialog(HWND,UINT,WPARAM,LPARAM){return FALSE;}
static LRESULT CALLBACK Parent(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
  if(message==WM_COMMAND&&LOWORD(wparam)==0x40a)
    return reinterpret_cast<DLGPROC>(gImage+0x32f20)(window,message,wparam,lparam);
  return DefWindowProcA(window,message,wparam,lparam);
}
static void Same(int kind,const std::string& expected) {
  const auto script=editor_scripts::Script(kind);
  if(editor_scripts::Length(script)!=expected.size()) {
    std::fprintf(stderr,"Script %d: expected %zu bytes, actual %zu\n",kind,expected.size(),editor_scripts::Length(script));
    throw std::runtime_error("Script length changed");
  }
  Require(std::memcmp(editor_scripts::Text(script),expected.data(),expected.size())==0,"Script bytes changed");
}
static std::string Sample(size_t length,int kind) {
  const std::string line="-- script "+std::to_string(kind)+" abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
  std::string result;
  while(result.size()<length)result+=line;
  result.resize(length);
  if(length==editor_scripts::kMaxTextBytes){result.assign(length,'a');result.replace(0,2,"--");}
  if(length>16)result.replace(length-16,16,"-- END SENTINEL!");
  return result;
}
static bool LoadBoard(const std::wstring& path,bool editing=false) {
  // FILE objects must come from the editor's CRT, not this harness's /MT CRT.
  const auto open=*reinterpret_cast<void* (__cdecl**)(const wchar_t*,const wchar_t*)>(gImage+0x45328);
  const auto close=*reinterpret_cast<int (__cdecl**)(void*)>(gImage+0x4532c);
  struct File {void* handle;uint8_t reserved[24]={};std::wstring name;};
  File file{open(path.c_str(),L"rb"),{},path};Require(file.handle!=nullptr,"Cannot open board fixture");
  using Load=int (__thiscall*)(void*,void*,bool,const std::string*);
  const std::string password;
  int result=0;
  try {result=reinterpret_cast<Load>(gImage+0x2c000)(gImage+0x555f0,&file,editing,&password);}
  catch(...) {close(file.handle);throw;}
  close(file.handle);return result!=0;
}
static std::vector<char> FileBytes(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);Require(static_cast<bool>(input),"Cannot read bundled board fixture");
  return std::vector<char>(std::istreambuf_iterator<char>(input),{});
}
static void BundledBoardTests(const std::filesystem::path& fixture,const std::filesystem::path& outputDirectory) {
  const auto before=FileBytes(fixture);
  const auto path=std::filesystem::absolute(fixture).wstring();
  Require(gImage[editor_scripts::kEditBundledBoardsRva]==0xeb,"Bundled-board edit branch is not installed");
  // Reproduce the stock editor's failure in this isolated mapping, then apply
  // precisely the Editor2 byte again. No source boardsets or EXEs are modified.
  const uint8_t lockedBranch=0x74,editBranch=0xeb;
  editor_scripts::Write(gImage+editor_scripts::kEditBundledBoardsRva,&lockedBranch,1);
  bool locked=false;
  try {LoadBoard(path,true);}
  catch(const std::exception& error) {
    if(std::strstr(error.what(),"Boardset file is locked.")==nullptr)throw;
    locked=true;
  }
  Require(locked,"Bundled fixture did not exercise the original locked-board rejection");
  editor_scripts::Write(gImage+editor_scripts::kEditBundledBoardsRva,&editBranch,1);
  Require(LoadBoard(path,true),"Bundled boardset still cannot be opened for editing");
  constexpr char edit[]="-- AEx bundled-board edit regression";
  reinterpret_cast<void* (__thiscall*)(void*,const char*,size_t)>(gImage+0xe7b0)(editor_scripts::Script(2),edit,sizeof(edit)-1);
  std::filesystem::create_directories(outputDirectory);
  const auto output=(std::filesystem::absolute(outputDirectory)/L"bundled-edit-roundtrip.bdz").wstring();
  Require(!std::filesystem::exists(output),"Bundled board output fixture already exists");
  using Save=int (__thiscall*)(void*,const std::wstring*,bool);
  Require(reinterpret_cast<Save>(gImage+0x2d0d0)(gImage+0x555f0,&output,false)!=0,"Bundled board edit could not be saved");
  Require(LoadBoard(output,true),"Edited bundled boardset could not be reopened");
  Same(2,edit);
  Require(FileBytes(fixture)==before,"Original bundled boardset was modified");
  std::puts("PASS: stock editor rejects locked boardset; DLL opens, edits, saves and reopens it; source unchanged");std::fflush(stdout);
}
static POINT CharacterPosition(HWND control,LONG character) {
  POINT point{};SendMessageA(control,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),character);return point;
}
static void ViewportTests(HWND parent,HWND edit) {
  auto* dialogs=reinterpret_cast<HWND*>(gImage+0x51e3c);dialogs[0]=parent;
  std::string script;
  for(int line=0;line<8000;line++)script+="-- scroll position regression\r\n";
  script+="-- end";
  Require(SetWindowTextA(edit,script.c_str())!=0,"Cannot create viewport fixture");
  SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
  for(const int line:{3600,3640,3641,4000,7000}) {
    const auto character=static_cast<LONG>(SendMessageA(edit,EM_LINEINDEX,line,0)+3);
    CHARRANGE caret{character,character};SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&caret));
    SendMessageA(edit,EM_LINESCROLL,0,line-5-SendMessageA(edit,EM_GETFIRSTVISIBLELINE,0,0));
    const auto first=SendMessageA(edit,EM_GETFIRSTVISIBLELINE,0,0);
    const auto before=CharacterPosition(edit,character);
    RECT client{};GetClientRect(edit,&client);
    Require(before.y>=0&&before.y<client.bottom,"Viewport test caret was not initially visible");
    SendMessageA(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>("x"));
    CHARRANGE after{};SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    const auto firstAfter=SendMessageA(edit,EM_GETFIRSTVISIBLELINE,0,0);
    const auto position=CharacterPosition(edit,after.cpMin);
    if(firstAfter!=first||position.y!=before.y)
      std::fprintf(stderr,"At line %d: first visible line %ld -> %ld; caret y %ld -> %ld\n",line,static_cast<long>(first),static_cast<long>(firstAfter),before.y,position.y);
    Require(after.cpMin==character+1&&after.cpMax==character+1,"Viewport fix moved the insertion point");
    Require(firstAfter==first&&position.y==before.y,"Typing lost the viewport after the 16-bit pixel boundary");
    // Backspace and a line break also rebuild syntax highlighting.
    SendMessageA(edit,WM_KEYDOWN,VK_BACK,1);
    SendMessageA(edit,WM_CHAR,VK_BACK,1);
    SendMessageA(edit,WM_KEYUP,VK_BACK,0xc0000001);
    SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    if(after.cpMin!=character||after.cpMax!=character)std::fprintf(stderr,"Backspace at line %d: expected %ld, actual %ld..%ld\n",line,character,after.cpMin,after.cpMax);
    Require(after.cpMin==character&&after.cpMax==character,"Backspace moved the caret incorrectly");
    Require(SendMessageA(edit,EM_GETFIRSTVISIBLELINE,0,0)==first&&CharacterPosition(edit,character).y==before.y,"Backspace lost the viewport");
    SendMessageA(edit,WM_KEYDOWN,VK_RETURN,1);
    SendMessageA(edit,WM_CHAR,VK_RETURN,1);
    SendMessageA(edit,WM_KEYUP,VK_RETURN,0xc0000001);
    Require(SendMessageA(edit,EM_GETFIRSTVISIBLELINE,0,0)==first,"Enter lost the viewport");
    SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    const auto enter=CharacterPosition(edit,after.cpMin);
    Require(enter.y>=0&&enter.y<client.bottom,"Enter scrolled the caret out of view");
    std::printf("PASS: line %d; typing, backspace and Enter preserve visible position\n",line);std::fflush(stdout);
  }
  // This fix must not reintroduce the rolled-back line-number patch.
  Require(*reinterpret_cast<const uint32_t*>(gImage+0x3373a)==1000,"Original line-number limit was changed");
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=4&&argc!=5){std::fprintf(stderr,"Usage: editor-script-tests.exe editor.exe fixture.bdz output-directory [bundled-boardset.bdz]\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    Require(editor_scripts::VerifiedExecutable(argv[1]),"Unverified editor executable");
    gImage=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Require(gImage!=nullptr,"Cannot map editor");ResolveImports(gImage,argv[1]);
    editor_scripts::Verify(gImage);
    // A mismatch must fail before installing any hook.
    const uint8_t saved=gImage[0x33470],invalid=0xcc;
    editor_scripts::Write(gImage+0x33470,&invalid,1);
    bool rejected=false;try{editor_scripts::Install(gImage);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&gImage[0x32f20]==0x55,"Mismatched editor was partially patched");
    editor_scripts::Write(gImage+0x33470,&saved,1);
    const uint8_t originalBranch=gImage[editor_scripts::kEditBundledBoardsRva],badBranch=0x90;
    editor_scripts::Write(gImage+editor_scripts::kEditBundledBoardsRva,&badBranch,1);rejected=false;
    try{editor_scripts::Install(gImage);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&gImage[0x32f20]==0x55&&gImage[editor_scripts::kEditBundledBoardsRva]==badBranch,"Mismatched edit branch allowed partial installation");
    editor_scripts::Write(gImage+editor_scripts::kEditBundledBoardsRva,&originalBranch,1);
    editor_scripts::Install(gImage);
    Require(gImage[editor_scripts::kEditBundledBoardsRva]==0xeb,"Editor2-equivalent branch was not installed");
    editor_scripts::Jump(gImage+0x39500,reinterpret_cast<void*>(&RefreshTitle));
    // Initialize the original C++ globals (including locale and keyword tables),
    // without running WinMain or registering destructors in an unstarted EXE CRT.
    editor_scripts::Jump(gImage+0x39bf7,reinterpret_cast<void*>(&NoExitRegistration));
    const auto* initializers=reinterpret_cast<void (__cdecl* const*)()>(gImage+0x45430);
    const auto* endInitializers=reinterpret_cast<void (__cdecl* const*)()>(gImage+0x45534);
    for(auto current=initializers;current!=endInitializers;current++)if(*current)(*current)();
    // DONT_RESOLVE does not allocate the editor's static TLS epoch. Initialize
    // its lazy locale directly rather than borrowing this harness's TLS slot.
    reinterpret_cast<void (__cdecl*)(void*)>(gImage+0x3be90)(gImage+0x5d010);
    std::puts("PASS: verified editor; mismatched instructions rejected before mutation");std::fflush(stdout);
    if(argc==5)BundledBoardTests(argv[4],argv[3]);
    const std::wstring fixture=std::filesystem::absolute(argv[2]).wstring();
    Require(LoadBoard(fixture),"Original editor could not load fixture");
    std::puts("PASS: original editor loaded workspace boardset fixture");std::fflush(stdout);

    Require(LoadLibraryA("Riched20.dll")!=nullptr,"Cannot load Rich Edit");
    WNDCLASSA cls{};cls.lpfnWndProc=Parent;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName="AexEditorScriptTest";
    Require(RegisterClassA(&cls)!=0,"Cannot register test window");
    const auto parent=CreateWindowA(cls.lpszClassName,"Script test",WS_OVERLAPPED,0,0,600,400,nullptr,nullptr,cls.hInstance,nullptr);
    Require(parent!=nullptr,"Cannot create test parent");
    const auto edit=CreateWindowExA(0,"RichEdit20A","",WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL,0,0,550,350,parent,reinterpret_cast<HMENU>(0x40a),cls.hInstance,nullptr);
    Require(edit!=nullptr,"Cannot create script control");
    // Exercise the production WM_INITDIALOG limit without the editor's unrelated
    // window-manager initialization. All edit notifications use the actual hook.
    const auto original=editor_scripts::originalDialog;editor_scripts::originalDialog=EmptyDialog;
    editor_scripts::Dialog(parent,WM_INITDIALOG,0,0);editor_scripts::originalDialog=original;
    Require(SendMessageA(edit,EM_GETLIMITTEXT,0,0)==editor_scripts::kMaxTextBytes,"Script input limit was not raised");
    SendMessageA(edit,EM_SETEVENTMASK,0,ENM_CHANGE);
    ViewportTests(parent,edit);
    auto* dialogs=reinterpret_cast<HWND*>(gImage+0x51e3c);
    std::array<std::string,5> expected;
    const std::array<size_t,5> lengths={16383,16384,65536,262144,editor_scripts::kMaxTextBytes};
    for(int kind=0;kind<5;kind++) {
      if(kind)dialogs[kind-1]=nullptr;
      dialogs[kind]=parent;expected[kind]=Sample(lengths[kind],kind);
      Require(SetWindowTextA(edit,expected[kind].c_str())!=0,"Cannot set test script");
      // Some Rich Edit versions do not notify for WM_SETTEXT. Explicitly send
      // the same notification to verify the hooked native dialog procedure.
      SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
      Same(kind,expected[kind]);
      Require(gImage[0x51dd6]==1,"Script edit did not mark boardset modified");
      if(lengths[kind]>65535) {
        const LONG position=lengths[kind]>75000?70000:60000;
        CHARRANGE caret{position,position+1};SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&caret));
        SendMessageA(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>("X"));
        // Use the actual control's normalized text to compare byte-for-byte.
        const int actualLength=GetWindowTextLengthA(edit);
        std::vector<char> actual(static_cast<size_t>(actualLength)+1);GetWindowTextA(edit,actual.data(),static_cast<int>(actual.size()));
        expected[kind].assign(actual.data(),actualLength);
        Same(kind,expected[kind]);
        CHARRANGE after{};SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        if(after.cpMin!=position+1||after.cpMax!=position+1)std::fprintf(stderr,"Caret expected %ld, actual %ld..%ld\n",position+1,after.cpMin,after.cpMax);
        Require(after.cpMin==position+1&&after.cpMax==position+1,"Caret wrapped at 65535 during highlighting");
      }
      std::printf("PASS: script type %d, %zu bytes, edit/highlight/caret\n",kind,expected[kind].size());std::fflush(stdout);
    }
    // At capacity EM_REPLACESEL must reject the next character rather than
    // allowing a change that the persistent script cannot store.
    CHARRANGE end{-1,-1};SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&end));
    const int before=GetWindowTextLengthA(edit);SendMessageA(edit,WM_CHAR,'Z',1);
    Require(GetWindowTextLengthA(edit)==before,"Input control exceeded 1 MiB capacity");Same(4,expected[4]);
    std::puts("PASS: input at capacity rejects additional text without truncating stored script");std::fflush(stdout);

    std::filesystem::create_directories(argv[3]);
    const std::wstring output=(std::filesystem::absolute(argv[3])/L"large-scripts.bdz").wstring();
    Require(!std::filesystem::exists(output),"Output fixture already exists");
    using Save=int (__thiscall*)(void*,const std::wstring*,bool);
    Require(reinterpret_cast<Save>(gImage+0x2d0d0)(gImage+0x555f0,&output,false)!=0,"Original editor writer failed");
    Require(LoadBoard(output),"Original editor reload failed");
    for(int kind=0;kind<5;kind++)Same(kind,expected[kind]);
    std::puts("PASS: original editor saved and reopened all five scripts byte-for-byte");
    Require(SetWindowTextA(edit,"")!=0,"Cannot clear script");
    SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));Same(4,"");
    Require(SetWindowTextA(edit,"-- small script after a full buffer")!=0,"Cannot edit after capacity");
    SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));Same(4,"-- small script after a full buffer");
    Require(reinterpret_cast<DLGPROC>(gImage+0x32f20)(parent,WM_NULL,0,0)==FALSE,"Original dialog fallback trampoline failed");
    std::puts("PASS: clear/re-edit after capacity and original dialog fallback");
    DestroyWindow(parent);return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
