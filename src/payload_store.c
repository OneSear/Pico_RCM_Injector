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
// The payload region sits at the very top of flash so it can never collide with
// the firmware image, which the linker places from the bottom upward. With the
// pico2 default of 4 MiB flash and a firmware well under 1 MiB, there is a large
// gap below this region.
//
//   [ PAYLOAD_HDR_OFFSET  ] 1 x 4 KiB  -> payload header (magic/len/crc)
//   [ PAYLOAD_DATA_OFFSET ] 1 MiB      -> payload data (FAT data region)

#define PAYLOAD_REGION_SIZE   (0x1000u + PAYLOAD_DATA_SIZE)            // 4 KiB + 1 MiB
#define PAYLOAD_REGION_OFFSET (PICO_FLASH_SIZE_BYTES - PAYLOAD_REGION_SIZE)
#define PAYLOAD_HDR_OFFSET    (PAYLOAD_REGION_OFFSET)                  // flash offset
#define PAYLOAD_DATA_OFFSET   (PAYLOAD_REGION_OFFSET + 0x1000u)        // flash offset

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

// A memory-mapped (XIP) view of the stored header / data.
#define HDR_XIP  ((const payload_hdr_t *)(XIP_BASE + PAYLOAD_HDR_OFFSET))
#define DATA_XIP ((const uint8_t *)(XIP_BASE + PAYLOAD_DATA_OFFSET))

// --- State -----------------------------------------------------------------

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
    const payload_hdr_t *h = HDR_XIP;
    if (h->magic == PAYLOAD_MAGIC && h->version >= 1 &&
        h->length > 0 && h->length <= PAYLOAD_DATA_SIZE &&
        (uint64_t)h->data_off + h->length <= PAYLOAD_DATA_SIZE) {
        uint32_t crc = crc32_update(0, DATA_XIP + h->data_off, h->length);
        if (crc == h->crc32) {
            s_valid = true;
            s_valid_off = h->data_off;
            s_valid_len = h->length;
        }
    }
    s_committed = true;
    s_have_pending = false;
}

bool payload_store_valid(void) { return s_valid; }

const uint8_t *payload_store_data(void) { return DATA_XIP + s_valid_off; }

uint32_t payload_store_length(void) { return s_valid_len; }

bool payload_store_upload_active(void) {
    return s_page_dirty || (s_have_pending && !s_committed);
}

void payload_store_flush(void) {
    if (s_page_idx < 0 || !s_page_dirty) {
        return;
    }
    uint32_t offset = PAYLOAD_DATA_OFFSET + (uint32_t)s_page_idx * FLASH_SECTOR_SIZE;
    if (flash_write(offset, s_page, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE)) {
        s_page_dirty = false;
    }
}

void payload_store_write(uint32_t data_off, const uint8_t *buf, uint32_t len) {
    if (len == 0 || (uint64_t)data_off + len > PAYLOAD_DATA_SIZE) {
        return;  // outside the payload data region: ignore
    }
    int32_t page = (int32_t)(data_off / FLASH_SECTOR_SIZE);
    if (page != s_page_idx) {
        payload_store_flush();
        // Seed the cache from current flash contents so sectors we do not
        // touch in this page survive the erase/program.
        memcpy(s_page, DATA_XIP + (uint32_t)page * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        s_page_idx = page;
        s_page_dirty = false;
    }
    memcpy(s_page + (data_off % FLASH_SECTOR_SIZE), buf, len);
    s_page_dirty = true;
    s_committed = false;
    s_last_activity_ms = board_millis();
}

void payload_store_read(uint32_t data_off, uint8_t *buf, uint32_t len) {
    if ((uint64_t)data_off + len > PAYLOAD_DATA_SIZE) {
        memset(buf, 0, len);
        return;
    }
    int32_t page = (int32_t)(data_off / FLASH_SECTOR_SIZE);
    if (page == s_page_idx && s_page_dirty) {
        memcpy(buf, s_page + (data_off % FLASH_SECTOR_SIZE), len);
    } else {
        memcpy(buf, DATA_XIP + data_off, len);
    }
}

void payload_store_note_entry(uint32_t data_off, uint32_t length) {
    if (length == 0 || length > PAYLOAD_DATA_SIZE ||
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
    if (!s_have_pending || s_committed) {
        return;
    }
    uint32_t crc = crc32_update(0, DATA_XIP + s_pending_off, s_pending_len);

    // Build a one-page header image and program it.
    static uint8_t hdr_buf[FLASH_PAGE_SIZE];
    memset(hdr_buf, 0xFF, sizeof(hdr_buf));
    payload_hdr_t *h = (payload_hdr_t *)hdr_buf;
    h->magic = PAYLOAD_MAGIC;
    h->version = PAYLOAD_VERSION;
    h->data_off = s_pending_off;
    h->length = s_pending_len;
    h->crc32 = crc;

    if (flash_write(PAYLOAD_HDR_OFFSET, hdr_buf, FLASH_SECTOR_SIZE, FLASH_PAGE_SIZE)) {
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
