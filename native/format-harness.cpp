#include "board-format.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

// Check the emitted v9 payload, rather than just successful source parsing.
// This catches dropping the generated half, transposing columns, changing
// stored lower cells, or shifting the next board's header while adapting it.
static void ValidateRival(const dxb::Bytes& source,const dxb::Normalized& result) {
  auto raw=dxb::Unwrap(source.data(),source.size());dxb::Reader r{raw};
  r.take(16);const auto count=r.u32();
  for(int i=0;i<5;i++)r.string(true);
  dxb::Reader out{result.bytes};out.take(16);
  out.string();out.string();out.u32();out.string();out.u32();out.string();
  for(int i=0;i<4;i++)out.string();
  out.u32();out.string();out.string();
  for(uint32_t board=0;board<count;board++) {
    if(r.u32()!=out.u32())throw std::runtime_error("Background changed");
    const auto* stored=r.take(2960);r.take(256);
    const bool mirrored=dxb::RivalNeedsMirror(stored);
    for(int i=0;i<3;i++)out.string();
    if(out.u32()!=1)throw std::runtime_error("Wrong layer count");
    const auto* grid=out.take(2960);
    for(size_t x=0;x<37;x++) {
      // The stored lower half must be byte-for-byte identical in the output.
      if(std::memcmp(stored+(x*20+10)*4,grid+(x*20+10)*4,40))throw std::runtime_error("Stored lower half changed");
      if(mirrored) {
        for(size_t y=0;y<10;y++)if(dxb::U32(grid+(x*20+y)*4)!=dxb::U32(stored+(x*20+19-y)*4))
          throw std::runtime_error("Mirrored upper half missing or misplaced");
      } else if(std::memcmp(stored+x*80,grid+x*80,80))throw std::runtime_error("Ordinary Rival layout changed");
    }
  }
  const auto end=out.at;
  if(out.u32()!=dxb::Checksum(result.bytes.data(),end)||out.at!=result.bytes.size())throw std::runtime_error("Wrong adapted checksum or length");
}

int wmain(int argc,wchar_t** argv) {
  try {
    size_t sets=0,boards=0,rival=0,mirrored=0;
    for(int arg=1;arg<argc;arg++)for(const auto& item:std::filesystem::recursive_directory_iterator(argv[arg])) {
      const auto extension=item.path().extension();
      if(!item.is_regular_file()||(extension!=L".bdz"&&extension!=L".bds"))continue;
      std::ifstream stream(item.path(),std::ios::binary);
      const dxb::Bytes source((std::istreambuf_iterator<char>(stream)),{});
      const auto result=dxb::Normalize(source.data(),source.size());
      if(result.rival)ValidateRival(source,result);
      else if(result.sourceVersion<5&&result.bytes!=dxb::Unwrap(source.data(),source.size()))
        throw std::runtime_error("Legacy DX-Ball payload changed during adaptation");
      std::cout<<item.path().filename().string()<<": "<<(result.rival?"Rival":"Anniversary")<<", "<<result.boards<<" boards, width "<<result.width<<", "<<result.bytes.size()<<" runtime bytes\n";
      sets++;boards+=result.boards;if(result.rival)rival+=result.boards;mirrored+=result.mirroredBoards;
    }
    std::cout<<"PASS: "<<sets<<" sets, "<<boards<<" total boards, "<<rival<<" Rival boards, "<<mirrored<<" mirrored layouts verified\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
