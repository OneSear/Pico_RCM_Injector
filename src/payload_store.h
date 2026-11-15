// SPDX-License-Identifier: GPL-3.0-only
//
// Persistent store for an RCM payload dropped onto the USB mass-storage drive.
//
// A dropped payload is streamed into a reserved region at the top of flash and
// validated with a small header (magic + length + CRC32). On the next boot the
// header is checked; if valid, the stored payload overrides the compiled-in
// default. All flash erase/program is performed through flash_safe_execute() so
// the PIO-USB host running on core1 is safely locked out for the operation.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// FAT data-region geometry shared with the MSC disk emulation (msc_disk.c).
// One cluster maps 1:1 onto one 4 KiB flash sector.
#define PAYLOAD_SECTOR_SIZE      512u
#define PAYLOAD_SEC_PER_CLUS     8u
#define PAYLOAD_CLUSTER_BYTES    (PAYLOAD_SECTOR_SIZE * PAYLOAD_SEC_PER_CLUS)  // 4096
#define PAYLOAD_DATA_CLUSTERS    256u
#define PAYLOAD_DATA_SIZE        (PAYLOAD_DATA_CLUSTERS * PAYLOAD_CLUSTER_BYTES)  // 1 MiB

// Read stored header on boot and latch validity. Call once, early, from core0.
void payload_store_init(void);

// True once a validated payload is present (either stored in flash or just
// committed from a drag-and-drop upload).
bool payload_store_valid(void);

// XIP pointer / length of the active stored payload. Only meaningful when
// payload_store_valid() is true.
const uint8_t *payload_store_data(void);
uint32_t payload_store_length(void);

// True while a drag-and-drop upload is being received or committed. The injector
// uses this to avoid injecting a half-written payload.
bool payload_store_upload_active(void);

// --- Called from the MSC disk callbacks (core0) ---------------------------

// Stream `len` bytes written at byte offset `data_off` within the FAT data
// region into the flash write-back cache. `len` never spans a 4 KiB page.
void payload_store_write(uint32_t data_off, const uint8_t *buf, uint32_t len);

// Read back `len` bytes from data-region byte offset `data_off`, honoring the
// in-RAM write-back cache if the page is dirty.
void payload_store_read(uint32_t data_off, uint8_t *buf, uint32_t len);

// Record the payload file discovered in the FAT root directory: byte offset of
// its first cluster within the data region, and its length in bytes.
void payload_store_note_entry(uint32_t data_off, uint32_t length);

// Flush the dirty write-back page to flash (no-op if clean).
void payload_store_flush(void);

// Commit immediately (used on eject / SYNCHRONIZE CACHE) if a payload is pending.
void payload_store_force_commit(void);

// Periodic housekeeping: flush + commit a pending upload once writes go quiet.
// Call frequently from the core0 main loop.
void payload_store_task(void);
