#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined by the Pico SDK build.
#endif

// PICO_OS is required because the PIO-USB host runs on the second core and uses
// the SDK sync primitives for the cross-core event queue.
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#ifndef CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_SECTION
#endif

#ifndef CFG_TUH_MEM_ALIGN
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))
#endif

#define CFG_TUH_ENABLED 1

// The Switch's Type-C plug is wired to the RP2350's PIO-USB port, not the native
// USB peripheral (which is the receptacle used for flashing). Route the host
// stack through Pico-PIO-USB.
#define CFG_TUH_RPI_PIO_USB 1

#define CFG_TUH_DEVICE_MAX 1
#define CFG_TUH_HUB 0

// PIO-USB is exposed by TinyUSB as root-hub port 1 (native USB is port 0).
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT 1
#endif

// PIO-USB is a full-speed (12 Mbps) host, which is what Tegra RCM/APX enumerates as.
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED OPT_MODE_FULL_SPEED
#endif

#define CFG_TUH_MAX_SPEED BOARD_TUH_MAX_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_ENDPOINT_MAX 4
#define CFG_TUH_API_EDPT_XFER 1

#define CFG_TUH_CDC 0
#define CFG_TUH_HID 0
#define CFG_TUH_MSC 0
#define CFG_TUH_VENDOR 0

#ifdef __cplusplus
}
#endif
