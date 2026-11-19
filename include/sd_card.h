#pragma once

// One filesystem handle for every SD consumer. Wio Tracker L2 wires the card
// to SDMMC; the other targets use SPI SD. Both mount at "/sd".
#if defined(LORA_TUNNEL_WIO_TRACKER_L2)
#include <SD_MMC.h>
inline auto& sdCard() { return SD_MMC; }
#else
#include <SD.h>
inline auto& sdCard() { return SD; }
#endif
