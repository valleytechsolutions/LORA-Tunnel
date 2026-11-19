#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lgfx {
namespace fonts { constexpr int Font0=0; }
namespace textdatum_t { constexpr int top_left=0,middle_center=1; }
struct Command { std::string kind,text; int x=0,y=0,w=0,h=0,r=0,color=0,scale=1; };
class LovyanGFX {
public:
  int width=240;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  int height=135;
#else
  int height=320;
#endif
  int scale=1,ink=65535,datum=textdatum_t::top_left;
  uint8_t* framebuffer=nullptr;
  unsigned dmaWaits=0,displayWaits=0;
  void waitDMA(){++dmaWaits;}
  void waitDisplay(){++displayWaits;}
  void startWrite(){}
  void endWrite(){}
  std::vector<Command> commands;
  void check(int x,int y,int w,int h) const { assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=width && y+h<=height); }
  void fillScreen(int color) { commands.clear();fillRect(0,0,width,height,color); }
  void fillRect(int x,int y,int w,int h,int color) { check(x,y,w,h);if(framebuffer)for(int row=y;row<y+h;++row)std::memset(framebuffer+row*width+x,((color>>8)&0xe0)|((color>>6)&0x1c)|((color>>3)&3),w);commands.push_back({"rect","",x,y,w,h,0,color,1}); }
  void fillRoundRect(int x,int y,int w,int h,int radius,int color) { check(x,y,w,h);commands.push_back({"round","",x,y,w,h,radius,color,1}); }
  void drawRoundRect(int x,int y,int w,int h,int radius,int color) {check(x,y,w,h);commands.push_back({"round-outline","",x,y,w,h,radius,color,1});}
  void drawRect(int x,int y,int w,int h,int color) {check(x,y,w,h);commands.push_back({"outline","",x,y,w,h,0,color,1});}
  void fillCircle(int x,int y,int radius,int color) {check(x-radius,y-radius,radius*2+1,radius*2+1);commands.push_back({"circle","",x-radius,y-radius,radius*2+1,radius*2+1,radius,color,1});}
  bool drawPng(const uint8_t*,size_t,int x,int y,int w,int h,int,int,float) {check(x,y,w,h);return false;}
  void drawFastHLine(int x,int y,int w,int color) { fillRect(x,y,w,1,color); }
  void setTextSize(int value) { scale=value; }
  void setTextColor(int value) { ink=value; }
  void setTextDatum(int value) {datum=value;}
  void setTextWrap(bool,bool) {}
  void setFont(const int*) {}
  void drawString(const char* text,int x,int y) { const int w=int(std::strlen(text))*6*scale;if(datum==textdatum_t::middle_center){x-=w/2;y-=4*scale;}check(x,y,w,8*scale);commands.push_back({"text",text,x,y,w,8*scale,0,ink,scale}); }

};
class LGFX_Device:public LovyanGFX {};
class LGFX_Sprite:public LovyanGFX {
public:
  void setColorDepth(int) {}
  void setPsram(bool) {}
  void setBuffer(void* buffer,int w,int h,int) {framebuffer=static_cast<uint8_t*>(buffer);width=w;height=h;}
  void* createSprite(int w,int h) {width=w;height=h;return this;}
  void pushSprite(LovyanGFX* target,int x,int y) {
    for(auto command:commands) { command.x+=x;command.y+=y;target->check(command.x,command.y,command.w,command.h);target->commands.push_back(command); }
  }
};
}
namespace hw {
enum Navigation : uint8_t {Next=1,Select=2,Back=4};
enum class TouchState : uint8_t {Unavailable,Released,Pressed};
static TouchState hostTouchState=TouchState::Released;
static uint16_t hostTouchX=0,hostTouchY=0;
inline TouchState readTouch(uint16_t& x,uint16_t& y){x=hostTouchX;y=hostTouchY;return hostTouchState;}
inline bool touch(uint16_t& x,uint16_t& y){return readTouch(x,y)==TouchState::Pressed;}
inline lgfx::LGFX_Device& display() {static lgfx::LGFX_Device screen;return screen;}
static bool hostRotatable=false,hostLandscape=false;
inline bool displayRotatable(){return hostRotatable;}
inline bool displayLandscape(){return hostLandscape;}
inline void setDisplayLandscape(bool landscape){hostLandscape=landscape;display().width=landscape?320:240;display().height=landscape?240:320;}
static bool hostVolumeAdjustable=false;
static uint8_t hostVolume=75;
static uint16_t hostLastToneHz=0;
inline bool alertVolumeAdjustable(){return hostVolumeAdjustable;}
inline uint8_t alertVolume(){return hostVolume;}
inline void setAlertVolume(uint8_t percent){hostVolume=percent;}
inline void alertTone(uint16_t hz,uint16_t){hostLastToneHz=hz;}
}
