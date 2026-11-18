// Exercise native laser collision, cleanup, counting and firing-limit code in
// a mapped game. No running game, settings or player saves are used.
#include "laser-paddle.hpp"
#include "verified-game.hpp"
#include "geometry.hpp"
#include <array>
#include <vector>

static uint8_t* gBase=nullptr;
static uint8_t* gObject=nullptr;
static void* gEntry=nullptr;
static int gDisposed=0;
static int gFired=0,gReleased=0;
static bool gCanFire=false;
template<class T> static T& At(uint32_t rva) {return *reinterpret_cast<T*>(gBase+rva);}
template<class T> static T& Field(size_t offset) {return *reinterpret_cast<T*>(gObject+offset);}
static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* fault) {
  if(fault->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
    std::fprintf(stderr,"Native laser fault at RVA %08X, access %lu at %p\n",
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(gBase)),
      static_cast<unsigned long>(fault->ExceptionRecord->ExceptionInformation[0]),reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]));
    TerminateProcess(GetCurrentProcess(),3);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
static int __fastcall Margin(void*,void*) {return 0;}
static bool __fastcall NoScript(void*,void*) {return false;}
static bool __fastcall NoSplash(void*,void*,void*) {return false;}
static void __cdecl Sound(int,float,int,int) {}
static void __cdecl Dirty(int,int) {}
static int __cdecl Points(void*) {return 123;}
static void __cdecl Score(int,int,int) {}
static void __cdecl Drop(int,int,int,int) {}
static void* __cdecl NoAllocation(size_t,void*) {return nullptr;}
static void* __fastcall Dispose(void* object,void*,int) {++gDisposed;return object;}
static void __cdecl CanFire() {gCanFire=true;++gFired;}
static __declspec(naked) void VolleyArguments() {
  __asm {
    add esp,0ch
    jmp CanFire
  }
}
static void __cdecl ReleaseBalls() {++gReleased;}

static __declspec(naked) void Execute() {
  __asm {
    push ebp
    mov ebp,esp
    sub esp,200h
    push ebx
    push esi
    push edi
    mov edi,gObject
    xor eax,eax
    call dword ptr [gEntry]
    pop edi
    pop esi
    pop ebx
    mov esp,ebp
    pop ebp
    ret
  }
}
static void Run(uint32_t rva) {gEntry=gBase+rva;Execute();}
static void Update() {Run(0x5d525);}
static void Remove() {Run(0x5fea3);}
static bool CanShoot() {gCanFire=false;Run(0x5cb74);return gCanFire;}

static void InputTests(bool holdClickFire) {
  int cases=0;
  for(bool click:{false,true})for(bool left:{false,true})for(bool right:{false,true})
  for(bool paused:{false,true})for(bool transition:{false,true})for(bool finished:{false,true})
  for(bool laser:{false,true})for(int shots:{0,2,4,5,6,8}) {
    At<int>(0x1b93b8)=click;At<int>(0x1b93b0)=left;At<int>(0x1b93b4)=right;
    At<int>(0x1a70cc)=paused;At<int>(0x1a714c)=transition;At<int>(0x1a40f4)=finished;
    At<int>(0x1a70e8)=laser;At<int>(0x1b91d8)=shots;
    laser_paddle::hasShot=false;
    gFired=gReleased=0;Run(0x5cb3f);
    const bool active=!paused&&!transition&&!finished;
    const bool expected=(click||(holdClickFire&&left))&&active&&laser&&shots<6;
    Require(gFired==(expected?1:0),"Hold-fire differed from native click eligibility or fired more than once per update");
    Require(gReleased==((click&&active)?1:0),"Hold-fire changed the native ball-release input");
    Require(At<int>(0x1b93b8)==(click?1:0),"Hold-fire synthesized a global click");++cases;
  }
  At<int>(0x1a70cc)=At<int>(0x1a714c)=At<int>(0x1a40f4)=0;
  At<int>(0x1a70e8)=1;
  // The original click gate has no elapsed-time test: consecutive fresh clicks
  // are eligible even one millisecond apart. HoldClickFire must preserve that path.
  for(int tick=0;tick<100;tick++) {
    At<int>(0x1b91d8)=0;At<uint32_t>(0x1a70bc)=1000+tick;
    gFired=0;At<int>(0x1b93b8)=1;At<int>(0x1b93b0)=1;Run(0x5cb3f);
    Require(gFired==1,"Manual clicking acquired an artificial cooldown");
  }
  std::printf("PASS: HoldClickFire %s; %d native input cases (hold/release, clicks, buttons, pause, transitions, powerup and shot limit); manual clicks have no timed cooldown\n",
    holdClickFire?"enabled":"disabled",cases);
}

static void TimingTests() {
  At<int>(0x1a70cc)=At<int>(0x1a714c)=At<int>(0x1a40f4)=0;
  At<int>(0x1a70e8)=At<int>(0x1b93b0)=1;At<int>(0x1b91d8)=0;
  const uint32_t delay=laser_paddle::repeatDelayMs;
  const auto frame=[](uint32_t time,bool click=false) {
    At<uint32_t>(0x1a70bc)=time;At<int>(0x1b93b8)=click;gFired=0;Run(0x5cb3f);return gFired;
  };
  laser_paddle::hasShot=false;
  Require(frame(1000,true)==1,"Initial click was delayed");
  for(uint32_t elapsed:{0u,1u,delay-1})Require(frame(1000+elapsed)==0,"Held shot fired before the repeat delay");
  Require(frame(1000+delay)==1,"Held shot missed the repeat boundary");
  Require(frame(1000+delay)==0,"Multiple shots fired at the same timestamp");
  Require(frame(1001+delay,true)==1,"Repeat limiter blocked a manual click");
  Require(frame(1000+2*delay)==0,"Held repeat ignored the last manual shot");
  Require(frame(1001+2*delay)==1,"Held repeat did not resume after manual shot delay");
  At<int>(0x1b91d8)=6;
  const uint32_t last=laser_paddle::lastShot;
  Require(frame(2000)==0&&laser_paddle::lastShot==last,"Full shot limit consumed the repeat timer");
  At<int>(0x1b91d8)=0;Require(frame(2001)==1,"Free shot slot did not resume firing");
  At<int>(0x1b93b0)=0;Require(frame(2002)==0,"Releasing the button fired a shot");
  At<int>(0x1b93b0)=1;Require(frame(2003)==1,"New button hold was not immediate");
  for(uint32_t guard:{0x1a70ccu,0x1a714cu,0x1a40f4u}) {
    At<int>(guard)=1;Require(frame(3000)==0,"Inactive play fired an automatic shot");At<int>(guard)=0;
  }
  // Repeats use elapsed milliseconds, not frame count. Slow frames must never
  // catch up by emitting a burst; fast frames must still honor the full delay.
  for(uint32_t step:{1u,9u,16u,33u,50u,250u}) {
    laser_paddle::hasShot=false;uint32_t previous=0;int fired=0;
    for(uint32_t time=0;time<=10000;time+=step) {
      const int count=frame(time);
      Require(count<=1,"A delayed update emitted a catch-up burst");
      if(count) {
        Require(fired==0||time-previous>=delay,"Frame rate accelerated automatic firing");
        previous=time;++fired;
      }
    }
    Require(fired>1&&fired<=static_cast<int>(10000/delay+1),"Automatic firing exceeded the timed rate");
  }
  laser_paddle::hasShot=false;Require(frame(0xfffffff0u)==1,"Initial shot near clock wrap failed");
  Require(frame(0xfffffff0u+delay-1)==0&&frame(0xfffffff0u+delay)==1,"Repeat timer failed across clock wrap");
  std::printf("PASS: %u ms automatic repeat delay; exact boundaries, manual shots, release/repress, full slots, inactive play, six frame intervals and clock wrap\n",delay);
}

static std::vector<uint8_t> gBoard(0x5c),gLayers(3*0x2e54);
static std::array<uint32_t,90> gDefinitions{};
static std::array<uint8_t,0x88> gShot{};
static void* gVtable[]={reinterpret_cast<void*>(&Dispose)};
static uint32_t* Cell(int x,int y,int layer,int stride) {
  return reinterpret_cast<uint32_t*>(gLayers.data()+layer*stride+4+(x*20+y)*16);
}
static void Prepare(int width,int stride,int layer,int brick,bool thru) {
  std::fill(gLayers.begin(),gLayers.end(),uint8_t{0});
  *reinterpret_cast<void**>(gBoard.data())=gBase+0x1bc708;
  *reinterpret_cast<void**>(gBoard.data()+8)=gLayers.data();
  *reinterpret_cast<void**>(gBoard.data()+12)=gLayers.data()+3*stride;
  At<void*>(0x1bc708)=gBoard.data();At<int>(0x1bedb0)=0;At<int>(0x1bedb4)=width;
  At<int>(0x1bc708+0x26a8)=0;At<int>(0x1bc708+0x26ac)=width;
  At<void*>(0x1bc708+0x2764)=gDefinitions.data();
  At<void*>(0x1bc708+0x2768)=gDefinitions.data()+gDefinitions.size();
  for(int i=0;i<3;i++) {
    *reinterpret_cast<void**>(gLayers.data()+i*stride)=gBoard.data();
    gLayers[(i+1)*stride-0x10]=1;
  }
  gDefinitions[9]=brick;
  Cell(5,6,layer,stride)[1]=1;
  gShot.fill(0);gObject=gShot.data();Field<void*>(0)=gVtable;
  Field<int>(0x40)=3;Field<float>(0x0c)=5.5f*PlayfieldWidth(width)/width;
  Field<float>(0x10)=52+6*24+16;Field<float>(0x20)=-600;
  Field<void*>(4)=gBase+0x1a4110;At<void*>(0x1a4118)=gObject;
  At<int>(0x1a70dc)=thru;At<int>(0x1a70e0)=0;At<int>(0x1a70f0)=0;
  At<int>(0x1a70e8)=1;At<int>(0x1b91d8)=0;At<int>(0x1b91cc)=512;
  At<int>(0x1a7124)=50;At<int>(0x1a712c)=0;At<int>(0x1a7128)=0;
  At<int>(0x1a7050)=0;At<int>(0x1a4104)=0;At<uint8_t>(0x1bef94)=0;
  At<int>(0x1ba630)=720;gDisposed=0;
}
static void Reproduce(int stride) {
  Prepare(26,stride,0,7,true);Update();
  Require(Field<float>(0x20)>0&&Field<int>(0x5c)==0,"Permanent brick did not reflect a thru laser");
  Require(Cell(5,6,0,stride)[1]==1,"Permanent brick was destroyed");
  Field<float>(0x10)=800;Update();Remove();
  Require(Field<int>(0x5c)==0&&At<void*>(0x1a4118)==gObject,"Original reflected laser leak not reproduced");
  At<int>(0x1b91d8)=5;Update();Require(!CanShoot(),"Leaked laser did not consume the last firing slot");
}

int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: laser-paddle-tests.exe path-to-DXBall2.exe\n");return 2;}
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Fault);
  try {
    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game");gBase=reinterpret_cast<uint8_t*>(module);
    laser_paddle::Verify(gBase,true);
    std::array<uint8_t,13> cleanup;std::memcpy(cleanup.data(),gBase+0x5d6bb,cleanup.size());
    std::array<uint8_t,6> input;std::memcpy(input.data(),gBase+0x5cb45,input.size());
    std::array<uint8_t,5> volleyEntry;std::memcpy(volleyEntry.data(),gBase+0x5cb8e,volleyEntry.size());
    const uint8_t wrong=0xcc,original=gBase[0x5d6bb];WriteMemory(gBase+0x5d6bb,&wrong,1);
    bool rejected=false;try{laser_paddle::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected,"Unexpected instruction was accepted");WriteMemory(gBase+0x5d6bb,&original,1);
    WriteMemory(gBase+0x5cb45,&wrong,1);
    rejected=false;try{laser_paddle::Install(gBase,true);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected&&!std::memcmp(gBase+0x5d6bb,cleanup.data(),cleanup.size()),"Unexpected firing branch was accepted or cleanup was partially installed");
    WriteMemory(gBase+0x5cb45,input.data(),input.size());
    JumpTo(gBase,0x55a70,reinterpret_cast<void*>(&Margin));
    JumpTo(gBase,0x810d0,reinterpret_cast<void*>(&NoScript));
    JumpTo(gBase,0x538a0,reinterpret_cast<void*>(&NoSplash));
    JumpTo(gBase,0x89490,reinterpret_cast<void*>(&Sound));
    JumpTo(gBase,0x84ee0,reinterpret_cast<void*>(&Dirty));
    JumpTo(gBase,0x82c50,reinterpret_cast<void*>(&Points));
    JumpTo(gBase,0x81440,reinterpret_cast<void*>(&Score));
    JumpTo(gBase,0x89520,reinterpret_cast<void*>(&Drop));
    JumpTo(gBase,0xd798d,reinterpret_cast<void*>(&NoAllocation));
    // End native slices before the next object, rendering, or shot allocation.
    const uint8_t done=0xc3;
    for(uint32_t rva:{0x5eafcu,0x5fee3u,0x61479u,0x5cd00u,0x5cd0au})WriteMemory(gBase+rva,&done,1);
    JumpTo(gBase,0x5cb8e,reinterpret_cast<void*>(&CanFire));
    JumpTo(gBase,0x62930,reinterpret_cast<void*>(&ReleaseBalls));
    InputTests(false);
    Reproduce(0x2094);
    std::puts("PASS: original permanent-brick reflection leaves an offscreen laser consuming a firing slot");
    laser_paddle::Install(gBase);ApplyGridPatches(gBase);
    Require(!std::memcmp(gBase+0x5cb45,input.data(),input.size()),"Disabled HoldClickFire changed native input instructions");
    Geometry geometry;geometry.base=gBase;geometry.Install();
    int cases=0;
    for(int width:{20,26,37})for(int layer:{0,1,2}) {
      geometry.Set(width==37);
      Prepare(width,0x2e54,layer,7,true);Update();
      Require(Field<float>(0x20)>0&&Field<int>(0x5c)==0,"Fix changed permanent-brick reflection");
      Require(Cell(5,6,layer,0x2e54)[1]==1,"Fix destroyed a permanent brick");
      Field<float>(0x10)=800;Update();Remove();
      Require(gDisposed==1&&At<void*>(0x1a4118)==nullptr,"Reflected shot was not removed from the native list");
      Require(CanShoot(),"Reflected shot permanently consumed ammunition");++cases;
      for(bool thru:{false,true})for(int height:{480,720,1080})for(float y:{-1.0f,0.0f,1.0f,479.0f,480.0f,481.0f,719.0f,720.0f,721.0f,1081.0f}) {
        Prepare(width,0x2e54,layer,7,thru);At<int>(0x1ba630)=height;
        // Keep collision sampling outside the brick grid for boundary checks.
        Field<float>(0x0c)=-100;Field<float>(0x10)=y;Update();
        const bool expired=y<0||y>height;
        Require((Field<int>(0x5c)!=0)==expired,"Laser boundary cleanup was incorrect");
        Remove();Require(gDisposed==(expired?1:0),"Native removal disagrees with laser lifetime");++cases;
      }
      Prepare(width,0x2e54,layer,0,true);Update();
      Require(Field<int>(0x5c)==0&&Cell(5,6,layer,0x2e54)[1]==0,"Thru laser stopped passing through ordinary bricks");
      Prepare(width,0x2e54,layer,7,false);Update();
      Require(Field<int>(0x5c)==1,"Ordinary laser stopped expiring on permanent-brick impact");
      // Six simultaneous reflected shots fill the native limit. Remove them
      // through the game's linked-list code, then count survivors next tick.
      for(int volley=0;volley<20;volley++) {
        Prepare(width,0x2e54,layer,7,true);
        std::array<std::array<uint8_t,0x88>,6> shots;
        for(int shot=0;shot<6;shot++) {
          shots[shot]=gShot;gObject=shots[shot].data();
          Field<void*>(4)=shot?shots[shot-1].data():gBase+0x1a4110;
          Field<void*>(8)=shot<5?shots[shot+1].data():nullptr;
          Update();Require(Field<float>(0x20)>0,"Volley shot did not reflect");
          Field<float>(0x10)=800;
        }
        At<void*>(0x1a4118)=shots[0].data();
        Require(!CanShoot(),"Six simultaneous reflected shots did not fill the firing limit");
        At<int>(0x1b91d8)=0;
        for(auto& shot:shots) {gObject=shot.data();Update();Remove();}
        Require(gDisposed==6&&At<void*>(0x1a4118)==nullptr,"Repeated volley leaked a shot");
        At<int>(0x1b91d8)=0;
        for(auto* object=At<uint8_t*>(0x1a4118);object;object=*reinterpret_cast<uint8_t**>(object+8)) {
          gObject=object;Update();
        }
        Require(CanShoot(),"Repeated volleys exhausted ammunition");
      }
      Prepare(width,0x2e54,layer,7,true);Field<float>(0x0c)=-100;
      At<int>(0x1b91d8)=5;Update();Require(!CanShoot(),"Six-shot firing limit changed");
      At<int>(0x1b91d8)=4;Update();Require(CanShoot(),"Available firing slot was blocked");
    }
    std::printf("PASS: %d boundary/reflection cases across all board widths and layers; native removal, six-shot limit and 180 repeated volleys\n",cases);
    WriteMemory(gBase+0x5d6bb,cleanup.data(),cleanup.size());
    WriteMemory(gBase+0x5cb8e,volleyEntry.data(),volleyEntry.size());
    JumpTo(gBase,0x5cb93,reinterpret_cast<void*>(&VolleyArguments));
    laser_paddle::Install(gBase,true);
    InputTests(true);
    TimingTests();
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
