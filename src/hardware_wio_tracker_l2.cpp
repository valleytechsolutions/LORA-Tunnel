#include "hardware.h"
#include "board_config.h"
#include "button_input.h"

#if defined(LORA_TUNNEL_WIO_TRACKER_L2)
#include <ESP_I2S.h>
#include <Preferences.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <freertos/semphr.h>
#include <lgfx/v1/panel/Panel_NV3031B.hpp>
#include <cmath>

namespace {
// ---------------------------------------------------------------------------
// Shared I2C. Arduino Wire locks each transaction; this recursive mutex also
// keeps multi-transaction sequences (register pointer + read, expander
// read-modify-write) atomic between the loop task and the audio task.
StaticSemaphore_t i2cMutexStorage;
SemaphoreHandle_t i2cMutex=nullptr;
struct I2cLock {
  I2cLock() { if(i2cMutex) xSemaphoreTakeRecursive(i2cMutex,portMAX_DELAY); }
  ~I2cLock() { if(i2cMutex) xSemaphoreGiveRecursive(i2cMutex); }
};

bool writeBytes(uint8_t address,const uint8_t* data,size_t length) {
  I2cLock lock;
  Wire.beginTransmission(address);
  Wire.write(data,length);
  return Wire.endTransmission()==0;
}
bool writeRegister8(uint8_t address,uint8_t reg,uint8_t value) {
  const uint8_t data[]={reg,value};
  return writeBytes(address,data,sizeof(data));
}
bool readBytes(uint8_t address,const uint8_t* reg,size_t regLength,uint8_t* out,size_t length) {
  I2cLock lock;
  Wire.beginTransmission(address);
  Wire.write(reg,regLength);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(address,static_cast<uint8_t>(length))!=length) {
    while(Wire.available()) Wire.read();
    return false;
  }
  for(size_t i=0;i<length;++i) {
    const int value=Wire.read();
    if(value<0) return false;
    out[i]=static_cast<uint8_t>(value);
  }
  return true;
}
bool probe(uint8_t address) {
  I2cLock lock;
  Wire.beginTransmission(address);
  return Wire.endTransmission()==0;
}

// ---------------------------------------------------------------------------
// PCA9555 I/O expander (Wio Tracker L2 schematic / Seeed BSP bit map).
namespace io {
enum Bit : uint8_t {
  WakeButton=0, I2cInterrupt=1, SdDetect=2, TouchInterrupt=3, LcdControl=4,
  LcdPower=5, LcdReset=6, GrovePower=7, TouchReset=8, GnssReset=9, UserLed=10,
  UsbOtg=11, AudioPower=12, GnssPower=13, SdPower=14, BatterySense=15
};
constexpr uint8_t INPUT_REG=0x00, OUTPUT_REG=0x02, CONFIG_REG=0x06;
uint16_t outputs=0, config=0xFFFF;
bool present=false;

bool write16(uint8_t reg,uint16_t value) {
  const uint8_t data[]={reg,uint8_t(value&0xFF),uint8_t(value>>8)};
  return writeBytes(board::EXPANDER_ADDRESS,data,sizeof(data));
}
bool read16(uint8_t reg,uint16_t& value) {
  uint8_t data[2];
  if(!readBytes(board::EXPANDER_ADDRESS,&reg,1,data,2)) return false;
  value=uint16_t(data[0])|(uint16_t(data[1])<<8);
  return true;
}
void stageOutput(Bit bit,bool high) {
  config&=~uint16_t(1U<<bit);
  if(high) outputs|=uint16_t(1U<<bit); else outputs&=~uint16_t(1U<<bit);
}
bool set(Bit bit,bool high) {
  if(!present) return false;
  I2cLock lock;
  const uint16_t before=outputs, beforeConfig=config;
  stageOutput(bit,high);
  bool ok=true;
  if(outputs!=before) ok=write16(OUTPUT_REG,outputs);
  if(ok && config!=beforeConfig) ok=write16(CONFIG_REG,config);
  return ok;
}
bool inputs(uint16_t& value) { return present && read16(INPUT_REG,value); }

// Seeed's bring-up order: rails on, GNSS and LCD reset pulses, then touch.
bool begin() {
  present=probe(board::EXPANDER_ADDRESS);
  if(!present) return false;
  I2cLock lock;
  if(!read16(OUTPUT_REG,outputs) || !read16(CONFIG_REG,config)) return present=false;
  for(Bit in:{WakeButton,I2cInterrupt,SdDetect,LcdControl}) config|=uint16_t(1U<<in);
  stageOutput(TouchInterrupt,false);
  stageOutput(LcdPower,true);
  stageOutput(LcdReset,true);
  stageOutput(GrovePower,false);
  stageOutput(TouchReset,false);
  stageOutput(GnssReset,true);
  stageOutput(UserLed,false);
  stageOutput(UsbOtg,false);
  stageOutput(AudioPower,false);
  stageOutput(GnssPower,true);
  stageOutput(SdPower,false);
  stageOutput(BatterySense,false);
  bool ok=write16(OUTPUT_REG,outputs) && write16(CONFIG_REG,config);
  delay(10);
  ok=ok && set(GnssReset,false);
  delay(40);
  ok=ok && set(LcdReset,false);
  delay(10);
  ok=ok && set(LcdReset,true);
  delay(500);
  ok=ok && set(LcdControl,true);
  delay(10);
  ok=ok && set(TouchReset,true);
  delay(60);
  return present=ok;
}
}

// ---------------------------------------------------------------------------
// LP5814 backlight driver on I2C.
class BacklightLp5814 final : public lgfx::v1::ILight {
public:
  bool init(uint8_t brightness) override {
    bool ok=true;
    ok=writeRegister8(board::BACKLIGHT_ADDRESS,0x00,0x01) && ok;
    ok=writeRegister8(board::BACKLIGHT_ADDRESS,0x01,0x01) && ok;
    ok=writeRegister8(board::BACKLIGHT_ADDRESS,0x02,0x00) && ok;
    ok=writeRegister8(board::BACKLIGHT_ADDRESS,0x04,0x4E) && ok;
    ok=writeRegister8(board::BACKLIGHT_ADDRESS,0x05,0xF0) && ok;
    for(uint8_t channel=0;channel<4;++channel)
      ok=writeRegister8(board::BACKLIGHT_ADDRESS,0x14+channel,200) && ok;
    ok=writeRegister8(board::BACKLIGHT_ADDRESS,0x02,0x0F) && ok;
    setBrightness(brightness);
    return ok;
  }
  void setBrightness(uint8_t brightness) override {
    for(uint8_t channel=0;channel<4;++channel)
      writeRegister8(board::BACKLIGHT_ADDRESS,0x18+channel,brightness);
    writeRegister8(board::BACKLIGHT_ADDRESS,0x0F,0x55);
  }
};

// NV3031B is natively 320x240 landscape at rotation 0 with offset_rotation 1.
constexpr uint8_t ROTATION_LANDSCAPE=0, ROTATION_PORTRAIT=1;

class TunnelDisplay final : public lgfx::LGFX_Device {
  lgfx::Bus_SPI bus_;
  lgfx::Panel_NV3031B panel_;
  BacklightLp5814 light_;

public:
  TunnelDisplay() {
    auto spi=bus_.config();
    spi.spi_host=SPI3_HOST;
    spi.spi_mode=3;
    spi.freq_write=board::LCD_SPI_HZ;
    spi.freq_read=16000000;
    spi.spi_3wire=false;
    spi.use_lock=true;
    spi.dma_channel=SPI_DMA_CH_AUTO;
    spi.pin_sclk=board::LCD_SCK;
    spi.pin_miso=-1;
    spi.pin_mosi=-1;
    spi.pin_dc=-1;
    spi.pin_io0=board::LCD_IO0;
    spi.pin_io1=board::LCD_IO1;
    spi.pin_io2=board::LCD_IO2;
    spi.pin_io3=board::LCD_IO3;
    bus_.config(spi);
    panel_.setBus(&bus_);

    auto panel=panel_.config();
    panel.pin_cs=board::LCD_CS;
    panel.pin_rst=-1;
    panel.pin_busy=-1;
    panel.panel_width=240;
    panel.panel_height=320;
    panel.memory_width=240;
    panel.memory_height=320;
    panel.offset_x=0;
    panel.offset_y=0;
    panel.offset_rotation=1;
    panel.readable=false;
    panel.invert=true;
    panel.rgb_order=true;
    panel.dlen_16bit=false;
    panel.bus_shared=false;
    panel_.config(panel);
    panel_.setLight(&light_);
    setPanel(&panel_);
  }
};

TunnelDisplay screen;
bool landscape=true;
bool initialized=false, hardwareReady=false;
SPIClass radioBus(FSPI);

// ---------------------------------------------------------------------------
// GT911 touch, polled. Raw coordinates are portrait 240x320.
constexpr uint16_t GT911_STATUS=0x814E, GT911_POINT1=0x8150;
uint8_t touchAddress=0;
hw::TouchState touchState=hw::TouchState::Released;
uint16_t touchX=0, touchY=0;

bool gt911Read(uint16_t reg,uint8_t* out,size_t length) {
  const uint8_t address[]={uint8_t(reg>>8),uint8_t(reg&0xFF)};
  return readBytes(touchAddress,address,sizeof(address),out,length);
}
bool gt911ClearStatus() {
  const uint8_t data[]={uint8_t(GT911_STATUS>>8),uint8_t(GT911_STATUS&0xFF),0};
  return writeBytes(touchAddress,data,sizeof(data));
}

// ---------------------------------------------------------------------------
// Buttons: GPIO0 USER and the expander WAKE key.
tunnel::ButtonInput userButton;
tunnel::ButtonInput wakeButton(false);
bool wakePressed=false;
uint32_t expanderPolledAt=0;

bool userPressed() {
  const bool high=digitalRead(board::USER_BUTTON)==HIGH;
  return board::USER_BUTTON_ACTIVE_LOW ? !high : high;
}
void pollExpanderInputs(uint32_t now,bool force=false) {
  // INT is active low on any input change; also poll in case an edge is missed.
  if(!force && digitalRead(board::EXPANDER_INTERRUPT)==HIGH && now-expanderPolledAt<50) return;
  expanderPolledAt=now;
  uint16_t value=0;
  if(io::inputs(value)) wakePressed=!(value&(1U<<io::WakeButton));
}

// ---------------------------------------------------------------------------
// ADS1115 battery monitor.
float cachedBattery=NAN;
uint32_t batteryReadAt=0;
bool batteryReadOnce=false;

float readBattery() {
  if(!io::set(io::BatterySense,true)) return NAN;
  delay(10);
  float volts=NAN;
  // AIN0 single-ended, +/-2.048 V, single shot, 128 SPS, comparator off.
  const uint8_t start[]={0x01,0xC5,0x83};
  if(writeBytes(board::BATTERY_ADC_ADDRESS,start,sizeof(start))) {
    const uint32_t began=millis();
    const uint8_t configReg=0x01, conversionReg=0x00;
    uint8_t status[2]={0,0};
    // Bounded: a stuck conversion must never stall the UI loop.
    while(millis()-began<50) {
      if(readBytes(board::BATTERY_ADC_ADDRESS,&configReg,1,status,2) && (status[0]&0x80)) break;
      delay(2);
    }
    uint8_t raw[2];
    if((status[0]&0x80) && readBytes(board::BATTERY_ADC_ADDRESS,&conversionReg,1,raw,2)) {
      const int16_t counts=int16_t((uint16_t(raw[0])<<8)|raw[1]);
      volts=counts*2.048f/32768.0f*board::BATTERY_DIVIDER*board::BATTERY_CALIBRATION;
    }
  }
  io::set(io::BatterySense,false);
  return (volts>=2.5f && volts<=4.5f) ? volts : NAN;
}

// ---------------------------------------------------------------------------
// ES8311 codec + I2S tone sink. A boot-created task renders notes so the main
// loop melody scheduler never blocks on DMA, amplifier settle or codec setup.
namespace audio {
constexpr uint32_t SAMPLE_RATE=16000;
constexpr uint32_t AMP_SETTLE_MS=250;
constexpr uint32_t AMP_IDLE_OFF_MS=3000;
constexpr size_t BLOCK_SAMPLES=128; // 8 ms per DMA write.
I2SClass i2s;
bool ready=false;
TaskHandle_t task=nullptr;
portMUX_TYPE noteLock=portMUX_INITIALIZER_UNLOCKED;
// Notes are timestamped when the main-loop scheduler emits them and replayed
// with one constant offset, so amplifier power-up delays a riff instead of
// dropping its first notes. 64 events hold an intro plus a 24-note cue.
struct Event { uint16_t hz, durationMs; uint32_t at; };
constexpr size_t QUEUE_SIZE=64;
Event queue[QUEUE_SIZE];
size_t queueHead=0, queueTail=0;
volatile bool shuttingDown=false;
volatile uint8_t volumePercent=75;

bool pop(Event& event) {
  bool ok=false;
  portENTER_CRITICAL(&noteLock);
  if(queueHead!=queueTail) { event=queue[queueHead]; queueHead=(queueHead+1)%QUEUE_SIZE; ok=true; }
  portEXIT_CRITICAL(&noteLock);
  return ok;
}
bool peek(Event& event) {
  bool ok=false;
  portENTER_CRITICAL(&noteLock);
  if(queueHead!=queueTail) { event=queue[queueHead]; ok=true; }
  portEXIT_CRITICAL(&noteLock);
  return ok;
}

namespace codec {
bool write(uint8_t reg,uint8_t value) { return writeRegister8(board::CODEC_ADDRESS,reg,value); }
// DAC-only ES8311 setup, codec as I2S slave on external MCLK = 256 x Fs
// (register sequence per Espressif's esp_codec_dev ES8311 reference).
bool start() {
  bool ok=probe(board::CODEC_ADDRESS);
  if(!ok) return false;
  ok&=write(0x44,0x08); ok&=write(0x44,0x08);
  ok&=write(0x01,0x30); ok&=write(0x02,0x00); ok&=write(0x03,0x10);
  ok&=write(0x16,0x24); ok&=write(0x04,0x10); ok&=write(0x05,0x00);
  ok&=write(0x0B,0x00); ok&=write(0x0C,0x00); ok&=write(0x10,0x1F);
  ok&=write(0x11,0x7F); ok&=write(0x00,0x80); ok&=write(0x01,0x3F);
  ok&=write(0x13,0x10); ok&=write(0x1B,0x0A); ok&=write(0x1C,0x6A);
  ok&=write(0x44,0x58);
  // Standard I2S, 16-bit; 16 kHz at 256 x Fs uses dac_osr 0x20.
  ok&=write(0x09,0x0C); ok&=write(0x0A,0x0C);
  ok&=write(0x02,0x00); ok&=write(0x05,0x00); ok&=write(0x03,0x10);
  ok&=write(0x04,0x20); ok&=write(0x07,0x00); ok&=write(0x08,0xFF);
  ok&=write(0x06,0x03);
  ok&=write(0x17,0xBF); ok&=write(0x0E,0x02); ok&=write(0x12,0x00);
  ok&=write(0x14,0x1A); ok&=write(0x0D,0x01); ok&=write(0x15,0x40);
  ok&=write(0x37,0x08); ok&=write(0x45,0x00);
  ok&=write(0x32,0xD0); // Digital volume.
  ok&=write(0x31,0x00); // Unmute.
  return ok;
}
void suspend() {
  constexpr uint8_t sequence[][2]={
    {0x31,0x60},{0x32,0x00},{0x17,0x00},{0x0E,0xFF},{0x12,0x02},{0x14,0x00},
    {0x0D,0xFA},{0x15,0x00},{0x02,0x10},{0x00,0x00},{0x00,0x1F},{0x01,0x30},
    {0x01,0x00},{0x45,0x00},{0x0D,0xFC},{0x02,0x00}};
  for(const auto& step:sequence) write(step[0],step[1]);
}
}

bool ampOn=false;
uint32_t ampOnAt=0;

bool powerUp() {
  if(ampOn) return true;
  if(!io::set(io::AudioPower,true)) return false;
  ampOnAt=millis();
  ampOn=true;
  // I2S is already clocking MCLK, so the codec PLL can lock during start().
  if(!codec::start()) {
    io::set(io::AudioPower,false);
    ampOn=false;
    return false;
  }
  const uint32_t elapsed=millis()-ampOnAt;
  if(elapsed<AMP_SETTLE_MS) vTaskDelay(pdMS_TO_TICKS(AMP_SETTLE_MS-elapsed));
  return true;
}
void powerDown() {
  if(!ampOn) return;
  codec::suspend();
  io::set(io::AudioPower,false);
  ampOn=false;
}

// Chime voice: 3 ms attack, gentle exponential decay, 6 ms release, with a
// soft octave partial so the small speaker sounds less like a test tone.
constexpr uint32_t ATTACK_SAMPLES=SAMPLE_RATE*3/1000;
constexpr uint32_t RELEASE_SAMPLES=SAMPLE_RATE*6/1000;
constexpr float DECAY_PER_SAMPLE=0.99985f; // ~0.4 s time constant.
constexpr float FULL_SCALE=14000.0f;

void run(void*) {
  int16_t block[BLOCK_SAMPLES];
  uint32_t idleSince=millis(), offset=0, noteSamples=0, noteLength=0;
  float phase=0.0f, step=0.0f, decay=1.0f, level=0.0f;
  bool timeline=false, noteOn=false;
  for(;;) {
    Event event;
    const bool busy=noteOn || peek(event);
    if(!busy) {
      if(timeline) {
        timeline=false;
        idleSince=millis();
        memset(block,0,sizeof(block));
        for(int i=0;i<4;++i) i2s.write(reinterpret_cast<uint8_t*>(block),sizeof(block));
      }
      if(ampOn && millis()-idleSince>=AMP_IDLE_OFF_MS) powerDown();
      ulTaskNotifyTake(pdTRUE,ampOn ? pdMS_TO_TICKS(250) : portMAX_DELAY);
      if(shuttingDown) { powerDown(); vTaskSuspend(nullptr); }
      continue;
    }
    if(shuttingDown) { powerDown(); vTaskSuspend(nullptr); }
    if(!timeline) {
      if(!powerUp()) { while(pop(event)) {} continue; }
      if(!peek(event)) continue;
      offset=millis()-event.at;
      timeline=true;
    }
    const uint32_t now=millis();
    while(peek(event) && int32_t(now-(event.at+offset))>=0) {
      pop(event);
      if(event.hz && event.durationMs) {
        step=2.0f*float(M_PI)*event.hz/SAMPLE_RATE;
        noteLength=uint32_t(event.durationMs)*SAMPLE_RATE/1000;
        noteSamples=0; decay=1.0f; noteOn=true;
        const float v=volumePercent/100.0f;
        level=FULL_SCALE*v*v;
      } else if(noteOn && noteSamples+RELEASE_SAMPLES<noteLength) {
        noteLength=noteSamples+RELEASE_SAMPLES; // Fade instead of clicking.
      }
    }
    for(auto& sample:block) {
      float value=0.0f;
      if(noteOn) {
        float envelope=decay;
        if(noteSamples<ATTACK_SAMPLES) envelope*=float(noteSamples)/ATTACK_SAMPLES;
        const uint32_t left=noteLength-noteSamples;
        if(left<RELEASE_SAMPLES) envelope*=float(left)/RELEASE_SAMPLES;
        value=level*envelope*(sinf(phase)+0.25f*sinf(2.0f*phase))/1.25f;
        phase+=step;
        if(phase>=2.0f*float(M_PI)) phase-=2.0f*float(M_PI);
        decay*=DECAY_PER_SAMPLE;
        if(++noteSamples>=noteLength) { noteOn=false; phase=0.0f; }
      }
      sample=int16_t(value);
    }
    i2s.write(reinterpret_cast<uint8_t*>(block),sizeof(block));
  }
}

bool begin() {
  i2s.setPins(board::I2S_BCLK,board::I2S_WS,board::I2S_DOUT,-1,board::I2S_MCLK);
  i2s.setTimeout(50);
  if(!i2s.begin(I2S_MODE_STD,SAMPLE_RATE,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO))
    return false;
  // Probe the codec once with its supply up, then park the amplifier.
  bool codecFound=false;
  if(io::set(io::AudioPower,true)) {
    delay(20);
    codecFound=probe(board::CODEC_ADDRESS);
    io::set(io::AudioPower,false);
  }
  if(!codecFound) { i2s.end(); return false; }
  if(xTaskCreatePinnedToCore(run,"alert-audio",3072,nullptr,2,&task,1)!=pdPASS) {
    i2s.end();
    return false;
  }
  return true;
}
}

void loadPreferences() {
  Preferences preferences;
  if(preferences.begin("lt-display",true)) {
    landscape=preferences.getBool("landscape",true);
    preferences.end();
  }
  if(preferences.begin("lt-audio",true)) {
    const uint8_t volume=preferences.getUChar("volume",75);
    audio::volumePercent=volume<10?10:volume>100?100:volume;
    preferences.end();
  }
}
}

namespace hw {
bool begin() {
  if(initialized) return hardwareReady;
  initialized=true;
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(static_cast<gpio_num_t>(board::LORA_RESET));
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::USER_BUTTON));
  i2cMutex=xSemaphoreCreateRecursiveMutexStatic(&i2cMutexStorage);

  pinMode(board::LORA_CS,OUTPUT);
  digitalWrite(board::LORA_CS,HIGH);
  pinMode(board::UART_LED,OUTPUT);
  digitalWrite(board::UART_LED,LOW);
  pinMode(board::USER_BUTTON,INPUT_PULLUP);
  pinMode(board::EXPANDER_INTERRUPT,INPUT_PULLUP);

  Wire.begin(board::I2C_SDA,board::I2C_SCL,board::I2C_HZ);
  Wire.setTimeOut(10);
  const bool expanderReady=io::begin();

  loadPreferences();
  const bool displayInitialized=expanderReady && screen.init();
  screen.setRotation(landscape ? ROTATION_LANDSCAPE : ROTATION_PORTRAIT);
  screen.setBrightness(160);

  for(uint8_t address:{board::TOUCH_ADDRESS,board::TOUCH_ADDRESS_ALT}) {
    if(probe(address)) { touchAddress=address; break; }
  }
  const bool radioReady=radioBus.begin(board::LORA_SCK,board::LORA_MISO,
      board::LORA_MOSI,board::LORA_CS);

  const uint32_t now=millis();
  pollExpanderInputs(now,true);
  userButton.reset(now,userPressed());
  wakeButton.reset(now,wakePressed);

  audio::ready=expanderReady && audio::begin();
  Serial.printf("LT AUDIO ready=%d\n",audio::ready);
  hardwareReady=displayInitialized && radioReady;
  Serial.printf("LT HARDWARE target=%s expander=%d touch=0x%02x spi=%d\n",
      board::TARGET_ID,expanderReady,touchAddress,radioReady);
  return hardwareReady;
}

lgfx::LGFX_Device& display() { return screen; }
// SD uses SDMMC and the display has its own QSPI host; only LoRa uses SPIClass.
SPIClass& peripheralSpi() { return radioBus; }
SPIClass& radioSpi() { return radioBus; }
// SX1262 DIO2 drives the RF switch directly; there is no external front end.
bool loraFrontendPresent() { return true; }
bool inputReady() { return touchAddress!=0; }
bool pollKeyboard(input::KeyEvent&) { return false; }
bool touchPresent() { return touchAddress!=0; }

uint8_t navigationButtons() {
  const uint32_t now=millis();
  pollExpanderInputs(now);
  const auto wake=wakeButton.update(now,wakePressed);
  const auto user=userButton.update(now,userPressed());
  uint8_t events=0;
  if(wake==tunnel::ButtonEvent::ShortPress) events|=Next;
  if(user==tunnel::ButtonEvent::ShortPress) events|=Select;
  if(user==tunnel::ButtonEvent::LongPress) events|=Back;
  return events;
}

TouchState readTouch(uint16_t& x,uint16_t& y) {
  if(!touchAddress) return TouchState::Unavailable;
  uint8_t status=0;
  if(!gt911Read(GT911_STATUS,&status,1)) return TouchState::Unavailable;
  // No new report since the last read: the previous contact state still holds.
  if(!(status&0x80)) {
    if(touchState==TouchState::Pressed) { x=touchX; y=touchY; }
    return touchState;
  }
  const uint8_t points=status&0x0F;
  uint8_t point[4];
  bool valid=points<=5;
  if(valid && points) valid=gt911Read(GT911_POINT1,point,sizeof(point));
  gt911ClearStatus();
  if(!valid) return touchState=TouchState::Unavailable;
  if(!points) return touchState=TouchState::Released;
  const uint16_t rawX=uint16_t(point[0])|(uint16_t(point[1])<<8);
  const uint16_t rawY=uint16_t(point[2])|(uint16_t(point[3])<<8);
  if(rawX>=240 || rawY>=320) return touchState=TouchState::Unavailable;
  // Match LovyanGFX's GT911 mapping (touch offset_rotation 2, panel offset 1).
  if(landscape) { touchX=319-rawY; touchY=rawX; }
  else { touchX=rawX; touchY=rawY; }
  x=touchX;
  y=touchY;
  return touchState=TouchState::Pressed;
}
bool touch(uint16_t& x,uint16_t& y) { return readTouch(x,y)==TouchState::Pressed; }

bool alertAudioReady() { return audio::ready; }
void alertTone(uint16_t frequencyHz,uint16_t durationMs) {
  if(!audio::ready) return;
  const audio::Event event{frequencyHz,durationMs,millis()};
  portENTER_CRITICAL(&audio::noteLock);
  const size_t next=(audio::queueTail+1)%audio::QUEUE_SIZE;
  // A full queue drops the event; every tone already carries its own end.
  if(next!=audio::queueHead) { audio::queue[audio::queueTail]=event; audio::queueTail=next; }
  portEXIT_CRITICAL(&audio::noteLock);
  xTaskNotifyGive(audio::task);
}
bool alertVolumeAdjustable() { return audio::ready; }
uint8_t alertVolume() { return audio::volumePercent; }
void setAlertVolume(uint8_t percent) {
  percent=percent<10?10:percent>100?100:percent;
  audio::volumePercent=percent;
  Preferences preferences;
  if(preferences.begin("lt-audio",false)) {
    preferences.putUChar("volume",percent);
    preferences.end();
  }
}

float batteryVolts() {
  const uint32_t now=millis();
  if(!batteryReadOnce || now-batteryReadAt>=5000) {
    cachedBattery=readBattery();
    batteryReadAt=now;
    batteryReadOnce=true;
  }
  return cachedBattery;
}

void setLoRaFrontend(bool,bool) {}

bool displayRotatable() { return true; }
bool displayLandscape() { return landscape; }
void setDisplayLandscape(bool enabled) {
  if(enabled!=landscape) {
    landscape=enabled;
    screen.waitDMA();
    screen.setRotation(landscape ? ROTATION_LANDSCAPE : ROTATION_PORTRAIT);
    touchState=TouchState::Released;
  }
  Preferences preferences;
  if(preferences.begin("lt-display",false)) {
    if(preferences.getBool("landscape",true)!=landscape) preferences.putBool("landscape",landscape);
    preferences.end();
  }
}

bool setSdPower(bool enabled) {
  const bool wasOn=io::outputs&(1U<<io::SdPower);
  if(!io::set(io::SdPower,enabled)) return false;
  if(enabled && !wasOn) delay(20);
  return true;
}

bool prepareSleepHardware() {
  const auto wake=static_cast<gpio_num_t>(board::USER_BUTTON);
  // Never enter an immediate sleep/wake loop while the wake button is held.
  if(userPressed() || !esp_sleep_is_valid_wakeup_gpio(wake)) return false;
  bool ok=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)==ESP_OK;
  if(ok) ok=rtc_gpio_init(wake)==ESP_OK;
  if(ok) ok=rtc_gpio_set_direction(wake,RTC_GPIO_MODE_INPUT_ONLY)==ESP_OK;
  if(ok) ok=rtc_gpio_pulldown_dis(wake)==ESP_OK;
  if(ok) ok=rtc_gpio_pullup_en(wake)==ESP_OK;
  if(ok) ok=esp_sleep_enable_ext0_wakeup(wake,0)==ESP_OK;
  if(ok && rtc_gpio_get_level(wake)==0) ok=false;
  if(!ok) cancelSleepPreparation();
  return ok;
}

void cancelSleepPreparation() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::USER_BUTTON));
  pinMode(board::USER_BUTTON,INPUT_PULLUP);
}

[[noreturn]] void enterDeepSleep() {
  // The caller has already saved/unmounted SD and stopped asynchronous users.
  if(audio::ready) {
    audio::shuttingDown=true;
    xTaskNotifyGive(audio::task);
    const uint32_t began=millis();
    while(audio::ampOn && millis()-began<200) delay(5);
    audio::ready=false;
  }
  screen.waitDMA();
  screen.setBrightness(0);
  screen.sleep();
  screen.waitDisplay();
  // The expander stays powered and latches its outputs through deep sleep.
  for(io::Bit rail:{io::AudioPower,io::GnssPower,io::SdPower,io::GrovePower,
                    io::BatterySense,io::UserLed,io::LcdPower})
    io::set(rail,false);
  io::set(io::GnssReset,true);
  Wire.end();
  radioBus.end();
  touchAddress=0;

  constexpr int floatingPins[]={
    board::LCD_SCK,board::LCD_IO0,board::LCD_IO1,board::LCD_IO2,board::LCD_IO3,
    board::LCD_CS,board::SD_CLK,board::SD_CMD,board::SD_D0,
    board::I2C_SDA,board::I2C_SCL,board::GNSS_RX,board::GNSS_TX,
    board::LORA_SCK,board::LORA_MOSI,board::LORA_MISO,board::LORA_CS,
    board::LORA_IRQ,board::LORA_BUSY,
    board::I2S_MCLK,board::I2S_BCLK,board::I2S_WS,board::I2S_DOUT
  };
  for(int pin:floatingPins) {
    pinMode(pin,INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(pin),GPIO_FLOATING);
  }
  const auto reset=static_cast<gpio_num_t>(board::LORA_RESET);
  gpio_set_level(reset,0);
  pinMode(board::LORA_RESET,OUTPUT);
  digitalWrite(board::LORA_RESET,LOW);
  gpio_hold_en(reset);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}
}
#endif
