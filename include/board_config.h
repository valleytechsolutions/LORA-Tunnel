#pragma once

#include <cstdint>

// Heltec Expansion V2 has a selectable LNA/bypass LoRa front end (FEM).
#if !defined(LORA_TUNNEL_CARDPUTER_ADV) && !defined(LORA_TUNNEL_WIO_TRACKER_L2)
#define LORA_TUNNEL_HAS_LORA_FEM 1
#endif

// Board-specific electrical and resource profiles. The Cardputer target is
// ADV + U214 only. The Heltec target is V4-R8 / Expansion V2, never V1.
// The Wio target is the Seeed Wio Tracker L2 (SX1262, L76K, NV3031B QSPI).
// Electrical references and validation boundaries are in docs/HARDWARE.md.
namespace board {
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
inline constexpr char TARGET_ID[] = "cardputer-adv-lora1262";
inline constexpr char NAME[] = "Cardputer ADV + LoRa-1262 U214";
inline constexpr uint32_t FLASH_BYTES = 8U * 1024U * 1024U;
inline constexpr uint32_t PSRAM_BYTES = 0;
inline constexpr bool HAS_TOUCH = false;
inline constexpr bool HAS_KEYBOARD = true;
inline constexpr bool DISPLAY_SHARED_WITH_SD = false;
inline constexpr bool LORA_SHARED_WITH_SD = true;
inline constexpr bool GNSS_PCAS = false;
inline constexpr float LORA_MIN_MHZ = 868.0f;
inline constexpr float LORA_MAX_MHZ = 923.0f;
inline constexpr uint16_t CAPTURE_RECORDS = 12;
inline constexpr uint16_t WRITER_RECORDS = 8;
// HW4 measured 7,192 bytes used; retain >3 KiB margin for HW5 validation.
inline constexpr uint32_t WRITER_STACK_BYTES = 10240;
inline constexpr uint16_t MANAGEMENT_RESERVE = 2;
inline constexpr uint16_t SUMMARY_RECORDS = 32;
// UI evidence is already limited to 64 bytes; full evidence remains in SD logs.
inline constexpr uint16_t SUMMARY_EVIDENCE_BYTES = 64;
inline constexpr uint16_t DISPLAY_WIDTH = 240;
inline constexpr uint16_t DISPLAY_HEIGHT = 135;
inline constexpr uint8_t DISPLAY_ROTATION = 1;
inline constexpr uint8_t DISPLAY_COLOR_DEPTH = 8;

inline constexpr int LCD_SCK = 36;
inline constexpr int LCD_MOSI = 35;
inline constexpr int LCD_MISO = -1;
inline constexpr int LCD_CS = 37;
inline constexpr int LCD_DC = 34;
inline constexpr int LCD_RESET = 33;
inline constexpr int LCD_BACKLIGHT = 38; // Also supplies the Stamp-S3A RGB LED.
inline constexpr uint32_t LCD_SPI_HZ = 40000000;
inline constexpr int SD_SCK = 40;
inline constexpr int SD_MOSI = 14;
inline constexpr int SD_MISO = 39;
inline constexpr int SD_CS = 12;
inline constexpr uint32_t SD_SPI_HZ = 8000000;
inline constexpr int INTERNAL_SDA = 8;
inline constexpr int INTERNAL_SCL = 9;
inline constexpr int KEYBOARD_IRQ = 11;
inline constexpr uint8_t KEYBOARD_ADDRESS = 0x34;
inline constexpr uint8_t CAP_IO_ADDRESS = 0x43;

inline constexpr int GNSS_RX = 15; // ESP32 input, ATGM336H-6N TXD.
inline constexpr int GNSS_TX = 13;
inline constexpr uint32_t GNSS_BAUD = 115200;
// The cap exposes no MCU-controlled GNSS supply/reset or PPS connection.
inline constexpr int GNSS_ENABLE = -1;
inline constexpr int GNSS_PPS = -1;
inline constexpr int LORA_SCK = SD_SCK;
inline constexpr int LORA_MOSI = SD_MOSI;
inline constexpr int LORA_MISO = SD_MISO;
inline constexpr int LORA_CS = 5;
inline constexpr int LORA_RESET = 3;
inline constexpr int LORA_IRQ = 4;
inline constexpr int LORA_BUSY = 6;
inline constexpr float LORA_TCXO_VOLTS = 3.0f;
inline constexpr bool LORA_USE_LDO = true;
inline constexpr int USER_BUTTON = 0;
inline constexpr bool USER_BUTTON_ACTIVE_LOW = true;
inline constexpr int BATTERY_ADC = 10;
inline constexpr float BATTERY_DIVIDER = 2.0f;
inline constexpr float BATTERY_CALIBRATION = 1.0f;
#elif defined(LORA_TUNNEL_WIO_TRACKER_L2)
inline constexpr char TARGET_ID[] = "wio-tracker-l2";
inline constexpr char NAME[] = "Seeed Wio Tracker L2";
inline constexpr uint32_t FLASH_BYTES = 16U * 1024U * 1024U;
inline constexpr uint32_t PSRAM_BYTES = 8U * 1024U * 1024U;
// Logical UI size. The panel is natively 320x240 landscape; hw maps rotation.
inline constexpr uint16_t DISPLAY_WIDTH = 240;
inline constexpr uint16_t DISPLAY_HEIGHT = 320;
inline constexpr uint8_t DISPLAY_ROTATION = 1;
inline constexpr uint8_t DISPLAY_COLOR_DEPTH = 16;
inline constexpr bool HAS_TOUCH = true;
inline constexpr bool HAS_KEYBOARD = false;
// QSPI display, SDMMC card and LoRa SPI are three independent buses.
inline constexpr bool DISPLAY_SHARED_WITH_SD = false;
inline constexpr bool LORA_SHARED_WITH_SD = false;
inline constexpr bool GNSS_PCAS = true;
inline constexpr float LORA_MIN_MHZ = 863.0f;
inline constexpr float LORA_MAX_MHZ = 928.0f;
inline constexpr uint16_t CAPTURE_RECORDS = 48;
inline constexpr uint16_t WRITER_RECORDS = 32;
inline constexpr uint32_t WRITER_STACK_BYTES = 12288;
inline constexpr uint16_t MANAGEMENT_RESERVE = 8;
inline constexpr uint16_t SUMMARY_RECORDS = 64;
inline constexpr uint16_t SUMMARY_EVIDENCE_BYTES = 128;

// Shared I2C: PCA9555 expander, GT911 touch, LP5814 backlight, ADS1115, ES8311.
inline constexpr int I2C_SDA = 47;
inline constexpr int I2C_SCL = 48;
inline constexpr uint32_t I2C_HZ = 100000;
inline constexpr uint8_t EXPANDER_ADDRESS = 0x21;
inline constexpr int EXPANDER_INTERRUPT = 45; // Active low.
inline constexpr uint8_t BACKLIGHT_ADDRESS = 0x2C;
inline constexpr uint8_t TOUCH_ADDRESS = 0x5D;
inline constexpr uint8_t TOUCH_ADDRESS_ALT = 0x14;
inline constexpr uint8_t BATTERY_ADC_ADDRESS = 0x48;
inline constexpr uint8_t CODEC_ADDRESS = 0x18;

// NV3031B on QSPI (SPI3); no DC or reset GPIO, reset is on the expander.
inline constexpr int LCD_SCK = 42;
inline constexpr int LCD_IO0 = 41;
inline constexpr int LCD_IO1 = 40;
inline constexpr int LCD_IO2 = 39;
inline constexpr int LCD_IO3 = 38;
inline constexpr int LCD_CS = 46;
inline constexpr uint32_t LCD_SPI_HZ = 75000000;

// SD_MMC 1-bit; card power is expander bit 14.
inline constexpr int SD_CLK = 2;
inline constexpr int SD_CMD = 3;
inline constexpr int SD_D0 = 1;

inline constexpr int GNSS_RX = 18; // ESP32 input, L76K TXD.
inline constexpr int GNSS_TX = 17; // ESP32 output, L76K RXD.
// L76K power/reset are expander bits; no PPS connection.
inline constexpr int GNSS_ENABLE = -1;
inline constexpr int GNSS_PPS = -1;
inline constexpr uint32_t GNSS_BAUD = 9600;

inline constexpr int LORA_SCK = 4;
inline constexpr int LORA_MISO = 5;
inline constexpr int LORA_MOSI = 6;
inline constexpr int LORA_CS = 21;
inline constexpr int LORA_RESET = 7;
inline constexpr int LORA_IRQ = 9;
inline constexpr int LORA_BUSY = 8;
inline constexpr float LORA_TCXO_VOLTS = 1.8f;
inline constexpr bool LORA_USE_LDO = false;

inline constexpr int I2S_MCLK = 10;
inline constexpr int I2S_BCLK = 11;
inline constexpr int I2S_WS = 12;
inline constexpr int I2S_DOUT = 16;

inline constexpr int USER_BUTTON = 0;
inline constexpr bool USER_BUTTON_ACTIVE_LOW = true;
inline constexpr int UART_LED = 43; // Held high by UART0 TX unless driven low.
// ADS1115 channel 0 sees battery / 2 while expander bit 15 is high.
inline constexpr float BATTERY_DIVIDER = 2.0f;
inline constexpr float BATTERY_CALIBRATION = 1.0f;
#else
inline constexpr char TARGET_ID[] = "heltec-v4-r8-tft";
inline constexpr char NAME[] = "Heltec V4-R8 Expansion V2";
inline constexpr uint32_t FLASH_BYTES = 16U * 1024U * 1024U;
inline constexpr uint32_t PSRAM_BYTES = 8U * 1024U * 1024U;
inline constexpr uint16_t DISPLAY_WIDTH = 240;
inline constexpr uint16_t DISPLAY_HEIGHT = 320;
inline constexpr uint8_t DISPLAY_ROTATION = 0;
inline constexpr uint8_t DISPLAY_COLOR_DEPTH = 16;
inline constexpr bool HAS_TOUCH = true;
inline constexpr bool HAS_KEYBOARD = false;
inline constexpr bool DISPLAY_SHARED_WITH_SD = true;
inline constexpr bool LORA_SHARED_WITH_SD = false;
inline constexpr bool GNSS_PCAS = true;
inline constexpr float LORA_MIN_MHZ = 863.0f;
inline constexpr float LORA_MAX_MHZ = 928.0f;
inline constexpr uint16_t CAPTURE_RECORDS = 48;
inline constexpr uint16_t WRITER_RECORDS = 32;
inline constexpr uint32_t WRITER_STACK_BYTES = 12288;
inline constexpr uint16_t MANAGEMENT_RESERVE = 8;
inline constexpr uint16_t SUMMARY_RECORDS = 64;
inline constexpr uint16_t SUMMARY_EVIDENCE_BYTES = 128;

inline constexpr int VEXT_ENABLE = 40; // Active low; display, SD, touch pullups.
inline constexpr int LCD_SCK = 16;
inline constexpr int LCD_MOSI = 15;
inline constexpr int LCD_MISO = 45;
inline constexpr int LCD_CS = 47;
inline constexpr int LCD_DC = 48;
inline constexpr int LCD_RESET = 21; // Physically shared with touch reset.
inline constexpr int LCD_BACKLIGHT = 44;
inline constexpr uint32_t LCD_SPI_HZ = 40000000;
inline constexpr int SD_CS = 3;
inline constexpr int SD_SCK = LCD_SCK;
inline constexpr int SD_MOSI = LCD_MOSI;
inline constexpr int SD_MISO = LCD_MISO;
inline constexpr uint32_t SD_SPI_HZ = 8000000;

inline constexpr int TOUCH_SDA = 17;
inline constexpr int TOUCH_SCL = 18;
inline constexpr uint8_t TOUCH_ADDRESS = 0x2e;
// TP_INT is an optional GPIO43 connection in the V2.03 drawing; poll instead.
inline constexpr int TOUCH_INTERRUPT = -1;

inline constexpr int GNSS_RX = 39; // ESP32 input, L76K TXD.
inline constexpr int GNSS_TX = 38; // ESP32 output, L76K RXD.
inline constexpr int GNSS_ENABLE = 42; // Active low.
inline constexpr int GNSS_PPS = 41;
inline constexpr uint32_t GNSS_BAUD = 9600;

inline constexpr int LORA_SCK = 9;
inline constexpr int LORA_MOSI = 10;
inline constexpr int LORA_MISO = 11;
inline constexpr int LORA_CS = 8;
inline constexpr int LORA_RESET = 12;
inline constexpr int LORA_IRQ = 14;
inline constexpr int LORA_BUSY = 13;
inline constexpr float LORA_TCXO_VOLTS = 1.8f;
inline constexpr bool LORA_USE_LDO = false;
inline constexpr int FEM_POWER = 7;
inline constexpr int FEM_ENABLE = 2;
inline constexpr int FEM_CTX = 5; // Low=LNA RX; high=bypass when SX DIO2 is low.

inline constexpr int BUZZER = 4;
inline constexpr int USER_BUTTON = 0;
inline constexpr int SECONDARY_BUTTON = 46;
// V4.3.2 R12 pulls USER up; Expansion V2.03 S4 presses it to ground.
inline constexpr bool USER_BUTTON_ACTIVE_LOW = true;
// Expansion V2.03 S1/R17 drives KEY high; V4.3.2 R47 biases GPIO46 low.
// Keep GPIO46 INPUT: external bias exists. Verify fitted values/levels on PCB.
inline constexpr bool SECONDARY_BUTTON_ACTIVE_LOW = false;
inline constexpr int BATTERY_ADC = 1;
inline constexpr float BATTERY_DIVIDER = 4.9f; // (390k + 100k) / 100k.
// Keep 1.0 until a multimeter calibration is measured for the actual unit.
inline constexpr float BATTERY_CALIBRATION = 1.0f;
#endif
}
