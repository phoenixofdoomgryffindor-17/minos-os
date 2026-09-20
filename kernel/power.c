#include "power.h"
#include "kernel.h"
#include "serial.h"

#define KBD_STATUS 0x64
#define KBD_COMMAND 0x64
#define KBD_CMD_PULSE_RESET 0xFE
#define ACPI_SHUTDOWN_PORT 0x604
#define BOCHS_SHUTDOWN_PORT 0xB004
#define QEMU_SHUTDOWN_VALUE 0x2000

void power_reboot(void) {
    uint32_t timeout = 100000U;

    serial_puts("[MinOS Power] Reboot requested.\n");
    __asm__ volatile ("cli" ::: "memory");
    while ((inb(KBD_STATUS) & 0x02U) && --timeout)
        __asm__ volatile ("pause");
    if (timeout)
        outb(KBD_COMMAND, KBD_CMD_PULSE_RESET);

    /* The keyboard-controller reset is the real hardware path. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void power_shutdown(void) {
    serial_puts("[MinOS Power] Shutdown requested.\n");
    __asm__ volatile ("cli" ::: "memory");
    outw(ACPI_SHUTDOWN_PORT, QEMU_SHUTDOWN_VALUE);
    outw(BOCHS_SHUTDOWN_PORT, QEMU_SHUTDOWN_VALUE);

    /* Firmware-independent fallback if the virtual power device is absent. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
