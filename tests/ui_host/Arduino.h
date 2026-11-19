#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
extern uint32_t hostMillis;
inline uint32_t millis() { return hostMillis; }

using TaskHandle_t=void*;
static uintptr_t hostTaskIdentity=1;
inline TaskHandle_t xTaskGetCurrentTaskHandle() {return reinterpret_cast<TaskHandle_t>(hostTaskIdentity);}
#include "../host_string.h"
#if !defined(LORA_TUNNEL_CARDPUTER_ADV)
struct HostSerial {void println(const char*){}};
static HostSerial Serial;
#endif
