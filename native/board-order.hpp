#pragma once
#include "patch.hpp"
#include <array>

namespace board_order {
// Board-selector rows contain native strings and source metadata. Keep their
// storage opaque so moving a row cannot allocate or free strings in our CRT.
struct Row {std::array<uint8_t,0xa0> bytes;};
static_assert(sizeof(Row)==0xa0);
inline constexpr uint32_t kSortCall=0x983f7,kNativeSort=0x93dc0;
inline constexpr const char* kLocalOrderPacks[]={
  "Board Pack 1","Board Pack 2","Board Pack 3","Classic Pack","Memorial Pack"
};
inline int Number(const Row& row,size_t offset) noexcept {
  int value;std::memcpy(&value,row.bytes.data()+offset,4);return value;
}
inline const char* Text(const Row& row,size_t offset) noexcept {
  const auto* value=row.bytes.data()+offset;
  if(Number(row,offset+0x14)<16)return reinterpret_cast<const char*>(value);
  const char* pointer;std::memcpy(&pointer,value,sizeof(pointer));return pointer;
}
inline bool Selected(const Row& row) noexcept {
  // Match Pack (0x34), independently of each boardset's Title (0x4c).
  // Source 1 is Documents community content (computer); 2 is Workshop (cloud).
  const int source=Number(row,0x9c);
  if(source!=1&&source!=2)return false;
  for(const auto* pack:kLocalOrderPacks)
    if(std::strcmp(Text(row,0x34),pack)==0)return true;
  return false;
}
inline bool Less(const Row& left,const Row& right) noexcept {
  const int sourceA=Selected(left)?0:Number(left,0x9c);
  const int sourceB=Selected(right)?0:Number(right,0x9c);
  if(sourceA!=sourceB)return sourceA<sourceB;
  // The native order is source, case-insensitive Pack, then within-pack order.
  const int pack=_stricmp(Text(left,0x34),Text(right,0x34));
  return pack?pack<0:Number(left,0x30)<Number(right,0x30);
}
inline void SortRows(Row* first,Row* last) noexcept {
  if(first==last)return;
  // Stable whole-row moves preserve ownership, IDs, paths, and source icons.
  for(auto* row=first+1;row!=last;++row) {
    Row value;std::memcpy(&value,row,sizeof(value));auto* position=row;
    while(position!=first&&Less(value,position[-1])) {
      std::memcpy(position,position-1,sizeof(value));--position;
    }
    std::memcpy(position,&value,sizeof(value));
  }
}
inline void __cdecl Sort(Row* first,Row* last,int,void*) noexcept {
  SortRows(first,last);
}
inline void Verify(uint8_t* base) {
  if(base[kSortCall]!=0xe8||base+kSortCall+5+*reinterpret_cast<int32_t*>(base+kSortCall+1)!=base+kNativeSort)
    throw std::runtime_error("Unexpected board-selection sort call");
}
inline void Install(uint8_t* base) {
  Verify(base);
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Sort)-reinterpret_cast<uintptr_t>(base+kSortCall+5));
  WriteMemory(base+kSortCall+1,&relative,4);
}
}
