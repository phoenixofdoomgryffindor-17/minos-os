#include "ata.h"
#include "kernel.h"
#include "serial.h"

#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_CONTROL 0x3F6
#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_IDENTIFY 0xEC

static int present;
static uint32_t sector_count;

static int ata_wait(uint8_t mask, uint8_t value) {
    uint32_t timeout = 1000000;
    uint8_t status;
    do {
        status = inb(ATA_STATUS);
        if ((status & mask) == value) return 0;
        __asm__ volatile ("pause");
    } while (--timeout);
    return -1;
}

static int ata_transfer(uint32_t lba, uint8_t *buffer, int write) {
    uint32_t i;
    if (!present || !buffer || (lba & 0xF0000000U) ||
        !sector_count || lba >= sector_count) return -1;
    if (ata_wait(ATA_SR_BSY, 0) != 0) return -1;
    outb(ATA_DRIVE, (uint8_t)(0xE0U | ((lba >> 24) & 0x0FU)));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, (uint8_t)(write ? ATA_CMD_WRITE : ATA_CMD_READ));
    if (ata_wait(ATA_SR_BSY | ATA_SR_DRQ | ATA_SR_ERR,
                 ATA_SR_DRQ) != 0) return -1;
    if (write) {
        for (i = 0; i < 256; ++i) {
            uint16_t word = (uint16_t)buffer[i * 2] |
                            ((uint16_t)buffer[i * 2 + 1] << 8);
            __asm__ volatile ("outw %0, %1" : : "a"(word), "Nd"((uint16_t)ATA_DATA));
        }
        outb(ATA_COMMAND, 0xE7); /* cache flush */
        return ata_wait(ATA_SR_BSY, 0);
    }
    for (i = 0; i < 256; ++i) {
        uint16_t word;
        __asm__ volatile ("inw %1, %0" : "=a"(word) : "Nd"((uint16_t)ATA_DATA));
        buffer[i * 2] = (uint8_t)word;
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }
    return 0;
}

int ata_primary_master_init(void) {
    uint8_t status;
    uint16_t word, sectors_low, sectors_high;
    uint32_t i;
    present = 0;
    sector_count = 0;
    outb(ATA_CONTROL, 2); /* disable interrupts while polling */
    outb(ATA_DRIVE, 0xA0);
    io_wait();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    status = inb(ATA_STATUS);
    if (!status) return -1;
    if (ata_wait(ATA_SR_BSY, 0) != 0) return -1;
    if (inb(ATA_LBA1) || inb(ATA_LBA2)) return -1; /* ATAPI/other device */
    if (ata_wait(ATA_SR_DRQ | ATA_SR_ERR, ATA_SR_DRQ) != 0) return -1;
    sectors_low = sectors_high = 0;
    for (i = 0; i < 256; ++i) {
        __asm__ volatile ("inw %1, %0" : "=a"(word) : "Nd"((uint16_t)ATA_DATA));
        if (i == 60) sectors_low = word;
        if (i == 61) sectors_high = word;
    }
    sector_count = ((uint32_t)sectors_high << 16) | sectors_low;
    present = 1;
    serial_puts("[MinOS ATA] primary master PIO device ready (28-bit LBA).\n");
    return 0;
}

int ata_primary_master_present(void) { return present; }
int ata_read28(uint32_t lba, uint8_t *buffer) { return ata_transfer(lba, buffer, 0); }
int ata_write28(uint32_t lba, const uint8_t *buffer) {
    return ata_transfer(lba, (uint8_t *)buffer, 1);
}
