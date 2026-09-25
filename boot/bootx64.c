#include "efi.h"
#include "bootinfo.h"
#include "../kernel/kernel.h"
#include "../kernel/serial.h"

static MinOS_BootInfo g_boot_info;
static EFI_GUID g_gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
extern uint64_t kernel_stack_base(void);
extern uint64_t kernel_stack_size(void);

static void detect_cpu_vendor(char *vendor_out) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);

    /* EBX, EDX, ECX form the 12-char vendor string */
    *(uint32_t *)(vendor_out + 0) = ebx;
    *(uint32_t *)(vendor_out + 4) = edx;
    *(uint32_t *)(vendor_out + 8) = ecx;
    vendor_out[12] = '\0';
}

static uint64_t add_saturating(uint64_t left, uint64_t right) {
    return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

static void update_boot_info(EFI_MEMORY_DESCRIPTOR *mem_map, UINTN map_size,
                             UINTN desc_size, UINT32 desc_ver) {
    uint64_t total_bytes = 0;
    uint64_t usable_bytes = 0;
    UINTN num_entries;

    g_boot_info.memory_map = mem_map;
    g_boot_info.memory_map_size = map_size;
    g_boot_info.descriptor_size = desc_size;
    g_boot_info.descriptor_version = desc_ver;
    if (!mem_map || !desc_size) return;

    num_entries = map_size / desc_size;
    for (UINTN i = 0; i < num_entries; i++) {
        EFI_MEMORY_DESCRIPTOR *desc =
            (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)mem_map + i * desc_size);
        uint64_t region_size = desc->NumberOfPages > UINT64_MAX / 4096
                                 ? UINT64_MAX
                                 : desc->NumberOfPages * 4096;
        total_bytes = add_saturating(total_bytes, region_size);
        if (desc->Type == EfiConventionalMemory ||
            desc->Type == EfiBootServicesCode ||
            desc->Type == EfiBootServicesData ||
            desc->Type == EfiLoaderCode ||
            desc->Type == EfiLoaderData) {
            usable_bytes = add_saturating(usable_bytes, region_size);
        }
    }

    g_boot_info.total_memory_bytes = total_bytes;
    g_boot_info.usable_memory_bytes = usable_bytes;
}

static EFI_STATUS get_memory_map(EFI_BOOT_SERVICES *boot_services,
                                 EFI_MEMORY_DESCRIPTOR **map,
                                 UINTN *capacity,
                                 UINTN *map_size,
                                 UINTN *map_key,
                                 UINTN *desc_size,
                                 UINT32 *desc_ver) {
    for (;;) {
        UINTN required = *capacity;
        EFI_STATUS status = boot_services->GetMemoryMap(&required, *map,
                                                        map_key, desc_size, desc_ver);
        if (status == EFI_BUFFER_TOO_SMALL) {
            UINTN base = required > *capacity ? required : *capacity;
            UINTN increment = base / 2;
            EFI_MEMORY_DESCRIPTOR *new_map = 0;
            if (!increment) increment = 1;
            if (base > UINT64_MAX - increment - 1) {
                if (*map) boot_services->FreePool(*map);
                *map = 0;
                *capacity = 0;
                return EFI_OUT_OF_RESOURCES;
            }
            status = boot_services->AllocatePool(EfiLoaderData,
                                                  base + increment + 1,
                                                  (void **)&new_map);
            if (EFI_ERROR(status)) {
                if (*map) boot_services->FreePool(*map);
                *map = 0;
                *capacity = 0;
                return status;
            }
            if (*map) boot_services->FreePool(*map);
            *map = new_map;
            *capacity = base + increment + 1;
            continue;
        }
        if (EFI_ERROR(status)) {
            if (*map) boot_services->FreePool(*map);
            *map = 0;
            *capacity = 0;
            return status;
        }
        if (!*map || !*desc_size) {
            if (*map) boot_services->FreePool(*map);
            *map = 0;
            *capacity = 0;
            return EFI_INVALID_PARAMETER;
        }
        *map_size = required;
        return EFI_SUCCESS;
    }
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

    /* 4. Obtain UEFI Memory Map. The map must be reacquired after every
     * failed ExitBootServices() attempt because its key changes with the map. */
    UINTN map_capacity = 0;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *mem_map = 0;

    status = get_memory_map(SystemTable->BootServices, &mem_map, &map_capacity,
                            &map_size, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        serial_puts("[MinOS Boot] ERROR: Failed to allocate or retrieve memory map!\n");
        return status;
    }

    update_boot_info(mem_map, map_size, desc_size, desc_ver);
    g_boot_info.magic = MINOS_BOOTINFO_MAGIC;
    g_boot_info.kernel_image_base = ((uint64_t)(uintptr_t)&efi_main) & ~4095ULL;
    /* Reserve enough of the PE image for the kernel BSS allocators and tables. */
    g_boot_info.kernel_image_size = 16ULL * 1024ULL * 1024ULL;
    g_boot_info.bootstrap_stack_base = kernel_stack_base();
    g_boot_info.bootstrap_stack_size = kernel_stack_size();

    UINTN num_entries = map_size / desc_size;
    serial_printf("[MinOS Boot] UEFI Memory Map: %u entries, %u MB total, %u MB usable\n",
                  (uint32_t)num_entries,
                  (uint32_t)(g_boot_info.total_memory_bytes / (1024 * 1024)),
                  (uint32_t)(g_boot_info.usable_memory_bytes / (1024 * 1024)));

    /* 5. Clean Transition: ExitBootServices. A failed attempt invalidates the
     * map key, so reacquire the map before every retry. */
    for (;;) {
        serial_puts("[MinOS Boot] Exiting UEFI Boot Services...\n");
        EFI_STATUS exit_status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
        if (!EFI_ERROR(exit_status)) break;

        serial_puts("[MinOS Boot] ExitBootServices failed; reacquiring memory map...\n");
        status = get_memory_map(SystemTable->BootServices, &mem_map, &map_capacity,
                                &map_size, &map_key, &desc_size, &desc_ver);
        if (EFI_ERROR(status)) {
            serial_puts("[MinOS Boot] FATAL: could not reacquire memory map!\n");
            halt_loop();
        }
        update_boot_info(mem_map, map_size, desc_size, desc_ver);
        num_entries = map_size / desc_size;
        serial_printf("[MinOS Boot] Refreshed map: %u entries, key=%p\n",
                      (uint32_t)num_entries, (uint64_t)map_key);

        /* Other failures are not map-key races and should not spin forever. */
        if (exit_status != EFI_INVALID_PARAMETER) {
            serial_puts("[MinOS Boot] FATAL: ExitBootServices failed!\n");
            halt_loop();
        }
    }

    serial_puts("[MinOS Boot] Boot Services exited. Jumping to MinOS kernel_main!\n");

    /* 6. Enter MinOS Kernel */
    kernel_enter(&g_boot_info);

    /* Should not return */
    halt_loop();
    return EFI_SUCCESS;
}
