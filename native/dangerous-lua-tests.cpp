// Exercise the game's registered Lua callbacks in an isolated PE mapping.
// Intercept command, file, and output boundaries before calling originals:
// no shell commands or native DLL loads can occur during these tests. Real
// file-open checks use only a disposable temporary fixture owned by the test.
#include "dangerous-lua.hpp"
#include "verified-game.hpp"
#include <array>
#include <string>

static void Require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
static int gSystemCalls=0,gResultCalls=0,gStatus=0;
static std::string gCommand;
static bool gShellProbe=false;
static int __cdecl System(const char* command) {
  gSystemCalls++;gShellProbe=command==nullptr;gCommand=command?command:"";return 37;
}
static int __fastcall ExecResult(void*,int status) {
  gResultCalls++;gStatus=status;return 3;
}
static int gRemoveCalls=0,gRenameCalls=0,gFileResultCalls=0,gFileStatus=0;
static std::string gOldPath,gNewPath;
static int __cdecl Remove(const char* path) {
  gRemoveCalls++;gOldPath=path;return 0;
}
static int __cdecl Rename(const char* oldPath,const char* newPath) {
  gRenameCalls++;gOldPath=oldPath;gNewPath=newPath;return 0;
}
// The original helper receives additional stack arguments with caller cleanup.
// These test stubs consume only ECX/EDX, leaving those stack arguments intact.
static int __fastcall FileResult(void*,int status) {
  gFileResultCalls++;gFileStatus=status;return 1;
}

// Only the fields read by the original callbacks are needed. A Lua TValue
// is 16 bytes; a TString's text begins after its 16-byte header in this build.
struct Value { uint64_t value=0;uint32_t type=0,padding=0; };
struct State {
  uint8_t header[12]={};
  Value* top=nullptr;
  void* global=nullptr;
  Value** callInfo=nullptr;
};
static_assert(sizeof(Value)==16&&offsetof(State,top)==12&&offsetof(State,callInfo)==20,"Unexpected Lua test layout");
static int gOutputLookups=0,gWriteCalls=0;
static void* gWrittenStream=nullptr;
static uintptr_t gStream[]={0x12345678,1};
static void __fastcall DefaultOutput(State* state,void*) {
  gOutputLookups++;
  state->top->value=reinterpret_cast<uintptr_t>(gStream);state->top->type=2;state->top++;
}
static int __fastcall Write(void*,void* stream) {
  gWriteCalls++;gWrittenStream=stream;return 5;
}
static int gOutputSelections=0,gFileLookups=0;
static int __fastcall SelectOutput(void*,const char*) {
  gOutputSelections++;return 6;
}
static void* __fastcall GetFile(void*,void*) {
  gFileLookups++;return gStream;
}
static int gFlushCalls=0,gBufferCalls=0,gOptionCalls=0,gNewFiles=0,gPopenCalls=0,gTmpfileCalls=0;
static void* gFlushedStream=nullptr;
static int __cdecl Flush(void* stream) {
  gFlushCalls++;gFlushedStream=stream;return 0;
}
static int __fastcall CheckOption(void*,int) {gOptionCalls++;return 0;}
static int __cdecl SetBuffer(void* stream,char* buffer,int,size_t size) {
  Require(stream==reinterpret_cast<void*>(gStream[0])&&buffer==nullptr&&size==4096,"Unexpected setvbuf arguments");
  gBufferCalls++;return 0;
}
static void* __fastcall NewFile(void*) {gNewFiles++;return gStream;}
static void* __cdecl Popen(const char* command,const char* mode) {
  gPopenCalls++;gCommand=command;Require(std::strcmp(mode,"r")==0,"Unexpected popen mode");
  return reinterpret_cast<void*>(0x12345678);
}
static void* __cdecl Tmpfile() {gTmpfileCalls++;return reinterpret_cast<void*>(0x12345678);}
static int gOpenCalls=0;
static bool gRealFileOpen=false;
static std::string gOpenedPath,gOpenedMode;
static void* __cdecl TestOpenFile(const char* filename,const char* mode) {
  gOpenCalls++;gOpenedPath=filename;gOpenedMode=mode;
  if(!gRealFileOpen)return reinterpret_cast<void*>(0x12345678);
  FILE* file=nullptr;fopen_s(&file,filename,mode);return file;
}
struct TemporaryFile {
  char path[MAX_PATH]={};
  static constexpr char content[]="AEx read-only sentinel\r\n";
  TemporaryFile() {
    char directory[MAX_PATH];
    const auto length=GetTempPathA(MAX_PATH,directory);
    Require(length>0&&length<MAX_PATH&&GetTempFileNameA(directory,"aex",0,path)!=0,"Cannot create test fixture");
    FILE* file=nullptr;fopen_s(&file,path,"wb");
    Require(file!=nullptr,"Cannot initialize test fixture");
    const auto written=std::fwrite(content,1,sizeof(content)-1,file);std::fclose(file);
    Require(written==sizeof(content)-1,"Cannot write test fixture");
  }
  ~TemporaryFile(){DeleteFileA(path);}
};

int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: dangerous-lua-tests.exe path-to-DXBall2.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game executable");
    auto* base=reinterpret_cast<uint8_t*>(module);
    VerifyDisableDangerousLua(base);
    using Execute=int (__cdecl*)(void*);
    // Independently list every requested registration, including all entries
    // of the debug library, so a missing production patch fails the tests.
    const std::array<uint32_t,31> entries={
      0x17dc3c,0x17dc54,0x17dc5c,0x17deb4,0x17de8c,0x17de44,
      0x17de1c,0x17de6c,0x17de3c,0x17de94,0x17dea4,
      0x17dee4,0x17deec,0x17def4,0x17defc,0x17df04,0x17df0c,
      0x17df14,0x17df1c,0x17df24,0x17df2c,0x17df34,0x17df3c,
      0x17df44,0x17df4c,0x17df54,0x17df5c,
      0x17dbd4,0x17dc14,0x17dbb8,0x17dbbc
    };
    std::array<Execute,31> aliases={};
    for(size_t i=0;i<entries.size();i++)aliases[i]=*reinterpret_cast<Execute*>(base+entries[i]);
    const auto registered=aliases[0];
    // A mismatched target must reject the whole installation before any
    // other callback is modified, regardless of patch table order.
    const uint8_t invalid=0xcc,original=base[0x2bc30];
    WriteMemory(base+0x2bc30,&invalid,1);
    bool rejected=false;
    try {InstallDisableDangerousLua(base);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&base[0x2bc30]==invalid,"Target mismatch was not rejected");
    for(const auto alias:aliases)if(reinterpret_cast<uint8_t*>(alias)!=base+0x2bc30)
      Require(*reinterpret_cast<uint8_t*>(alias)==0x55,"Target mismatch allowed partial installation");
    WriteMemory(base+0x2bc30,&original,1);
    // io.open must also be verified before any no-op is installed.
    const uint8_t badCall=0x90,callOpcode=base[0x27df0];
    WriteMemory(base+0x27df0,&badCall,1);rejected=false;
    try {InstallDisableDangerousLua(base);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"io.open call mismatch was not rejected");
    for(const auto alias:aliases)Require(*reinterpret_cast<uint8_t*>(alias)==0x55,"io.open mismatch allowed partial installation");
    WriteMemory(base+0x27df0,&callOpcode,1);
    // Redirect side effects even for the unblocked baseline.
    JumpTo(base,0x11da4f,reinterpret_cast<void*>(&System));
    JumpTo(base,0x8cb0,reinterpret_cast<void*>(&ExecResult));
    JumpTo(base,0x11c96d,reinterpret_cast<void*>(&Remove));
    JumpTo(base,0x11da5a,reinterpret_cast<void*>(&Rename));
    JumpTo(base,0x8be0,reinterpret_cast<void*>(&FileResult));
    JumpTo(base,0x67f0,reinterpret_cast<void*>(&DefaultOutput));
    JumpTo(base,0x29500,reinterpret_cast<void*>(&Write));
    JumpTo(base,0x28110,reinterpret_cast<void*>(&SelectOutput));
    JumpTo(base,0x8f50,reinterpret_cast<void*>(&GetFile));
    JumpTo(base,0x115b8e,reinterpret_cast<void*>(&Flush));
    JumpTo(base,0x8ff0,reinterpret_cast<void*>(&CheckOption));
    JumpTo(base,0x116e1e,reinterpret_cast<void*>(&SetBuffer));
    JumpTo(base,0x279a0,reinterpret_cast<void*>(&NewFile));
    JumpTo(base,0x11f8b8,reinterpret_cast<void*>(&Popen));
    JumpTo(base,0x11e63a,reinterpret_cast<void*>(&Tmpfile));
    JumpTo(base,0xf2d00,reinterpret_cast<void*>(&TestOpenFile));
    std::array<Value,8> stack={};
    Value* function=stack.data();
    State state;state.top=stack.data()+2;state.callInfo=&function;
    std::array<char,128> command={};
    strcpy_s(command.data()+16,command.size()-16,"aex-test-command-never-executed");
    stack[1].value=reinterpret_cast<uintptr_t>(command.data());stack[1].type=4;
    Require(registered(&state)==3&&gSystemCalls==1&&gResultCalls==1&&gStatus==37&&!gShellProbe&&gCommand==command.data()+16,
      "Original os.execute did not reach intercepted system() with the command");
    state.top=stack.data()+1;
    Require(registered(&state)==1&&gSystemCalls==2&&gShellProbe&&stack[1].type==1&&stack[1].value==1,
      "Original no-argument os.execute did not probe the intercepted shell");
    std::puts("PASS: original registered os.execute reaches intercepted system() for commands and shell probes");

    std::array<char,128> newPath={};
    strcpy_s(newPath.data()+16,newPath.size()-16,"aex-test-new-path-never-created");
    stack[1].value=reinterpret_cast<uintptr_t>(command.data());stack[1].type=4;state.top=stack.data()+2;
    Require(aliases[1](&state)==1&&gRemoveCalls==1&&gOldPath==command.data()+16&&gFileResultCalls==1&&gFileStatus==1,
      "Original os.remove did not reach intercepted remove()");
    stack[2].value=reinterpret_cast<uintptr_t>(newPath.data());stack[2].type=4;state.top=stack.data()+3;
    Require(aliases[2](&state)==1&&gRenameCalls==1&&gOldPath==command.data()+16&&gNewPath==newPath.data()+16&&gFileResultCalls==2&&gFileStatus==1,
      "Original os.rename did not reach intercepted rename()");
    std::array<uint8_t,64> global={};state.global=global.data();state.top=stack.data()+2;
    Require(aliases[3](&state)==5&&gOutputLookups==1&&gWriteCalls==1&&gWrittenStream==reinterpret_cast<void*>(gStream[0]),
      "Original io.write did not reach intercepted output writer");
    std::puts("PASS: original os.remove, os.rename, and io.write reach intercepted file/output handlers");
    state.top=stack.data()+2;
    Require(aliases[4](&state)==6&&gOutputSelections==1,"Original io.output did not reach intercepted selector");
    Require(aliases[5](&state)==5&&gFileLookups==1&&gWriteCalls==2&&gWrittenStream==reinterpret_cast<void*>(gStream[0]),
      "Original file:write did not reach intercepted file writer");
    std::puts("PASS: original io.output and file:write reach intercepted output/file handlers");
    state.top=stack.data()+2;
    Require(aliases[6](&state)==1&&gFlushCalls==1&&gFileLookups==2&&gFileResultCalls==3&&gFileStatus==1,
      "Original file:flush did not reach intercepted flush handler");
    Require(aliases[7](&state)==1&&gFlushCalls==2&&gOutputLookups==2&&gFileResultCalls==4&&gFlushedStream==reinterpret_cast<void*>(gStream[0]),
      "Original io.flush did not reach intercepted flush handler");
    state.top=stack.data()+2;
    Require(aliases[8](&state)==1&&gBufferCalls==1&&gOptionCalls==1&&gFileLookups==3&&gFileResultCalls==5&&gFileStatus==1,
      "Original file:setvbuf did not reach intercepted buffer handler");
    Require(aliases[9](&state)==1&&gPopenCalls==1&&gNewFiles==1&&gCommand==command.data()+16,
      "Original io.popen did not reach intercepted process handler");
    Require(aliases[10](&state)==1&&gTmpfileCalls==1&&gNewFiles==2,
      "Original io.tmpfile did not reach intercepted temporary-file handler");
    std::puts("PASS: original flush/setvbuf/popen/tmpfile callbacks reach intercepted handlers without file or process side effects");

    const char* debugNames[]={
      "debug","getuservalue","gethook","getinfo","getlocal","getregistry","getmetatable","getupvalue",
      "upvaluejoin","upvalueid","setuservalue","sethook","setlocal","setmetatable","setupvalue","traceback"
    };
    for(size_t i=0;i<16;i++)
      Require(std::strcmp(*reinterpret_cast<const char**>(base+0x17dee0+i*8),debugNames[i])==0,
        "Unexpected debug library registration");
    Require(*reinterpret_cast<uintptr_t*>(base+0x17df60)==0,"Uncovered debug library function");

    // The os registration table ends after tmpname; kill is not registered.
    for(size_t entry=0x17dc20;entry<0x17dc78;entry+=8)
      Require(std::strcmp(*reinterpret_cast<const char**>(base+entry),"kill")!=0,"Unexpected os.kill registration");
    Require(*reinterpret_cast<uintptr_t*>(base+0x17dc78)==0,"Unexpected os library table terminator");

    // Exercise the actual io.open callback, including its original argument
    // handling and mode validation. Initially intercept every open operation.
    const auto openAlias=*reinterpret_cast<Execute*>(base+0x17de84);
    std::array<char,MAX_PATH+16> openPath={};
    std::array<char,128> openMode={};
    const auto openWithMode=[&](const char* filename,const char* mode,bool explicitNil=false) {
      strcpy_s(openPath.data()+16,openPath.size()-16,filename);
      stack.fill({});stack[1].value=reinterpret_cast<uintptr_t>(openPath.data());stack[1].type=4;
      state.top=stack.data()+2;
      if(mode) {
        strcpy_s(openMode.data()+16,openMode.size()-16,mode);
        stack[2].value=reinterpret_cast<uintptr_t>(openMode.data());stack[2].type=4;state.top++;
      }else if(explicitNil)state.top++;
      return openAlias(&state);
    };
    for(const char* mode:{"r","rb","w","wb","a","ab","r+","r+b","w+","w+b","a+","a+b"})
      Require(openWithMode("aex-intercepted-open",mode)==1&&gOpenedMode==mode,"Original io.open changed its mode");
    Require(openWithMode("aex-intercepted-open",nullptr)==1&&gOpenedMode=="r","Original io.open default failed");
    Require(openWithMode("aex-intercepted-open",nullptr,true)==1&&gOpenedMode=="r","Original io.open nil default failed");
    std::puts("PASS: unblocked io.open preserves read/write/append/update modes and omitted/nil defaults");
    const auto sideEffects=[]() {
      return std::array<int,16>{gSystemCalls,gResultCalls,gRemoveCalls,gRenameCalls,gFileResultCalls,gOutputLookups,
        gWriteCalls,gOutputSelections,gFileLookups,gFlushCalls,gBufferCalls,gOptionCalls,gNewFiles,gPopenCalls,gTmpfileCalls,gOpenCalls};
    };
    const auto beforeCallbacks=sideEffects();
    InstallDisableDangerousLua(base);
    // A pointer captured before patching must also become harmless. Vary
    // argument counts/types, and verify the state and stack stay unchanged.
    for(const auto alias:aliases)for(uint32_t type:{0u,1u,3u,4u,5u,6u})for(size_t count:{0u,1u,2u,3u}) {
      stack.fill({});stack[1].type=type;stack[1].value=reinterpret_cast<uintptr_t>(command.data());
      state.top=stack.data()+1+count;
      const auto beforeStack=stack;const auto beforeState=state;
      Require(alias(&state)==0,"Disabled Lua callback must return zero results");
      Require(std::memcmp(stack.data(),beforeStack.data(),sizeof(stack))==0&&std::memcmp(&state,&beforeState,sizeof(state))==0,
        "Disabled Lua callback changed the Lua state or stack");
    }
    for(size_t i=0;i<aliases.size();i++) {
      Require(aliases[i](nullptr)==0,"Disabled Lua callback accessed its Lua state");
      Require(*reinterpret_cast<Execute*>(base+entries[i])==aliases[i],"Lua callable registration was changed");
    }
    Require(sideEffects()==beforeCallbacks,
      "Disabled Lua callbacks reached command, file, or output handlers");
    std::puts("PASS: all 31 callbacks block side effects, invalid arguments, and captured aliases without touching Lua state");
    std::puts("PASS: complete 16-function debug library blocked; all targets verified before patching; os.kill absent");

    // Verify the filter with real files: writable modes must preserve the
    // sentinel, allow reading, and fail to create a missing file.
    TemporaryFile fixture;gRealFileOpen=true;
    for(const char* mode:{"r","rb","w","wb","a","ab","r+","r+b","w+","w+b","a+","a+b"}) {
      Require(openWithMode(fixture.path,mode)==1,"Protected io.open did not open existing file");
      Require(gOpenedPath==fixture.path&&gOpenedMode==(std::strpbrk(mode,"wa+")?"r":mode),"Protected io.open used wrong mode/path");
      auto* file=reinterpret_cast<FILE*>(gStream[0]);Require(file!=nullptr,"Protected io.open returned null file");
      char contents[128]={};const auto count=std::fread(contents,1,sizeof(contents)-1,file);std::fclose(file);
      // Windows text reads normalize CRLF; binary reads preserve it.
      const auto expected=gOpenedMode=="rb"?"AEx read-only sentinel\r\n":"AEx read-only sentinel\n";
      Require(std::string(contents,count)==expected,"Writable io.open truncated or changed the fixture");
    }
    for(bool explicitNil:{false,true}) {
      Require(openWithMode(fixture.path,nullptr,explicitNil)==1&&gOpenedMode=="r"&&gStream[0],"Protected default/nil read failed");
      std::fclose(reinterpret_cast<FILE*>(gStream[0]));
    }
    const std::string missing=std::string(fixture.path)+".missing";
    Require(GetFileAttributesA(missing.c_str())==INVALID_FILE_ATTRIBUTES,"Missing-file fixture already exists");
    for(const char* mode:{"w","wb","a","ab","r+","r+b","w+","w+b","a+","a+b"}) {
      openWithMode(missing.c_str(),mode);
      Require(gOpenedMode=="r"&&gStream[0]==0&&gFileStatus==0&&GetFileAttributesA(missing.c_str())==INVALID_FILE_ATTRIBUTES,
        "Protected writable io.open created a file or did not use the original failure path");
    }
    Require(*reinterpret_cast<Execute*>(base+0x17de84)==openAlias,"io.open callable registration changed");
    std::puts("PASS: io.open aliases preserve read/binary/default modes; writable modes fall back to r without truncating or creating files");
    FreeLibrary(module);return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
