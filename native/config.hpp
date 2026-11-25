#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <istream>
#include <iterator>
#include <sstream>
#include <string>

// Add options here: this defines the runtime defaults, parser and file template.
// The fourth argument keeps protection enabled for values other than exactly 0.
#define AEX_CONFIG_OPTIONS(X) \
  X(SkipLongbow,skipLongbow,false,false,"# Skip the Longbow Games splash screen") \
  X(NoScreenTransition,noScreenTransition,false,false,"# Disable the carpet rolling screen transition") \
  X(HoldClickFire,holdClickFire,false,false,"# Hold left-click to fire from the laser paddle every 200ms") \
  X(LaunchAngle,launchAngle,false,false,"# Restore the original launch angle for new balls") \
  X(RehabMode,rehabMode,false,false,"# Unlimited paddles and number-key ball speeds (1 slowest, 0 fastest)\n# Disables local high-score entry and online score submission\n# Saved Rehab games remain ineligible after turning RehabMode off") \
  X(DisableDangerousLua,disableDangerousLua,true,true,"# Disable execution of dangerous lua commands from community content (default=1)") \
  X(DisableAchievementReset,disableAchievementReset,true,true,"# Prevents a \"cheat code\" from resetting Steam achievements (default=1)") \
  X(FilterScores,filterScores,true,true,"# Filter out scores from known cheaters (default=1)") \
  X(CheckForUpdates,checkForUpdates,true,false,"# Check at startup for AEx updates") \
  X(Log,log,false,false,"# Disable / Enable Logging")

struct AexConfig {
#define AEX_CONFIG_MEMBER(key,member,initial,protect,description) bool member=initial;
  AEX_CONFIG_OPTIONS(AEX_CONFIG_MEMBER)
#undef AEX_CONFIG_MEMBER
};

struct AexConfigOption {
  const char* key;
  bool AexConfig::* member;
  bool initial;
  bool enabledUnlessZero;
  const char* description;
};
inline constexpr AexConfigOption kAexConfigOptions[]={
#define AEX_CONFIG_OPTION(key,member,initial,protect,description) {#key,&AexConfig::member,initial,protect,description},
  AEX_CONFIG_OPTIONS(AEX_CONFIG_OPTION)
#undef AEX_CONFIG_OPTION
};
#undef AEX_CONFIG_OPTIONS
using AexConfigPresence=std::array<bool,std::size(kAexConfigOptions)>;

inline AexConfig ReadAexConfig(std::istream& config,AexConfigPresence* present=nullptr) noexcept {
  try {
    AexConfig settings;
    std::string line;
    bool firstLine=true;
    if(present)present->fill(false);
    while(std::getline(config,line)) {
      if(firstLine&&line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
      firstLine=false;
      const auto comment=line.find('#');
      if(comment!=std::string::npos)line.erase(comment);
      const auto equals=line.find('=');
      if(equals==std::string::npos)continue;
      const auto trim=[](const std::string& text) {
        const auto first=text.find_first_not_of(" \t\r\n");
        if(first==std::string::npos)return std::string{};
        return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
      };
      const auto key=trim(line.substr(0,equals));
      const auto value=trim(line.substr(equals+1));
      for(size_t i=0;i<std::size(kAexConfigOptions);++i) {
        const auto& option=kAexConfigOptions[i];
        if(key!=option.key)continue;
        settings.*(option.member)=option.enabledUnlessZero?value!="0":value=="1";
        if(present)(*present)[i]=true;
        break;
      }
    }
    return config.bad()?AexConfig{}:settings;
  }catch(...){return {};}
}

inline std::string AexConfigAppendix(const std::string& existing,const AexConfigPresence& present) {
  const auto firstNewline=existing.find('\n');
  const char* newline=firstNewline==std::string::npos||
    (firstNewline>0&&existing[firstNewline-1]=='\r')?"\r\n":"\n";
  std::string appendix;
  for(size_t i=0;i<std::size(kAexConfigOptions);++i) {
    if(present[i])continue;
    if(appendix.empty()) {
      if(existing.empty())appendix=std::string("# AEx Options")+newline+newline;
      else {
        if(existing.back()!='\n')appendix+=newline;
        appendix+=newline;
      }
    }else appendix+=newline;
    const auto& option=kAexConfigOptions[i];
    appendix+=std::string(option.key)+" = "+(option.initial?"1":"0")+newline;
    for(const char* p=option.description;*p;++p) {
      if(*p=='\n')appendix+=newline;
      else appendix+=*p;
    }
    appendix+=newline;
  }
  return appendix;
}

inline AexConfig LoadAexConfig(const std::filesystem::path& path) noexcept {
  try {
    std::string existing;
    {
      std::ifstream input(path,std::ios::binary);
      if(input.is_open()) {
        existing.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
        if(input.bad())return {};
      }else {
        // An unreadable existing file must never be replaced or appended to.
        std::error_code error;
        if(std::filesystem::exists(path,error)||error)return {};
      }
    }
    std::istringstream input(existing);
    AexConfigPresence present{};
    const auto settings=ReadAexConfig(input,&present);
    const auto appendix=AexConfigAppendix(existing,present);
    if(!appendix.empty()) {
      // Append only: preserve every existing value, comment and unknown option.
      // Failure to write (e.g. read-only installs) still uses the loaded settings.
      std::ofstream output(path,std::ios::binary|std::ios::app);
      if(output)output.write(appendix.data(),static_cast<std::streamsize>(appendix.size()));
    }
    return settings;
  }catch(...){return {};}
}
