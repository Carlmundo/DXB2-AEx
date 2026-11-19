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
  const std::string line="local value=123-- script "+std::to_string(kind)+" abcdefghijklmnopqrstuvwxyz 0123456789 } { \\par\r\n";
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
static std::string ControlText(HWND control) {
  std::string text(static_cast<size_t>(GetWindowTextLengthA(control))+1,'\0');
  text.resize(GetWindowTextA(control,text.data(),static_cast<int>(text.size())));return text;
}
#include "editor-input-audit.hpp"
static void HighlightIntegrityTests(HWND parent,HWND edit) {
  auto* dialogs=reinterpret_cast<HWND*>(gImage+0x51e3c);
  for(int kind=0;kind<5;kind++) {
    for(int i=0;i<5;i++)dialogs[i]=i==kind?parent:nullptr;
    for(const char* text:{"", "\r\n", "\r\n\r\n", "return 42\r\n", "-- comment\r\n", "--[[\r\n", "--[[unfinished\r\n\r\n", "--[[closed]]\r\n"}) {
      {editor_scripts::Updating guard;SetWindowTextA(edit,"");Require(SetWindowTextA(edit,text)!=0,"Cannot set newline fixture");}
      SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
      Require(ControlText(edit)==text,"Highlighting changed final whitespace");Same(kind,text);
      for(int repeat=0;repeat<3;repeat++) {
        reinterpret_cast<void (__cdecl*)(HWND,int)>(gImage+0x33b40)(edit,kind);
        Require(ControlText(edit)==text,"Native highlighting changed script text");Same(kind,text);
      }
    }
    constexpr char colored[]="local value = 42 -- comment";
    Require(SetWindowTextA(edit,colored)!=0,"Cannot set color fixture");
    SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
    CHARRANGE keyword{0,5},comment{19,26};CHARFORMATA keywordFormat{},commentFormat{};
    keywordFormat.cbSize=sizeof(keywordFormat);commentFormat.cbSize=sizeof(commentFormat);
    SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&keyword));
    SendMessageA(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&keywordFormat));
    SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&comment));
    SendMessageA(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&commentFormat));
    Require(keywordFormat.crTextColor!=commentFormat.crTextColor,"Undo fix disabled syntax colors");Same(kind,colored);
    constexpr char before[]="local value = 42\r\nreturn value";
    Require(SetWindowTextA(edit,before)!=0,"Cannot set undo fixture");
    SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
    SendMessageA(edit,EM_EMPTYUNDOBUFFER,0,0);SendMessageA(edit,EM_SETSEL,2,2);
    SendMessageA(edit,WM_CHAR,'x',1);
    const std::string typed="loxcal value = 42\r\nreturn value";
    Require(ControlText(edit)==typed,"Cannot type undo fixture");Same(kind,typed);
    SendMessageA(edit,WM_UNDO,0,0);
    Require(ControlText(edit)==before,"Highlighting prevented Undo");Same(kind,before);
    CHARRANGE after{};SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
    Require(after.cpMin==2&&after.cpMax==2,"Undo selected unexpected script text");
    Require(SendMessageA(edit,EM_CANREDO,0,0)!=0,"Highlighting destroyed Redo");
    SendMessageA(edit,EM_REDO,0,0);
    Require(ControlText(edit)==typed,"Highlighting prevented Redo");Same(kind,typed);
    SendMessageA(edit,WM_UNDO,0,0);SendMessageA(edit,WM_CHAR,'q',1);
    const std::string retyped="loqcal value = 42\r\nreturn value";
    Require(ControlText(edit)==retyped,"Typing after Undo replaced unexpected script text");Same(kind,retyped);
    std::printf("PASS: script type %d; final newlines, native highlighting, Undo and Redo preserve text\n",kind);std::fflush(stdout);
  }
  for(int i=0;i<5;i++)dialogs[i]=nullptr;
}
static void InlineCommentTests(HWND parent,HWND edit) {
  auto* dialogs=reinterpret_cast<HWND*>(gImage+0x51e3c);
  for(int kind=0;kind<5;kind++) {
    for(int i=0;i<5;i++)dialogs[i]=i==kind?parent:nullptr;
    for(const char* fixture:{"prefix|suffix", "local value| = 42", "return| value", "    identifier|tail", "local s = \"prefix|suffix\"", "identifier|"}) {
      for(const char* insertion:{"--","--[[","--[[ } { \\ ]]"}) {
        std::string before=fixture;
        const size_t offset=before.find('|');before.erase(offset,1);
        if(std::strcmp(fixture,"identifier|")!=0)before+="\r\nlocal sentinel = 42\r\nreturn sentinel";
        Require(SetWindowTextA(edit,before.c_str())!=0,"Cannot set inline-comment fixture");
        SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
        Require(ControlText(edit)==before,"Loading inline-comment fixture changed text");
        CHARRANGE caret{static_cast<LONG>(offset),static_cast<LONG>(offset)};
        SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&caret));
        std::string expected=before;size_t added=0;
        for(const char* c=insertion;*c;c++) {
          expected.insert(offset+added++,1,*c);SendMessageA(edit,WM_CHAR,static_cast<unsigned char>(*c),1);
          if(ControlText(edit)!=expected)std::fprintf(stderr,"Inline comment reordered: fixture [%s], insertion [%s], typed %zu characters; actual [%s]\n",fixture,insertion,added,ControlText(edit).c_str());
          Require(ControlText(edit)==expected,"Typing an inline comment reordered script text");Same(kind,expected);
          CHARRANGE after{};SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
          Require(after.cpMin==static_cast<LONG>(offset+added)&&after.cpMax==after.cpMin,"Inline comment moved the caret");
        }
        // Reopening the same text exercises the formatter before any typing.
        Require(SetWindowTextA(edit,expected.c_str())!=0,"Cannot reload inline comment");
        SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
        Require(ControlText(edit)==expected,"Highlighting an existing inline comment reordered text");Same(kind,expected);
      }
    }
    std::printf("PASS: script type %d; inline line/block comments preserve token order and caret\n",kind);std::fflush(stdout);
  }
  for(int i=0;i<5;i++)dialogs[i]=nullptr;
}
static void CommentTests(HWND parent,HWND edit) {
  auto* dialogs=reinterpret_cast<HWND*>(gImage+0x51e3c);
  for(int kind=0;kind<5;kind++) {
    for(int i=0;i<5;i++)dialogs[i]=i==kind?parent:nullptr;
    for(const char* comment:{"-- this is a comment", "local b = 2 -- inline comment", "--[[ block comment ]]", "--[[ unclosed block comment"}) {
      for(const char* insertion:{"}","{","\\","} { \\par \\cf1 123"}) {
        const std::string before=std::string("local a = 1\r\n")+comment+"\r\nlocal sentinel = 42\r\nreturn sentinel";
        Require(SetWindowTextA(edit,before.c_str())!=0,"Cannot set comment fixture");
        SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
        Require(ControlText(edit)==before,"Loading a comment changed script text");
        // Rich Edit caret offsets count CRLF as a single character.
        const LONG position=static_cast<LONG>(before.find("comment")-1);
        CHARRANGE caret{position,position};SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&caret));
        std::string expected=before;expected.insert(static_cast<size_t>(position)+1,insertion);
        for(const char* c=insertion;*c;c++)SendMessageA(edit,WM_CHAR,static_cast<unsigned char>(*c),1);
        if(ControlText(edit)!=expected)std::fprintf(stderr,"Comment corruption: kind %d, comment [%s], insertion [%s]\n",kind,comment,insertion);
        Require(ControlText(edit)==expected,"Typing RTF characters in a comment corrupted following code");
        Same(kind,expected);
        CHARRANGE after{};SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        Require(after.cpMin==position+static_cast<LONG>(std::strlen(insertion))&&after.cpMax==after.cpMin,"Comment edit moved the caret");
        // A second edit previously committed the truncated display to the boardset.
        SendMessageA(edit,WM_CHAR,'x',1);expected.insert(static_cast<size_t>(position)+1+std::strlen(insertion),"x");
        Require(ControlText(edit)==expected,"Editing after a comment lost following code");Same(kind,expected);
      }
    }
    std::printf("PASS: script type %d; braces and backslashes in line/block comments preserve text and caret\n",kind);std::fflush(stdout);
  }
  for(int i=0;i<5;i++)dialogs[i]=nullptr;
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
    for(const auto rva:editor_scripts::kCommentAppendRvas) {
      const uint8_t savedOffset=gImage[rva+1];
      editor_scripts::Write(gImage+rva+1,&invalid,1);rejected=false;
      try{editor_scripts::Install(gImage);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected&&gImage[0x32f20]==0x55&&gImage[editor_scripts::kEditBundledBoardsRva]==originalBranch,"Mismatched comment formatter allowed partial installation");
      editor_scripts::Write(gImage+rva+1,&savedOffset,1);
    }
    for(const auto& hook:editor_scripts::kCommentStarts) {
      const uint8_t savedOffset=gImage[hook.rva+1];
      editor_scripts::Write(gImage+hook.rva+1,&invalid,1);rejected=false;
      try{editor_scripts::Install(gImage);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected&&gImage[0x32f20]==0x55&&gImage[editor_scripts::kEditBundledBoardsRva]==originalBranch,"Mismatched comment start allowed partial installation");
      editor_scripts::Write(gImage+hook.rva+1,&savedOffset,1);
    }
    const auto paragraph=editor_scripts::kFinalParagraphRva;
    const uint8_t savedParagraph=gImage[paragraph+1];editor_scripts::Write(gImage+paragraph+1,&invalid,1);rejected=false;
    try{editor_scripts::Install(gImage);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&gImage[0x32f20]==0x55&&gImage[0x33b40]==0x55,"Mismatched final paragraph allowed partial installation");
    editor_scripts::Write(gImage+paragraph+1,&savedParagraph,1);
    editor_scripts::Install(gImage);
    Require(gImage[0x33b40]==0xe9,"Native highlighting hook was not installed");
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
    if(GetEnvironmentVariableA("AEX_EDITOR_INPUT_AUDIT",nullptr,0))InputAudit(parent,edit);
    HighlightIntegrityTests(parent,edit);
    InlineCommentTests(parent,edit);
    CommentTests(parent,edit);
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
