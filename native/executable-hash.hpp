#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <cstring>
#pragma comment(lib,"bcrypt.lib")

namespace executable_hash {
template<size_t N>
inline bool Matches(const wchar_t* path,const unsigned char (&expected)[N][32]) noexcept {
  HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  unsigned char digest[32];bool ok=false;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&
     BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
    unsigned char bytes[65536];DWORD count=0;bool readOk=true;
    for(;;) {
      if(!ReadFile(file,bytes,sizeof(bytes),&count,nullptr)){readOk=false;break;}
      if(!count)break;
      if(BCryptHashData(hash,bytes,count,0)<0){readOk=false;break;}
    }
    if(readOk&&BCryptFinishHash(hash,digest,sizeof(digest),0)>=0)
      for(const auto& candidate:expected)if(std::memcmp(candidate,digest,sizeof(digest))==0)ok=true;
  }
  if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
  CloseHandle(file);return ok;
}
}
