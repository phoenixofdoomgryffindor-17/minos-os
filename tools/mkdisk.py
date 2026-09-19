r"""
MinOS Bootable Disk Generator
Creates a standard FAT16 disk image for UEFI boot containing \EFI\BOOT\BOOTX64.EFI
100% Pure Python - Zero external host dependencies
"""

import os
import sys
import struct
import math

SECTOR_SIZE = 512
SECTORS_PER_CLUSTER = 4
CLUSTER_SIZE = SECTOR_SIZE * SECTORS_PER_CLUSTER # 2048 bytes
TOTAL_SECTORS = 65536 # 32 MB
RESERVED_SECTORS = 4
NUM_FATS = 2
ROOT_ENTRIES = 512
ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32) // SECTOR_SIZE # 32 sectors
SECTORS_PER_FAT = 64

def make_dir_entry(name8, ext3, attr, first_cluster, size):
    name_bytes = name8.ljust(8)[:8].encode('ascii')
    ext_bytes = ext3.ljust(3)[:3].encode('ascii')
    entry = bytearray(32)
    entry[0:8] = name_bytes
    entry[8:11] = ext_bytes
    entry[11] = attr
    # time & date (2026-09-19)
    struct.pack_into('<H', entry, 22, 0x5800) # 11:00:00
    struct.pack_into('<H', entry, 24, 0x5D33) # 2026-09-19
    struct.pack_into('<H', entry, 26, first_cluster)
    struct.pack_into('<I', entry, 28, size)
    return bytes(entry)

def create_fat16_image(efi_file_path, output_img_path):
    with open(efi_file_path, 'rb') as f:
        efi_data = f.read()

    efi_size = len(efi_data)
    efi_clusters_needed = math.ceil(efi_size / CLUSTER_SIZE)
    print(f"[MinOS mkdisk] EFI size: {efi_size} bytes ({efi_clusters_needed} clusters)")

    # Sector 0: Boot Sector (BPB)
    boot_sector = bytearray(SECTOR_SIZE)
    boot_sector[0:3] = b'\xEB\x3C\x90' # JMP short + NOP
    boot_sector[3:11] = b'MINOS1.0'    # OEM Name
    struct.pack_into('<H', boot_sector, 11, SECTOR_SIZE)
    boot_sector[13] = SECTORS_PER_CLUSTER
    struct.pack_into('<H', boot_sector, 14, RESERVED_SECTORS)
    boot_sector[16] = NUM_FATS
    struct.pack_into('<H', boot_sector, 17, ROOT_ENTRIES)
    struct.pack_into('<H', boot_sector, 19, 0) # Total sectors 16 (0 means use 32-bit field)
    boot_sector[21] = 0xF8                     # Media descriptor (Fixed disk)
    struct.pack_into('<H', boot_sector, 22, SECTORS_PER_FAT)
    struct.pack_into('<H', boot_sector, 24, 63) # Sectors per track
    struct.pack_into('<H', boot_sector, 26, 255) # Heads
    struct.pack_into('<I', boot_sector, 28, 0) # Hidden sectors
    struct.pack_into('<I', boot_sector, 32, TOTAL_SECTORS)
    boot_sector[36] = 0x80 # Drive number
    boot_sector[38] = 0x29 # Extended boot signature
    struct.pack_into('<I', boot_sector, 39, 0x19940520) # Volume ID
    boot_sector[43:54] = b'MINOS BOOT '
    boot_sector[54:62] = b'FAT16   '
    boot_sector[510:512] = b'\x55\xAA'

    # FAT Tables (2 copies)
    fat = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    struct.pack_into('<H', fat, 0, 0xFFF8) # Media byte
    struct.pack_into('<H', fat, 2, 0xFFFF) # End of chain

    # Cluster allocation:
    # Cluster 2: \EFI directory
    # Cluster 3: \EFI\BOOT directory
    # Cluster 4 .. (4 + efi_clusters_needed - 1): BOOTX64.EFI file
    struct.pack_into('<H', fat, 2 * 2, 0xFFFF) # Cluster 2 (EOF)
    struct.pack_into('<H', fat, 3 * 2, 0xFFFF) # Cluster 3 (EOF)

    first_file_cluster = 4
    for i in range(efi_clusters_needed):
        c = first_file_cluster + i
        if i == efi_clusters_needed - 1:
            struct.pack_into('<H', fat, c * 2, 0xFFFF) # EOF
        else:
            struct.pack_into('<H', fat, c * 2, c + 1)

    # Root Directory
    root_dir = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)
    # Entry 0: Volume Label
    root_dir[0:32] = make_dir_entry("MINOS BOOT", "", 0x08, 0, 0)
    # Entry 1: "EFI" directory -> Cluster 2
    root_dir[32:64] = make_dir_entry("EFI", "", 0x10, 2, 0)

    # Cluster 2: \EFI Directory Contents
    efi_dir_cluster = bytearray(CLUSTER_SIZE)
    efi_dir_cluster[0:32] = make_dir_entry(".", "", 0x10, 2, 0)
    efi_dir_cluster[32:64] = make_dir_entry("..", "", 0x10, 0, 0)
    efi_dir_cluster[64:96] = make_dir_entry("BOOT", "", 0x10, 3, 0)

    # Cluster 3: \EFI\BOOT Directory Contents
    boot_dir_cluster = bytearray(CLUSTER_SIZE)
    boot_dir_cluster[0:32] = make_dir_entry(".", "", 0x10, 3, 0)
    boot_dir_cluster[32:64] = make_dir_entry("..", "", 0x10, 2, 0)
    boot_dir_cluster[64:96] = make_dir_entry("BOOTX64", "EFI", 0x20, first_file_cluster, efi_size)

    # Assemble Disk Image
    os.makedirs(os.path.dirname(output_img_path), exist_ok=True)
    with open(output_img_path, 'wb') as img:
        # Reserved sectors (Sector 0 is BPB, followed by padding to RESERVED_SECTORS)
        img.write(boot_sector)
        img.write(b'\x00' * (SECTOR_SIZE * (RESERVED_SECTORS - 1)))

        # FAT 1 and FAT 2
        img.write(fat)
        img.write(fat)

        # Root Directory
        img.write(root_dir)

        # Cluster 2 (\EFI)
        img.write(efi_dir_cluster)

        # Cluster 3 (\EFI\BOOT)
        img.write(boot_dir_cluster)

        # Clusters 4+: File Data
        img.write(efi_data)
        file_padding = (efi_clusters_needed * CLUSTER_SIZE) - efi_size
        if file_padding > 0:
            img.write(b'\x00' * file_padding)

        # Pad to full disk size (32 MB)
        written_so_far = img.tell()
        target_size = TOTAL_SECTORS * SECTOR_SIZE
        if written_so_far < target_size:
            img.write(b'\x00' * (target_size - written_so_far))

    print(f"[MinOS mkdisk] Successfully generated UEFI boot disk image: {output_img_path} ({os.path.getsize(output_img_path)} bytes)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python mkdisk.py <BOOTX64.EFI> <output.img>")
        sys.exit(1)
    create_fat16_image(sys.argv[1], sys.argv[2])
