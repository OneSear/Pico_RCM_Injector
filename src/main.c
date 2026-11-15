// SPDX-License-Identifier: GPL-3.0-only
//
// RCM payload injector for the Waveshare RP2350-USB-CM.
//
// On this board the Type-C plug that mates with the target (the Switch) is wired
// to the RP2350's PIO-USB port (D+ = GPIO13, D- = GPIO12), not the native USB
// peripheral -- the native receptacle is used only for flashing. The PIO-USB
// host therefore runs on core1 (roothub port 1) while core0 drives the WS2812
// status LED on GPIO16.

#include <stdio.h>

#include "hardware/clocks.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "bsp/board_api.h"
#include "pio_usb.h"
#include "tusb.h"

#include "payload.h"
#include "payload_store.h"
#include "rcm_injector.h"
#include "status_led.h"

// --- Board wiring (Waveshare RP2350-USB-CM) ---
#define PIO_USB_DP_PIN 13  // D- is GPIO12 via the DMDP pinout (DM = DP - 1)

typedef enum {
    APP_WAITING = 0,
    APP_INJECTING,
    APP_DONE,
    APP_ERROR,
} app_state_t;

// Shared between core0 (LED) and core1 (USB). Single writer (core1) for state.
static volatile app_state_t app_state = APP_WAITING;
static volatile uint8_t error_blink_count = 0;

// Owned by core1 only.
static uint8_t apx_dev_addr = 0;
static volatile bool apx_ready = false;

// ---------------------------------------------------------------------------
// core0: WS2812 status indicator
// ---------------------------------------------------------------------------

static void status_task(void) {
    static uint32_t last_ms = 0;
    static uint32_t pause_start_ms = 0;
    static bool led = false;
    static uint8_t pulses_done = 0;
    const uint32_t now = board_millis();
    const app_state_t state = app_state;

    switch (state) {
        case APP_WAITING:
            // Slow blue blink while waiting for the target.
            if (now - last_ms >= 500) {
                last_ms = now;
                led = !led;
                status_led_set(0, 0, led ? 40 : 0);
            }
            pulses_done = 0;
            pause_start_ms = 0;
            return;

        case APP_INJECTING:
            status_led_set(40, 25, 0);  // solid amber
            pulses_done = 0;
            pause_start_ms = 0;
            return;

        case APP_DONE:
            status_led_set(0, 60, 0);  // solid green
            return;

        case APP_ERROR:
            break;
    }

    // APP_ERROR: blink red error_blink_count times, then pause.
    const uint8_t count = error_blink_count;
    if (count == 0) {
        status_led_set(60, 0, 0);
        return;
    }

    if (pulses_done >= count) {
        status_led_set(0, 0, 0);
        if (pause_start_ms == 0) {
            pause_start_ms = now;
        }
        if (now - pause_start_ms >= 1000) {
            pause_start_ms = 0;
            pulses_done = 0;
            last_ms = now;
        }
        return;
    }

    if (now - last_ms >= 160) {
        last_ms = now;
        led = !led;
        status_led_set(led ? 60 : 0, 0, 0);
        if (!led) {
            pulses_done++;
        }
    }
}

// ---------------------------------------------------------------------------
// core1: PIO-USB host + injection
// ---------------------------------------------------------------------------

void tuh_mount_cb(uint8_t dev_addr) {
    if (rcm_is_apx_device(dev_addr)) {
        apx_dev_addr = dev_addr;
        apx_ready = true;
    }
}

void tuh_umount_cb(uint8_t dev_addr) {
    if (apx_dev_addr == dev_addr) {
        apx_dev_addr = 0;
        apx_ready = false;
        error_blink_count = 0;
        app_state = APP_WAITING;
    }
}

static void core1_main(void) {
    // Allow core0 to lock this core out while it erases/programs flash to store
    // a dropped payload. Must be done before any flash_safe_execute() on core0.
    flash_safe_execute_core_init();

    // Hand the PIO-USB stack this board's pinout before bringing it up.
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIO_USB_DP_PIN;
    pio_cfg.pinout = PIO_USB_PINOUT_DMDP;  // D- = D+ minus 1 (GPIO12)
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    if (!tuh_init(BOARD_TUH_RHPORT)) {
        error_blink_count = 0;
        app_state = APP_ERROR;
        return;
    }

    while (true) {
        tuh_task();

        // Hold off while a drag-and-drop payload is still being written to flash.
        if (app_state == APP_WAITING && apx_ready && !payload_store_upload_active()) {
            app_state = APP_INJECTING;
            sleep_ms(100);

            // A stored (dropped) payload overrides the compiled-in default.
            const uint8_t *payload = rcm_payload;
            uint32_t payload_len = rcm_payload_len;
            if (payload_store_valid()) {
                payload = payload_store_data();
                payload_len = payload_store_length();
            }

            rcm_inject_result_t result =
                rcm_inject_payload(apx_dev_addr, payload, payload_len);
            printf("Injection result: %s\n", rcm_inject_result_name(result));

            if (result == RCM_INJECT_OK) {
                app_state = APP_DONE;
            } else {
                error_blink_count = (uint8_t)result;
                app_state = APP_ERROR;
            }
        }
    }
}

int main(void) {
    // PIO-USB bit-banging needs a system clock that is a multiple of 12 MHz.
    // Do this before anything else so both cores run at the right frequency.
    set_sys_clock_khz(120000, true);

    stdio_init_all();  // UART; native USB is claimed by the MSC device below
    status_led_init();

    // Latch any payload previously dropped onto the drive and stored in flash.
    payload_store_init();

    // Bring up the USB *device* (MSC drive) on the native port (roothub 0).
    tud_init(BOARD_TUD_RHPORT);

    printf("Pico RCM injector (RP2350-USB-CM) starting\n");
    if (payload_store_valid()) {
        printf("Payload source: dropped file, %lu bytes\n",
               (unsigned long)payload_store_length());
    } else {
        printf("Payload source: compiled-in default, %lu bytes\n",
               (unsigned long)rcm_payload_len);
    }

    // Visible boot indicator: 3 green blinks if a stored (dropped) payload is
    // active, 3 blue blinks if using the compiled-in default. Lets you confirm
    // a dropped payload took effect even without a UART console.
    for (int i = 0; i < 3; i++) {
        if (payload_store_valid()) {
            status_led_set(0, 60, 0);  // green: stored payload
        } else {
            status_led_set(0, 0, 60);  // blue: compiled-in fallback
        }
        sleep_ms(120);
        status_led_set(0, 0, 0);
        sleep_ms(120);
    }

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    while (true) {
        tud_task();             // service the MSC drive
        payload_store_task();   // commit a dropped payload once writes go quiet
        status_task();          // self-throttled via board_millis()
    }
}
