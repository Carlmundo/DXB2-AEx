// Execute the real upload description-read block in an isolated EXE mapping.
// Stop before upload starts; no Steam initialization or publishing occurs.
#include "workshop-description.hpp"
#include <cstdio>
#include <filesystem>
#include <string>

static uint8_t* gImage=nullptr;
static void* gRead=nullptr;
static UINT (WINAPI* gGetText)(HWND,int,LPSTR,int)=GetDlgItemTextA;
static DWORD gStackBufferFirst=0;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}

static void ResolveImports(const std::filesystem::path& executable) {
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(gImage+reinterpret_cast<const IMAGE_DOS_HEADER*>(gImage)->e_lfanew);
  auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(gImage+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
  for(;descriptor->Name;descriptor++) {
    const auto* name=reinterpret_cast<const char*>(gImage+descriptor->Name);
    const auto dll=_stricmp(name,"steam_api.dll")==0?
      LoadLibraryW((executable.parent_path().parent_path()/L"steam_api.dll").c_str()):LoadLibraryA(name);
    Require(dll!=nullptr,"Cannot load Workshop dependency");
    const auto* source=reinterpret_cast<const IMAGE_THUNK_DATA*>(gImage+descriptor->OriginalFirstThunk);
    auto* target=reinterpret_cast<IMAGE_THUNK_DATA*>(gImage+descriptor->FirstThunk);
    for(;source->u1.AddressOfData;source++,target++) {
      const auto nameOrOrdinal=IMAGE_SNAP_BY_ORDINAL(source->u1.Ordinal)?MAKEINTRESOURCEA(IMAGE_ORDINAL(source->u1.Ordinal)):
        reinterpret_cast<const char*>(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(gImage+source->u1.AddressOfData)->Name);
      const auto address=GetProcAddress(dll,nameOrOrdinal);Require(address!=nullptr,"Missing Workshop import");
      memory_patch::Write(&target->u1.Function,&address,sizeof(address));
    }
  }
}

static __declspec(naked) void __stdcall ReadDescription(HWND) {
  __asm {
    push ebp
    mov ebp,esp
    sub esp,110h
    push ebx
    push esi
    push edi
    mov dword ptr [ebp-110h],5a5a5a5ah
    mov edi,[ebp+8]
    mov esi,gGetText
    call gRead
    mov eax,[ebp-110h]
    mov gStackBufferFirst,eax
    pop edi
    pop esi
    pop ebx
    mov esp,ebp
    pop ebp
    ret 4
  }
}

static std::string Description() {
  const auto* object=gImage+0x20ab0;
  const auto length=*reinterpret_cast<const uint32_t*>(object+0x10);
  const auto capacity=*reinterpret_cast<const uint32_t*>(object+0x14);
  const auto* text=capacity>=16?*reinterpret_cast<const char* const*>(object):reinterpret_cast<const char*>(object);
  return std::string(text,length);
}
static void Same(HWND dialog,HWND control,const std::string& input,const std::string& expected,bool patched) {
  Require(SetWindowTextA(control,input.c_str())!=FALSE,"Cannot set description fixture");
  ReadDescription(dialog);
  Require(Description()==expected,"Description bytes changed or were truncated at the wrong boundary");
  if(patched)Require(gStackBufferFirst==0x5a5a5a5a,"Hook wrote into the original 256-byte stack buffer");
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: workshop-description-tests.exe Tools/DXB2Workshop.exe\n");return 2;}
  try {
    Require(workshop_description::VerifiedExecutable(argv[1]),"Unverified Workshop executable");
    wchar_t testExecutable[MAX_PATH];
    Require(GetModuleFileNameW(nullptr,testExecutable,MAX_PATH)!=0,"Cannot locate test executable");
    Require(!workshop_description::VerifiedExecutable(testExecutable),"Unrelated executable accepted as Workshop");
    gImage=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Require(gImage!=nullptr,"Cannot map Workshop executable");ResolveImports(argv[1]);
    workshop_description::Verify(gImage);
    gRead=gImage+workshop_description::kReadRva;
    const uint8_t stop=0xc3;memory_patch::Write(gImage+workshop_description::kResumeRva,&stop,1);
    // Initialize just the stock description's empty std::string. Do not run
    // the EXE's CRT constructors, WinMain, callbacks or Steam networking.
    *reinterpret_cast<uint32_t*>(gImage+0x20ab0+0x14)=15;
    const auto instance=GetModuleHandleW(nullptr);
    const auto dialog=CreateWindowExA(0,"STATIC","Workshop test",WS_OVERLAPPED,0,0,400,200,nullptr,nullptr,instance,nullptr);
    Require(dialog!=nullptr,"Cannot create test dialog");
    const auto edit=CreateWindowExA(0,"EDIT","",WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL,0,0,400,200,dialog,reinterpret_cast<HMENU>(0x3ee),instance,nullptr);
    Require(edit!=nullptr,"Cannot create description edit control");
    const std::string longText(8000,'x');
    Same(dialog,edit,longText,longText.substr(0,255),false);
    std::puts("PASS: stock upload path reproduces the 255-byte cutoff");
    constexpr size_t count=workshop_description::kResumeRva-workshop_description::kReadRva;
    uint8_t original[count];std::memcpy(original,gImage+workshop_description::kReadRva,count);
    for(size_t offset=0;offset<count;offset++) {
      const uint8_t invalid=original[offset]^0xff;
      memory_patch::Write(gImage+workshop_description::kReadRva+offset,&invalid,1);
      bool rejected=false;try{workshop_description::Install(gImage);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Mismatched upload instructions accepted");
      Require(gImage[workshop_description::kReadRva+offset]==invalid,"Rejected installation changed instructions");
      memory_patch::Write(gImage+workshop_description::kReadRva,original,count);
    }
    workshop_description::Install(gImage);
    for(const size_t length:{0u,1u,15u,16u,253u,255u,256u,257u,7999u,8000u,8001u,16000u}) {
      std::string input(length,'a');
      if(length>32)input.replace(length-16,16,"-- END SENTINEL!");
      Same(dialog,edit,input,input.substr(0,workshop_description::kMaxBytes),true);
    }
    std::string multiline;
    while(multiline.size()<8000)multiline+="Description [b]formatting[/b]\r\n\r\n";
    multiline.resize(8000);Same(dialog,edit,multiline,multiline,true);
    Same(dialog,edit,"","",true);Same(dialog,edit,"short after long","short after long",true);
    // Release the string through the EXE's own CRT as well.
    reinterpret_cast<void (__thiscall*)(void*)>(gImage+0x2660)(gImage+0x20ab0);
    DestroyWindow(dialog);
    std::puts("PASS: real patched upload read preserves 8000 bytes, multiline text and re-edits; overflow caps at 8000; original stack buffer untouched");
    std::puts("PASS: every mismatched hook byte rejected before patching; no upload performed");
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s (Windows error %lu)\n",error.what(),GetLastError());return 1;}
}
