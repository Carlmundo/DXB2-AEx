// Executes selected original game routines in an isolated child process.
// It does not launch the game, edit its files, or access saves/settings.
#include "patch.hpp"
#include "verified-game.hpp"
#include "geometry.hpp"
#include <cstdlib>
#include <vector>
#include <set>
#include <cmath>

static uint8_t* gBase=nullptr;
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  std::fprintf(stderr,"Native fault %08lX at %p (game RVA %08lX)\n",
    fault->ExceptionRecord->ExceptionCode,fault->ExceptionRecord->ExceptionAddress,
    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gBase)));
  if(fault->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
    std::fprintf(stderr,"Access type %lu, address %p\n",static_cast<unsigned long>(fault->ExceptionRecord->ExceptionInformation[0]),reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]));
  std::fflush(stderr);return EXCEPTION_CONTINUE_SEARCH;
}
static void Require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
static std::set<std::pair<int,int>> gBorderPixels;
using NativeBorderPixel=void (__thiscall*)(void*,int,int);
static NativeBorderPixel gNativeBorderPixel=nullptr;
static void __fastcall BorderPixel(void* context,void*,int x,int y) {gBorderPixels.emplace(x,y);gNativeBorderPixel(context,x,y);}
static double __vectorcall BorderSin(double angle) {return std::sin(angle);}
static double __vectorcall BorderCos(double angle) {return std::cos(angle);}
static double __vectorcall BorderPow(double value,double exponent) {return std::pow(value,exponent);}
static double CheckBorder(int width) {
  using DrawBorder=void (__cdecl*)(void*,int,int,float,int,int,int);
  const auto draw=reinterpret_cast<DrawBorder>(gBase+0x89e60);
  std::vector<uint32_t> pixels(1280*128);
  uint32_t surface[]={4,0,0,1280*4,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pixels.data()))};
  double minimumGreen=255;
  for(int x:{0,160,1094})for(int frame=0;frame<64;frame++) {
    std::fill(pixels.begin(),pixels.end(),0);
    gBorderPixels.clear();draw(surface,x,100,static_cast<float>(frame*6.283185307179586/64),0,255,0);
    Require(gBorderPixels.size()==static_cast<size_t>(4*width+80),"Highlight border has an incorrect pixel count");
    for(const auto& p:gBorderPixels) {
      Require(p.first>=x&&p.first<x+width&&p.second>=100&&p.second<124,"Highlight border extends outside its brick");
      Require(p.first<x+2||p.first>=x+width-2||p.second<102||p.second>=122,"Highlight border fills the brick interior");
    }
    for(int dx=0;dx<width;dx++)Require(gBorderPixels.count({x+dx,100})&&gBorderPixels.count({x+dx,123}),"Highlight top/bottom edge is incomplete");
    double green=0;
    for(const auto& p:gBorderPixels)green+=(pixels[p.second*1280+p.first]>>8)&255;
    minimumGreen=(std::min)(minimumGreen,green/gBorderPixels.size());
  }
  return minimumGreen;
}

int wmain(int argc,wchar_t** argv) {
  if(argc!=2) {std::fprintf(stderr,"Usage: engine-harness.exe path-to-DXBall2.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  AddVectoredExceptionHandler(1,Fault);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module)throw std::runtime_error("Could not map game executable");
    gBase=reinterpret_cast<uint8_t*>(module);
    ApplyGridPatches(gBase);
    // Execute the original border rasterizer, capturing its final pixel calls.
    // Only maths and drawing dependencies are redirected in this harness.
    auto* pixelEntry=static_cast<uint8_t*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Require(pixelEntry!=nullptr,"Could not allocate original pixel entry trampoline");
    std::memcpy(pixelEntry,gBase+0x811f0,6);JumpTo(pixelEntry,6,gBase+0x811f6);
    gNativeBorderPixel=reinterpret_cast<NativeBorderPixel>(pixelEntry);
    // Set up a 32-bit RGB surface and run the original pixel blending code.
    const uint32_t format[]={32,0xff0000,0,16,0,0xff00,0,8,0,0xff,0,0,0};
    WriteMemory(gBase+0x1a7080,format,sizeof(format));
    JumpTo(gBase,0x811f0,reinterpret_cast<void*>(&BorderPixel));
    JumpTo(gBase,0x112410,reinterpret_cast<void*>(&BorderSin));
    JumpTo(gBase,0x113120,reinterpret_cast<void*>(&BorderCos));
    JumpTo(gBase,0x1125d0,reinterpret_cast<void*>(&BorderPow));
    JumpTo(gBase,0xe6280,reinterpret_cast<void*>(&std::memcpy));
    Geometry geometry;geometry.base=gBase;geometry.Install();
    const auto anniversaryGreen=CheckBorder(48);
    geometry.Set(true);
    uint32_t correctedWave;std::memcpy(&correctedWave,gBase+0x89ea1,4);
    const uint32_t oldWave=0x41c00000;WriteMemory(gBase+0x89ea1,&oldWave,4);
    const auto dimRivalGreen=CheckBorder(kRivalRasterWidth);
    WriteMemory(gBase+0x89ea1,&correctedWave,4);
    const auto rivalGreen=CheckBorder(kRivalRasterWidth);
    std::printf("Border minimum average green: Anniversary %.1f, previous Rival %.1f, corrected Rival %.1f (0-255)\n",anniversaryGreen,dimRivalGreen,rivalGreen);
    Require(rivalGreen>=dimRivalGreen*2,"Rival highlight brightness did not improve sufficiently");
    Require(rivalGreen>=anniversaryGreen,"Rival highlight is dimmer than the original Anniversary highlight");
    Require(PlayfieldWidth(37)==20*48,"Rival field does not match standard width");
    Require(ScaleCoordinate(37*3,0)==20*48,"Rival coordinates do not match standard width");
    Require(ScaleCoordinate(36*3,0)+kRivalRasterWidth==20*48,"Rightmost raster cell does not meet standard boundary");
    for(int x=0;x<37;x++) {
      const auto position=ScaleCoordinate(x*3,0),next=ScaleCoordinate((x+1)*3,0);
      Require(next-position==25||next-position==26,"Uneven Rival column spacing");
    }
    Require(ScaleCoordinate(37*3,1)*8==20*48,"Rival HUD does not match standard width");
    const int margin=(1280-20*48)/2;
    Require(ScaleCoordinate(margin+37*24,2)==margin*2+20*48,"Background clipping does not match standard width");
    Require(PlayfieldWidth(20)==20*48&&PlayfieldWidth(26)==26*48,"Anniversary board widths changed");
    geometry.Set(false);
    Require(CheckBorder(48)==anniversaryGreen,"Anniversary border brightness was not restored");
    Require(ScaleCoordinate(26*3,0)==26*48,"Wide geometry was not restored");
    Require(ScaleCoordinate(20*3,0)==20*48,"Standard geometry was not restored");
    for(size_t i=0;i<sizeof(kGeometryPatches)/sizeof(kGeometryPatches[0]);i++) {
      const auto expected=geometry.Value(kGeometryPatches[i],false);
      Require(std::memcmp(geometry.operands[i],expected.data(),kGeometryPatches[i].size)==0,"Geometry toggle did not restore original operands");
    }
    std::puts("PASS: original animated border rasterizer stays inside one Rival brick; Anniversary border restored");
    std::puts("PASS: standard-width boundaries, 37 columns, HUD/background clipping, original standard/Wide geometry restored");
    // Only the isolated harness replaces allocator entry points. Game routines
    // otherwise have no initialized EXE CRT when mapped without running main.
    JumpTo(gBase,0x111f9a,reinterpret_cast<void*>(&std::malloc));
    JumpTo(gBase,0x111f95,reinterpret_cast<void*>(&std::free));
    JumpTo(gBase,0xd6d9c,reinterpret_cast<void*>(&std::malloc));
    constexpr size_t layerBytes=0x2e54;
    std::vector<uint8_t> set(0x2800),board(0x5c),storage(layerBytes+128,0xcc),other(layerBytes+128,0xcc);
    *reinterpret_cast<void**>(board.data())=set.data();
    *reinterpret_cast<int*>(set.data()+0x26ac)=37;
    using LayerCtor=void* (__thiscall*)(void*,void*);
    using CellGetter=void* (__thiscall*)(void*,int,int,bool);
    using LayerSwap=void (__cdecl*)(void*,void*);
    auto ctor=reinterpret_cast<LayerCtor>(gBase+0x52880);
    auto cell=reinterpret_cast<CellGetter>(gBase+0x54560);
    auto first=storage.data()+64,second=other.data()+64;
    ctor(first,board.data());ctor(second,board.data());
    for(int i=0;i<740;i++) {
      auto* c=reinterpret_cast<uint32_t*>(first+4+i*16);
      Require(c[0]==0&&c[1]==0&&c[2]==0&&c[3]==0,"Constructor did not clear all cells");
      c[0]=1000+i;c[1]=2000+i;c[2]=3000+i;c[3]=4000+i;
    }
    Require(first[0x2e44]==1,"Layer visibility is not correctly located");
    for(int x=0;x<37;x++)for(int y=0;y<20;y++) {
      auto* c=reinterpret_cast<uint32_t*>(cell(first,x,y,true));
      Require(reinterpret_cast<uint8_t*>(c)==first+4+(x*20+y)*16,"Cell getter clamps or indexes incorrectly");
      Require(c[1]==static_cast<uint32_t>(2000+x*20+y),"Cell data was corrupted");
    }
    reinterpret_cast<LayerSwap>(gBase+0x52680)(first,second);
    Require(*reinterpret_cast<uint32_t*>(second+4+739*16+4)==2739,"Layer swap lost the final column");
    for(size_t i=0;i<64;i++) {
      Require(storage[i]==0xcc&&storage[layerBytes+64+i]==0xcc,"Layer constructor/swap overwrote guard bytes");
      Require(other[i]==0xcc&&other[layerBytes+64+i]==0xcc,"Layer swap overwrote other guard bytes");
    }
    std::puts("PASS: native construction, 740 cell lookups, final-column data, full-layer swap, guard bytes");std::fflush(stdout);
    // The board constructor exercises native vector allocation and its large
    // stack temporary. Use a zeroed owner with a real 37-column width.
    using BoardCtor=void* (__thiscall*)(void*,void*);
    using BoardDtor=void (__thiscall*)(void*);
    reinterpret_cast<BoardCtor>(gBase+0x52790)(board.data(),set.data());
    auto layers=reinterpret_cast<uint8_t**>(board.data()+8);
    Require(layers[1]-layers[0]==layerBytes,"Board constructor uses an incorrect layer stride");
    Require(layers[0][0x2e44]==1,"Board constructor lost layer metadata");
    Require(reinterpret_cast<uint8_t*>(cell(layers[0],36,19,true))==layers[0]+4+739*16,"Board constructor cannot address the right edge");
    using ResizeLayers=void (__thiscall*)(void*,int);
    reinterpret_cast<ResizeLayers>(gBase+0x91c60)(board.data(),3);
    layers=reinterpret_cast<uint8_t**>(board.data()+8);
    Require(layers[1]-layers[0]==3*layerBytes,"Native runtime layer resize has the wrong stride");
    Require(layers[0][2*layerBytes+0x2e44]==1,"Runtime layer resize lost the final layer metadata");
    reinterpret_cast<BoardDtor>(gBase+0x49bd0)(board.data());
    std::puts("PASS: native board construction, layer allocation and destruction");
    return 0;
  } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
