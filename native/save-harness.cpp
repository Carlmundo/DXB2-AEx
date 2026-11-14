// Runs the original save manager against explicit workspace copies only.
// Steam verification is stubbed; no live saves/settings are accessed.
#include "patch.hpp"
#include "verified-game.hpp"
#include "save-format.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <array>

static uint8_t* gBase=nullptr;
static uint32_t gChecksum=0;
static dxb::Bytes Read(const std::filesystem::path& path) {
  std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read save fixture");
  return dxb::Bytes(std::istreambuf_iterator<char>(file),{});
}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
  std::fprintf(stderr,"Native save fault %08lX at RVA %08lX\n",fault->ExceptionRecord->ExceptionCode,
    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gBase)));
  if(fault->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)TerminateProcess(GetCurrentProcess(),3);
  return EXCEPTION_CONTINUE_SEARCH;
}
static FILE* __cdecl HarnessOpenFile(const wchar_t* path,const wchar_t* mode) {FILE* file=nullptr;_wfopen_s(&file,path,mode);return file;}
static int __cdecl Decode(const void* input,int length,void** output,int* outLength) {
  const auto result=dxb::NormalizeSave(static_cast<const uint8_t*>(input),length);
  *output=std::malloc(result.bytes.size());std::memcpy(*output,result.bytes.data(),result.bytes.size());*outLength=static_cast<int>(result.bytes.size());
  gChecksum=result.checksum;
  const uint32_t columns=result.version>=5?37:26,bytes=result.version>=5?740*16:520*4;
  WriteMemory(gBase+0x90598,&columns,4);WriteMemory(gBase+0x90540,&bytes,4);return 1;
}
static uint32_t __cdecl Checksum(const void*,int) {return gChecksum;}
static void __fastcall NoSteam(void*,void*) {}
static void Call(uint32_t rva,void* target) {
  auto* at=gBase+rva;Require(at[0]==0xe8,"Expected callsite");
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(at+5));WriteMemory(at+1,&relative,4);
}
static void SameLayers(const dxb::NormalizedSave& a,const dxb::NormalizedSave& b) {
  Require(a.records.size()==b.records.size(),"Saved records lost");
  for(size_t i=0;i<a.records.size();i++) {
    Require(a.records[i].key==b.records[i].key,"Save key changed");
    Require(a.records[i].players==b.records[i].players,"Saved player lost");
    Require(a.records[i].layerChecksums==b.records[i].layerChecksums,"Native save/load lost brick state or columns");
  }
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=4){std::cerr<<"Usage: save-harness.exe DXBall2.exe input-copy.dat output-copy.dat\n";return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    Require(std::filesystem::weakly_canonical(argv[2])!=std::filesystem::weakly_canonical(argv[3]),"Use separate input/output fixtures");
    Require(!std::filesystem::exists(argv[3]),"Output fixture already exists");
    const auto source=Read(argv[2]);const auto expected=dxb::NormalizeSave(source.data(),source.size());
    for(const auto& record:expected.records)std::cout<<"Recovered record: "<<(record.key.empty()?"<empty>":record.key)<<", "<<record.players<<" players, "<<record.layers<<" layers\n";
    Require(VerifiedGame(argv[1]),"Unsupported executable");
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));Require(gBase!=nullptr,"Could not map game");
    ApplyGridPatches(gBase);
    JumpTo(gBase,0x111f9a,reinterpret_cast<void*>(&std::malloc));JumpTo(gBase,0x111f95,reinterpret_cast<void*>(&std::free));
    JumpTo(gBase,0x111d6a,reinterpret_cast<void*>(&std::rand));
    JumpTo(gBase,0x111d8b,reinterpret_cast<void*>(&std::srand));
    JumpTo(gBase,0xd6d9c,reinterpret_cast<void*>(&std::malloc));JumpTo(gBase,0xd6dcf,reinterpret_cast<void*>(&std::free));
    JumpTo(gBase,0xe6280,reinterpret_cast<void*>(&std::memcpy));JumpTo(gBase,0xe5a70,reinterpret_cast<void*>(&std::memmove));JumpTo(gBase,0xe6060,reinterpret_cast<void*>(&std::memset));
    JumpTo(gBase,0xf2c97,reinterpret_cast<void*>(&HarnessOpenFile));JumpTo(gBase,0x116579,reinterpret_cast<void*>(&std::fseek));
    JumpTo(gBase,0x116ba1,reinterpret_cast<void*>(&std::ftell));JumpTo(gBase,0x116bac,reinterpret_cast<void*>(&std::rewind));
    JumpTo(gBase,0x1160fc,reinterpret_cast<void*>(&std::fread));JumpTo(gBase,0xf2e1d,reinterpret_cast<void*>(&std::fclose));
    JumpTo(gBase,0xf31b7,reinterpret_cast<void*>(&std::fwrite));JumpTo(gBase,0x90bd0,reinterpret_cast<void*>(&NoSteam));
    Call(0x8ff2e,reinterpret_cast<void*>(&Decode));Call(0x8ffbf,reinterpret_cast<void*>(&Checksum));
    const uint8_t version=6;WriteMemory(gBase+0x90060,&version,1);WriteMemory(gBase+0x91655,&version,1);
    using Constructor=void* (__thiscall*)(void*);using Load=bool (__thiscall*)(void*,const std::wstring*);using Save=bool (__thiscall*)(void*);
    std::array<uint8_t,0x80> manager{};
    std::cerr<<"Native manager constructor\n";
    reinterpret_cast<Constructor>(gBase+0x488d0)(manager.data());
    std::cerr<<"Native manager load\n";
    const std::wstring input=argv[2];Require(reinterpret_cast<Load>(gBase+0x8fe00)(manager.data(),&input),"Original save manager rejected fixture");
    Require(*reinterpret_cast<uint32_t*>(manager.data()+4)==expected.records.size(),"Native record count mismatch");
    std::cerr<<"Native manager save\n";
    *reinterpret_cast<std::wstring*>(manager.data()+0x34)=argv[3];
    Require(reinterpret_cast<Save>(gBase+0x915c0)(manager.data()),"Original save writer failed");
    const auto roundtrip=Read(argv[3]);const auto actual=dxb::NormalizeSave(roundtrip.data(),roundtrip.size());
    Require(actual.version==6,"Writer did not identify expanded save version");SameLayers(expected,actual);
    std::array<uint8_t,0x80> reloaded{};reinterpret_cast<Constructor>(gBase+0x488d0)(reloaded.data());
    const std::wstring output=argv[3];Require(reinterpret_cast<Load>(gBase+0x8fe00)(reloaded.data(),&output),"Native reload failed");
    Require(*reinterpret_cast<uint32_t*>(reloaded.data()+4)==expected.records.size(),"Reloaded records lost");
    std::cout<<"PASS: original native save manager loaded, rewrote and reloaded all "<<expected.records.size()<<" records; every saved brick-state layer preserved\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
