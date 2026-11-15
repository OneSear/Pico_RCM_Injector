// SPDX-License-Identifier: GPL-3.0-only
//
// USB *device* descriptors for the native-USB Mass Storage interface that
// exposes the drag-and-drop payload drive. The PIO-USB *host* side does not use
// these; it enumerates the target as a host and needs no device descriptors.

#include <string.h>

#include "tusb.h"
#include "pico/unique_id.h"

// VID/PID: TinyUSB's example VID with a PID picked from the MSC range. These are
// for development/hobby use and are not a registered USB-IF assignment.
#define USB_VID 0xCAFE
#define USB_PID 0x4002
#define USB_BCD 0x0200

// --- Device descriptor -----------------------------------------------------

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,  // defined at interface level
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

// --- Configuration descriptor ----------------------------------------------

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL,
};

#define EPNUM_MSC_OUT 0x01
#define EPNUM_MSC_IN  0x81

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // Config: number, interface count, string index, total length, attributes, power (mA)
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // MSC: interface number, string index, EP Out, EP In, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// --- String descriptors ----------------------------------------------------

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MSC,
};

static const char *const string_desc_arr[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},  // English (0x0409)
    [STRID_MANUFACTURER] = "Pico RCM Injector",
    [STRID_PRODUCT]      = "RCM Payload Drive",
    [STRID_SERIAL]       = NULL,  // filled from the chip unique ID
    [STRID_MSC]          = "Payload",
};

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t char_count;

    switch (index) {
        case STRID_LANGID:
            memcpy(&desc_str[1], string_desc_arr[STRID_LANGID], 2);
            char_count = 1;
            break;

        case STRID_SERIAL: {
            // 16 hex chars from the 8-byte flash unique ID.
            pico_unique_board_id_t uid;
            pico_get_unique_board_id(&uid);
            char_count = 0;
            for (int i = 0; i < 8 && char_count < 32; i++) {
                const char hex[] = "0123456789ABCDEF";
                desc_str[1 + char_count++] = hex[(uid.id[i] >> 4) & 0xF];
                desc_str[1 + char_count++] = hex[uid.id[i] & 0xF];
            }
            break;
        }

        default: {
            if (index >= TU_ARRAY_SIZE(string_desc_arr) || string_desc_arr[index] == NULL) {
                return NULL;
            }
            const char *str = string_desc_arr[index];
            char_count = strlen(str);
            if (char_count > 32) {
                char_count = 32;
            }
            for (size_t i = 0; i < char_count; i++) {
                desc_str[1 + i] = str[i];
            }
            break;
        }
    }

    // First element is the length (in bytes) + descriptor type.
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * char_count + 2));
    return desc_str;
}
