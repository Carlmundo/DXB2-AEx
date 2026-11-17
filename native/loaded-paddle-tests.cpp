// Execute the original paused update, active positioning and paddle renderer
// against memory surfaces. No game, settings or live saves are opened.
#include "loaded-paddle.hpp"
#include "verified-game.hpp"
#include <array>
#include <vector>

using loaded_paddle::At;
static uint8_t* gPaddle=nullptr;
static void* gEntry=nullptr;
static float gHalf=0.5f;
static int gMargin=0;
static constexpr int kWidth=1280,kHeight=720;
static constexpr uint32_t kInk=0xabcdef;
static std::vector<uint32_t> gPixels(kWidth*kHeight),gSprite(256*32,kInk);
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
    std::fprintf(stderr,"Native paddle fault at RVA %08X, access %lu at %p\n",
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(loaded_paddle::game)),
      static_cast<unsigned long>(fault->ExceptionRecord->ExceptionInformation[0]),reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]));
    TerminateProcess(GetCurrentProcess(),3);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
static int __fastcall Margin(void*,void*) {return gMargin;}
static int __fastcall Lock(void*,void*,uint32_t* surface) {
  const uint32_t fields[]={4,kWidth,kHeight,kWidth*4,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gPixels.data()))};
  std::memcpy(surface,fields,sizeof(fields));return 1;
}
static void __fastcall Unlock(void*,void*) {}
static void __fastcall Dirty(void*,void*,int,int,int,int) {}
static __declspec(naked) void Execute() {
  __asm {
    push ebp
    mov ebp,esp
    sub esp,0b0h
    push ebx
    push esi
    push edi
    mov edi,gPaddle
    movss xmm6,gHalf
    call dword ptr [gEntry]
    pop edi
    pop esi
    pop ebx
    mov esp,ebp
    pop ebp
    ret
  }
}
static void Run(uint32_t rva) {gEntry=loaded_paddle::game+rva;Execute();}
static void Image(uint8_t* image,int width,int height) {
  std::memset(image,0,0x50);
  const uint32_t fields[]={static_cast<uint32_t>(width/2),0,
    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gSprite.data())),
    static_cast<uint32_t>(width),static_cast<uint32_t>(height),static_cast<uint32_t>(width*4),32,4};
  std::memcpy(image+0x10,fields,sizeof(fields));
}
static void Draw() {
  std::fill(gPixels.begin(),gPixels.end(),0);Run(0x61011);
  const int x=static_cast<int>(At<float>(0x1a411c)),y=static_cast<int>(At<float>(0x1a4120));
  const int width=static_cast<int>(At<float>(0x1a413c)),height=static_cast<int>(At<float>(0x1a4140));
  for(int row=0;row<kHeight;row++)for(int col=0;col<kWidth;col++) {
    const bool inside=col>=x-width/2&&col<x-width/2+width&&row>=y&&row<y+height;
    Require(gPixels[row*kWidth+col]==(inside?kInk:0),"Paddle pixels were misplaced");
  }
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: loaded-paddle-tests.exe path-to-DXBall2.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  AddVectoredExceptionHandler(1,Fault);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game");
    auto* base=reinterpret_cast<uint8_t*>(module);loaded_paddle::game=base;gPaddle=base+0x1a4110;
    loaded_paddle::Verify(base);
    const uint8_t wrong=0xcc,original=base[0x98a36];WriteMemory(base+0x98a36,&wrong,1);
    bool rejected=false;try{loaded_paddle::Install(base);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"Unexpected callsite was accepted");WriteMemory(base+0x98a36,&original,1);
    Geometry geometry;geometry.base=base;geometry.Install();
    JumpTo(base,0x55a70,reinterpret_cast<void*>(&Margin));
    JumpTo(base,0xe6280,reinterpret_cast<void*>(&std::memcpy));
    JumpTo(base,0x35390,reinterpret_cast<void*>(&Lock));
    JumpTo(base,0x35910,reinterpret_cast<void*>(&Unlock));
    JumpTo(base,0x34df0,reinterpret_cast<void*>(&Dirty));
    // Bound the native slices before any animation, simulation or presentation.
    const uint8_t done=0xc3;
    for(uint32_t rva:{0x98a3bu,0x5d152u,0x5eafcu,0x5d380u,0x61455u})WriteMemory(base+rva,&done,1);
    const uint32_t surface[]={static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gPixels.data())),kWidth,kHeight,kWidth*4};
    WriteMemory(base+0x1cf218,surface,sizeof(surface));
    At<int>(0x1a4094)=kWidth;At<int>(0x1a4098)=kHeight;
    std::memset(gPaddle,0,0x88);At<int>(0x1a4170)=256;
    At<int>(0x1a70d8)=2;At<int>(0x1bedb4)=20;At<int>(0x1b93a8)=640;gMargin=160;
    Image(base+0x1cc7f0,128,24);
    Run(0x98a36);Run(0x5d013);
    Require(At<int>(0x1a70cc)==1&&At<float>(0x1a411c)==0&&At<float>(0x1a4120)==0,"Original paused origin bug not reproduced");
    Draw();std::puts("PASS: original paused update renders the paddle at the top-left origin");
    loaded_paddle::Install(base);
    std::vector<uint8_t> frames(59*0x950);
    std::array<uint8_t,0x50> selected{};
    for(int i=1;i<60;i++)Image(frames.data()+(i-1)*0x950,80+8*(i%5),24);
    Image(frames.data()+58*0x950,1400,24); // Match native bounds even for an oversized custom frame.
    Image(selected.data(),152,28);
    std::array<uint8_t,0x88> moving{};moving.fill(0x5a);
    const auto savedMoving=moving;
    int cases=0,pixelCases=0;
    for(int columns:{20,26,37}) {
      geometry.Set(columns==37);At<int>(0x1bedb4)=columns;gMargin=(kWidth-PlayfieldWidth(columns))/2;
      for(int size=0;size<=4;size++)for(int mode=0;mode<5;mode++)for(int time:{0,99,100,399,2147483600})
        for(int mouse:{-100,0,640,1280,1500})for(bool custom:{false,true}) {
          std::memset(gPaddle,0,0x88);At<int>(0x1a4170)=256;
          At<uint8_t*>(0x1a4118)=moving.data();
          At<int>(0x1a70d8)=size;At<int>(0x1a70e4)=mode==1||mode==4;At<int>(0x1a70e8)=mode==2||mode==4;
          At<int>(0x1a7104)=mode==3;At<int>(0x1a7180)=time;At<int>(0x1b93a8)=mouse;
          At<int>(0x1cd148)=custom?60:0;At<uint8_t*>(0x1cd140)=frames.data();
          auto* frame=frames.data()+8*0x950;
          *reinterpret_cast<int*>(frame+0x4c)=custom?2:0;
          *reinterpret_cast<uint8_t**>(frame+0x48)=selected.data();
          At<float>(0x1a7158)=0;At<int>(0x1a7154)=0;
          At<int>(0x1a70dc)=731;At<int>(0x1a70bc)=123456;At<int>(0x1a70f8)=912;
          // Capture native sprite selection and positioning as the reference.
          At<int>(0x1a70cc)=0;Run(0x5d013);Run(0x5d241);
          std::array<uint8_t,0x30> expected{};std::memcpy(expected.data(),gPaddle+0x0c,expected.size());
          const int expectedFrame=At<int>(0x1a4148);
          const float expectedHalf=At<float>(0x1a7148);
          At<float>(0x1a411c)=cases%2?978.0f:0.0f;At<float>(0x1a4120)=cases%2?42.0f:0.0f;
          At<float>(0x1a4124)=0;At<float>(0x1a4128)=0;
          At<float>(0x1a413c)=0;At<float>(0x1a4140)=0;
          std::array<uint8_t,0xe4> state{};std::memcpy(state.data(),base+0x1a70bc,state.size());
          // Execute the actual patched load call; no update precedes this draw.
          Run(0x98a36);
          for(size_t i=0;i<state.size();i++) {
            const auto rva=0x1a70bc+i;
            if((rva>=0x1a70cc&&rva<0x1a70d8)||(rva>=0x1a7140&&rva<0x1a714c))continue;
            Require(base[rva]==state[i],"Load initialization changed unrelated gameplay globals");
          }
          Require(moving==savedMoving,"Load initialization changed a saved moving object");
          Require(At<int>(0x1a70cc)==1&&At<int>(0x1a70dc)==731&&At<int>(0x1a70bc)==123456&&At<int>(0x1a70f8)==912,"Load initialization advanced gameplay state");
          Require(At<float>(0x1a411c)==*reinterpret_cast<float*>(expected.data())&&At<float>(0x1a4120)==*reinterpret_cast<float*>(expected.data()+4),"Loaded paddle differs from native position");
          Require(At<float>(0x1a4124)==At<float>(0x1a411c)&&At<float>(0x1a4128)==At<float>(0x1a4120),"Previous position was not initialized");
          Require(At<float>(0x1a413c)==*reinterpret_cast<float*>(expected.data()+0x20)&&At<float>(0x1a4140)==*reinterpret_cast<float*>(expected.data()+0x24)&&At<int>(0x1a4148)==expectedFrame&&At<float>(0x1a7148)==expectedHalf,"Loaded paddle sprite differs from native selection");
          if(size==2&&mode==0&&time==100&&(mouse==0||mouse==640||mouse==1280)){Draw();++pixelCases;}
          const float x=At<float>(0x1a411c);At<int>(0x1b93a8)=17;Run(0x5d013);
          Require(At<float>(0x1a411c)==x,"Paddle followed mouse while paused");
          At<int>(0x1a70cc)=0;Run(0x5d013);Run(0x5d241);
          Require(At<float>(0x1a411c)!=x||mouse!=640||expectedHalf*2>=PlayfieldWidth(columns),"Paddle did not resume normal positioning");
          ++cases;
        }
    }
    std::printf("PASS: %d load/reload cases match original positioning (standard/Wide/Rival, sizes, powerups, animation frames, mouse bounds); %d native pixel checks; pause/timers and normal resume preserved\n",cases,pixelCases);
    FreeLibrary(module);return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
