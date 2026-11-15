// SPDX-License-Identifier: GPL-3.0-only
//
// FAT12 mass-storage emulation for drag-and-drop payload upload.
//
// The native-USB port presents a small, empty, FAT12-formatted drive. When an
// OS copies a file onto it, the writes are intercepted here: the boot sector,
// FAT and root directory are kept in RAM, while writes to the data region are
// streamed into the flash-backed payload store. The root directory is parsed to
// learn the dropped file's start cluster and length, which the store uses to
// commit a validated payload.
//
// Layout (512-byte sectors), matching the BPB built below:
//   LBA 0          boot sector
//   LBA 1          FAT #1
//   LBA 2          FAT #2
//   LBA 3          root directory (16 entries)
//   LBA 4 ..       data region (cluster 2 = LBA 4), 256 x 4 KiB clusters

#include <string.h>

#include "tusb.h"
#include "payload_store.h"

#define SECTOR_SIZE   PAYLOAD_SECTOR_SIZE   // 512
#define SEC_PER_CLUS  PAYLOAD_SEC_PER_CLUS  // 8
#define RESERVED_SEC  1u
#define NUM_FATS      2u
#define FAT_SECS      1u
#define ROOT_SECS     1u
#define ROOT_ENTRIES  16u
#define DATA_CLUSTERS PAYLOAD_DATA_CLUSTERS // 256
#define DATA_SECS     (DATA_CLUSTERS * SEC_PER_CLUS)  // 2048

#define FAT_LBA       (RESERVED_SEC)                      // 1
#define ROOT_LBA      (RESERVED_SEC + NUM_FATS * FAT_SECS) // 3
#define DATA_LBA      (ROOT_LBA + ROOT_SECS)              // 4
#define TOTAL_SECS    (DATA_LBA + DATA_SECS)              // 2052

// A read-only STATUS.TXT diagnostic file is pinned to cluster 2 (the first data
// cluster). Host-written payloads therefore start at cluster 3. The file content
// is generated on read, so it always reflects the firmware's current state.
#define STATUS_CLUSTER 2u
#define STATUS_SIZE    512u

// In-RAM metadata (small). The data region is backed by flash, not RAM.
static uint8_t s_fat[SECTOR_SIZE];
static uint8_t s_root[SECTOR_SIZE];
static bool    s_inited = false;

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void build_boot_sector(uint8_t sector[SECTOR_SIZE]) {
    memset(sector, 0, SECTOR_SIZE);
    sector[0] = 0xEB; sector[1] = 0x3C; sector[2] = 0x90;  // jump
    memcpy(sector + 3, "MSDOS5.0", 8);                      // OEM name
    write_u16(sector + 11, SECTOR_SIZE);                    // bytes/sector
    sector[13] = SEC_PER_CLUS;                              // sectors/cluster
    write_u16(sector + 14, RESERVED_SEC);                   // reserved sectors
    sector[16] = NUM_FATS;                                  // number of FATs
    write_u16(sector + 17, ROOT_ENTRIES);                   // root entries
    write_u16(sector + 19, TOTAL_SECS);                     // total sectors (16-bit)
    sector[21] = 0xF8;                                      // media type (fixed disk)
    write_u16(sector + 22, FAT_SECS);                       // sectors per FAT
    write_u16(sector + 24, 1);                              // sectors per track
    write_u16(sector + 26, 1);                              // number of heads
    sector[36] = 0x80;                                      // drive number
    sector[38] = 0x29;                                      // extended boot signature
    sector[39] = 0x78; sector[40] = 0x56;                   // volume serial
    sector[41] = 0x34; sector[42] = 0x12;
    memcpy(sector + 43, "RCMINJECT  ", 11);                 // volume label
    memcpy(sector + 54, "FAT12   ", 8);                     // fs type
    sector[510] = 0x55; sector[511] = 0xAA;                 // boot signature
}

static void put_dir_entry(uint8_t *e, const char name[11], uint8_t attr,
                          uint16_t cluster, uint32_t size) {
    memset(e, 0, 32);
    memcpy(e, name, 11);
    e[11] = attr;
    e[26] = (uint8_t)(cluster & 0xFF);
    e[27] = (uint8_t)(cluster >> 8);
    e[28] = (uint8_t)(size & 0xFF);
    e[29] = (uint8_t)((size >> 8) & 0xFF);
    e[30] = (uint8_t)((size >> 16) & 0xFF);
    e[31] = (uint8_t)((size >> 24) & 0xFF);
}

static void disk_init(void) {
    if (s_inited) {
        return;
    }
    // FAT: clusters 0 and 1 reserved; cluster 2 marked EOC (0xFFF) because it is
    // allocated to the pinned STATUS.TXT file.
    memset(s_fat, 0, sizeof(s_fat));
    s_fat[0] = 0xF8; s_fat[1] = 0xFF; s_fat[2] = 0xFF;  // clusters 0, 1
    s_fat[3] = 0xFF; s_fat[4] = 0x0F;                   // cluster 2 = 0xFFF (EOC)
    // Root directory: one read-only STATUS.TXT at cluster 2.
    memset(s_root, 0, sizeof(s_root));
    put_dir_entry(s_root, "STATUS  TXT", 0x01, STATUS_CLUSTER, STATUS_SIZE);
    s_inited = true;
}

static void build_status_sector(uint8_t out[SECTOR_SIZE]) {
    char tmp[SECTOR_SIZE + 1];
    int n = payload_store_format_status(tmp, sizeof(tmp));
    if (n < 0) {
        n = 0;
    }
    if (n > (int)SECTOR_SIZE) {
        n = (int)SECTOR_SIZE;
    }
    memset(out, ' ', SECTOR_SIZE);
    memcpy(out, tmp, (size_t)n);
}

// Parse the root directory and hand the largest regular file to the store.
static void scan_root_dir(void) {
    uint32_t best_size = 0;
    uint16_t best_clus = 0;

    for (unsigned i = 0; i < ROOT_ENTRIES; i++) {
        const uint8_t *e = s_root + i * 32;
        uint8_t c0 = e[0];
        if (c0 == 0x00) {
            break;          // no further entries
        }
        if (c0 == 0xE5 || c0 == 0x2E) {
            continue;       // deleted, or "." / ".." / dot-files (macOS metadata)
        }
        if (memcmp(e, "STATUS  TXT", 11) == 0) {
            continue;       // our own diagnostic file
        }
        uint8_t attr = e[11];
        if (attr == 0x0F) {
            continue;       // long-file-name entry
        }
        if (attr & (0x08 | 0x10)) {
            continue;       // volume label or directory
        }
        uint16_t clus = (uint16_t)(e[26] | (e[27] << 8));
        uint32_t size = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
                        ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
        if (size == 0 || clus < 2) {
            continue;
        }
        if (size > best_size) {
            best_size = size;
            best_clus = clus;
        }
    }

    if (best_size > 0) {
        uint32_t data_off = (uint32_t)(best_clus - 2) * PAYLOAD_CLUSTER_BYTES;
        payload_store_note_entry(data_off, best_size);
    }
}

// --- TinyUSB MSC callbacks -------------------------------------------------

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                        uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "RCMINJ  ", 8);
    memcpy(product_id, "Payload Drive   ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    disk_init();
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = TOTAL_SECS;
    *block_size = SECTOR_SIZE;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer,
                          uint32_t bufsize) {
    (void)lun;
    disk_init();
    if (bufsize == 0) {
        return 0;
    }

    uint8_t tmp[SECTOR_SIZE];
    if (lba == 0) {
        build_boot_sector(tmp);
        memcpy(buffer, tmp + offset, bufsize);
    } else if (lba >= FAT_LBA && lba < FAT_LBA + NUM_FATS * FAT_SECS) {
        memcpy(buffer, s_fat + offset, bufsize);  // both FAT copies are identical
    } else if (lba >= ROOT_LBA && lba < ROOT_LBA + ROOT_SECS) {
        memcpy(buffer, s_root + offset, bufsize);
    } else if (lba == DATA_LBA) {
        build_status_sector(tmp);           // cluster 2, sector 0: STATUS.TXT
        memcpy(buffer, tmp + offset, bufsize);
    } else if (lba > DATA_LBA && lba < DATA_LBA + SEC_PER_CLUS) {
        memset(buffer, 0, bufsize);         // rest of the reserved STATUS cluster
    } else if (lba >= DATA_LBA + SEC_PER_CLUS && lba < TOTAL_SECS) {
        uint32_t data_off = (lba - DATA_LBA) * SECTOR_SIZE + offset;
        payload_store_read(data_off, (uint8_t *)buffer, bufsize);
    } else {
        memset(buffer, 0, bufsize);
    }
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer,
                           uint32_t bufsize) {
    (void)lun;
    disk_init();
    if (bufsize == 0) {
        return 0;
    }

    if (lba == 0) {
        // Ignore writes to the boot sector; keep our synthesized BPB.
    } else if (lba >= FAT_LBA && lba < FAT_LBA + NUM_FATS * FAT_SECS) {
        if (offset + bufsize <= SECTOR_SIZE) {
            memcpy(s_fat + offset, buffer, bufsize);
        }
    } else if (lba >= ROOT_LBA && lba < ROOT_LBA + ROOT_SECS) {
        if (offset + bufsize <= SECTOR_SIZE) {
            memcpy(s_root + offset, buffer, bufsize);
            scan_root_dir();
        }
    } else if (lba >= DATA_LBA && lba < DATA_LBA + SEC_PER_CLUS) {
        // Reserved STATUS cluster: ignore writes.
    } else if (lba >= DATA_LBA + SEC_PER_CLUS && lba < TOTAL_SECS) {
        uint32_t data_off = (lba - DATA_LBA) * SECTOR_SIZE + offset;
        payload_store_write(data_off, buffer, bufsize);
    }
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer,
                        uint16_t bufsize) {
    (void)buffer;
    (void)bufsize;
    switch (scsi_cmd[0]) {
        case 0x35:  // SCSI SYNCHRONIZE CACHE (10): host flushed; commit now.
            payload_store_force_commit();
            return 0;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
                           bool load_eject) {
    (void)lun;
    (void)power_condition;
    if (load_eject && !start) {
        // Volume ejected/unmounted: make sure the payload is committed.
        payload_store_force_commit();
    }
    return true;
}
