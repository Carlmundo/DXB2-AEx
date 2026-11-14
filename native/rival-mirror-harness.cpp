// Executes Rival Ball's original reflection routine without starting the game.
// Its drawing/cell-setter call is redirected to a local grid; no imports,
// settings, saves, audio, networking or game entry point are executed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include "board-format.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

static std::array<uint32_t,740> gCells{};
static uint32_t gBoardIndex=0;
static auto* gGrid=reinterpret_cast<uint8_t*>(gCells.data());
static size_t gWrites=0;
static void __cdecl SetCell(int x,int y,uint32_t id) {
  if(x<0||x>=37||y<0||y>=20)throw std::runtime_error("Original routine wrote outside grid");
  std::memcpy(gGrid+(x*20+y)*4,&id,4);gWrites++;
}
static dxb::Bytes Read(const std::filesystem::path& file) {
  std::ifstream stream(file,std::ios::binary);
  if(!stream)throw std::runtime_error("Could not open input");
  return dxb::Bytes(std::istreambuf_iterator<char>(stream),{});
}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static void VerifyRival(const dxb::Bytes& source) {
  constexpr uint8_t expected[]={0x82,0x04,0x6f,0xe3,0x07,0x03,0x75,0xd4,0x4a,0x0f,0xcf,0x15,0xf0,0xf6,0x32,0xa0,0xfd,0x2a,0xf9,0xbe,0x99,0x6d,0x44,0x2d,0xa5,0xe2,0x5f,0x8c,0x4d,0xfb,0x7d,0x02};
  BCRYPT_ALG_HANDLE algorithm=nullptr;uint8_t digest[32];
  Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Could not initialize SHA-256");
  const auto status=BCryptHash(algorithm,nullptr,0,const_cast<uint8_t*>(source.data()),static_cast<ULONG>(source.size()),digest,32);
  BCryptCloseAlgorithmProvider(algorithm,0);
  Require(status>=0&&!std::memcmp(digest,expected,32),"Unsupported Rival Ball executable hash");
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=3){std::cerr<<"Usage: rival-mirror-harness.exe RivalBall.exe Boards-folder\n";return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try {
    const auto executable=Read(argv[1]);VerifyRival(executable);
    // This old executable cannot always be mapped by the modern Windows
    // loader. Copy only its 262-byte routine. Relocate the two data references
    // and redirect its single helper call; leave its decision/loop code intact.
    constexpr size_t routineSize=0x106;
    auto* routine=static_cast<uint8_t*>(VirtualAlloc(nullptr,routineSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Require(routine!=nullptr,"Could not allocate isolated routine");
    std::memcpy(routine,executable.data()+0x18ef0,routineSize);
    constexpr uint8_t expected[]={0x51,0x53,0x55,0x8b,0x2d,0xc8,0xe1,0x5f,0x00};
    Require(!std::memcmp(routine,expected,sizeof(expected)),"Unexpected reflection routine bytes");
    size_t indices=0,grids=0;
    for(size_t i=0;i+4<=routineSize;i++) {
      const auto address=dxb::U32(routine+i);uint32_t replacement=0;
      if(address==0x5fe1c8){replacement=reinterpret_cast<uintptr_t>(&gBoardIndex);indices++;}
      if(address==0x59627c){replacement=reinterpret_cast<uintptr_t>(gGrid);grids++;}
      if(replacement){std::memcpy(routine+i,&replacement,4);i+=3;}
    }
    Require(indices==4&&grids==2,"Unexpected original data references");
    constexpr size_t call=0x18fdb-0x18ef0;
    Require(routine[call]==0xe8&&dxb::U32(routine+call+1)==0x84d0,"Unexpected original helper call");
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SetCell)-reinterpret_cast<uintptr_t>(routine+call+5));
    std::memcpy(routine+call+1,&relative,4);DWORD old;
    Require(VirtualProtect(routine,routineSize,PAGE_EXECUTE_READ,&old)!=0,"Could not protect isolated routine");
    FlushInstructionCache(GetCurrentProcess(),routine,routineSize);
    const auto reflect=reinterpret_cast<void (__cdecl*)()>(routine);
    size_t sets=0,boards=0,mirrored=0;
    for(const auto& item:std::filesystem::recursive_directory_iterator(argv[2])) {
      if(!item.is_regular_file()||item.path().extension()!=L".bdz")continue;
      const auto source=Read(item.path());const auto result=dxb::Normalize(source.data(),source.size());
      if(!result.rival)continue;
      const auto raw=dxb::Unwrap(source.data(),source.size());dxb::Reader input{raw};input.take(20);
      for(int i=0;i<5;i++)input.string(true);
      dxb::Reader output{result.bytes};output.take(16);
      output.string();output.string();output.u32();output.string();output.u32();output.string();
      for(int i=0;i<4;i++)output.string();output.u32();output.string();output.string();
      size_t reflected=0;
      for(uint32_t i=0;i<result.boards;i++) {
        input.u32();const auto* cells=input.take(2960);input.take(256);
        output.u32();for(int j=0;j<3;j++)output.string();Require(output.u32()==1,"Unexpected layer count");
        const auto* adapted=output.take(2960);
        std::memcpy(gGrid,cells,2960);gWrites=0;reflect();
        if(std::memcmp(gGrid,adapted,2960)) {
          std::cerr<<item.path().filename().string()<<" board "<<i+1<<": original/adapter mismatch\n";return 1;
        }
        reflected+=gWrites!=0;boards++;
      }
      Require(reflected==result.mirroredBoards,"Reflection count mismatch");
      mirrored+=reflected;sets++;
      std::cout<<result.title<<": "<<result.boards<<" boards matched original routine, "<<reflected<<" reflected\n";
    }
    std::cout<<"PASS: "<<sets<<" sets, "<<boards<<" boards byte-for-byte matched original Rival Ball routine; "<<mirrored<<" reflected\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
