#pragma once
// Optional deterministic input audit: set AEX_EDITOR_INPUT_AUDIT=1 before
// running editor-script-tests.exe. Compare the highlighted control against
// an otherwise identical Rich Edit control without the highlighter.
static std::string Escaped(const std::string& text) {
  std::string out;
  for(unsigned char c:text) {
    if(c=='\r')out+="\\r";else if(c=='\n')out+="\\n";else if(c=='\t')out+="\\t";
    else if(c<32||c>=127){char hex[5];std::snprintf(hex,sizeof(hex),"\\x%02x",c);out+=hex;}
    else out+=c;
  }
  return out;
}
static uint32_t auditRandom=0x52bb19a7;
static uint32_t NextAuditRandom() {auditRandom^=auditRandom<<13;auditRandom^=auditRandom>>17;auditRandom^=auditRandom<<5;return auditRandom;}
static void InputAudit(HWND parent,HWND edit) {
  auto* dialogs=reinterpret_cast<HWND*>(gImage+0x51e3c);dialogs[0]=parent;
  const auto reference=CreateWindowExA(0,"RichEdit20A","",WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL,0,0,550,350,parent,reinterpret_cast<HMENU>(0x500),GetModuleHandleW(nullptr),nullptr);
  Require(reference!=nullptr,"Cannot create reference control");SendMessageA(reference,EM_EXLIMITTEXT,0,editor_scripts::kMaxTextBytes);
  unsigned cases=0,failures=0,trailingSpaces=0,otherFailures=0;
  auto check=[&](const char* label,const std::string& expected) {
    cases++;const auto actual=ControlText(edit);const auto script=editor_scripts::Script(0);
    const bool stored=editor_scripts::Length(script)==expected.size()&&std::memcmp(editor_scripts::Text(script),expected.data(),expected.size())==0;
    if(actual!=expected||!stored) {
      failures++;
      if(!expected.empty()&&expected.back()=='\n'&&actual==expected+" "&&stored)trailingSpaces++;
      else if(otherFailures++<30)std::printf("FAIL %s: expected=[%s] actual=[%s] stored=%s\n",label,Escaped(expected).c_str(),Escaped(actual).c_str(),stored?"yes":"no");
      return false;
    }
    return true;
  };
  auto load=[&](const char* label,const std::string& input) {
    Require(SetWindowTextA(reference,"")&&SetWindowTextA(reference,input.c_str())!=0,"Cannot set reference fixture");
    const auto normalized=ControlText(reference);
    {editor_scripts::Updating guard;Require(SetWindowTextA(edit,"")&&SetWindowTextA(edit,normalized.c_str())!=0,"Cannot set audit fixture");}
    SendMessageA(parent,WM_COMMAND,MAKEWPARAM(0x40a,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
    return check(label,normalized);
  };
  const std::vector<std::string> fragments={"a","return","123","-","--","---","--[[","--[=[","[[","]]","]=]","--]]","{","}","\\","'","\""," ","\t","\r\n","\n","\r","\\par","\\u1234?","\\'00","\\bin10","\\*","\\rtf1",std::string(1,'\x80'),std::string(1,'\xe9')};
  const std::vector<std::string> seeds={"prefixsuffix","local a = { 1, 2 }\r\nreturn a","-- comment\r\nreturn 42","a--[[block]]b\r\nreturn 42","--[[\r\nblock\r\n]]\r\nreturn 42","--[=[long comment]=]\r\nreturn 42","local s = \"--[[ string ]]\"\r\nreturn s","local s = [[-- string]]\r\nreturn s","local s = [=[-- string]=]\r\nreturn s","--[[--]]","--[[\r\n--]]","a--\r\n--[[b]]c","","-","--","--[","--[[","--[[[","--[[--[[nested]]tail]]"};
  for(const auto& seed:seeds)for(const auto& fragment:fragments)for(size_t i=0;i<=seed.size();i++) {
    auto input=seed;input.insert(i,fragment);load("seed insertion",input);
  }
  std::printf("AUDIT seed insertions: %u comparisons, %u failures\n",cases,failures);std::fflush(stdout);
  const std::string alphabet="a-[]{}\\'\"\r\n\t";
  for(uint32_t number=0;number<20736;number++) {
    auto value=number;std::string input="prefix";
    for(int i=0;i<4;i++){input+=alphabet[value%alphabet.size()];value/=static_cast<uint32_t>(alphabet.size());}
    input+="suffix\r\nreturn 42";load("four-character combinations",input);
  }
  std::printf("AUDIT combinations: %u comparisons, %u failures\n",cases,failures);std::fflush(stdout);
  for(int trial=0;trial<15000;trial++) {
    std::string input;
    const auto count=NextAuditRandom()%25;
    for(uint32_t i=0;i<count;i++)input+=fragments[NextAuditRandom()%fragments.size()];
    if(trial%2)input+="\r\nlocal sentinel = 42\r\nreturn sentinel";load("random fragments",input);
  }
  for(const auto& seed:seeds) {
    if(!load("undo seed",seed))continue;
    for(const auto control:{reference,edit}) {
      SendMessageA(control,EM_EMPTYUNDOBUFFER,0,0);
      SendMessageA(control,EM_SETSEL,2,2);
      SendMessageA(control,WM_CHAR,'x',1);
    }
    check("before undo",ControlText(reference));
    for(const auto control:{reference,edit})SendMessageA(control,WM_UNDO,0,0);
    check("undo",ControlText(reference));
    for(const auto control:{reference,edit})SendMessageA(control,EM_REDO,0,0);
    check("redo",ControlText(reference));
  }
  // Other ANSI characters which can be pasted into the script control.
  for(unsigned byte=1;byte<256;byte++) {
    if(byte=='\r'||byte=='\n')continue;
    const std::string c(1,static_cast<char>(byte));
    load("ANSI byte",std::string("prefix")+c+"suffix\r\nreturn 42");
    load("ANSI byte in comment",std::string("-- comment ")+c+"\r\nreturn 42");
  }
  std::printf("AUDIT categories: %u trailing-space insertions, %u other text failures\n",trailingSpaces,otherFailures);
  std::printf("AUDIT random fragments: %u comparisons, %u failures\n",cases,failures);std::fflush(stdout);
  for(int trial=0;trial<300;trial++) {
    if(!load("editing seed",seeds[NextAuditRandom()%seeds.size()]))continue;
    for(int step=0;step<25;step++) {
      SendMessageA(reference,EM_SETSEL,static_cast<WPARAM>(-1),-1);CHARRANGE end{};SendMessageA(reference,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&end));
      LONG at=static_cast<LONG>(NextAuditRandom()%(static_cast<uint32_t>(end.cpMax)+1));
      CHARRANGE selection{at,at};if(NextAuditRandom()%3==0)selection.cpMax=at+static_cast<LONG>(NextAuditRandom()%(static_cast<uint32_t>(end.cpMax-at)+1));
      SendMessageA(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageA(reference,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
      const auto action=NextAuditRandom()%8;
      const auto fragment=fragments[NextAuditRandom()%fragments.size()];
      const auto typed=static_cast<WPARAM>(32+NextAuditRandom()%95);
      if(action==0)for(const auto control:{reference,edit})SendMessageA(control,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(fragment.c_str()));
      else if(action==1)for(const auto control:{reference,edit})SendMessageA(control,WM_CHAR,typed,1);
      else if(action==2)for(const auto control:{reference,edit})SendMessageA(control,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(""));
      else if(action==3)for(const auto control:{reference,edit})SendMessageA(control,WM_CHAR,VK_BACK,1);
      else if(action==4)for(const auto control:{reference,edit})SendMessageA(control,WM_CHAR,VK_RETURN,1);
      else if(action==5)for(const auto control:{reference,edit})SendMessageA(control,WM_UNDO,0,0);
      else if(action==6)for(const auto control:{reference,edit})SendMessageA(control,EM_REDO,0,0);
      else for(const auto control:{reference,edit}) {
        SendMessageA(control,WM_KEYDOWN,VK_DELETE,1);SendMessageA(control,WM_KEYUP,VK_DELETE,0xc0000001);
      }
      if(!check("random edit",ControlText(reference)))break;
      CHARRANGE actual{},expected{};SendMessageA(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&actual));SendMessageA(reference,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&expected));
      if(actual.cpMin!=expected.cpMin||actual.cpMax!=expected.cpMax){failures++;std::printf("FAIL caret: expected %ld..%ld actual %ld..%ld\n",expected.cpMin,expected.cpMax,actual.cpMin,actual.cpMax);break;}
    }
  }
  std::printf("AUDIT total: %u comparisons, %u failures (seed 0x52bb19a7; ANSI code page %u)\n",cases,failures,GetACP());std::fflush(stdout);
  Require(failures==0,"Input audit found text corruption");DestroyWindow(reference);
}

