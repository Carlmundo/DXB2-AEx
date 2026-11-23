// Run native brick damage, falling, zap and multiply updates in a mapped executable.
// Presentation, Lua and score callbacks are replaced; no live saves are used.
#include "brick-behaviour.hpp"
#include "verified-game.hpp"
#include "geometry.hpp"
#include <array>
#include <vector>

static uint8_t* gBase=nullptr;
static int gScores=0,gPoints=0,gReveals=0;
static bool gSplash=false;
template<class T> static T& At(uint32_t rva) {return *reinterpret_cast<T*>(gBase+rva);}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
    std::fprintf(stderr,"Native brick fault at RVA %08X, access %lu at %p\n",
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gBase)),
      static_cast<unsigned long>(fault->ExceptionRecord->ExceptionInformation[0]),reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]));
    auto* stack=reinterpret_cast<uint32_t*>(fault->ContextRecord->Esp);
    for(int i=0;i<100;i++)if(stack[i]>=reinterpret_cast<uintptr_t>(gBase)&&stack[i]<reinterpret_cast<uintptr_t>(gBase)+0x15e000)
      std::fprintf(stderr,"  stack +%X: RVA %08X\n",i*4,stack[i]-static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gBase)));
    TerminateProcess(GetCurrentProcess(),3);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
static int __fastcall Margin(void*,void*) {return 0;}
static bool __fastcall NoScript(void*,void*) {return false;}
static bool __fastcall SplashEligible(void*,void*,void*) {return gSplash;}
static void __cdecl Dirty(int,int) {}
static int __cdecl Points(void*) {return 123;}
static void __cdecl Score(int points,int,int) {++gScores;gPoints+=points;if(points==2)++gReveals;}
static void __cdecl Sound(int,float,int,int) {}
static void __cdecl Drop(int,int,int,int) {}
static void* __cdecl NoAllocation(size_t,void*) {return nullptr;}
static int __cdecl Random() {return 0;}
static int __fastcall Variant(void*,void*,int,int,int) {return 1;}
static int __fastcall VariantData(void*,void*,int) {return 0;}

struct Result {
  int id,scores,points,reveals,remaining,broken;
  bool operator==(const Result& b) const {
    return id==b.id&&scores==b.scores&&points==b.points&&reveals==b.reveals&&remaining==b.remaining&&broken==b.broken;
  }
};
static std::array<uint8_t,0x88> gObject{};
static std::vector<uint8_t> gBoard(0x5c),gLayers(3*0x2e54);
static std::array<uint32_t,10*9> gDefinitions{};
static uint32_t* Cell(int x,int y,int layer,int stride) {
  return reinterpret_cast<uint32_t*>(gLayers.data()+layer*stride+4+(x*20+y)*16);
}
static void Prepare(int width,int stride) {
  std::fill(gLayers.begin(),gLayers.end(),uint8_t{0});
  *reinterpret_cast<void**>(gBoard.data())=gBase+0x1bc708;
  *reinterpret_cast<void**>(gBoard.data()+8)=gLayers.data();
  *reinterpret_cast<void**>(gBoard.data()+12)=gLayers.data()+3*stride;
  At<void*>(0x1bc708)=gBoard.data();At<int>(0x1bedb0)=0;At<int>(0x1bedb4)=width;
  At<int>(0x1bc708+0x26a8)=0;At<int>(0x1bc708+0x26ac)=width;
  At<void*>(0x1bc708+0x2764)=gDefinitions.data();
  At<void*>(0x1bc708+0x2768)=gDefinitions.data()+gDefinitions.size();
  for(int i=0;i<3;i++)*reinterpret_cast<void**>(gLayers.data()+i*stride)=gBoard.data();
}
static void Hit(int x,int y,int layer,int objectType,bool fire) {
  gObject.fill(0);*reinterpret_cast<int*>(gObject.data()+0x40)=objectType;
  At<int>(0x1a70e0)=fire;
  using HitBrick=void (__cdecl*)(int,int,int,void*);
  reinterpret_cast<HitBrick>(gBase+0x85b90)(x,y,layer,gObject.data());
}
static Result Run(int width,int stride,int layer,int x,int y,int brickType,int objectType,bool fire,bool hidden,int hits=1) {
  Prepare(width,stride);
  // Use ID 1, changing its native brick definition independently of cell flags.
  gDefinitions[9]=brickType;
  auto* cell=Cell(x,y,layer,stride);cell[1]=1;cell[2]=hidden?2:0;
  // A sentinel in another layer checks that native addressing stays local.
  auto* sentinel=Cell(x,y,(layer+1)%3,stride);sentinel[1]=1;sentinel[2]=2;
  At<int>(0x1a7124)=50;At<int>(0x1a712c)=0;At<int>(0x1a7128)=0;
  At<int>(0x1a70dc)=0;At<int>(0x1a70f0)=0;At<int>(0x1a7050)=0;
  At<uint8_t>(0x1bef94)=0;At<int>(0x1a4104)=0;
  gScores=gPoints=gReveals=0;
  for(int i=0;i<hits;i++)Hit(x,y,layer,objectType,fire);
  Require(sentinel[1]==1&&sentinel[2]==2,"Hit damaged a different layer");
  return {static_cast<int>(cell[1]),gScores,gPoints,gReveals,At<int>(0x1a7124),At<int>(0x1a712c)};
}
static int Splash(int width,int stride) {
  Prepare(width,stride);gDefinitions[9]=0;
  for(int x=1;x<=3;x++)for(int y=1;y<=3;y++) {
    auto* cell=Cell(x,y,1,stride);cell[1]=1;cell[2]=2;
  }
  At<void*>(0x1a4118)=nullptr;
  At<int>(0x1a7124)=50;At<int>(0x1a712c)=0;
  gSplash=true;Hit(2,2,1,1,true);gSplash=false;
  int remaining=0;
  for(int x=1;x<=3;x++)for(int y=1;y<=3;y++)remaining+=Cell(x,y,1,stride)[1]!=0;
  return remaining;
}

// Exercise the native pending-fall update using the same mapped game.
namespace falling_tests {
static void* gEntry=nullptr;
static int gStride=0x2094,gWidth=26,gLayerCount=3,gDraws=0;
static bool gCheckDraws=false;
static std::array<uint8_t,0x5c> gBoard{};
static std::array<uint8_t,0x20> gTree{};
static std::vector<uint8_t> gStorage,gExpected;
static uint32_t* Cell(std::vector<uint8_t>& storage,int layer,int x,int y) {
  return reinterpret_cast<uint32_t*>(storage.data()+32+layer*gStride+4+(x*20+y)*16);
}
static void __cdecl Dirty(int x,int y) {
  ++gDraws;
  if(gCheckDraws) {
    // Native dirties destination first, source second. The destination must
    // already have the moved brick's hidden bit when the redraw is requested.
    for(int layer=0;layer<gLayerCount;layer++) {
      auto* actual=Cell(gStorage,layer,x,y);
      auto* expected=Cell(gExpected,layer,x,y);
      if(actual[1]==expected[1])Require((actual[2]&2)==(expected[2]&2),"Redraw observed the wrong hidden state");
    }
  }
}
static __declspec(naked) void Execute() {
  __asm {
    push ebp
    mov ebp,esp
    sub esp,200h
    push ebx
    push esi
    push edi
    call dword ptr [gEntry]
    pop edi
    pop esi
    pop ebx
    mov esp,ebp
    pop ebp
    ret
  }
}
static void Prepare(int width,int stride,int layers) {
  gWidth=width;gStride=stride;gLayerCount=layers;gDraws=0;
  gStorage.assign(64+layers*stride,0xa5);
  std::memset(gStorage.data()+32,0,layers*stride);
  gBoard.fill(0);gTree.fill(0);
  *reinterpret_cast<void**>(gBoard.data())=gBase+0x1bc708;
  *reinterpret_cast<void**>(gBoard.data()+8)=gStorage.data()+32;
  *reinterpret_cast<void**>(gBoard.data()+12)=gStorage.data()+32+layers*stride;
  *reinterpret_cast<void**>(gTree.data()+4)=gTree.data();gTree[0x0d]=1;
  At<void*>(0x1cf248)=gTree.data();
  At<void*>(0x1bc708)=gBoard.data();At<int>(0x1bedb0)=0;
  At<int>(0x1bedb4)=width;At<int>(0x1bc708+0x26a8)=0;
  At<int>(0x1a70fc)=1;At<int>(0x1a7100)=100;
  At<int>(0x1a7124)=123;At<int>(0x1a712c)=7;
  for(int layer=0;layer<layers;layer++)
    *reinterpret_cast<void**>(gStorage.data()+32+layer*stride)=gBoard.data();
}
static void Model(bool moveHidden) {
  for(int layer=0;layer<gLayerCount;layer++)for(int y=18;y>=0;y--)for(int x=0;x<gWidth;x++) {
    auto* source=Cell(gExpected,layer,x,y);
    auto* destination=Cell(gExpected,layer,x,y+1);
    if(source[1]&& !destination[1]) {
      destination[1]=source[1];source[1]=0;
      if(moveHidden) {destination[2]=(destination[2]&~2u)|(source[2]&2u);source[2]&=~2u;}
    }
  }
}
static void Fall(bool pending=true) {
  At<int>(0x1a70fc)=pending;
  gEntry=gBase+0x5c473;Execute();
  Require(At<int>(0x1a70fc)==0,"Fall trigger was not consumed");
  Require(At<int>(0x1a7124)==123&&At<int>(0x1a712c)==7,"Falling changed brick counters");
}
static void ReproduceOriginal() {
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
  const uint8_t done=0xc3;WriteMemory(gBase+0x5c795,&done,1);
  Prepare(26,0x2094,1);
  Cell(gStorage,0,5,4)[1]=10;Cell(gStorage,0,5,5)[1]=11;Cell(gStorage,0,5,5)[2]=2;
  Fall();
  Require(Cell(gStorage,0,5,5)[1]==10&&(Cell(gStorage,0,5,5)[2]&2),"Original normal-brick invisibility bug not reproduced");
  Require(Cell(gStorage,0,5,6)[1]==11&&!(Cell(gStorage,0,5,6)[2]&2),"Original hidden-brick reveal bug not reproduced");
  std::puts("PASS: original Falling Bricks leaves invisibility behind and hides the brick above");
}
static void RunPatched() {
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
  int cases=0;
  for(int width:{20,26,37})for(int layers:{1,3})for(int seed=0;seed<12;seed++) {
    Prepare(width,0x2e54,layers);
    for(int layer=0;layer<layers;layer++)for(int x=0;x<width;x++)for(int y=0;y<20;y++) {
      auto* cell=Cell(gStorage,layer,x,y);
      const int value=seed+layer*17+x*31+y*7;
      cell[0]=0x12340000u+value;cell[3]=0x43210000u+value;
      // Include gaps, stacks, revealed bricks, hidden empty cells, and a
      // completely blocked board; unrelated flag bits remain positional.
      cell[1]=(seed==0||value%5<3)?1+value:0;
      cell[2]=0xa5a50001u|((value%3==0)?2u:0u);
    }
    gExpected=gStorage;Fall(false);Require(gStorage==gExpected,"Board moved without a paddle-hit trigger");
    for(int step=0;step<6;step++) {
      Model(true);gCheckDraws=true;Fall();gCheckDraws=false;
      Require(gStorage==gExpected,"Falling changed IDs, hidden states, unrelated cell data, layer metadata or guard bytes");++cases;
    }
  }
  std::printf("PASS: %d native fall updates across standard/Wide/Rival boards, repeated falls, hidden/revealed stacks, blocked cells, bottom row and multiple layers\n",cases);
  std::puts("PASS: native redraw sees transferred invisibility; unrelated flags, metadata, counters and guard bytes preserved");
}
}

namespace zap_tests {
struct Expected {std::vector<uint8_t> cells;bool changed;int draws;};
static std::vector<Expected> gExpected;
static int gDraws=0;
static bool gCheckReveal=false;
static int gWidth=26,gStride=0x2094,gMask=0;
static int __fastcall ZappedVariant(void*,void*,int,int) {return 1;}
static void __cdecl Dirty(int,int) {
  ++gDraws;
  if(gCheckReveal)for(int layer=0;layer<3;layer++)if(gMask&(1<<layer))
    for(int x=0;x<gWidth;x++)for(int y=0;y<20;y++)
      Require(!(Cell(x,y,layer,gStride)[2]&2),"Zap redraw observed an invisible brick");
}
static void PrepareZap(int width,int stride,int mask,int mode) {
  Prepare(width,stride);gWidth=width;gStride=stride;gMask=mask;
  gDefinitions.fill(0);
  gDefinitions[2*9]=2;gDefinitions[3*9]=3;
  gDefinitions[4*9]=gDefinitions[5*9]=4;
  gDefinitions[7*9]=7;gDefinitions[8*9]=9;
  for(int layer=0;layer<3;layer++) {
    gLayers[layer*stride+stride-16]=(mask>>layer)&1;
    for(int x=0;x<width;x++)for(int y=0;y<20;y++) {
      auto* cell=Cell(x,y,layer,stride);
      const auto value=static_cast<uint32_t>(layer*740+x*20+y);
      cell[0]=0x12340000u+value;cell[3]=0x43210000u+value;
      cell[1]=mode==2?value%10:mode==3?0:1;
      cell[2]=0xa5a50001u|((mode!=1&&(mode==3||value%2))?2u:0u);
    }
  }
  At<int>(0x1a7124)=123;At<int>(0x1a712c)=7;At<int>(0x1a70b8)=1;
  gScores=gPoints=gReveals=0;
}
static bool HiddenCells() {
  for(int layer=0;layer<3;layer++)if(gMask&(1<<layer))
    for(int x=0;x<gWidth;x++)for(int y=0;y<20;y++)
      if(Cell(x,y,layer,gStride)[2]&2)return true;
  return false;
}
static bool Zap() {
  gDraws=0;
  reinterpret_cast<void (__cdecl*)()>(gBase+0x8c1e0)();
  Require(At<int>(0x1a7124)==123&&At<int>(0x1a712c)==7,"Zap changed brick counters");
  Require(gScores==0&&gPoints==0&&gReveals==0,"Zap awarded hit or destruction points");
  Require(gDraws==0||gDraws==gWidth*20,"Zap did not retain its native board redraw behavior");
  return gDraws!=0;
}
static void ReproduceOriginal() {
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
  PrepareZap(26,0x2094,7,0);
  Require(!Zap()&&HiddenCells(),"Original Zap Bricks invisibility bug not reproduced");
  std::puts("PASS: original Zap Bricks leaves hidden normal bricks invisible without requesting a redraw");
}
static void CaptureReferences() {
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
  JumpTo(gBase,0x56b20,reinterpret_cast<void*>(&ZappedVariant));
  // Capture the original native weakening behavior before installing the fix.
  for(int width:{20,26,37})for(int mask:{0,1,2,5,7})for(int mode=0;mode<4;mode++) {
    PrepareZap(width,0x2e54,mask,mode);
    const bool hidden=HiddenCells();
    for(int step=0;step<2;step++) {
      const bool changed=Zap()||(step==0&&hidden);
      auto cells=gLayers;
      for(int layer=0;layer<3;layer++)if(mask&(1<<layer))
        for(int x=0;x<width;x++)for(int y=0;y<20;y++)
          *reinterpret_cast<uint32_t*>(cells.data()+layer*0x2e54+4+(x*20+y)*16+8)&=~2u;
      gExpected.push_back({std::move(cells),changed,changed?width*20:0});
    }
  }
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&::Dirty));
  JumpTo(gBase,0x56b20,reinterpret_cast<void*>(&Variant));
}
static void RunPatched() {
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
  JumpTo(gBase,0x56b20,reinterpret_cast<void*>(&ZappedVariant));
  gCheckReveal=true;size_t index=0;
  for(int width:{20,26,37})for(int mask:{0,1,2,5,7})for(int mode=0;mode<4;mode++) {
    PrepareZap(width,0x2e54,mask,mode);
    for(int step=0;step<2;step++) {
      const auto& expected=gExpected[index++];
      const bool changed=Zap();
      if(changed!=expected.changed||gDraws!=expected.draws)
        std::fprintf(stderr,"Zap mismatch: width %d, mask %d, mode %d, step %d; changed %d/%d, draws %d/%d\n",width,mask,mode,step,changed,expected.changed,gDraws,expected.draws);
      Require(changed==expected.changed&&gDraws==expected.draws,"Zap reveal did not request the expected redraw");
      Require(gLayers==expected.cells,"Zap changed native weakening, unrelated flags, opaque cell data or inactive layers");
    }
  }
  gCheckReveal=false;
  std::printf("PASS: %zu native zap cases across all board widths, mixed brick types, visible/inactive layers and repeated pickups; invisibility revealed and native weakening preserved\n",index);
  PrepareZap(26,0x2e54,7,0);Zap();
  Hit(5,6,1,1,false);
  Require(Cell(5,6,1,0x2e54)[1]==0&&gReveals==0&&gScores==1,"Zapped brick still required a reveal hit before destruction");
  std::puts("PASS: a normal ball destroys a previously hidden, zapped brick in one hit");
}
}

namespace multiply_tests {
static void* gEntry=nullptr;
static std::vector<uint8_t> gCopy;
static std::vector<std::vector<uint8_t>> gExpected;
static std::array<bool,740> gTouched{};
static int gLayer=0,gDraws=0;
static bool gCheckReveal=false;
// Replace only the temporary board-copy allocation, retaining the native
// pickup's snapshot reads, neighbourhood traversal, layer selection and writes.
static void* __fastcall CopyBoard(void* destination,void*,void* source) {
  std::memcpy(destination,source,0x5c);
  auto** layers=reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(source)+8);
  gCopy.assign(layers[0],layers[1]);
  auto** copy=reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(destination)+8);
  copy[0]=gCopy.data();copy[1]=copy[2]=gCopy.data()+gCopy.size();
  return destination;
}
static void __cdecl Dirty(int x,int y) {
  ++gDraws;gTouched[x*20+y]=true;
  auto* cell=falling_tests::Cell(falling_tests::gStorage,gLayer,x,y);
  if(gCheckReveal&&gDefinitions[cell[1]*9]!=9)
    Require(!(cell[2]&2),"Multiply redraw observed a hidden replacement brick");
}
static __declspec(naked) void Execute() {
  __asm {
    push ebp
    mov ebp,esp
    sub esp,100h
    push ebx
    push esi
    push edi
    call dword ptr [gEntry]
    pop edi
    pop esi
    pop ebx
    mov esp,ebp
    pop ebp
    ret
  }
}
static void PrepareMultiply(int width,int stride,int layer,int x,int y,bool hidden) {
  falling_tests::Prepare(width,stride,3);gLayer=layer;gTouched.fill(false);
  gDefinitions.fill(0);gDefinitions[2*9]=1;
  At<void*>(0x1bee6c)=gDefinitions.data();At<void*>(0x1bee70)=gDefinitions.data()+gDefinitions.size();
  for(int z=0;z<3;z++) {
    falling_tests::gStorage[32+z*stride+stride-16]=1;
    for(int col=0;col<width;col++)for(int row=0;row<20;row++) {
      auto* cell=falling_tests::Cell(falling_tests::gStorage,z,col,row);
      const auto value=static_cast<uint32_t>(z*740+col*20+row);
      cell[0]=0x12340000u+value;cell[3]=0x43210000u+value;
      cell[1]=value%5<2?1:0;cell[2]=0xa5a50001u|(hidden?2u:0u);
    }
  }
  // Keep this exploding source exposed through every layer above it.
  for(int z=layer+1;z<3;z++)falling_tests::Cell(falling_tests::gStorage,z,x,y)[1]=0;
  falling_tests::Cell(falling_tests::gStorage,layer,x,y)[1]=2;
  gScores=gPoints=gReveals=0;
}
static void Multiply() {
  gEntry=gBase+0x836b5;gDraws=0;Execute();
  Require(At<int>(0x1a7124)==123&&At<int>(0x1a712c)==7&&gScores==0,"Multiply changed hit/destruction accounting");
  Require(gDraws>0,"Multiply did not update any neighbours");
}
static void PrepareSwitches(int width,int stride,int layer,int x,int y,bool hidden) {
  PrepareMultiply(width,stride,layer,x,y,hidden);
  gDefinitions[3*9]=gDefinitions[8*9]=9;
  for(int col=x-1;col<=x+1;col++)for(int row=y-1;row<=y+1;row++) {
    if(col<0||col>=width||row<0||row>=20||(col==x&&row==y))continue;
    auto* cell=falling_tests::Cell(falling_tests::gStorage,layer,col,row);
    // Different IDs for switches, plus ID 9 with a normal definition, catch
    // accidental checks of the ID rather than the brick definition's type.
    cell[1]=(col+row)%3==0?9:(col+row)%3==1?3:8;
  }
}
static void ReproduceOriginal() {
  JumpTo(gBase,0x53ab0,reinterpret_cast<void*>(&CopyBoard));
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
  const uint8_t done=0xc3;WriteMemory(gBase+0x83e0a,&done,1);
  PrepareMultiply(26,0x2094,0,5,6,true);Multiply();
  auto* overwritten=falling_tests::Cell(falling_tests::gStorage,0,6,6);
  Require(overwritten[1]==2&&(overwritten[2]&2),"Original hidden Multiply Exploding replacement bug not reproduced");
  std::puts("PASS: original Multiply Exploding leaves replacement exploding bricks hidden");
  PrepareSwitches(26,0x2094,0,5,6,true);
  Require(falling_tests::Cell(falling_tests::gStorage,0,4,6)[1]==3,"Switch fixture has the wrong ID");
  Multiply();
  Require(falling_tests::Cell(falling_tests::gStorage,0,4,6)[1]==2,"Original switch conversion bug not reproduced");
  std::puts("PASS: original Multiply Exploding replaces a type-9 switch brick");
}
static void CaptureReferences() {
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
  for(int width:{20,26,37})for(int layer:{0,1,2})for(int x:{0,width/2,width-1})for(int y:{0,10,19})for(bool hidden:{false,true}) {
    PrepareMultiply(width,0x2e54,layer,x,y,hidden);
    for(int step=0;step<2;step++) {
      Multiply();auto expected=falling_tests::gStorage;
      for(int col=0;col<width;col++)for(int row=0;row<20;row++)if(gTouched[col*20+row])
        falling_tests::Cell(expected,layer,col,row)[2]&=~2u;
      gExpected.push_back(std::move(expected));
    }
  }
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&::Dirty));
}
static void RunPatched() {
  JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));gCheckReveal=true;size_t index=0;
  for(int width:{20,26,37})for(int layer:{0,1,2})for(int x:{0,width/2,width-1})for(int y:{0,10,19})for(bool hidden:{false,true}) {
    PrepareMultiply(width,0x2e54,layer,x,y,hidden);
    for(int step=0;step<2;step++) {
      Multiply();
      Require(falling_tests::gStorage==gExpected[index++],"Multiply changed native brick placement, unrelated hidden cells, other flags, layers or metadata");
    }
  }
  gCheckReveal=false;
  std::printf("PASS: %zu native Multiply Exploding cases; replacements visible before redraw, native placement preserved across widths, layers, edges and repeated pickups\n",index);
}
static void RunSwitches() {
  gCheckReveal=true;int cases=0;
  for(int width:{20,26,37})for(int layer:{0,1,2})for(int x:{0,width/2,width-1})for(int y:{0,10,19})for(bool hidden:{false,true}) {
    PrepareSwitches(width,0x2e54,layer,x,y,hidden);
    auto expected=falling_tests::gStorage;
    for(int col=x-1;col<=x+1;col++)for(int row=y-1;row<=y+1;row++) {
      if(col<0||col>=width||row<0||row>=20)continue;
      auto* cell=falling_tests::Cell(expected,layer,col,row);
      if(gDefinitions[cell[1]*9]==9)continue;
      cell[1]=2;cell[2]&=~2u;
    }
    Multiply();
    Require(falling_tests::gStorage==expected,"Multiply did not preserve switches while converting eligible neighbours");
    for(int step=0;step<2;step++) {
      Multiply();
      for(int col=0;col<width;col++)for(int row=0;row<20;row++) {
        auto* before=falling_tests::Cell(expected,layer,col,row);
        if(gDefinitions[before[1]*9]==9)
          Require(!std::memcmp(before,falling_tests::Cell(falling_tests::gStorage,layer,col,row),16),"Repeated Multiply changed switch ID, flags or metadata");
      }
    }
    ++cases;
  }
  gCheckReveal=false;
  std::printf("PASS: %d switch neighbourhoods across widths, layers, edges and hidden states; switch IDs, flags and metadata survive repeated Multiply pickups, normal ID 9 still converts\n",cases);
}
}

int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: brick-behaviour-tests.exe path-to-DXBall2.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game");gBase=reinterpret_cast<uint8_t*>(module);
    brick_behaviour::Verify(gBase);
    const uint8_t wrong=0xcc,original=gBase[0x85f40];WriteMemory(gBase+0x85f40,&wrong,1);
    bool rejected=false;try{brick_behaviour::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"Unexpected instruction was accepted");WriteMemory(gBase+0x85f40,&original,1);
    const uint8_t moveOriginal=gBase[0x5c725];WriteMemory(gBase+0x5c725,&wrong,1);
    rejected=false;try{brick_behaviour::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"Unexpected move call was accepted");
    Require(gBase[0x85f40]==original,"Failed move verification partially installed the damage fix");
    WriteMemory(gBase+0x5c725,&moveOriginal,1);
    const uint8_t zapOriginal=gBase[0x8c27b];WriteMemory(gBase+0x8c27b,&wrong,1);
    rejected=false;try{brick_behaviour::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"Unexpected zap instruction was accepted");
    Require(gBase[0x85f40]==original&&gBase[0x5c725]==moveOriginal,"Failed zap verification partially installed a fix");
    WriteMemory(gBase+0x8c27b,&zapOriginal,1);
    for(const auto rva:brick_behaviour::multiplyCalls) {
      const uint8_t before=gBase[rva];WriteMemory(gBase+rva,&wrong,1);
      rejected=false;try{brick_behaviour::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Unexpected multiply call was accepted");
      Require(gBase[0x85f40]==original&&gBase[0x5c725]==moveOriginal&&gBase[0x8c27b]==zapOriginal,"Failed multiply verification partially installed a fix");
      WriteMemory(gBase+rva,&before,1);
    }
    JumpTo(gBase,0x55a70,reinterpret_cast<void*>(&Margin));
    JumpTo(gBase,0x810d0,reinterpret_cast<void*>(&NoScript));
    JumpTo(gBase,0x538a0,reinterpret_cast<void*>(&SplashEligible));
    JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
    JumpTo(gBase,0x82c50,reinterpret_cast<void*>(&Points));
    JumpTo(gBase,0x81440,reinterpret_cast<void*>(&Score));
    JumpTo(gBase,0x89490,reinterpret_cast<void*>(&Sound));
    JumpTo(gBase,0x89520,reinterpret_cast<void*>(&Drop));
    JumpTo(gBase,0x864f0,reinterpret_cast<void*>(&Drop)); // Switch-brick script callback.
    JumpTo(gBase,0xd798d,reinterpret_cast<void*>(&NoAllocation));
    JumpTo(gBase,0x111d6a,reinterpret_cast<void*>(&Random));
    JumpTo(gBase,0x56b20,reinterpret_cast<void*>(&Variant));
    JumpTo(gBase,0x56e40,reinterpret_cast<void*>(&VariantData));
    auto originalExplosion=Run(26,0x2094,0,5,6,0,6,false,true);
    Require(originalExplosion.id==1&&originalExplosion.broken==0,"Original hidden explosion bug not reproduced");
    Require(Run(26,0x2094,0,5,6,0,6,false,false).id==0,"Original revealed explosion did not destroy brick");
    std::puts("PASS: original area damage reveals hidden bricks but destroys revealed bricks");
    Require(Splash(26,0x2094)==9,"Original fireball splash bug not reproduced");
    // Capture original behavior, including native special-brick protections.
    std::vector<Result> expected;
    for(int brick:{0,2,4,7,9})for(int object:{1,3,6,7,14})for(bool fire:{false,true})for(bool hidden:{false,true}) {
      const bool area=object==6||object==7||((object==1||object==3||object==14)&&fire);
      expected.push_back(Run(26,0x2094,0,5,6,brick,object,fire,hidden&&!area));
    }
    zap_tests::ReproduceOriginal();
    falling_tests::ReproduceOriginal();
    multiply_tests::ReproduceOriginal();
    JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
    ApplyGridPatches(gBase);
    zap_tests::CaptureReferences();
    multiply_tests::CaptureReferences();
    brick_behaviour::Install(gBase);
    Geometry geometry;geometry.base=gBase;geometry.Install();
    int cases=0;
    for(int width:{20,26,37}) {
      geometry.Set(width==37);
      Require(Splash(width,0x2e54)==0,"Fireball splash left hidden neighbors alive");
      Require(At<int>(0x1a7124)==41&&At<int>(0x1a712c)==9,"Fireball splash counted brick destruction incorrectly");
      for(int layer:{0,1,2})for(int x:{0,width-1})for(int y:{0,19}) {
        size_t index=0;
        for(int brick:{0,2,4,7,9})for(int object:{1,3,6,7,14})for(bool fire:{false,true})for(bool hidden:{false,true}) {
          const auto actual=Run(width,0x2e54,layer,x,y,brick,object,fire,hidden);
          Require(actual==expected[index++],"Patched damage differs from native revealed/normal-hit behavior");++cases;
        }
        for(int object:{1,3}) {
          const auto first=Run(width,0x2e54,layer,x,y,0,object,false,true);
          Require(first.id==1&&first.broken==0,"Normal first hit destroyed hidden brick");
          const auto second=Run(width,0x2e54,layer,x,y,0,object,false,true,2);
          Require(second.id==0&&second.broken==1,"Normal second hit failed to destroy revealed brick");
        }
      }
    }
    std::printf("PASS: %d native damage cases; hidden/revealed, balls/lasers/explosions/fireballs, special bricks, three layers and all board widths\n",cases);
    std::puts("PASS: native fireball splash destroys all nine hidden bricks and updates the board counters once per brick");
    falling_tests::RunPatched();
    zap_tests::RunPatched();
    multiply_tests::RunPatched();
    multiply_tests::RunSwitches();
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
