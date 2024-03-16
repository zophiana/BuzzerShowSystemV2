/*
  Copyright © 2024 Leonard Sebastian Schwennesen. All rights reserved.
*/

#ifndef BSS_SHARED_H
#define BSS_SHARED_H

// Message Types
#define BSS_MSG_PAIRING_REQUEST 'P'
#define BSS_MSG_PAIRING_ACCEPTED 'A'
#define BSS_MSG_BUZZER_PRESSED 'B'
#define BSS_MSG_SET_NEOPIXEL_COLOR 'N'
#define BSS_MSG_RESET_NEOPIXEL 'R'

// ESP NOW Config
#define BSS_ESP_NOW_CHANNEL 0
#define BSS_ESP_NOW_ENCRYPT false

#endif