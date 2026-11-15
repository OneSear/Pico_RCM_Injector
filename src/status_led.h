#pragma once

#include <stdint.h>

// Status output for boards whose only LED is a single WS2812 (RP2350-USB-CM:
// WS2812B on GPIO16). Safe to call from a single core only.
void status_led_init(void);

// Set the LED to a fixed GRB colour. r/g/b are 0..255.
void status_led_set(uint8_t r, uint8_t g, uint8_t b);
