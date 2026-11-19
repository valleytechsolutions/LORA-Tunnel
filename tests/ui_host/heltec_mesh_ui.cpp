#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdint>
std::uint32_t hostMillis=2000;
#include "../../src/ui.cpp"
#include "messaging_regression.h"

// Unused startup/evidence dependencies still need symbols on MinGW. These
// placeholders are never drawn or treated as logo/Recon validation.
const uint8_t logoStart[24]={},logoEnd[1]={},vtsStart[24]={},vtsEnd[1]={};
namespace tunnel {namespace recon {const char* categoryName(Category){return "HOST FIXTURE";}}}

namespace {
unsigned assertions=0;
struct Sent {ui::Action action;int value;std::string text;};
std::vector<Sent> sent;
void callback(ui::Action action,int value,const char* text) {sent.push_back({action,value,text});}
void expect(bool ok,const char* message) {++assertions;if(!ok){std::cerr<<"FAIL: "<<message<<"\n";std::exit(1);}}
std::string quote(const std::string& text) {std::string out="\"";for(char c:text){if(c=='\\'||c=='\"')out+='\\';out+=c;}return out+'\"';}
void paint(const ui::Model& model) {
  target().commands.clear();fullClearNeeded=true;
  if(page==Page::Home)home(model);
  else if(page==Page::Wardriver)wardriver(model);
  else if(page==Page::Recon)recon(model);
  else if(page==Page::ReconDetail)reconDetail();
  else if(page==Page::Credentials)credentials(model);
  else if(page==Page::Regions)regions(model);
  else if(page==Page::Messages)messageScreen(model);
  else if(page==Page::Keyboard)drawKeyboard();
  else if(page==Page::Settings)settings(model);
  else if(page==Page::MeshSettings)meshSettings(model);
  else meshChoices(model);
  drawFocus(model);
}
void capture(const char* directory,const char* name,const ui::Model& model) {
  paint(model);std::ofstream out(std::string(directory)+"/"+name+".json",std::ios::binary);out<<"[";bool first=true;
  for(const auto& c:target().commands){if(!first)out<<",";first=false;
    out<<"{\"kind\":"<<quote(c.kind)<<",\"text\":"<<quote(c.text)<<",\"x\":"<<c.x<<",\"y\":"<<c.y<<",\"w\":"<<c.w<<",\"h\":"<<c.h<<",\"r\":"<<c.r<<",\"color\":"<<c.color<<",\"scale\":"<<c.scale<<"}";}
  out<<"]\n";expect(bool(out),"preview commands written");
}
bool screenHas(const char* text) {for(const auto& c:target().commands)if(c.text.find(text)!=std::string::npos)return true;return false;}
void tap(int x,int y,const ui::Model& m) {touchAction(uint16_t(x),uint16_t(y),m);paint(m);}
void press(uint8_t key,const ui::Model& m) {ui::navigation(key,m);paint(m);}
void touchTick(hw::TouchState state,uint16_t x,uint16_t y,uint32_t elapsed,const ui::Model& m) {
  hostMillis+=elapsed;hw::hostTouchState=state;hw::hostTouchX=x;hw::hostTouchY=y;
  ui::tick(m);
}
void touchTick(bool pressed,uint16_t x,uint16_t y,uint32_t elapsed,const ui::Model& m) {
  touchTick(pressed?hw::TouchState::Pressed:hw::TouchState::Released,x,y,elapsed,m);
}
void touchGestureChecks() {
  using ui::messages::Screen;using tunnel::mesh::Protocol;
  ui::Model m;m.region_selection_made=true;m.messages.active=true;m.messages.protocol=Protocol::Meshtastic;
  m.messages.tx_enabled=true;
  const bool previousBuffered=buffered;buffered=false;splashShown=onboardingShown=true;bootAt=0;
  wasPressed=false;touchReleasePending=false;focusVisible=false;hostMillis=10000;sent.clear();
  setPage(Page::Messages);messageState={};messageState.screen=Screen::Contacts;messagePage=0;
  touchTick(false,0,0,1,m);
  touchTick(true,100,296,1,m);
  expect(messageState.screen==Screen::Menu&&sent.empty(),"one Back touch leaves Contacts without stopping messaging");
  touchTick(true,100,296,10,m);
  expect(page==Page::Messages&&sent.empty(),"held Back does not repeat");
  touchTick(false,100,296,10,m); // A brief valid no-touch report also needs filtering.
  touchTick(true,100,296,10,m);
  expect(page==Page::Messages&&messageState.screen==Screen::Menu&&sent.empty(),"brief missing touch report cannot turn Back into Stop Messages");
  touchTick(hw::TouchState::Unavailable,100,296,500,m);
  touchTick(hw::TouchState::Unavailable,100,296,500,m);touchTick(true,100,296,1,m);
  expect(page==Page::Messages&&sent.empty(),"sustained touch read failure and recovery cannot repeat held Back");
  touchTick(false,100,296,1,m);touchTick(hw::TouchState::Unavailable,100,296,500,m);
  touchTick(true,100,296,1,m);
  expect(page==Page::Messages&&sent.empty(),"release then sustained unavailable then pressed is not a stable release");
  touchTick(false,100,296,1,m);touchTick(hw::TouchState::Unavailable,100,296,500,m);
  touchTick(false,100,296,1,m);touchTick(true,100,296,10,m);
  expect(page==Page::Messages&&sent.empty(),"unavailable reports interrupt rather than complete a pending release");
  touchTick(false,100,296,1,m);touchTick(false,100,296,70,m);
  touchTick(true,100,296,1,m);
  expect(page==Page::Home&&sent.size()==1&&sent.back().action==ui::Action::StopMessages,"a separate released Back press still stops messaging exactly once");
  touchTick(false,100,296,1,m);touchTick(false,100,296,70,m);
  setPage(Page::Messages);messageState={};messageState.screen=Screen::Menu;messagePage=1;
  m.messages.tx_enabled=false;const auto before=sent.size();
  touchTick(true,100,105,1,m);
  expect(messageState.screen==Screen::TxConfirm&&sent.size()==before,"real touch event opens TX confirmation without enabling transmission");
  touchTick(false,100,105,10,m);touchTick(true,100,225,10,m);
  expect(messageState.screen==Screen::TxConfirm&&sent.size()==before,"drag and brief missing touch report cannot accept the new TX confirmation");
  touchTick(hw::TouchState::Unavailable,100,105,500,m);touchTick(hw::TouchState::Unavailable,100,225,500,m);
  touchTick(true,100,225,1,m);
  expect(messageState.screen==Screen::TxConfirm&&sent.size()==before,"sustained controller failure cannot accept TX confirmation on recovery");
  touchTick(false,100,225,1,m);touchTick(false,100,225,70,m);
  touchTick(true,100,225,1,m);
  expect(messageState.screen==Screen::Menu&&sent.size()==before+1&&sent.back().action==ui::Action::ToggleMessageTx&&sent.back().value==1,"a separate confirm touch enables TX once");
  touchTick(true,100,225,100,m);
  expect(sent.size()==before+1,"holding confirmed TX touch emits no duplicate action");
  touchTick(false,100,225,1,m);touchTick(false,100,225,70,m);
  // The release filter must use unsigned time, just like the rest of the UI.
  hostMillis=UINT32_MAX-30;bootAt=hostMillis-2000;messageState.screen=Screen::Contacts;
  touchTick(true,100,296,1,m);const auto beforeWrap=sent.size();
  touchTick(false,100,296,1,m);touchTick(true,100,296,40,m);
  expect(page==Page::Messages&&sent.size()==beforeWrap,"touch dropout spanning millis wrap does not repeat Back");
  touchTick(false,100,296,1,m);touchTick(false,100,296,70,m);
  touchTick(true,100,296,1,m);
  expect(page==Page::Home&&sent.size()==beforeWrap+1,"released Back remains usable across millis wrap");
  touchTick(false,100,296,1,m);touchTick(false,100,296,70,m);
  // Exercise the real focus target ordering, not only touch coordinates.
  hostMillis=10000;bootAt=0;m.buttons_enabled=true;
  setPage(Page::Messages);messageState={};messageState.screen=Screen::Menu;messagePage=0;focusIndex=-1;
  touchTick(hw::TouchState::Unavailable,0,0,500,m);
  for(unsigned i=0;i<6;++i)press(hw::Next,m);
  const auto beforeButtons=sent.size();press(hw::Select,m);
  expect(messagePage==1&&sent.size()==beforeButtons,"physical buttons reach the next message menu page without stopping the radio");
  expect(focusIndex==0,"new message page visibly focuses its first row");press(hw::Select,m);
  expect(messageState.screen==Screen::TxConfirm&&sent.size()==beforeButtons,"physical Select opens TX confirmation on the second menu page");
  expect(focusIndex==0,"TX confirmation defaults physical focus to Cancel");press(hw::Next,m);press(hw::Select,m);
  expect(messageState.screen==Screen::Menu&&sent.size()==beforeButtons+1&&sent.back().action==ui::Action::ToggleMessageTx&&sent.back().value==1,"physical buttons select explicit TX approval exactly once");
  expect(hw::hostTouchState==hw::TouchState::Unavailable,"physical navigation remains independent of unavailable touch hardware");
  hw::hostTouchState=hw::TouchState::Released;
  buffered=previousBuffered;sent.clear();hostMillis=2000;bootAt=0;focusVisible=false;messageEditor=false;
}
void messagingChecks(const char* output,ui::Model& m) {
  ui_message_regression::run(expect);
  using ui::messages::Screen;using tunnel::messaging::Delivery;using tunnel::mesh::Protocol;
  m.messages={};m.notice[0]=0;m.running=m.upload_busy=false;
  std::strcpy(m.profile,"US LongTurbo / 908.750 MHz");std::strcpy(m.messages.channel,"LongTurbo");std::strcpy(m.messages.name,"Example operator");
  std::strcpy(m.messages.identity,"00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF");m.messages.identity_ready=true;
  setPage(Page::Home);tap(100,260,m);expect(page==Page::Messages&&messageState.screen==Screen::Mode,"Home touch opens Messages separately from capture");capture(output,"messages-protocol",m);
  const auto before=sent.size();tap(100,105,m);expect(sent.size()==before+1&&sent.back().action==ui::Action::StartMessages&&sent.back().value==1,"protocol touch starts Meshtastic without capture action");
  m.messages.active=true;capture(output,"messages-menu",m);expect(!ui::allowsBackgroundUpload()&&!ui::allowsUploadJobPolling(),"Messages suppresses upload work");
  tap(200,252,m);expect(messagePage==1,"message menu next page reachable by touch");capture(output,"messages-menu-more",m);
  const auto noTx=sent.size();tap(100,105,m);expect(messageState.screen==Screen::TxConfirm&&sent.size()==noTx,"enable TX requires confirmation");capture(output,"messages-enable-tx",m);
  tap(100,192,m);expect(sent.size()==noTx&&!m.messages.tx_enabled,"TX confirmation cancel sends no action");
  tap(200,252,m);tap(100,105,m);tap(100,225,m);expect(sent.back().action==ui::Action::ToggleMessageTx&&sent.back().value==1,"explicit TX enable accepted");m.messages.tx_enabled=true;
  tap(100,139,m);expect(page==Page::Keyboard&&messageEditor&&messageState.target==-1,"channel compose uses existing touchscreen keyboard");
  tap(16,122,m);tap(40,260,m);tap(16,122,m);tap(120,260,m);tap(40,149,m);tap(165,230,m);
  expect(std::string(input)=="qQ@ ","touch keyboard supports mixed case, punctuation and space in message");tap(200,260,m);expect(std::string(input)=="qQ@","touch Backspace works");
  for(unsigned i=0;i<140;++i)append('x');expect(inputSize==120,"touch message limited to120 bytes");capture(output,"messages-compose",m);
  tap(175,296,m);expect(sent.back().action==ui::Action::SendMessage&&sent.back().value==-1&&sent.back().text.size()==120,"Send queues exactly one channel message");expect(!inputSize&&!messageEditor,"sent draft erased");
  tunnel::messaging::Record records[6]{};tunnel::messaging::Contact contacts[8]{};
  for(unsigned i=0;i<6;++i) {auto& r=records[i];r.protocol=Protocol::Meshtastic;r.id=100+i;r.outgoing=i>0;r.direct=i==3;r.delivery=static_cast<Delivery>(i);std::snprintf(r.name,sizeof(r.name),"Example peer %u",i);std::memset(r.text,'a'+i,120);}
  records[4].delivery=Delivery::Sent;
  for(unsigned i=0;i<8;++i) {auto& c=contacts[i];c.protocol=Protocol::Meshtastic;c.node=0x12340000+i;std::snprintf(c.name,sizeof(c.name),"Example contact %u",i);for(unsigned j=0;j<32;++j)c.public_key[j]=uint8_t(j+1+i);}
  m.messages.history=records;m.messages.history_count=255;m.messages.contacts=contacts;m.messages.contact_count=255;
  expect(ui::messages::count(messageState,m)==6,"inbox count is bounded to actual six slots");capture(output,"messages-inbox",m);tap(200,252,m);capture(output,"messages-inbox-more",m);tap(100,105,m);capture(output,"messages-detail",m);
  expect(screenHas("Sent (unconfirmed)"),"successful transmission is not called Delivered");records[4].delivery=Delivery::ReceiptUnverified;capture(output,"messages-unverified-receipt",m);expect(screenHas("Receipt (unverified)"),"open detail tracks unverified receipt status");records[4].delivery=Delivery::Delivered;capture(output,"messages-delivered",m);expect(screenHas("Delivered"),"verified receipt updates open detail");tap(100,296,m);tap(100,296,m);tap(100,173,m);capture(output,"messages-contacts",m);tap(200,252,m);capture(output,"messages-contacts-more",m);tap(30,252,m);tap(100,105,m);
  capture(output,"messages-contact",m);const auto untrusted=sent.size();tap(100,157,m);expect(page==Page::Messages&&sent.size()==untrusted&&messageState.error[0],"untrusted contact cannot compose private text");
  tap(100,191,m);expect(messageState.screen==Screen::Trust,"trust opens independent fingerprint confirmation");capture(output,"messages-trust",m);tap(100,231,m);
  expect(sent.back().action==ui::Action::TrustContact&&sent.back().value==0&&sent.back().text.size()==64,"trust action binds displayed full key and slot");
  contacts[0].trusted=true;capture(output,"messages-contact-trusted",m);tap(100,157,m);expect(page==Page::Keyboard&&messageState.target==0,"trusted private composer selects target");append('Q');const auto cancel=sent.size();tap(50,296,m);expect(sent.size()==cancel&&!inputSize&&page==Page::Messages,"cancel private draft wipes without sending");
  contacts[0].key_changed=true;capture(output,"messages-key-changed",m);tap(100,157,m);expect(page==Page::Messages&&messageState.error[0],"changed key cannot use previous trust");
  contacts[0].key_changed=contacts[0].trusted=false;tap(100,191,m);const auto reviewed=messageState.contact;const auto beforeReplacement=sent.size();
  contacts[0].public_key[1]^=0x40;paint(m);
  expect(messageState.contact_stale&&!std::memcmp(messageState.contact.public_key,reviewed.public_key,32),"Heltec redraw cannot replace the fingerprint during review");
  tap(100,231,m);expect(sent.size()==beforeReplacement&&messageState.screen==Screen::Trust,"Heltec touch approval rejects a changed live key and requires reopening");
  messageState.screen=Screen::Settings;messagePage=0;setPage(Page::Messages);capture(output,"messages-settings",m);tap(100,173,m);expect(messageEditor&&!inputSize,"key editor blank on entry");capture(output,"messages-key-blank",m);append('Q');const auto invalid=sent.size();tap(175,296,m);expect(page==Page::Keyboard&&sent.size()==invalid,"invalid hex cannot save");
  tap(200,260,m);for(unsigned i=0;i<32;++i)append('A');capture(output,"messages-key-masked",m);expect(!screenHas("AAAA"),"saved channel secret never shown in pixels");tap(175,296,m);expect(sent.back().action==ui::Action::SaveMessagingSetting&&sent.back().value==2&&sent.back().text.size()==32,"key saves through separate messaging action");
  tap(200,252,m);capture(output,"messages-settings-more",m);tap(100,105,m);append('8');const auto badHop=sent.size();tap(175,296,m);expect(sent.size()==badHop&&messageState.error[0],"hop limit outside0..7 rejected");tap(200,260,m);append('3');tap(175,296,m);expect(sent.back().value==4&&sent.back().text=="3","valid hop saved");
  for(Screen screen:{Screen::Mode,Screen::Menu,Screen::Inbox,Screen::Detail,Screen::Contacts,Screen::Contact,Screen::Trust,Screen::Settings,Screen::TxConfirm,Screen::Identity}) {
    messageState.screen=screen;setPage(Page::Messages);const unsigned count=ui::messages::count(messageState,m),visible=messageVisible();
    for(unsigned p=0;p<std::max(1U,(count+visible-1)/visible);++p) {
      messagePage=p;Rect targets[MAX_TARGETS];const auto n=focusTargets(m,targets);
      for(size_t i=0;i<n;++i) {focusVisible=true;focusIndex=int(i);paint(m);++assertions;}
    }
  }
  messageState.screen=Screen::Identity;messagePage=0;setPage(Page::Messages);capture(output,"messages-identity",m);
  messageState.screen=Screen::Menu;messagePage=0;setPage(Page::Messages);press(hw::Back,m);expect(page==Page::Home&&sent.back().action==ui::Action::StopMessages,"hardware Back exits Messages by stopping its radio");
  m.messages.active=false;tap(100,260,m);tap(100,139,m);expect(sent.back().action==ui::Action::StartMessages&&sent.back().value==2,"MeshCore selectable by touch");m.messages.protocol=Protocol::MeshCore;m.messages.active=true;std::strcpy(m.messages.channel,"Public");capture(output,"messages-meshcore",m);
  messageState.screen=Screen::Menu;m.messages.tx_enabled=false;tap(100,139,m);append('Q');const auto off=sent.size();tap(175,296,m);expect(sent.size()==off&&page==Page::Keyboard&&messageState.error[0],"TX-off keyboard cannot queue transmission");tap(50,296,m);
  messageState.screen=Screen::Menu;press(hw::Back,m);m.messages={};m.running=true;tap(100,260,m);tap(100,105,m);expect(messageState.screen==Screen::Mode&&sent.back().action==ui::Action::StartMessages,"active capture leaves chooser visible and asks root to report stop/save first");press(hw::Back,m);m.running=false;messageEditor=false;
}
}
bool textOverlaps() {
  const auto& c=target().commands;
  for(size_t i=0;i<c.size();++i)for(size_t j=i+1;j<c.size();++j) {
    if(c[i].kind!="text"||c[j].kind!="text"||!c[i].w||!c[j].w)continue;
    if(c[i].x<c[j].x+c[j].w&&c[j].x<c[i].x+c[i].w&&c[i].y<c[j].y+c[j].h&&c[j].y<c[i].y+c[i].h) {
      std::cerr<<"overlap: \""<<c[i].text<<"\" @"<<c[i].x<<","<<c[i].y<<" / \""<<c[j].text<<"\" @"<<c[j].x<<","<<c[j].y<<"\n";return true;
    }
  }
  return false;
}
// Rotatable targets map the portrait layout onto 320x240. Every page and focus
// outline must stay on the landscape surface, and text rows must not collide.
void reconFilterChecks(const char* output,ui::Model m,const char* preview) {
  using tunnel::recon::Category;
  std::strcpy(m.recon_recent[0],"Flock camera");m.recon_categories[0]=uint8_t(Category::Flock);
  std::strcpy(m.recon_recent[1],"AirTag");m.recon_categories[1]=uint8_t(Category::AirTag);
  reconFilter=0;setPage(Page::Recon);paint(m);expect(screenHas("FILTER: ALL v"),"closed filter shows a dropdown caret");
  const size_t before=sent.size();
  tap(90,90,m);expect(reconFilterOpen&&screenHas("FILTER: ALL ^")&&!textOverlaps(),"filter tap opens the dropdown");
  for(uint8_t c=0;c<=LAST_CATEGORY;++c)expect(screenHas(categoryShort(c)),"dropdown lists every category");
  {Rect targets[MAX_TARGETS];const size_t count=focusTargets(m,targets);expect(count==LAST_CATEGORY+1u,"open dropdown focuses only its options");
   for(size_t i=0;i<count;++i){focusVisible=true;focusIndex=int(i);paint(m);++assertions;}focusVisible=false;focusIndex=-1;}
  capture(output,preview,m);
  const Rect flock=filterOption(uint8_t(Category::Flock));tap(flock.x+5,flock.y+5,m);
  expect(!reconFilterOpen&&reconFilter==uint8_t(Category::Flock)&&screenHas("Flock camera")&&!screenHas("AirTag"),"choosing an option applies the filter and closes");
  tap(90,90,m);tap(120,55,m);
  expect(!reconFilterOpen&&reconFilter==uint8_t(Category::Flock)&&page==Page::Recon&&sent.size()==before,"tapping outside dismisses without touching controls beneath");
  tap(90,90,m);press(hw::Back,m);expect(!reconFilterOpen&&page==Page::Recon,"Back closes the dropdown before leaving Recon");
  m.buttons_enabled=true;focusIndex=0;press(hw::Select,m);
  expect(reconFilterOpen&&focusIndex==int(Category::Flock),"physical Select opens with the current filter focused");
  press(hw::Next,m);press(hw::Select,m);
  expect(!reconFilterOpen&&reconFilter==uint8_t(Category::Flock)+1,"physical buttons choose the next category");
  reconFilter=0;focusVisible=false;focusIndex=-1;sent.resize(before);
}
void landscapeChecks(const char* output,ui::Model m) {
  using ui::messages::Screen;
  hw::hostRotatable=true;hw::setDisplayLandscape(true);buffered=true;applyOrientation();
  expect(landscapeUi&&frame.width==320&&frame.height==240,"landscape swaps framebuffer dimensions");
  m.running=true;m.battery=4.01f;m.gps=true;std::strcpy(m.notice,"Landscape notice text for wrapping checks on settings");
  const struct {Page page;const char* name;} pages[]={{Page::Home,"landscape-home"},{Page::Wardriver,"landscape-wardriver"},
    {Page::Recon,"landscape-recon"},{Page::ReconDetail,"landscape-recon-detail"},{Page::Settings,"landscape-settings"},
    {Page::Credentials,"landscape-credentials"},{Page::Regions,"landscape-regions"},{Page::Keyboard,"landscape-keyboard"},
    {Page::MeshSettings,"landscape-mesh-settings"}};
  for(const auto& entry:pages) {
    setPage(entry.page);paint(m);expect(!textOverlaps(),"landscape text rows do not overlap");
    Rect targets[MAX_TARGETS];const size_t count=focusTargets(m,targets);
    for(size_t i=0;i<count;++i){focusVisible=true;focusIndex=int(i);paint(m);++assertions;}
    focusVisible=false;focusIndex=-1;capture(output,entry.name,m);
  }
  setPage(Page::Keyboard);messageEditor=true;messageState={};messageState.composing=true;
  for(unsigned i=0;i<140;++i)append(char('a'+i%26));paint(m);
  expect(!textOverlaps(),"landscape compose lines do not overlap");capture(output,"landscape-compose",m);
  messageEditor=false;eraseInput();
  reconFilterChecks(output,m,"landscape-recon-filter");
  for(Screen screen:{Screen::Mode,Screen::Menu,Screen::Inbox,Screen::Contacts,Screen::Settings,Screen::Identity}) {
    messageState={};messageState.screen=screen;messagePage=0;setPage(Page::Messages);paint(m);
    Rect targets[MAX_TARGETS];const size_t count=focusTargets(m,targets);
    for(size_t i=0;i<count;++i){focusVisible=true;focusIndex=int(i);paint(m);++assertions;}
  }
  messageState={};messageState.screen=Screen::Menu;setPage(Page::Messages);capture(output,"landscape-messages",m);
  // Settings exposes the orientation toggle only on rotatable targets.
  setPage(Page::Settings);paint(m);expect(screenHas("DISPLAY: LANDSCAPE"),"rotatable settings show orientation");
  tap(120,222,m);
  expect(!hw::hostLandscape&&!landscapeUi&&frame.width==240&&frame.height==320&&screenHas("DISPLAY: PORTRAIT"),"toggle switches to portrait");
  capture(output,"portrait-rotatable-settings",m);
  tap(120,222,m);expect(hw::hostLandscape&&landscapeUi,"toggle returns to landscape");
  // Panel touch coordinates are mapped back to the portrait layout.
  splashShown=onboardingShown=true;bootAt=0;wasPressed=false;touchReleasePending=false;hostMillis=20000;
  touchTick(false,0,0,1,m);
  touchTick(true,160,uint16_t(222*3/4),1,m);
  expect(!hw::hostLandscape&&!landscapeUi,"landscape touch hits the mapped display row");
  touchTick(false,0,0,100,m);
  hw::hostRotatable=false;hw::setDisplayLandscape(false);applyOrientation();
  expect(frame.width==240&&frame.height==320,"fixed targets keep portrait");
  setPage(Page::Settings);paint(m);expect(!screenHas("DISPLAY:"),"fixed targets hide orientation toggle");
  expect(!screenHas("ALERT VOLUME"),"boards without a codec hide alert volume");
  // Codec boards add an alert volume row that cycles 25/50/75/100 percent.
  hw::hostRotatable=true;hw::hostVolumeAdjustable=true;hw::hostVolume=75;hw::setDisplayLandscape(true);applyOrientation();
  setPage(Page::Settings);paint(m);
  expect(screenHas("ALERT VOLUME: 75%")&&screenHas("DISPLAY: LANDSCAPE")&&!textOverlaps(),"six settings rows fit in landscape");
  {Rect targets[MAX_TARGETS];const size_t count=focusTargets(m,targets);expect(count>=8,"volume row is focusable");
   for(size_t i=0;i<count;++i){focusVisible=true;focusIndex=int(i);paint(m);++assertions;}focusVisible=false;focusIndex=-1;}
  capture(output,"landscape-settings-volume",m);
  hw::hostLastToneHz=0;tap(120,224,m);
  expect(hw::hostVolume==100&&hw::hostLastToneHz!=0&&screenHas("ALERT VOLUME: 100%"),"volume steps up with a preview tone");
  tap(120,224,m);expect(hw::hostVolume==25,"volume wraps to the lowest level");
  tap(120,192,m);expect(!hw::hostLandscape&&hw::hostVolume==25,"display row still toggles orientation");
  paint(m);expect(!textOverlaps(),"six settings rows fit in portrait");capture(output,"portrait-settings-volume",m);
  hw::hostVolumeAdjustable=false;hw::hostRotatable=false;hw::setDisplayLandscape(false);applyOrientation();
}
int main(int argc,char** argv) {
  expect(argc==2,"output directory supplied");const char* output=argv[1];
  ui::Model model;model.sd=model.region_configured=model.region_selection_made=true;
  std::strcpy(model.region,"US");std::strcpy(model.profile,"US LongFast / 906.875 MHz");model.meshtastic_slots=104;
  // Use the production page and event handlers; no display allocation, radio,
  // live storage, startup logos, or unrelated capture callbacks are simulated.
  onAction=callback;buffered=true;splashShown=true;bootAt=0;
  setPage(Page::Settings);tap(100,218,model);
  expect(page==Page::MeshSettings && sent.empty(),"Settings touch opens mesh configuration without a mode change");capture(output,"mesh-settings",model);
  tap(100,77,model);expect(page==Page::MeshChoice && meshField==ui::mesh_settings::Protocol,"protocol chooser reachable by touch");
  tap(100,155,model);expect(meshChoice==2 && sent.empty(),"touch selects combined preview without saving");capture(output,"mesh-protocol",model);
  tap(175,296,model);expect(page==Page::MeshSettings && sent.size()==1 && sent.back().action==ui::Action::SaveLoRaMode && sent.back().value==2,"explicit Save emits exactly one combined-mode action");
  model.lora_mode=2;tap(100,107,model);tap(100,116,model);
  expect(meshChoice==1 && screenHas("908.750 MHz / 500 kHz"),"LongTurbo touch preview uses preset default slot and bandwidth");capture(output,"mesh-long-turbo",model);
  tap(175,296,model);expect(sent.back().action==ui::Action::SaveMeshtasticPreset && sent.back().text=="LONG_TURBO","LongTurbo save emits stable preset code");
  std::strcpy(model.meshtastic_preset,"LONG_TURBO");std::strcpy(model.profile,"US LongTurbo / 908.750 MHz");model.meshtastic_slots=52;
  tap(100,137,model);tap(205,164,model);for(int i=0;i<4;++i)tap(145,164,model);
  expect(meshChoice==14 && screenHas("908.750 MHz / slot 14"),"slot touch editor provides increments of ten and one");capture(output,"mesh-slot",model);
  tap(175,296,model);expect(sent.back().action==ui::Action::SaveMeshtasticSlot && sent.back().value==14,"slot saved explicitly");model.meshtastic_slot=14;
  const size_t beforeCancel=sent.size();tap(100,137,model);tap(145,164,model);tap(60,296,model);
  expect(page==Page::MeshSettings && sent.size()==beforeCancel && model.meshtastic_slot==14,"Cancel discards preview and preserves existing slot");
  tap(100,167,model);expect(ui::mesh_settings::count(meshField,model)==3,"Heltec exposes both compatible US MeshCore profiles plus AUTO");
  tap(100,155,model);capture(output,"mesh-meshcore",model);tap(175,296,model);
  expect(sent.back().action==ui::Action::SaveMeshCoreProfile && sent.back().text=="US-SOCAL","Heltec saves region-specific MeshCore selection");
  // Actual two-button navigation: next to the dwell row, select, next-page,
  // select its sole option, then explicitly focus and activate Save.
  setPage(Page::MeshSettings);focusIndex=-1;
  for(int i=0;i<5;++i)press(hw::Next,model);press(hw::Select,model);
  expect(page==Page::MeshChoice && meshField==ui::mesh_settings::Dwell,"physical buttons reach dwell chooser");
  for(int i=0;i<5;++i)press(hw::Next,model);press(hw::Select,model);
  expect(meshChoice==4 && screenHas("600 seconds"),"physical buttons reach second choice page");capture(output,"mesh-dwell",model);
  for(int i=0;i<4;++i)press(hw::Next,model);press(hw::Select,model);
  expect(sent.back().action==ui::Action::SaveLoRaDwell && sent.back().value==600000,"physical buttons activate explicit dwell save");
  tap(100,227,model);tap(100,78,model);capture(output,"mesh-public-decoding",model);tap(175,296,model);
  expect(sent.back().action==ui::Action::SavePublicDecode && sent.back().value==0,"public decoding toggle is independent of protocol");
  tap(100,107,model);tap(205,252,model);expect(meshChoice==4,"preset next page selects first local preview only");
  const size_t beforeBack=sent.size();press(hw::Back,model);
  expect(page==Page::MeshSettings && focusIndex==1 && sent.size()==beforeBack,"physical Back cancels and restores row focus");press(hw::Back,model);
  expect(page==Page::Settings && focusIndex==3,"physical Back returns to original Settings entry");
  std::strcpy(model.region,"EU_868");model.meshtastic_slot=0;model.meshtastic_slots=1;setPage(Page::MeshChoice);meshField=ui::mesh_settings::Preset;meshChoice=1;
  const size_t rejected=sent.size();tap(175,296,model);
  expect(sent.size()==rejected && screenHas("Does not fit region"),"EU868 cannot save LongTurbo's 500 kHz bandwidth");capture(output,"mesh-incompatible-region",model);
  std::strcpy(model.region,"US");model.meshtastic_slots=52;
  for(unsigned guard=0;guard<4;++guard) {
    model.running=guard==0;model.upload_busy=guard==1;model.sd=guard!=2;model.region_configured=guard!=3;
    setPage(Page::MeshChoice);meshField=ui::mesh_settings::Protocol;meshChoice=1;const size_t old=sent.size();tap(175,296,model);
    expect(sent.size()==old && page==Page::MeshChoice && screenHas(ui::mesh_settings::blockedReason(model)),"busy capture/upload, missing storage and unset region block save visibly");
  }
  model.running=model.upload_busy=false;model.sd=model.region_configured=true;
  std::strcpy(model.meshtastic_preset,"CUSTOM");model.meshtastic_slots=0;setPage(Page::MeshSettings);paint(model);
  expect(screenHas("Custom / SD"),"custom numeric SD settings remain visibly custom");capture(output,"mesh-custom",model);
  for(Page guarded:{Page::MeshSettings,Page::MeshChoice}) {setPage(guarded);expect(!ui::allowsBackgroundUpload()&&!ui::allowsUploadJobPolling(),"staged mesh settings suppress automatic upload work");}
  std::strcpy(model.meshtastic_preset,"LONG_TURBO");model.meshtastic_slots=52;
  std::memset(model.profile,'P',sizeof(model.profile)-1);
  for(const char* region:{"US","EU_868","ANZ","JP","UNSET"}) {
    std::strcpy(model.region,region);setPage(Page::MeshSettings);paint(model);
    for(meshField=0;meshField<ui::mesh_settings::FieldCount;++meshField) {
      setPage(Page::MeshChoice);
      for(meshChoice=0;meshChoice<ui::mesh_settings::count(meshField,model);++meshChoice) {
        Rect targets[MAX_TARGETS];const size_t count=focusTargets(model,targets);
        for(size_t i=0;i<count;++i) {focusVisible=true;focusIndex=int(i);paint(model);++assertions;}
      }
    }
  }
  messagingChecks(output,model);
  touchGestureChecks();
  reconFilterChecks(output,model,"recon-filter");
  landscapeChecks(output,model);
  expect(!hostAllocationCount,"mesh and messaging pages require no new framebuffer allocation");
  std::cout<<"Heltec mesh UI: "<<assertions<<" checks passed; all draw and button-focus bounds checked. Synthetic previews only.\n";
}
