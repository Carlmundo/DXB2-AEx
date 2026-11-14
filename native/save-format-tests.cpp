#include "save-format.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static dxb::Bytes Fixture(uint32_t version,uint32_t cells) {
  dxb::Bytes out;const auto u=[&](uint32_t v){dxb::Put(out,v);};const auto s=[&](const char* v){dxb::String(out,v);};
  u(0x53415645);u(version);for(int i=0;i<16;i++)u(i);u(2);
  for(int record=0;record<2;record++) {
    s(record?"rival":"original");u(1);s("12345678");u(0);s("Fixture date");s("Casual");s("");u(265);u(0);u(10);u(2);
    for(int player=0;player<2;player++) {
      for(int stat=0;stat<5;stat++)u(100+stat);u(1);u(4);u(6);s("playerState = 7");u(player+1);
      for(int layer=0;layer<player+1;layer++)for(uint32_t i=0;i<cells;i++) {
        u(1+i%221);u(i+layer*1000);u((i%3)?0:2);u(i%7);
      }
    }
    for(int stat=0;stat<9;stat++)u(200+stat);s("saveCommon = {}");u(0);u(0);u(0);u(1);u(5);u(8);
  }
  u(dxb::Checksum(out.data(),out.size()));return out;
}
static void Reject(const dxb::Bytes& bytes) {
  try{dxb::NormalizeSave(bytes.data(),bytes.size());}catch(const dxb::FormatError&){return;}
  throw std::runtime_error("Malformed save was accepted");
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::cerr<<"Usage: save-format-tests.exe new-fixture-directory\n";return 2;}
  try {
    Require(!std::filesystem::exists(argv[1]),"Fixture directory already exists");std::filesystem::create_directory(argv[1]);
    for(const auto& item:{std::pair<uint32_t,uint32_t>{5,520},{5,740},{6,740}}) {
      const auto bytes=Fixture(item.first,item.second),before=bytes;
      const auto result=dxb::NormalizeSave(bytes.data(),bytes.size());
      Require(result.storedCells==item.second&&result.records.size()==2,"Save layout detection failed");
      Require(result.records[0].layers==3&&result.records[1].layers==3,"Mixed-player layers lost");
      Require(bytes==before,"Input fixture changed");
      if(item.second==740)Require(result.bytes==bytes,"Expanded save state changed");
      else Require(result.bytes.size()==bytes.size()+6*220*16,"Stock save layer padding failed");
      auto damaged=bytes;damaged[100]^=1;Reject(damaged);
      damaged=bytes;damaged.resize(damaged.size()-9);Reject(damaged);
      damaged=bytes;damaged[72]=255;damaged[73]=255;
      const auto crc=dxb::Checksum(damaged.data(),damaged.size()-4);std::memcpy(damaged.data()+damaged.size()-4,&crc,4);Reject(damaged);
      const auto path=std::filesystem::path(argv[1])/("save-v"+std::to_string(item.first)+"-"+std::to_string(item.second)+".dat");
      std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    }
    std::cout<<"PASS: stock v5, previous expanded v5, explicit v6; mixed players/layers; corrupt and truncated saves rejected\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
