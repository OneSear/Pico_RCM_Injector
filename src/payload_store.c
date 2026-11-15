// SPDX-License-Identifier: GPL-3.0-only
//
// See payload_store.h. Implements the flash-backed payload store.

#include "payload_store.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"  // XIP_BASE
#include "bsp/board_api.h"             // board_millis

// --- Flash layout ----------------------------------------------------------
//
// The payload region sits at the very top of flash so it cannot collide with
// the firmware image, which the linker places from the bottom upward.
//
//   [ header offset ] 1 x 4 KiB  -> payload header (magic/len/crc)
//   [ data offset   ] 1 MiB      -> payload data (FAT data region)
//
// The flash size is detected at runtime (see detect_flash_size). The board's
// real size matters: a Waveshare RP2350-USB-A/CM has 2 MiB, not the 4 MiB the
// generic pico2 board assumes, so a compile-time PICO_FLASH_SIZE_BYTES would
// place this region past the end of the chip.

#define PAYLOAD_REGION_SIZE (0x1000u + PAYLOAD_DATA_SIZE)  // 4 KiB + 1 MiB

#define PAYLOAD_MAGIC   0x504D4352u  // "RCMP"
#define PAYLOAD_VERSION 1u

#define COMMIT_IDLE_MS  750u   // commit once writes have been quiet this long
#define FLASH_OP_TIMEOUT_MS 3000u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t data_off;  // byte offset of payload within the data region
    uint32_t length;    // payload length in bytes
    uint32_t crc32;     // CRC32 of the payload bytes
} payload_hdr_t;

// --- State -----------------------------------------------------------------

// Detected flash geometry (set in payload_store_init).
static uint32_t s_flash_size = 0;
static uint32_t s_hdr_offset = 0;   // flash offset of the header
static uint32_t s_data_offset = 0;  // flash offset of the data region
static bool     s_ready = false;    // geometry valid and region fits

static volatile bool     s_valid = false;
static volatile uint32_t s_valid_off = 0;
static volatile uint32_t s_valid_len = 0;

// Write-back cache: one 4 KiB flash page held in RAM.
static uint8_t  s_page[FLASH_SECTOR_SIZE];
static int32_t  s_page_idx = -1;      // data-region 4 KiB page index, -1 = empty
static bool     s_page_dirty = false;

// Pending file discovered via the FAT root directory.
static bool     s_have_pending = false;
static bool     s_committed = true;   // nothing to commit until an upload starts
static uint32_t s_pending_off = 0;
static uint32_t s_pending_len = 0;

static volatile uint32_t s_last_activity_ms = 0;

// Memory-mapped (XIP) views of the stored header / data.
static inline const payload_hdr_t *hdr_xip(void) {
    return (const payload_hdr_t *)(uintptr_t)(XIP_BASE + s_hdr_offset);
}
static inline const uint8_t *data_xip(void) {
    return (const uint8_t *)(uintptr_t)(XIP_BASE + s_data_offset);
}

// --- Flash size detection --------------------------------------------------

// Read the JEDEC ID (RDID, 0x9F) and decode the capacity byte. flash_do_cmd is
// RAM-resident and restores XIP on return, so this is safe to call at startup
// (single core, before core1 launches).
static uint32_t detect_flash_size(void) {
    uint8_t tx[4] = { 0x9Fu, 0, 0, 0 };
    uint8_t rx[4] = { 0 };
    flash_do_cmd(tx, rx, 4);
    uint8_t capacity = rx[3];  // log2(size in bytes) for standard SPI NOR
    if (capacity >= 16 && capacity <= 28) {  // 64 KiB .. 256 MiB
        return 1u << capacity;
    }
    return 2u * 1024u * 1024u;  // safe fallback: this board is 2 MiB
}

// --- CRC32 (poly 0xEDB88320) ----------------------------------------------

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) {
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

// --- Flash helpers (run under flash_safe_execute, i.e. core1 locked out) ---

typedef struct {
    uint32_t offset;       // flash offset (from XIP_BASE)
    const uint8_t *data;
    uint32_t erase_size;   // multiple of FLASH_SECTOR_SIZE
    uint32_t prog_size;    // multiple of FLASH_PAGE_SIZE
} flash_job_t;

static void flash_job_run(void *param) {
    const flash_job_t *j = (const flash_job_t *)param;
    flash_range_erase(j->offset, j->erase_size);
    flash_range_program(j->offset, j->data, j->prog_size);
}

static bool flash_write(uint32_t offset, const uint8_t *data,
                        uint32_t erase_size, uint32_t prog_size) {
    flash_job_t job = {
        .offset = offset,
        .data = data,
        .erase_size = erase_size,
        .prog_size = prog_size,
    };
    int rc = flash_safe_execute(flash_job_run, &job, FLASH_OP_TIMEOUT_MS);
    if (rc != PICO_OK) {
        printf("payload_store: flash op failed at 0x%08lx (rc=%d)\n",
               (unsigned long)offset, rc);
        return false;
    }
    return true;
}

// --- Public API ------------------------------------------------------------

void payload_store_init(void) {
    s_flash_size = detect_flash_size();

    if (s_flash_size < PAYLOAD_REGION_SIZE) {
        printf("payload_store: flash too small (%lu bytes); store disabled\n",
               (unsigned long)s_flash_size);
        s_ready = false;
        return;
    }

    uint32_t region_offset = s_flash_size - PAYLOAD_REGION_SIZE;
    s_hdr_offset = region_offset;
    s_data_offset = region_offset + 0x1000u;
    s_ready = true;

    printf("payload_store: flash=%lu KiB, region at 0x%08lx (data 0x%08lx)\n",
           (unsigned long)(s_flash_size / 1024u),
           (unsigned long)s_hdr_offset, (unsigned long)s_data_offset);

    const payload_hdr_t *h = hdr_xip();
    if (h->magic == PAYLOAD_MAGIC && h->version >= 1 &&
        h->length > 0 && h->length <= PAYLOAD_DATA_SIZE &&
        (uint64_t)h->data_off + h->length <= PAYLOAD_DATA_SIZE) {
        uint32_t crc = crc32_update(0, data_xip() + h->data_off, h->length);
        if (crc == h->crc32) {
            s_valid = true;
            s_valid_off = h->data_off;
            s_valid_len = h->length;
        } else {
            printf("payload_store: stored header CRC mismatch; ignoring\n");
        }
    }
    s_committed = true;
    s_have_pending = false;
}

bool payload_store_valid(void) { return s_valid; }

const uint8_t *payload_store_data(void) { return data_xip() + s_valid_off; }

uint32_t payload_store_length(void) { return s_valid_len; }

bool payload_store_upload_active(void) {
    return s_page_dirty || (s_have_pending && !s_committed);
}

void payload_store_flush(void) {
    if (!s_ready || s_page_idx < 0 || !s_page_dirty) {
        return;
    }
    uint32_t offset = s_data_offset + (uint32_t)s_page_idx * FLASH_SECTOR_SIZE;
    if (flash_write(offset, s_page, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE)) {
        s_page_dirty = false;
    }
}

void payload_store_write(uint32_t data_off, const uint8_t *buf, uint32_t len) {
    if (!s_ready || len == 0 || (uint64_t)data_off + len > PAYLOAD_DATA_SIZE) {
        return;  // outside the payload data region: ignore
    }
    int32_t page = (int32_t)(data_off / FLASH_SECTOR_SIZE);
    if (page != s_page_idx) {
        payload_store_flush();
        // Seed the cache from current flash contents so sectors we do not
        // touch in this page survive the erase/program.
        memcpy(s_page, data_xip() + (uint32_t)page * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        s_page_idx = page;
        s_page_dirty = false;
    }
    memcpy(s_page + (data_off % FLASH_SECTOR_SIZE), buf, len);
    s_page_dirty = true;
    s_committed = false;
    s_last_activity_ms = board_millis();
}

void payload_store_read(uint32_t data_off, uint8_t *buf, uint32_t len) {
    if (!s_ready || (uint64_t)data_off + len > PAYLOAD_DATA_SIZE) {
        memset(buf, 0, len);
        return;
    }
    int32_t page = (int32_t)(data_off / FLASH_SECTOR_SIZE);
    if (page == s_page_idx && s_page_dirty) {
        memcpy(buf, s_page + (data_off % FLASH_SECTOR_SIZE), len);
    } else {
        memcpy(buf, data_xip() + data_off, len);
    }
}

void payload_store_note_entry(uint32_t data_off, uint32_t length) {
    if (!s_ready || length == 0 || length > PAYLOAD_DATA_SIZE ||
        (uint64_t)data_off + length > PAYLOAD_DATA_SIZE) {
        return;
    }
    s_pending_off = data_off;
    s_pending_len = length;
    s_have_pending = true;
    s_committed = false;
    s_last_activity_ms = board_millis();
}

static void commit_now(void) {
    payload_store_flush();
    if (!s_ready || !s_have_pending || s_committed) {
        return;
    }
    uint32_t crc = crc32_update(0, data_xip() + s_pending_off, s_pending_len);

    // Build a one-page header image and program it.
    static uint8_t hdr_buf[FLASH_PAGE_SIZE];
    memset(hdr_buf, 0xFF, sizeof(hdr_buf));
    payload_hdr_t *h = (payload_hdr_t *)hdr_buf;
    h->magic = PAYLOAD_MAGIC;
    h->version = PAYLOAD_VERSION;
    h->data_off = s_pending_off;
    h->length = s_pending_len;
    h->crc32 = crc;

    if (flash_write(s_hdr_offset, hdr_buf, FLASH_SECTOR_SIZE, FLASH_PAGE_SIZE)) {
        s_valid = true;
        s_valid_off = s_pending_off;
        s_valid_len = s_pending_len;
        s_committed = true;
        printf("payload_store: stored %lu bytes (data+0x%lx, crc 0x%08lx)\n",
               (unsigned long)s_pending_len, (unsigned long)s_pending_off,
               (unsigned long)crc);
    }
}

void payload_store_force_commit(void) { commit_now(); }

void payload_store_task(void) {
    if (!s_have_pending && !s_page_dirty) {
        return;
    }
    if (board_millis() - s_last_activity_ms < COMMIT_IDLE_MS) {
        return;  // still receiving data
    }
    if (s_have_pending && !s_committed) {
        commit_now();
    } else {
        payload_store_flush();
    }
}
