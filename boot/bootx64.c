#include "efi.h"
#include "bootinfo.h"
#include "../kernel/kernel.h"
#include "../kernel/serial.h"

static MinOS_BootInfo g_boot_info;
static EFI_GUID g_gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static void detect_cpu_vendor(char *vendor_out) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);

    /* EBX, EDX, ECX form the 12-char vendor string */
    *(uint32_t *)(vendor_out + 0) = ebx;
    *(uint32_t *)(vendor_out + 4) = edx;
    *(uint32_t *)(vendor_out + 8) = ecx;
    vendor_out[12] = '\0';
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    /* 1. Initialize COM1 early for boot diagnostics */
    serial_init();
    serial_puts("\n[MinOS Boot] MinOS UEFI Bootloader v0.1.0 starting...\n");

    if (SystemTable->ConOut && SystemTable->ConOut->OutputString) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)L"MinOS UEFI Bootloader starting...\r\n");
    }

    /* 2. Detect CPU Vendor */
    detect_cpu_vendor(g_boot_info.cpu_vendor);
    serial_printf("[MinOS Boot] CPU Vendor detected via CPUID: %s\n", g_boot_info.cpu_vendor);

    /* 3. Locate Graphics Output Protocol (GOP) */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = SystemTable->BootServices->LocateProtocol(&g_gop_guid, 0, (void **)&gop);
    if (EFI_ERROR(status) || !gop) {
        serial_puts("[MinOS Boot] ERROR: Failed to locate GOP!\n");
        return status;
    }

    g_boot_info.fb_base   = (void *)gop->Mode->FrameBufferBase;
    g_boot_info.fb_size   = gop->Mode->FrameBufferSize;
    g_boot_info.fb_width  = gop->Mode->Info->HorizontalResolution;
    g_boot_info.fb_height = gop->Mode->Info->VerticalResolution;
    g_boot_info.fb_pitch  = gop->Mode->Info->PixelsPerScanLine * 4;
    g_boot_info.fb_bpp    = 32;
    g_boot_info.fb_is_bgr = (gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) ? 1 : 0;

    serial_printf("[MinOS Boot] GOP Framebuffer: %ux%u, pitch: %u, format: %s, base: %p\n",
                  g_boot_info.fb_width, g_boot_info.fb_height, g_boot_info.fb_pitch,
                  g_boot_info.fb_is_bgr ? "BGRA" : "RGBA", g_boot_info.fb_base);

    /* 4. Obtain UEFI Memory Map */
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *mem_map = 0;

    /* Get required buffer size */
    status = SystemTable->BootServices->GetMemoryMap(&map_size, 0, &map_key, &desc_size, &desc_ver);
    map_size += 4096; /* Allocate extra for the allocation descriptor itself */

    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, map_size, (void **)&mem_map);
    if (EFI_ERROR(status)) {
        serial_puts("[MinOS Boot] ERROR: Failed to allocate memory map pool!\n");
        return status;
    }

    status = SystemTable->BootServices->GetMemoryMap(&map_size, mem_map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        serial_puts("[MinOS Boot] ERROR: Failed to get memory map!\n");
        return status;
    }

    /* Analyze physical memory */
    uint64_t total_bytes = 0;
    uint64_t usable_bytes = 0;
    UINTN num_entries = map_size / desc_size;

    for (UINTN i = 0; i < num_entries; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)mem_map + (i * desc_size));
        uint64_t region_size = desc->NumberOfPages * 4096;
        total_bytes += region_size;
        if (desc->Type == EfiConventionalMemory ||
            desc->Type == EfiBootServicesCode ||
            desc->Type == EfiBootServicesData ||
            desc->Type == EfiLoaderCode ||
            desc->Type == EfiLoaderData) {
            usable_bytes += region_size;
        }
    }

    g_boot_info.total_memory_bytes  = total_bytes;
    g_boot_info.usable_memory_bytes = usable_bytes;
    g_boot_info.memory_map          = mem_map;
    g_boot_info.memory_map_size     = map_size;
    g_boot_info.descriptor_size     = desc_size;
    g_boot_info.descriptor_version  = desc_ver;
    g_boot_info.magic               = MINOS_BOOTINFO_MAGIC;

    serial_printf("[MinOS Boot] UEFI Memory Map: %u entries, %u MB total, %u MB usable\n",
                  (uint32_t)num_entries,
                  (uint32_t)(total_bytes / (1024 * 1024)),
                  (uint32_t)(usable_bytes / (1024 * 1024)));

    /* 5. Clean Transition: ExitBootServices */
    serial_puts("[MinOS Boot] Cleanly exiting UEFI Boot Services...\n");
    status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
    if (EFI_ERROR(status)) {
        /* If memory map changed, retry once with updated key */
        serial_puts("[MinOS Boot] Retrying ExitBootServices with refreshed map key...\n");
        map_size += 4096;
        SystemTable->BootServices->GetMemoryMap(&map_size, mem_map, &map_key, &desc_size, &desc_ver);
        status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
        if (EFI_ERROR(status)) {
            serial_puts("[MinOS Boot] FATAL: ExitBootServices failed!\n");
            halt_loop();
        }
    }

    serial_puts("[MinOS Boot] Boot Services exited. Jumping to MinOS kernel_main!\n");

    /* 6. Enter MinOS Kernel */
    kernel_main(&g_boot_info);

    /* Should not return */
    halt_loop();
    return EFI_SUCCESS;
}
