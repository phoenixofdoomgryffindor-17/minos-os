#ifndef MINOS_ATA_H
#define MINOS_ATA_H

#include <stdint.h>

/* ATA primary channel, device 0 (28-bit LBA PIO).  All operations time out. */
int ata_primary_master_init(void);
int ata_primary_master_present(void);
int ata_read28(uint32_t lba, uint8_t *buffer);
int ata_write28(uint32_t lba, const uint8_t *buffer);
int ata_flush(void);

/* Stable sector block-device interface used by filesystems. */
struct block_device {
    uint32_t block_size;
    uint32_t block_count;
    int (*read)(struct block_device *, uint32_t, void *);
    int (*write)(struct block_device *, uint32_t, const void *);
    int (*flush)(struct block_device *);
};
int ata_block_device_get(struct block_device *device);
int block_device_read(struct block_device *device, uint32_t block, void *buffer);
int block_device_write(struct block_device *device, uint32_t block, const void *buffer);
int block_device_flush(struct block_device *device);

#endif
