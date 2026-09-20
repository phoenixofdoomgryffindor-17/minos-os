#ifndef MINOS_ATA_H
#define MINOS_ATA_H

#include <stdint.h>

/* ATA primary channel, device 0 (28-bit LBA PIO).  All operations time out. */
int ata_primary_master_init(void);
int ata_primary_master_present(void);
int ata_read28(uint32_t lba, uint8_t *buffer);
int ata_write28(uint32_t lba, const uint8_t *buffer);

#endif
