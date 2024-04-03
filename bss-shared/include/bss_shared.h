/*
  Copyright © 2024 Leonard Sebastian Schwennesen. All rights reserved.
*/

#ifndef BSS_SHARED_H
#define BSS_SHARED_H

#include <stdint.h>

void digitalWrite(uint8_t pin, uint8_t val);

// Message Types
#define BSS_MSG_PAIRING_REQUEST 0x00
#define BSS_MSG_PAIRING_ACCEPTED 0x01
#define BSS_MSG_PAIRING_REMOVE 0x02
#define BSS_MSG_WAKEUP_REQUEST 0x03
#define BSS_MSG_WAKEUP_ACCEPTED 0x04
#define BSS_MSG_BUZZER_PRESSED 0x05
#define BSS_MSG_PING 0x06
#define BSS_MSG_SET_NEOPIXEL_COLOR 0x07
#define BSS_MSG_RESET_NEOPIXEL 0x08

// ESP NOW Config
#define BSS_ESP_NOW_CHANNEL 0
#define BSS_ESP_NOW_ENCRYPT false

inline void blink(uint8_t pin, bool initial_state)
{
  static bool state = !initial_state;

  digitalWrite(pin, state = !state);
}

#endif