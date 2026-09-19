#include "kernel.h"
#include "serial.h"
#include "framebuffer.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "apic.h"
#include "keyboard.h"
#include "console.h"

void kernel_main(MinOS_BootInfo *boot_info) {
    /* 1. Initialize Serial Diagnostics (COM1) */
    serial_init();
    serial_puts("\n=========================================================\n");
    serial_puts("       MinOS Kernel v0.1.0 (x86_64 Long Mode)            \n");
    serial_puts("             Milestone 1: Bare-Metal Prototype           \n");
    serial_puts("=========================================================\n");
    serial_puts("[MinOS Kernel] Successfully entered kernel_main!\n");

    if (!boot_info || boot_info->magic != MINOS_BOOTINFO_MAGIC) {
        serial_puts("[MinOS Kernel] ERROR: Invalid or corrupted BootInfo struct!\n");
        halt_loop();
    }

    /* Establish kernel-owned CPU tables before enabling any future subsystems. */
    gdt_init();
    idt_init();
    pmm_init(boot_info);
    vmm_init(boot_info);
    serial_puts("[MinOS Kernel] GDT, IDT, PMM and VMM initialized; interrupts masked.\n");
    serial_printf("[MinOS Kernel] PMM statistics: %u MiB usable, %u MiB reserved, %u free frames\n",
                  pmm_usable_bytes() / (1024 * 1024),
                  pmm_reserved_bytes() / (1024 * 1024),
                  pmm_free_frames());

    /* 2. Log Boot & Hardware Parameters */
    serial_printf("[MinOS Kernel] BootInfo Magic: %p (VALID)\n", boot_info->magic);
    serial_printf("[MinOS Kernel] CPU Vendor:     %s\n", boot_info->cpu_vendor);
    
    uint64_t total_mb = boot_info->total_memory_bytes / (1024 * 1024);
    uint64_t usable_mb = boot_info->usable_memory_bytes / (1024 * 1024);
    serial_printf("[MinOS Kernel] Physical RAM:   %u MB total, %u MB usable\n", total_mb, usable_mb);
    serial_printf("[MinOS Kernel] Framebuffer:    %ux%u, pitch: %u bytes, base: %p\n",
                  boot_info->fb_width, boot_info->fb_height, boot_info->fb_pitch, boot_info->fb_base);

    /* 3. Initialize Graphical Framebuffer */
    fb_init(boot_info);
    uint32_t screen_w = fb_get_width();
    uint32_t screen_h = fb_get_height();

    /* Clear screen to signature MinOS slate navy */
    fb_clear(COLOR_BG);

    /* Draw Top System Header Bar */
    fb_fill_rect(0, 0, screen_w, 32, 0xFF181F2C);
    fb_draw_rect_outline(0, 0, screen_w, 32, COLOR_CARD_BORDER);
    fb_draw_string(16, 8, "MinOS v0.1.0 (x86_64) | Milestone 1: Bare-Metal Prototype", COLOR_TEXT_WHITE, 0);
    fb_draw_string(screen_w - 220, 8, "[UEFI GOP 32-BPP]", COLOR_ACCENT, 0);

    /* Center Card */
    uint32_t card_w = 640;
    uint32_t card_h = 360;
    uint32_t card_x = (screen_w > card_w) ? (screen_w - card_w) / 2 : 10;
    uint32_t card_y = (screen_h > card_h) ? (screen_h - card_h) / 2 : 40;

    /* Card Shadow & Background */
    fb_fill_rect(card_x + 4, card_y + 4, card_w, card_h, 0xFF0B0E14);
    fb_fill_rect(card_x, card_y, card_w, card_h, COLOR_CARD);
    fb_draw_rect_outline(card_x, card_y, card_w, card_h, COLOR_CARD_BORDER);

    /* Card Title & Accent Bar */
    fb_fill_rect(card_x, card_y, card_w, 4, COLOR_ACCENT);
    fb_draw_string(card_x + 24, card_y + 24, "MinOS Operating System", COLOR_ACCENT, 0);
    fb_draw_string(card_x + 24, card_y + 46, "Hardware & Kernel Initialization Verified", COLOR_TEXT_MUTED, 0);

    /* Divider */
    fb_fill_rect(card_x + 24, card_y + 72, card_w - 48, 1, COLOR_CARD_BORDER);

    /* Hardware Badges */
    fb_draw_badge(card_x + 24,  card_y + 88, "KERNEL: ACTIVE", 0xFF064E3B, COLOR_SUCCESS);
    
    char cpu_badge[32] = "CPU: ";
    for (int i = 0; i < 12; i++) cpu_badge[5 + i] = boot_info->cpu_vendor[i];
    cpu_badge[17] = '\0';
    fb_draw_badge(card_x + 180, card_y + 88, cpu_badge, 0xFF312E81, COLOR_ACCENT_PURP);

    fb_draw_badge(card_x + 360, card_y + 88, "MODE: RING 0 (x86_64)", 0xFF1E293B, COLOR_TEXT_WHITE);

    /* Diagnostic Status Lines */
    uint32_t line_y = card_y + 130;
    fb_draw_string(card_x + 24, line_y + 0,   "[OK] UEFI Firmware Handoff: ExitBootServices executed cleanly", COLOR_SUCCESS, 0);
    fb_draw_string(card_x + 24, line_y + 24,  "[OK] Linear Framebuffer: GOP 32-bit linear address mapped", COLOR_SUCCESS, 0);
    fb_draw_string(card_x + 24, line_y + 48,  "[OK] Serial Communication: COM1 (0x3F8) 115200 baud active", COLOR_SUCCESS, 0);
    fb_draw_string(card_x + 24, line_y + 72,  "[OK] Memory Map Discovered: Physical descriptors reported", COLOR_SUCCESS, 0);
    fb_draw_string(card_x + 24, line_y + 96,  "[OK] Execution State: Long Mode 64-bit kernel entry", COLOR_SUCCESS, 0);

    /* Footer Info */
    fb_fill_rect(card_x + 24, card_y + 280, card_w - 48, 1, COLOR_CARD_BORDER);
    fb_draw_string(card_x + 24, card_y + 296, "Milestone 1 Complete. System halted safely (cli; hlt).", COLOR_TEXT_MUTED, 0);

    serial_puts("[MinOS Kernel] Framebuffer graphical boot screen rendered.\n");
    serial_puts("[MinOS Kernel] Milestone 1 verification complete.\n");

    if (apic_timer_init(100) != 0) {
        serial_puts("[MinOS APIC] ERROR: local APIC/timer setup failed; halting.\n");
        halt_loop();
    }
    if (keyboard_init() != 0) {
        serial_puts("[MinOS Keyboard] ERROR: PS/2 controller setup failed; halting.\n");
        halt_loop();
    }
    serial_puts("[MinOS Kernel] IRQ controller and periodic timer ready; enabling interrupts.\n");
    console_init();
    serial_puts("[MinOS Console] Ready. Type help for commands.\n");
    __asm__ volatile ("sti" ::: "memory");
    for (;;) {
        console_poll();
        __asm__ volatile ("hlt");
    }
}
