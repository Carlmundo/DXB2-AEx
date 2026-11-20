// File migration tests use an isolated directory; no game or updater is started.
#include "config.hpp"
#include <windows.h>
#include <cstdio>
#include <stdexcept>

static void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static std::string Read(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  Require(input.is_open(),"Cannot read fixture");
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
static void Write(const std::filesystem::path& path,const std::string& text) {
  std::ofstream output(path,std::ios::binary|std::ios::trunc);
  output<<text;Require(output.good(),"Cannot write fixture");
}
static std::string LF(std::string text) {
  for(size_t i=0;(i=text.find('\r',i))!=std::string::npos;)text.erase(i,1);
  return text;
}
static void CheckDefaults(const AexConfig& config) {
  Require(!config.skipLongbow&&!config.noScreenTransition&&!config.holdClickFire&&
    !config.launchAngle&&!config.rehabMode&&!config.log&&config.disableDangerousLua&&
    config.disableAchievementReset&&config.checkForUpdates,"Incorrect requested defaults");
}

int wmain(int argc,wchar_t** argv) {
  if(argc!=3){std::fprintf(stderr,"Usage: config-tests.exe new-fixture-directory reference-aex.cfg\n");return 2;}
  try {
    const std::filesystem::path directory=argv[1],path=directory/L"aex.cfg";
    Require(std::filesystem::create_directory(directory),"Use a new fixture directory");
    CheckDefaults(LoadAexConfig(path));
    const auto generated=Read(path);
    Require(LF(generated)==LF(Read(argv[2])),"Generated file differs from documented template");
    const auto timestamp=std::filesystem::last_write_time(path);
    CheckDefaults(LoadAexConfig(path));
    Require(Read(path)==generated&&std::filesystem::last_write_time(path)==timestamp,"Complete file was rewritten");
    Write(path,"");CheckDefaults(LoadAexConfig(path));
    Require(Read(path)==generated,"Empty file did not get the template");
    std::puts("PASS: missing/empty config generates documented defaults; repeated startup leaves complete file untouched");

    // Remove each option in turn; every other setting is deliberately non-default.
    for(size_t missing=0;missing<std::size(kAexConfigOptions);++missing) {
      std::string original="# My settings\nFutureOption = 7\n";
      for(size_t i=0;i<std::size(kAexConfigOptions);++i)if(i!=missing) {
        const auto& option=kAexConfigOptions[i];
        original+=std::string(option.key)+" = "+(option.initial?"0":"1")+" # keep\n";
      }
      Write(path,original);
      const auto config=LoadAexConfig(path);
      const auto migrated=Read(path);
      const auto& option=kAexConfigOptions[missing];
      Require(migrated.compare(0,original.size(),original)==0,"Existing bytes changed");
      Require(migrated.substr(original.size())=="\n"+std::string(option.key)+" = "+
        (option.initial?"1":"0")+"\n"+option.description+"\n","Missing option or description incorrect");
      for(size_t i=0;i<std::size(kAexConfigOptions);++i)
        Require(config.*(kAexConfigOptions[i].member)==(i==missing?kAexConfigOptions[i].initial:!kAexConfigOptions[i].initial),"Migration changed an existing value");
      LoadAexConfig(path);Require(Read(path)==migrated,"Repeated migration duplicated an option");
    }
    std::puts("PASS: each missing option gets its default and description; existing values/comments/unknown keys preserved");

    for(const char* newline:{"\n","\r\n"}) {
      const std::string original=std::string("\xef\xbb\xbf# Custom")+newline+
        "# SkipLongbow = 0"+newline+"OtherSkipLongbow = 0"+newline+
        "Log=1"+newline+"Log=0"+newline+" CheckForUpdates\t= 0 # offline"+newline+
        "HoldClickFire = invalid"+newline+"DisableDangerousLua = 0";
      Write(path,original);const auto settings=LoadAexConfig(path);
      const auto migrated=Read(path);
      Require(migrated.compare(0,original.size(),original)==0,"Custom config prefix changed");
      Require(!settings.log&&!settings.checkForUpdates&&!settings.holdClickFire&&!settings.disableDangerousLua,"Existing parsing behavior changed");
      Require(migrated.find(std::string(newline)+newline+"SkipLongbow = 0"+newline+"# Skip the Longbow Games splash screen")!=std::string::npos,"Comment/unknown key hid missing option or line ending changed");
      Require(migrated.find("HoldClickFire = 0")==std::string::npos&&migrated.find("CheckForUpdates = 1")==std::string::npos,"Invalid/disabled existing option overwritten");
      LoadAexConfig(path);Require(Read(path)==migrated,"BOM or missing final newline caused repeated appends");
    }
    std::puts("PASS: BOM, LF/CRLF, missing final newline, duplicate keys, disabled and invalid values preserved");

    Write(path,"SkipLongbow=1\nCheckForUpdates=0");
    Require(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot make read-only fixture");
    const auto readOnly=LoadAexConfig(path);
    Require(readOnly.skipLongbow&&!readOnly.checkForUpdates&&readOnly.disableDangerousLua,"Write failure lost loaded settings/defaults");
    Require(Read(path)=="SkipLongbow=1\nCheckForUpdates=0","Read-only file changed");
    Require(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL)!=0,"Cannot restore fixture attributes");
    const auto locked=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(locked!=INVALID_HANDLE_VALUE,"Cannot lock fixture");
    CheckDefaults(LoadAexConfig(path));CloseHandle(locked);
    Require(Read(path)=="SkipLongbow=1\nCheckForUpdates=0","Unreadable existing file changed");
    CheckDefaults(LoadAexConfig(directory/L"nonexistent"/L"aex.cfg"));
    Require(!std::filesystem::exists(directory/L"nonexistent"),"Unexpected parent directory creation");
    CheckDefaults(LoadAexConfig(directory));
    const auto unicode=directory/L"\u00e9\u7403.cfg";
    CheckDefaults(LoadAexConfig(unicode));Require(Read(unicode)==generated,"Unicode config path failed");
    std::puts("PASS: read-only, locked, invalid and Unicode paths; IO failures use defaults without damaging existing files");
    return 0;
  }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
