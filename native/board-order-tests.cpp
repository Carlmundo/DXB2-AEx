#include "board-order.hpp"
#include "verified-game.hpp"
#include <algorithm>
#include <vector>
using namespace board_order;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void Put(Row& row,size_t offset,int value){std::memcpy(row.bytes.data()+offset,&value,4);}
static void String(Row& row,size_t offset,const char* text) {
  const auto n=std::strlen(text);Put(row,offset+0x10,static_cast<int>(n));
  Put(row,offset+0x14,n>=16?static_cast<int>(n):15);
  if(n>=16)std::memcpy(row.bytes.data()+offset,&text,sizeof(text));
  else std::memcpy(row.bytes.data()+offset,text,n+1);
}
static Row Make(int id,int source,const char* group,const char* title,int ordinal=0) {
  Row row{};Put(row,0x94,id);Put(row,0x9c,source);Put(row,0x30,ordinal);
  String(row,0x34,group);String(row,0x4c,title);return row;
}
int wmain(int argc,wchar_t** argv) {
  try {
    Require(argc==2&&VerifiedGame(argv[1]),"Unverified game");
    auto* base=reinterpret_cast<uint8_t*>(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Require(base!=nullptr,"Cannot map original game");
    // The original comparator uses only a CRT case-insensitive string compare.
    JumpTo(base,0x117081,reinterpret_cast<void*>(&_stricmp));
    using NativeLess=bool (__stdcall*)(const Row*,const Row*);
    auto native=reinterpret_cast<NativeLess>(base+0x94df0);
    std::vector<Row> rows={
      Make(1,2,"Board Pack 1","30 Years - Bonus Set",1),
      Make(2,0,"Rival Ball","Wendy"),
      Make(3,2,"Board Pack 1","30 Years of DX-Ball"),
      Make(4,0,"Board Pack 1","Board Pack 1"),
      Make(5,0,"20th Anniversary","20th Anniversary"),
      Make(6,1,"Custom","Unrelated community"),
      Make(7,2,"Workshop","Other Workshop"),
      Make(8,0,"Board Pack 1","Board Pack 1 (Wide)",1),
      Make(9,2,"Workshop","Board Pack 1"),
      Make(10,2,"Board Pack 1 Extra","Different pack"),
      Make(11,1,"Board Pack 1","Local Documents source"),
      Make(12,1,"Board Pack 2","Second pack local"),
      Make(13,2,"Board Pack 2","Second pack Workshop",1),
      Make(14,1,"Board Pack 3","Third pack local"),
      Make(15,2,"Board Pack 3","Third pack Workshop",1),
      Make(16,1,"Memorial Pack","Memorial local"),
      Make(17,2,"Memorial Pack","Memorial Workshop",1)};
    const auto previousPack=Make(18,2,"30 Years of DX-Ball","30 Years of DX-Ball");
    Require(!Selected(previousPack),"Previous whitelist Pack still selected");
    Require(Selected(rows[0])&&Selected(rows[2])&&Selected(rows[10]),"Whitelisted Workshop and local Documents boardsets must be selected");
    const auto unknownSource=Make(19,3,"Board Pack 1","Unknown source");
    Require(!Selected(unknownSource),"Unknown source category matched the whitelist");
    for(size_t i=0;i<rows.size();i++)if(i!=0&&i!=2&&i<10)
      Require(!Selected(rows[i]),"Title, partial Pack name, or official source matched the whitelist");
    for(size_t i=10;i<rows.size();i++)Require(Selected(rows[i]),"Whitelisted Pack was not selected");
    for(const auto* pack:{"Board Pack 1 Extra","Board Pack 2 Extra","Board Pack 3 Extra","Memorial Pack Extra","board pack 1","Board Pack 2 ","Memorial"})
      for(int source:{1,2})Require(!Selected(Make(0,source,pack,"Unrelated title")),"Non-exact Pack matched the whitelist");
    for(const auto& a:rows)for(const auto& b:rows) {
      if(!Selected(a)&&!Selected(b))Require(Less(a,b)==native(&a,&b),"Unrelated native order changed");
      auto projectedA=a,projectedB=b;
      for(auto* row:{&projectedA,&projectedB})if(Selected(*row)) {
        Put(*row,0x9c,0);
      }
      Require(Less(a,b)==native(&projectedA,&projectedB),"Promotion differs from projected native order");
    }
    const auto original=rows;
    std::array<uint8_t,5> originalCall{};std::memcpy(originalCall.data(),base+kSortCall,originalCall.size());
    for(bool wrongOpcode:{true,false}) {
      auto wrong=originalCall;wrong[wrongOpcode?0:1]^=1;
      WriteMemory(base+kSortCall,wrong.data(),wrong.size());
      bool rejected=false;try{Install(base);}catch(const std::runtime_error&){rejected=true;}
      Require(rejected&&std::memcmp(base+kSortCall,wrong.data(),wrong.size())==0,"Bad sort call accepted or partially modified");
      WriteMemory(base+kSortCall,originalCall.data(),originalCall.size());
    }
    Install(base);
    auto target=base+0x983fc+*reinterpret_cast<int32_t*>(base+0x983f8);
    Require(target==reinterpret_cast<uint8_t*>(&Sort),"Hook has incorrect destination");
    reinterpret_cast<void (__cdecl*)(Row*,Row*,int,void*)>(target)(rows.data(),rows.data()+rows.size(),static_cast<int>(rows.size()),nullptr);
    const int expected[]={5,3,4,11,1,8,12,13,14,15,16,17,2,6,10,7,9};
    for(size_t i=0;i<rows.size();i++) {
      Require(Number(rows[i],0x94)==expected[i],"Unexpected mixed order");
      const auto id=Number(rows[i],0x94);
      Require(std::memcmp(&rows[i],&original[id-1],sizeof(Row))==0,"Native row data or source identity changed");
    }
    std::puts("PASS: 289 native comparator pairs; all four Packs include Workshop and Documents entries; exact Pack matching; native row bytes, source flags and within-pack order preserved; guarded hook installation.");
    // Exercise a longer list with inline and heap strings, repeats, and no target.
    std::vector<Row> large;for(int i=0;i<150;i++)large.push_back(Make(i,i%3,i%2?"Short":"Long group title for heap storage",i%2?"Inline":"A longer unrelated board title",150-i));
    auto expectedLarge=large;
    std::stable_sort(expectedLarge.begin(),expectedLarge.end(),[&](const Row& a,const Row& b){return native(&a,&b);});
    SortRows(large.data(),large.data()+large.size());
    Require(std::memcmp(large.data(),expectedLarge.data(),large.size()*sizeof(Row))==0,"No-match list differs from native comparator order");
    SortRows(nullptr,nullptr);Row single=original[2];SortRows(&single,&single+1);
    Require(std::memcmp(&single,&original[2],sizeof(Row))==0,"Single entry changed");
    std::puts("PASS: 150-row no-match regression, empty and single-entry lists.");
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
