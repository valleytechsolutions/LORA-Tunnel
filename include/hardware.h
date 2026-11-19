#pragma once

#include <Arduino.h>
#include "graphics_compat.h"
#include "keyboard_input.h"
#include <SPI.h>

namespace hw {
// Initializes the board peripherals and SPI buses once, before tasks start.
// Respect board::DISPLAY_SHARED_WITH_SD and board::LORA_SHARED_WITH_SD when
// serializing access. Does not mount SD, start GNSS UART or enable LoRa RX.
bool begin();
lgfx::LGFX_Device& display();
SPIClass& peripheralSpi();
SPIClass& radioSpi();
bool loraFrontendPresent();
bool inputReady();
// Call from one UI task, at most eight events per tick. Press edges are queued;
// directions and Backspace repeat after 400 ms, every 80 ms. Text never repeats.
// This remains available even when optional navigation buttons are disabled.
bool pollKeyboard(input::KeyEvent& event);
enum class TouchState : uint8_t { Unavailable, Released, Pressed };
// Unavailable includes missing hardware and invalid/failed reports. It must not
// be treated as finger release when debouncing a touchscreen gesture.
TouchState readTouch(uint16_t& x, uint16_t& y);
bool touch(uint16_t& x, uint16_t& y);
bool touchPresent();
enum NavigationButton : uint8_t { Next = 1, Select = 2, Back = 4 };
// Poll every UI tick from one task; consume the returned one-shot bitmask.
// IO: Next on release. USER: Select on short release, Back after 700 ms hold.
// Poll and discard while navigation is disabled; no input event is queued.
uint8_t navigationButtons();
// Single note sink for the bounded main-loop melody scheduler. Zero stops now.
// Audio initialization happens at boot; notes do not create playback tasks.
void alertTone(uint16_t frequencyHz, uint16_t durationMs);
bool alertAudioReady();
float batteryVolts();
// Call only while SX1262 is in standby/RX, with DIO2 RF-switch control set up.
// Messaging installs a persistent RadioLib table to select CTX/CSD on TX.
void setLoRaFrontend(bool enabled, bool lna = false);
// Software sleep only. Heltec POWER/S3 is an independent battery latch;
// Cardputer has a physical switch and its cap GNSS supply is not MCU-switched.
// Prepare the USER/G0 GPIO0 active-low wake while peripherals remain on.
// Returns false if USER is held or wake configuration fails; display stays on.
bool prepareSleepHardware();
// Restore ordinary USER input if a later preparation step aborts before sleep.
// Safe even when no EXT0 wake source is armed; does not alter peripheral power.
void cancelSleepPreparation();
// Call only after prepareSleepHardware() succeeds, the capture/logger/uploader
// are stopped, SD files are flushed/closed and SD.end() has completed, radios
// are off and GNSS UART is ended. Caller must exclusively own the display/SPI
// bus and perform no further hardware work. Wake via USER restarts the firmware.
// This shuts down controllable peripherals and deep-sleeps ESP32; it does not
// disconnect the battery or claim zero consumption. It never returns.
[[noreturn]] void enterDeepSleep();
// Optional user orientation. Rotatable targets keep the 240x320 portrait UI as
// the logical layout; in landscape the UI maps it onto a 320x240 panel.
// setDisplayLandscape applies the rotation immediately and persists it in NVS.
#if defined(LORA_TUNNEL_WIO_TRACKER_L2)
bool displayRotatable();
bool displayLandscape();
void setDisplayLandscape(bool landscape);
// SD card power is switched by the I/O expander; enable before SD_MMC mount.
bool setSdPower(bool enabled);
// Alert loudness in percent (10-100), persisted in NVS; boards with a codec.
bool alertVolumeAdjustable();
uint8_t alertVolume();
void setAlertVolume(uint8_t percent);
#else
inline bool displayRotatable() { return false; }
inline bool displayLandscape() { return false; }
inline void setDisplayLandscape(bool) {}
inline bool alertVolumeAdjustable() { return false; }
inline uint8_t alertVolume() { return 100; }
inline void setAlertVolume(uint8_t) {}
#endif
}
