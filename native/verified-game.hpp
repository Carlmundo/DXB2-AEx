#pragma once
#include "patch.hpp"
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")

inline bool VerifiedGame(const wchar_t* path) {
  static const unsigned char expected[32]={0x1b,0x60,0x79,0x21,0xdc,0xe6,0x66,0xac,0x51,0x74,0xc9,0xe4,0xcb,0xad,0x37,0x87,0x84,0x6a,0xe3,0x36,0x11,0xde,0x3c,0x85,0xef,0x84,0xcb,0x82,0x72,0xfd,0xbd,0x5d};
  HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  unsigned char digest[32];bool ok=false;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 &&
     BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
    unsigned char buffer[65536];DWORD count=0;bool readOk=true;
    for(;;) {
      if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr)){readOk=false;break;}
      if(!count)break;
      if(BCryptHashData(hash,buffer,count,0)<0){readOk=false;break;}
    }
    if(readOk && BCryptFinishHash(hash,digest,sizeof(digest),0)>=0)ok=std::memcmp(digest,expected,32)==0;
  }
  if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
  CloseHandle(file);return ok;
}
