// Run the original asset resolver with controlled filesystem search results.
// No game loop, Steam, subscriptions, settings or live saves are touched.
#include "community-assets.hpp"
#include "verified-game.hpp"
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

struct Wide {
  union {wchar_t local[8];wchar_t* heap;};
  uint32_t length,capacity;
};
static_assert(sizeof(Wide)==24);
struct Node {Node* next;Node* previous;Wide root;};
static uint8_t* gBase=nullptr;
static int gAvailable=0;
static void* gFiles=reinterpret_cast<void*>(0x1234);
static std::vector<std::wstring> gQueries;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static const wchar_t* Text(const Wide& value){return value.capacity<8?value.local:value.heap;}
static Wide* Make(Wide* output,const std::wstring& text) {
  std::memset(output,0,sizeof(*output));output->capacity=7;output->length=static_cast<uint32_t>(text.size());
  if(text.size()>=8) {
    output->capacity=output->length;
    output->heap=static_cast<wchar_t*>(std::malloc((text.size()+1)*sizeof(wchar_t)));
    Require(output->heap!=nullptr,"Allocation failed");
  }
  std::memcpy(output->capacity<8?output->local:output->heap,text.c_str(),(text.size()+1)*sizeof(wchar_t));return output;
}
static void Release(Wide& value){if(value.capacity>=8)std::free(value.heap);}
static void* __fastcall Empty(void* output,void*,const char* text,size_t length) {
  Require(!*text&&length==0,"Unexpected nonempty fallback constructor");
  return Make(static_cast<Wide*>(output),L"");
}
static void* __fastcall Search(void* files,void*,Wide* output,const Wide* root,const Wide* name,bool recursive) {
  Require(files==gFiles&&recursive,"Native search arguments changed");
  const std::wstring directory=Text(*root);gQueries.push_back(directory);
  int bit=directory==L"docs"?1:directory==L"workshop/1565804267"?2:
    directory==L"workshop/other"?4:directory==L"game"?8:directory==L"game/dlc"?16:0;
  Require(bit!=0,"Unexpected search root");
  return Make(output,(gAvailable&bit)?directory+L"/"+Text(*name):L"");
}
static std::wstring Resolve(const wchar_t* name,int enabled,int available,std::vector<std::wstring> queries) {
  *reinterpret_cast<int*>(gBase+community_assets::kEnabled)=enabled;
  gAvailable=available;gQueries.clear();Wide input{},output{};Make(&input,name);
  using Resolver=Wide* (__cdecl*)(Wide*,void*,const Wide*);
  Require(reinterpret_cast<Resolver>(gBase+0x5a940)(&output,gFiles,&input)==&output,"Incorrect return storage");
  const std::wstring path=Text(output);Release(input);Release(output);
  Require(gQueries==queries,"Wrong search order or disabled community folder was searched");return path;
}
int wmain(int argc,wchar_t** argv) {
  try {
    Require(argc==2&&VerifiedGame(argv[1]),"Unverified game");
    gBase=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Require(gBase!=nullptr,"Cannot map original game");
    community_assets::Verify(gBase);
    std::array<std::array<uint8_t,5>,2> original{};
    for(size_t i=0;i<2;i++)std::memcpy(original[i].data(),gBase+community_assets::kFindCalls[i],5);
    for(size_t i=0;i<2;i++)for(int byte:{0,1}) {
      auto wrong=original[i];wrong[byte]^=1;
      WriteMemory(gBase+community_assets::kFindCalls[i],wrong.data(),5);
      bool rejected=false;try{community_assets::Install(gBase);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected,"Unexpected search call accepted");
      for(size_t j=0;j<2;j++)Require(std::memcmp(gBase+community_assets::kFindCalls[j],j==i?wrong.data():original[j].data(),5)==0,"Rejected hook partially installed");
      WriteMemory(gBase+community_assets::kFindCalls[i],original[i].data(),5);
    }
    // Only filesystem results and uninitialized EXE CRT dependencies are
    // substituted. The resolver, source loops, string moves and cleanup run.
    JumpTo(gBase,community_assets::kNativeFind,reinterpret_cast<void*>(&Search));
    JumpTo(gBase,0x5a070,reinterpret_cast<void*>(&Empty));
    JumpTo(gBase,0xe5a70,reinterpret_cast<void*>(&std::memcpy));
    JumpTo(gBase,0xd6dcf,reinterpret_cast<void*>(&std::free));
    Wide docs{};Make(&docs,L"docs");WriteMemory(gBase+0x1d2b00,&docs,sizeof(docs));
    Node workshop[3]{},official[3]{};
    for(auto* nodes:{workshop,official}) {
      nodes[0].next=&nodes[1];nodes[1].next=&nodes[2];nodes[2].next=&nodes[0];
      nodes[0].previous=&nodes[2];nodes[1].previous=&nodes[0];nodes[2].previous=&nodes[1];
    }
    Make(&workshop[1].root,L"workshop/1565804267");Make(&workshop[2].root,L"workshop/other");
    Make(&official[1].root,L"game");Make(&official[2].root,L"game/dlc");
    auto* head=workshop;WriteMemory(gBase+0x1d2b18,&head,sizeof(head));
    head=official;WriteMemory(gBase+0x1d2b20,&head,sizeof(head));
    Require(Resolve(L"BrickSetJim.img",0,2|8,{L"docs",L"workshop/1565804267"})==L"workshop/1565804267/BrickSetJim.img","Original disabled replacement bug not reproduced");
    std::puts("PASS: original resolver loads Workshop BrickSetJim.img with community content disabled");
    community_assets::Install(gBase);
    for(auto rva:community_assets::kFindCalls)
      Require(gBase+rva+5+*reinterpret_cast<int32_t*>(gBase+rva+1)==reinterpret_cast<uint8_t*>(&community_assets::Find),"Wrong hook destination");
    int checks=0;
    for(const auto* name:{L"BrickSetJim.img",L"BackgroundSet.img",L"BricksFull.img",L"Missing.img"})for(int mask=0;mask<32;mask++)for(int enabled:{0,1,2,0,1}) {
      std::vector<std::wstring> expectedQueries;std::wstring chosen;
      const wchar_t* roots[]={L"docs",L"workshop/1565804267",L"workshop/other",L"game",L"game/dlc"};
      for(int i=enabled?0:3;i<5;i++) {
        expectedQueries.push_back(roots[i]);
        if(mask&(1<<i)){chosen=std::wstring(roots[i])+L"/"+name;break;}
      }
      Require(Resolve(name,enabled,mask,expectedQueries)==chosen,"Wrong asset selected");++checks;
    }
    // The wrapper must also construct an empty, safely destructible wstring
    // directly in uninitialized caller storage without altering its guards.
    std::array<uint8_t,40> guarded;guarded.fill(0xcc);
    *reinterpret_cast<int*>(gBase+community_assets::kEnabled)=0;
    auto* value=reinterpret_cast<Wide*>(guarded.data()+8);
    Require(community_assets::Find(nullptr,nullptr,value,nullptr,nullptr,false)==value&&value->length==0&&value->capacity==7&&!*Text(*value),"Invalid empty native string");
    for(size_t i=0;i<8;i++)Require(guarded[i]==0xcc&&guarded[32+i]==0xcc,"Return storage overflow");
    for(auto* nodes:{workshop,official})for(int i=1;i<3;i++)Release(nodes[i].root);
    Release(docs);
    std::printf("PASS: %d original resolver cases cover bricks/backgrounds, all source combinations, missing assets, multiple roots, enabled priority and repeated live option toggles; guarded installation and return storage.\n",checks);
    return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
