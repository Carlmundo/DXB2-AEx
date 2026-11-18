#pragma once
#include <istream>
#include <string>

struct AexConfig {
  bool log=false;
  bool skipLongbow=false;
  bool noScreenTransition=false;
  bool holdClickFire=false;
  bool launchAngle=false;
  bool disableDangerousLua=true;
  bool disableAchievementReset=true;
};

inline AexConfig ReadAexConfig(std::istream& config) noexcept {
  try {
    AexConfig settings;
    std::string line;
    bool firstLine=true;
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
      const auto enabled=value=="1";
      if(key=="Log")settings.log=enabled;
      else if(key=="SkipLongbow")settings.skipLongbow=enabled;
      else if(key=="NoScreenTransition")settings.noScreenTransition=enabled;
      else if(key=="HoldClickFire")settings.holdClickFire=enabled;
      else if(key=="LaunchAngle")settings.launchAngle=enabled;
      else if(key=="DisableDangerousLua")settings.disableDangerousLua=value!="0";
      else if(key=="DisableAchievementReset")settings.disableAchievementReset=value!="0";
    }
    return config.bad()?AexConfig{}:settings;
  }catch(...){return {};}
}
