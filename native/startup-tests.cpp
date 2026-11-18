// Tests config parsing and executes the original splash update in an isolated
// mapping. No game launch, rendering, settings, or saves are involved.
#include "config.hpp"
#include "skip-longbow.hpp"
#include "verified-game.hpp"
#include <sstream>
#include <fstream>

static void Require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}

static void CheckConfig(const std::string& text,bool log,bool skip,bool noTransition=false,bool disableDangerousLua=true,bool disableAchievementReset=true,bool holdClickFire=false) {
  std::istringstream input(text);
  const auto config=ReadAexConfig(input);
  Require(config.log==log&&config.skipLongbow==skip&&config.noScreenTransition==noTransition&&config.disableDangerousLua==disableDangerousLua&&config.disableAchievementReset==disableAchievementReset&&config.holdClickFire==holdClickFire,"Incorrect config settings");
}

static DWORD gTickCalls=0;
static DWORD gElapsed=0;
static DWORD WINAPI Ticks() {return gTickCalls++?gElapsed:0;}

int wmain(int argc,wchar_t** argv) {
  if(argc!=2){std::fprintf(stderr,"Usage: startup-tests.exe path-to-DXBall2.exe\n");return 2;}
  try {
    CheckConfig("",false,false);
    for(const char* value:{"0","2","true","01","1junk","","1 = 1"})
      CheckConfig(std::string("HoldClickFire = ")+value,false,false);
    CheckConfig("HoldClickFire = 1",false,false,false,true,true,true);
    CheckConfig("\xef\xbb\xbf# options\r\n HoldClickFire\t= 1 # repeat lasers\r\n",false,false,false,true,true,true);
    CheckConfig("# HoldClickFire=1\nOtherHoldClickFire=1\nholdclickfire=1",false,false);
    CheckConfig("HoldClickFire=1\nHoldClickFire=0",false,false);
    CheckConfig("HoldClickFire=0\nHoldClickFire=1",false,false,false,true,true,true);
    CheckConfig("HoldClickFire=1\nHoldClickFire=invalid",false,false);
    CheckConfig("HoldClickFire=1\nLog=1\nSkipLongbow=1\nNoScreenTransition=1\nDisableDangerousLua=0\nDisableAchievementReset=0",true,true,true,false,false,true);
    std::istringstream failedHold("HoldClickFire=1");failedHold.setstate(std::ios::badbit);
    Require(!ReadAexConfig(failedHold).holdClickFire,"Unreadable config must disable HoldClickFire");
    for(const char* key:{"Log","SkipLongbow","NoScreenTransition"}) {
      for(const char* value:{"0","2","true","01","1junk","","1 = 1"})
        CheckConfig(std::string(key)+" = "+value,false,false);
      const bool log=std::strcmp(key,"Log")==0;
      const bool skip=std::strcmp(key,"SkipLongbow")==0;
      const bool noTransition=std::strcmp(key,"NoScreenTransition")==0;
      CheckConfig(std::string(key)+"=1",log,skip,noTransition);
      CheckConfig(std::string("\xef\xbb\xbf# config\r\n\r\n\t")+key+" \t= \t1 \t# comment\r\n",log,skip,noTransition);
      CheckConfig(std::string(key)+"=1\n"+key+"=0",false,false);
      CheckConfig(std::string(key)+"=0\n"+key+"=1",log,skip,noTransition);
    }
    for(int log=0;log<2;log++)for(int skip=0;skip<2;skip++)for(int noTransition=0;noTransition<2;noTransition++)for(int disableDangerousLua=0;disableDangerousLua<2;disableDangerousLua++)
      CheckConfig("Log="+std::to_string(log)+"\nSkipLongbow="+std::to_string(skip)+"\nNoScreenTransition="+std::to_string(noTransition)+"\nDisableDangerousLua="+std::to_string(disableDangerousLua),log!=0,skip!=0,noTransition!=0,disableDangerousLua!=0);
    CheckConfig("# Log=1\n# SkipLongbow=1\n# NoScreenTransition=1\n# DisableDangerousLua=1\nOtherLog=1\nOtherSkipLongbow=1\nOtherNoScreenTransition=1\nOtherDisableDangerousLua=1\nlog=1\nskiplongbow=1\nnoscreentransition=1\ndisabledangerouslua=1",false,false);
    CheckConfig("SkipLongbow=1\nLog=1\nLog=0",false,true);
    CheckConfig("Log=1\nSkipLongbow=1\nSkipLongbow=0",true,false);
    CheckConfig("Log=1\nSkipLongbow=1\nNoScreenTransition=1\nNoScreenTransition=0",true,true,false);
    CheckConfig("Log=1\nSkipLongbow=1\nNoScreenTransition=1\nDisableDangerousLua=1\nDisableDangerousLua=0",true,true,true,false);
    std::istringstream failed("Log=1\nSkipLongbow=1\nNoScreenTransition=1\nDisableDangerousLua=1");failed.setstate(std::ios::badbit);
    const auto config=ReadAexConfig(failed);
    Require(!config.log&&!config.skipLongbow&&!config.noScreenTransition&&config.disableDangerousLua&&config.disableAchievementReset,"Unreadable config must use safe defaults");
    std::ifstream missing;
    Require(ReadAexConfig(missing).disableDangerousLua,"Missing config must keep Lua protection enabled");
    Require(ReadAexConfig(missing).disableAchievementReset,"Missing config must keep achievement protection enabled");
    Require(AexConfig{}.disableDangerousLua,"Default configuration must keep Lua protection enabled");
    for(const char* value:{"1","2","true","false","00","01","0junk","","0 = 0"})
      CheckConfig(std::string("DisableDangerousLua = ")+value,false,false);
    CheckConfig("DisableDangerousLua = 0",false,false,false,false);
    CheckConfig("\xef\xbb\xbf# config\r\n\tDisableDangerousLua \t= \t0 # allow original Lua functions\r\n",false,false,false,false);
    CheckConfig("# DisableDangerousLua = 0\nOtherDisableDangerousLua = 0\ndisabledangerouslua=0",false,false);
    CheckConfig("DisableDangerousLua=0\nDisableDangerousLua=1",false,false);
    CheckConfig("DisableDangerousLua=0\nDisableDangerousLua=invalid",false,false);
    CheckConfig("DisableDangerousLua=0\nDisableDangerousLua=",false,false);
    std::istringstream unreadableLua("DisableDangerousLua=0");unreadableLua.setstate(std::ios::badbit);
    Require(ReadAexConfig(unreadableLua).disableDangerousLua,"Read errors must not disable Lua protection");
    std::istringstream throwingLua("DisableDangerousLua=0");throwingLua.exceptions(std::ios::badbit|std::ios::failbit);
    Require(ReadAexConfig(throwingLua).disableDangerousLua,"Config read exceptions must keep Lua protection enabled");
    for(const char* value:{"1","2","true","00","01","0junk","","0 = 0"})
      CheckConfig(std::string("DisableAchievementReset = ")+value,false,false);
    CheckConfig("DisableAchievementReset = 0",false,false,false,true,false);
    CheckConfig("\xef\xbb\xbf# config\r\n\tDisableAchievementReset \t= \t0 # restore original command\r\n",false,false,false,true,false);
    CheckConfig("# DisableAchievementReset = 0\nOtherDisableAchievementReset = 0\ndisableachievementreset=0",false,false);
    CheckConfig("DisableAchievementReset=0\nDisableAchievementReset=1",false,false);
    CheckConfig("DisableAchievementReset=1\nDisableAchievementReset=0",false,false,false,true,false);
    CheckConfig("DisableAchievementReset=0\nDisableAchievementReset=invalid",false,false);
    CheckConfig("Log=1\nSkipLongbow=1\nNoScreenTransition=1\nDisableDangerousLua=1\nDisableAchievementReset=0",true,true,true,true,false);
    std::istringstream unreadableProtection("DisableAchievementReset=0");unreadableProtection.setstate(std::ios::badbit);
    Require(ReadAexConfig(unreadableProtection).disableAchievementReset,"Read errors must not disable achievement protection");
    std::puts("PASS: config defaults, independent settings, exact values, comments, BOM, duplicate keys, and read errors");

    Require(VerifiedGame(argv[1]),"Unsupported executable hash");
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"Could not map game executable");
    auto* base=reinterpret_cast<uint8_t*>(module);
    VerifySkipLongbow(base);
    const auto ticks=&Ticks;WriteMemory(base+0x15f310,&ticks,sizeof(ticks));
    auto& state=*reinterpret_cast<int*>(base+0x1a7074);
    auto& previous=*reinterpret_cast<int*>(base+0x1a4090);
    auto& input=*reinterpret_cast<int*>(base+0x1b93b8);
    auto& key=*(base+0x1a719d);
    const auto update=reinterpret_cast<int (__cdecl*)()>(base+0x99a00);
    state=0;previous=-1;input=0;key=0;gTickCalls=0;gElapsed=5001;
    Require(update()==1&&state==2&&gTickCalls==2,"Original timed splash transition failed");
    state=0;previous=-1;input=1;gTickCalls=0;
    Require(update()==1&&state==2&&gTickCalls==1,"Original input splash transition failed");
    InstallSkipLongbow(base);
    state=0;previous=-1;input=0;key=0;gTickCalls=0;gElapsed=0;
    Require(update()==1&&state==2&&gTickCalls==1,"Enabled skip did not reach title immediately without input");
    state=0;previous=0;gTickCalls=0;
    Require(update()==1&&state==2&&gTickCalls==0,"Enabled skip failed when splash was already active");
    std::puts("PASS: original splash timeout/input transitions and immediate title transition with SkipLongbow");
    FreeLibrary(module);
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
