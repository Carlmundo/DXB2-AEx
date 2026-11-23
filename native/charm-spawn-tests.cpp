// Execute the original Charm search and eligibility gate in a private game
// mapping. Only random selection/allocation/construction are supplied by the
// harness; no game session, Steam calls, settings or saves are opened.
#include "charm-spawn.hpp"
#include "geometry.hpp"
#include "verified-game.hpp"
#include "board-format.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <cmath>

static uint8_t* gBase=nullptr;
template<class T> static T& At(uint32_t rva) {return *reinterpret_cast<T*>(gBase+rva);}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
  std::fprintf(stderr,"Charm test fault %08lX at RVA %08X\n",fault->ExceptionRecord->ExceptionCode,
    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gBase)));
  TerminateProcess(GetCurrentProcess(),3);return EXCEPTION_CONTINUE_SEARCH;
}
static std::vector<uint8_t> gBoard(0x5c),gLayers(3*charm_spawn::layerBytes);
static std::array<uint32_t,9*5> gDefinitions{};
static std::array<uint8_t,0x88> gObject{};
static std::array<uint8_t,0x2c> gCharm{};
static int gRandomCalls=0,gSelected=0,gCreated=0,gStartX=5,gStartY=5;
static float gX=0,gY=0;
static int __cdecl Random() {return gRandomCalls++?gStartX:gStartY;}
static int __cdecl SelectCharm() {++gSelected;return 0;}
static void* __cdecl Allocate(size_t size,void*) {Require(size==gObject.size(),"Unexpected allocation");return gObject.data();}
static void* __fastcall Construct(void* object,void*,int type,int id,int flags,float x,float y,float vx,float vy) {
  Require(type==10&&id==0&&flags==0&&vx==0&&vy==0,"Native Charm parameters changed");
  ++gCreated;gX=x;gY=y;std::memset(object,0,gObject.size());return object;
}
static int __fastcall Margin(void* board,void*) {
  return (1280-PlayfieldWidth(*reinterpret_cast<int*>(static_cast<uint8_t*>(board)+0x26ac)))/2;
}
static uint32_t* Cell(int x,int y,int layer=0) {
  return reinterpret_cast<uint32_t*>(gLayers.data()+layer*charm_spawn::layerBytes+4+(x*20+y)*16);
}
static uint8_t* Layer(int layer) {return gLayers.data()+layer*charm_spawn::layerBytes;}
static void Prepare(int width) {
  gLayers.resize(3*charm_spawn::layerBytes);
  std::fill(gLayers.begin(),gLayers.end(),uint8_t{});gBoard.assign(0x5c,0);
  *reinterpret_cast<void**>(gBoard.data())=gBase+0x1bc708;
  *reinterpret_cast<void**>(gBoard.data()+8)=gLayers.data();
  *reinterpret_cast<void**>(gBoard.data()+12)=gLayers.data()+gLayers.size();
  At<void*>(0x1bc708)=gBoard.data();At<int>(0x1bedb0)=0;At<int>(0x1bedb4)=width;
  At<int>(0x1a4094)=1280;
  At<void*>(0x1bee6c)=gDefinitions.data();At<void*>(0x1bee70)=gDefinitions.data()+gDefinitions.size();
  // ID 1 is permanent, ID 2 ordinary, ID 3 switch, ID 4 permanent too.
  gDefinitions.fill(0);gDefinitions[9]=gDefinitions[36]=7;gDefinitions[27]=9;
  for(int layer=0;layer<3;layer++) {
    *reinterpret_cast<void**>(Layer(layer))=gBoard.data();Layer(layer)[0x2e44]=1;
  }
  At<void*>(0x1c78dc)=gCharm.data();At<void*>(0x1c78e0)=gCharm.data()+gCharm.size();
  At<void*>(0x1a4118)=nullptr;At<void*>(0x1a7128)=nullptr;
  At<int>(0x1a7124)=0;At<int>(0x1b91d0)=0;At<uint8_t>(0x1bef96)=1;
  gRandomCalls=gSelected=gCreated=0;gX=gY=-1;gStartX=gStartY=5;
}
static void Ring(int x,int y,int layer=0) {
  for(int dy=0;dy<2;dy++) {
    if(x>0)Cell(x-1,y+dy,layer)[1]=1;
    Cell(x+1,y+dy,layer)[1]=1;
  }
  Cell(x,y-1,layer)[1]=1;Cell(x,y+2,layer)[1]=1;
}
static void Run(bool gate=false) {
  const auto before=gLayers;
  reinterpret_cast<void (__cdecl*)()>(gBase+(gate?0x5cd82:0x5cda6))();
  Require(gLayers==before,"Spawn changed board bricks or metadata");
  Require(charm_spawn::active==nullptr,"Spawn retained stale reachability state");
}
static void Position(int width,int x,int y) {
  const float expectedX=static_cast<float>(Margin(gBase+0x1bc708,nullptr)+ScaleCoordinate(x*3,0))+
    (width==37?13.0f:24.0f);
  Require(gCreated==1&&gSelected==1,"Charm was not created/selected exactly once");
  Require(gX==expectedX&&gY==52+y*24,"Wrong Charm spawn position");
  Require(At<int>(0x1a7124)==10000,"Native successful-spawn counter changed");
}
static void SwitchItUp(const wchar_t* path) {
  std::ifstream input(std::filesystem::path(path),std::ios::binary);
  Require(input.is_open(),"Cannot read Switch It Up regression boardset");
  const dxb::Bytes encoded{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  const auto bytes=dxb::Unwrap(encoded.data(),encoded.size());dxb::Reader r{bytes};r.take(4);
  Require(r.u32()==9&&r.u32()==20&&r.u32()==26,"Unexpected Switch It Up boardset format");
  r.string();r.string();r.u32();r.string();r.u32();r.string();
  Require(r.string()=="Switch It Up","Wrong regression boardset");
  r.string();r.string();r.string();r.u32();r.string();r.string();
  std::vector<uint32_t> bricks;
  for(int board=0;board<12;board++) {
    r.u32();r.string();r.string();r.string();const auto layers=r.u32();
    if(board!=11){r.take(layers*520*4);continue;}
    Require(layers==5,"Unexpected board 12 layers");
    for(uint32_t i=0;i<layers*520;i++)bricks.push_back(r.u32());
  }
  // Preserve the real board's layout with fixture definitions: ClassicBrick
  // ID 137 is its permanent frame, ID 131 its switch, and the remaining IDs
  // are ordinary coloured bricks. Scripts are inspected above, never executed.
  const int offsets[]={0,3,7,15,19,22};int checked=0;
  for(int step=0;step<6;step++)for(bool cleared:{false,true}) {
    Prepare(26);gLayers.resize(5*charm_spawn::layerBytes,0);
    *reinterpret_cast<void**>(gBoard.data()+8)=gLayers.data();
    *reinterpret_cast<void**>(gBoard.data()+12)=gLayers.data()+gLayers.size();
    for(int layer=0;layer<5;layer++) {
      *reinterpret_cast<void**>(Layer(layer))=gBoard.data();Layer(layer)[0x2e44]=1;
      if(layer>=1&&layer<=3)*reinterpret_cast<int*>(Layer(layer)+0x2e48)=offsets[(step+2*(layer-1))%6];
      for(int x=0;x<26;x++)for(int y=0;y<20;y++) {
        const auto id=bricks[layer*520+x*20+y];
        Cell(x,y,layer)[1]=id==137?1:(id==131?3:(!cleared&&id?2:0));
      }
    }
    charm_spawn::Space space;space.Build();Require(space.enabled,"Board 12 did not enable reachability");
    using Empty=bool (__thiscall*)(void*,int,int);const auto empty=reinterpret_cast<Empty>(gBase+0x559d0);
    int tested=0;
    for(int x=0;x<26;x++)for(int y=0;y<15;y++) {
      if(space.Reachable(x,y)||space.Reachable(x,y+1)||!empty(gBase+0x1bc708,x,y)||!empty(gBase+0x1bc708,x,y+1))continue;
      gStartX=x;gStartY=y;gRandomCalls=gSelected=gCreated=0;
      // With no active snapshot, the second-cell hook defers to the original
      // check. Confirm that native search really selects this trapped space.
      reinterpret_cast<void (__cdecl*)()>(gBase+0x81840)();Position(26,x,y);
      At<void*>(0x1a4118)=nullptr;At<void*>(0x1a7128)=nullptr;
      gRandomCalls=gSelected=gCreated=0;Run();
      Require(gCreated==1&&gSelected==1,"Board 12 lost its Charm instead of choosing a reachable space");
      const int chosenX=static_cast<int>((gX-Margin(gBase+0x1bc708,nullptr)-24)/48);
      const int chosenY=static_cast<int>((gY-52)/24);
      Require(space.Reachable(chosenX,chosenY)&&space.Reachable(chosenX,chosenY+1),"Board 12 still spawned in a sealed frame");
      ++tested;++checked;
    }
    Require(tested>0,"Board 12 regression did not exercise a trapped spawn");
  }
  std::printf("PASS: %d real Switch It Up board 12 trapped-spawn regressions, five layers, all six switch positions, before/after clearing coloured bricks\n",checked);
}
int wmain(int argc,wchar_t** argv) {
  if(argc<2||argc>3){std::fprintf(stderr,"Usage: charm-spawn-tests.exe game.exe [classicscripts.bdz]\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Require(gBase!=nullptr,"Could not map executable");
    charm_spawn::Verify(gBase);
    std::array<uint8_t,5> original;std::memcpy(original.data(),gBase+0x5cda6,5);
    const uint8_t wrong=0xcc,correct=gBase[0x818dd];WriteMemory(gBase+0x818dd,&wrong,1);
    bool rejected=false;try{charm_spawn::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&!std::memcmp(original.data(),gBase+0x5cda6,5),"Verification failed after partially installing hooks");
    WriteMemory(gBase+0x818dd,&correct,1);
    ApplyGridPatches(gBase);
    Geometry geometry;geometry.base=gBase;geometry.Install();
    JumpTo(gBase,0x55a70,reinterpret_cast<void*>(&Margin));
    JumpTo(gBase,0x111d6a,reinterpret_cast<void*>(&Random));
    JumpTo(gBase,0x89140,reinterpret_cast<void*>(&SelectCharm));
    JumpTo(gBase,0xd798d,reinterpret_cast<void*>(&Allocate));
    JumpTo(gBase,0x489b0,reinterpret_cast<void*>(&Construct));
    const uint8_t done=0xc3;WriteMemory(gBase+0x5cdab,&done,1);
    Prepare(20);Ring(5,5);Run();Position(20,5,5);
    std::puts("PASS: original native search spawns inside a sealed six-brick pocket");
    charm_spawn::Install(gBase);
    int cases=0;
    for(int width:{20,26,37}) {
      geometry.Set(width==37);
      for(int layer=0;layer<3;layer++)for(bool hidden:{false,true}) {
        Prepare(width);Ring(5,5,layer);
        if(hidden)for(int x=0;x<width;x++)for(int y=0;y<20;y++)Cell(x,y,layer)[2]=2;
        Run();Position(width,7,5);++cases;
      }
      Prepare(width);Ring(0,5);gStartX=0;Run();Position(width,2,5);++cases;
      // One less than the threshold must preserve the native choice and RNG.
      Prepare(width);Ring(0,5);Cell(0,7)[1]=0;gStartX=0;Run();Position(width,0,5);
      Require(gRandomCalls==2,"Below-threshold search consumed extra random values");++cases;
      for(int count=0;count<=4;count++) {
        Prepare(width);for(int i=0;i<count;i++)Cell(i,0)[1]=4;
        charm_spawn::Space space;space.Build();Require(space.enabled==(count>=4),"Wrong permanent-brick threshold");
        Run();Position(width,5,5);Require(gRandomCalls==2,"Accessible choice changed native RNG");++cases;
      }
      // An ordinary-brick gate is a future path, although it is still excluded
      // by the native empty-space check as a direct spawn position.
      Prepare(width);Ring(5,5);Cell(5,7)[1]=2;Run();Position(width,5,5);++cases;
      // The same applies to a passage containing a hidden ordinary brick.
      Prepare(width);Ring(5,5);Cell(5,7)[1]=2;Cell(5,7)[2]=2;Run();Position(width,5,5);++cases;
      // Inactive layer bricks count toward the threshold but cannot enclose.
      Prepare(width);Ring(5,5,1);Layer(1)[0x2e44]=0;Run();Position(width,5,5);++cases;
      // Compose an enclosure across layers, even with an ordinary brick
      // covering a permanent brick in the topmost layer.
      Prepare(width);Ring(5,5,0);Cell(5,7,0)[1]=0;Cell(5,7,1)[1]=1;Cell(5,7,2)[1]=2;
      Run();Position(width,7,5);++cases;
      // Move a ring onto the candidate and then off it again, rebuilding the
      // map every attempt; also exercise native negative offsets and wrapping.
      for(bool wrap:{false,true}) {
        Prepare(width);Ring(7,7,1);*reinterpret_cast<int*>(Layer(1)+0x2e48)=-2;
        *reinterpret_cast<int*>(Layer(1)+0x2e4c)=-2;Layer(1)[0x2e50]=wrap;
        Run();Position(width,7,5);++cases;
      }
      Prepare(width);Ring(width-2,5,1);*reinterpret_cast<int*>(Layer(1)+0x2e48)=2;Layer(1)[0x2e50]=1;
      gStartX=0;Run();Position(width,2,5);++cases;
      // A top-row opening connects to the actual ceiling space, not a wall.
      Prepare(width);Ring(5,1);Cell(5,0)[1]=0;gStartY=1;Run();Position(width,5,1);++cases;
      // Force the top margin to be the only route from below to the target.
      Prepare(width);for(int y=0;y<20;y++)Cell(1,y)[1]=1;
      Cell(0,7)[1]=1;gStartX=0;Run();Position(width,0,5);++cases;
      // Reject a diagonal-only escape and preserve native retry behavior when
      // the sealed pocket is the only space the original search could accept.
      Prepare(width);for(int x=0;x<width;x++)for(int y=0;y<20;y++)Cell(x,y)[1]=2;
      Ring(5,5);Cell(5,5)[1]=Cell(5,6)[1]=0;Run();
      Require(!gCreated&&!gSelected&&At<int>(0x1a7124)==10,"Failed search spawned a trapped Charm or changed retries");++cases;
      // No snapshot survives a second attempt after a gate is destroyed.
      Cell(5,7)[1]=0;gRandomCalls=0;Run();Position(width,5,5);++cases;
      for(int disabled=0;disabled<4;disabled++) {
        Prepare(width);
        if(disabled==0)At<uint8_t>(0x1bef96)=0;
        if(disabled==1)At<int>(0x1b91d0)=1;
        if(disabled==2)At<void*>(0x1a7128)=gObject.data();
        if(disabled==3)At<int>(0x1a7124)=1;
        Run(true);Require(!gCreated&&!gRandomCalls,"Native spawn eligibility changed");++cases;
      }
      Prepare(width);Run(true);Position(width,5,5);++cases;
    }
    geometry.Set(false);if(argc==3)SwitchItUp(argv[2]);
    std::printf("PASS: %d native spawn cases; threshold, sealed/accessible pockets, corners, future paths, hidden bricks, layers/offsets/wrapping, ceiling route, retries, RNG and eligibility across 20/26/37 columns\n",cases);
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
