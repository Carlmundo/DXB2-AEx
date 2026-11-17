// Execute the game's HUD slice and original sprite blitters on memory surfaces.
// Only the board margin and screen presentation are stubbed; no game is started.
#include "lives-counter.hpp"
#include "geometry.hpp"
#include "verified-game.hpp"
#include <array>
#include <algorithm>
#include <vector>

static constexpr int kWidth=1280,kHeight=40;
static constexpr uint32_t kBackground=0x123456,kPaddle=0xabcdef;
static std::array<uint32_t,kWidth*kHeight> gWorking,gOutput,gBackground;
static std::vector<uint32_t> gIcon;
static uint8_t* gBase=nullptr;
static void* gEntry=nullptr;
static int gMargin=0,gLeft=0,gRedrawWidth=0,gPresents=0;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int __fastcall Margin(void*,void*) {return gMargin;}
static void __cdecl Present(int x,int y,int width,int height) {
  Require(x>=0&&x+width<=kWidth&&y==0&&height==kHeight,"Invalid HUD redraw rectangle");
  gLeft=x;gRedrawWidth=width;++gPresents;
  for(int row=y;row<y+height;row++)
    std::copy_n(gWorking.begin()+row*kWidth+x,width,gOutput.begin()+row*kWidth+x);
}
static __declspec(naked) void Execute() {
  __asm {
    push ebp
    mov ebp,esp
    sub esp,0b0h
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
static void Image(uint8_t* image,uint32_t* pixels,int width,int height,int origin) {
  std::memset(image,0,0x50);
  const uint32_t fields[]={static_cast<uint32_t>(origin),0,
    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pixels)),
    static_cast<uint32_t>(width),static_cast<uint32_t>(height),static_cast<uint32_t>(width*4),32,4};
  std::memcpy(image+0x10,fields,sizeof(fields));
}
static bool Run(int columns,int spareLives,int iconWidth,int origin,bool customFrame) {
  gMargin=(kWidth-PlayfieldWidth(columns))/2;
  const int lives=spareLives+1;
  WriteMemory(gBase+0x1bedb4,&columns,4);WriteMemory(gBase+0x1a4080,&lives,4);
  gIcon.assign(iconWidth*16,kPaddle);
  auto* image=gBase+0x1cc7f0;
  // Exercise both the stock image and an active mod's multi-frame image.
  std::array<uint8_t,0x50> frame{};
  std::vector<uint8_t> custom(0x8c00);
  if(customFrame) {
    const int count=17;auto* pointer=custom.data();
    WriteMemory(gBase+0x1cd148,&count,4);WriteMemory(gBase+0x1cd140,&pointer,4);
    image=custom.data()+0x8bb0;Image(frame.data(),gIcon.data(),iconWidth,16,origin);
    std::memset(image,0,0x50);const int frames=2;auto* selected=frame.data();
    std::memcpy(image+0x4c,&frames,4);std::memcpy(image+0x48,&selected,4);
  } else {
    const int count=0;WriteMemory(gBase+0x1cd148,&count,4);
    Image(image,gIcon.data(),iconWidth,16,origin);
  }
  gWorking.fill(kBackground);gOutput.fill(kBackground);gPresents=0;
  Execute();Require(gPresents==1,"Counter was not presented exactly once");
  const int right=gMargin+PlayfieldWidth(columns);
  bool complete=true;
  for(int y=0;y<kHeight;y++)for(int x=0;x<kWidth;x++) {
    bool paddle=false;
    for(int n=1;n<=spareLives;n++) {
      const int left=right-(iconWidth+6)*n;
      if(x>=left&&x<left+iconWidth&&y>=10&&y<26)paddle=true;
    }
    if(gOutput[y*kWidth+x]!=(paddle?kPaddle:kBackground))complete=false;
  }
  if(complete&&spareLives)
    Require(gLeft<=right-(iconWidth+6)*spareLives&&gLeft+gRedrawWidth==right,"Redraw does not cover the full counter");
  return complete;
}

int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: lives-counter-tests.exe path-to-DXBall2.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game executable");gBase=reinterpret_cast<uint8_t*>(module);
    VerifyLivesCounter(gBase);
    const uint8_t wrong=0xcc,original=gBase[0x5fc6b];WriteMemory(gBase+0x5fc6b,&wrong,1);
    bool rejected=false;try{InstallLivesCounter(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&gBase[0x5fb7a]==4,"Unexpected drawing code was not safely rejected");
    WriteMemory(gBase+0x5fc6b,&original,1);
    Geometry geometry;geometry.base=gBase;geometry.Install();
    JumpTo(gBase,0x55a70,reinterpret_cast<void*>(&Margin));
    JumpTo(gBase,0x85740,reinterpret_cast<void*>(&Present));
    JumpTo(gBase,0xe6280,reinterpret_cast<void*>(&std::memcpy));
    const uint8_t done=0xc3;WriteMemory(gBase+0x5fcbc,&done,1);gEntry=gBase+0x5fb17;
    gBackground.fill(kBackground);Image(gBase+0x1bb908,gBackground.data(),kWidth,kHeight,0);
    const uint32_t surface[]={static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gWorking.data())),kWidth,kHeight,kWidth*4};
    WriteMemory(gBase+0x1cf218,surface,sizeof(surface));
    Require(Run(20,3,40,0,false),"Baseline three-icon counter changed");
    Require(!Run(20,4,40,0,false),"Baseline four-icon clipping was not reproduced");
    InstallLivesCounter(gBase);
    int cases=0;
    for(int columns:{20,26,37}) {
      geometry.Set(columns==37);
      for(int spare=0;spare<=10;spare++)for(int width:{40,48})
        for(int origin:{-5,0,7})for(bool custom:{false,true}) {
          Require(Run(columns,spare,width,origin,custom),"Lives counter pixels were clipped or misplaced");++cases;
        }
    }
    std::printf("PASS: reproduced original four-icon clipping; %d patched pixel cases passed (1-11 lives, standard/wide/Rival boards, stock/custom frames and origins)\n",cases);
    FreeLibrary(module);return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
