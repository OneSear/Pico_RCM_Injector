// SPDX-License-Identifier: GPL-3.0-only
//
// Single WS2812 status LED. PIO-USB owns pio0/pio1, so this driver is pinned to
// pio2 (only present on RP2350) to avoid any state-machine or program clash.

#include "status_led.h"

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "ws2812.pio.h"

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 16
#endif

#ifndef STATUS_LED_PIO
#define STATUS_LED_PIO pio2
#endif

static PIO led_pio = STATUS_LED_PIO;
static uint led_sm = 0;
static bool led_ready = false;

void status_led_init(void) {
    led_sm = (uint)pio_claim_unused_sm(led_pio, true);
    uint offset = pio_add_program(led_pio, &ws2812_program);
    ws2812_program_init(led_pio, led_sm, offset, STATUS_LED_PIN, 800000.0f, false);
    led_ready = true;
    status_led_set(0, 0, 0);
}

void status_led_set(uint8_t r, uint8_t g, uint8_t b) {
    if (!led_ready) {
        return;
    }

    // WS2812 expects GRB, MSB first, in the top 24 bits of the 32-bit word.
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
    pio_sm_put_blocking(led_pio, led_sm, grb << 8u);
}
