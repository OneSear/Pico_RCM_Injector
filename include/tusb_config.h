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

// ---------------------------------------------------------------------------
// Device stack: the native USB peripheral (roothub port 0) presents a USB Mass
// Storage drive so a payload.bin can be dragged onto it. This coexists with the
// PIO-USB host above; TinyUSB runs both stacks at once.
// ---------------------------------------------------------------------------

#define CFG_TUD_ENABLED 1

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#endif

#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED

#ifndef CFG_TUD_MEM_SECTION
#define CFG_TUD_MEM_SECTION
#endif

#ifndef CFG_TUD_MEM_ALIGN
#define CFG_TUD_MEM_ALIGN __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 1
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

// One 512-byte logical block per MSC bulk transfer.
#define CFG_TUD_MSC_EP_BUFSIZE 512

#ifdef __cplusplus
}
#endif
