#include "apic.h"
#include "kernel.h"
#include "serial.h"

#define APIC_BASE_MSR       0x1B
#define APIC_BASE_ENABLE    (1ULL << 11)
#define APIC_BASE_X2APIC    (1ULL << 10)
#define APIC_BASE_ADDRESS_MASK 0xFFFFFFFFFFFFF000ULL

#define APIC_ID              0x020
#define APIC_EOI             0x0B0
#define APIC_SVR             0x0F0
#define APIC_LVT_TIMER       0x320
#define APIC_TIMER_INITIAL   0x380
#define APIC_TIMER_CURRENT   0x390
#define APIC_TIMER_DIVIDE    0x3E0
#define APIC_SVR_ENABLE      0x100
#define APIC_TIMER_MASK      0x10000
#define APIC_TIMER_PERIODIC  0x20000

#define PIT_COMMAND          0x43
#define PIT_CHANNEL2         0x42
#define PIT_CONTROL          0x61

static volatile uint32_t *lapic;
static volatile uint64_t ticks;
static uint32_t tick_hz;

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value, hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static uint32_t lapic_read(uint32_t reg) {
    return lapic[reg / 4];
}

static void lapic_write(uint32_t reg, uint32_t value) {
    lapic[reg / 4] = value;
    (void)lapic[APIC_ID / 4]; /* serialize posted MMIO writes */
}

static void pic_mask_all(void) {
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20); /* keep legacy vectors out of the exception range */
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04); /* master is connected to the slave on IRQ2 */
    io_wait();
    outb(0xA1, 0x02); /* slave identity */
    io_wait();
    outb(0x21, 0x01); /* 8086 mode */
    io_wait();
    outb(0xA1, 0x01); /* 8086 mode */
    io_wait();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void pic_unmask_irq(uint8_t irq) {
    if (irq < 8)
        outb(0x21, (uint8_t)(inb(0x21) & (uint8_t)~(1U << irq)));
    else if (irq < 16)
        outb(0xA1, (uint8_t)(inb(0xA1) & (uint8_t)~(1U << (irq - 8))));
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/*
 * Measure one PIT channel-2 gate interval while the APIC counts down.
 * QEMU's PIT is stable enough for this short calibration and this avoids
 * assuming a particular virtual APIC bus frequency.
 */
static uint32_t calibrate_apic(void) {
    const uint16_t pit_count = 11932; /* approximately 10 ms at 1.193182 MHz */
    uint8_t control = inb(PIT_CONTROL);

    lapic_write(APIC_TIMER_INITIAL, 0xFFFFFFFFU);
    outb(PIT_COMMAND, 0xB0); /* channel 2, lobyte/hibyte, one-shot */
    outb(PIT_CHANNEL2, (uint8_t)pit_count);
    outb(PIT_CHANNEL2, (uint8_t)(pit_count >> 8));
    outb(PIT_CONTROL, (uint8_t)(control | 1U));

    uint32_t guard = 20000000U;
    while (!(inb(PIT_CONTROL) & 0x20U) && --guard != 0)
        __asm__ volatile ("pause");

    uint32_t elapsed = 0xFFFFFFFFU - lapic_read(APIC_TIMER_CURRENT);
    outb(PIT_CONTROL, control);
    if (guard == 0 || elapsed < 1000U)
        return 0;
    return elapsed;
}

void apic_eoi(void) {
    if (lapic)
        lapic_write(APIC_EOI, 0);
}

uint64_t timer_ticks(void) {
    return ticks;
}

uint32_t timer_frequency(void) {
    return tick_hz;
}

void timer_irq(void) {
    ++ticks;
    apic_eoi();
    if (ticks == 1)
        serial_puts("[MinOS Timer] First periodic IRQ received (APIC vector 32).\n");
    if ((ticks % tick_hz) == 0)
        serial_printf("[MinOS Timer] ticks=%u frequency=%u Hz\n",
                      ticks, (uint64_t)tick_hz);
}

int apic_timer_init(uint32_t frequency) {
    uint32_t max_leaf, eax, ebx, ecx, edx;
    uint64_t base;

    cpuid(0, &max_leaf, &ebx, &ecx, &edx);
    if (max_leaf < 1)
        return -1;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    if (!(edx & (1U << 9)) || (ecx & (1U << 21)))
        return -1; /* no APIC, or x2APIC already selected */

    base = rdmsr(APIC_BASE_MSR);
    if (!(base & APIC_BASE_ENABLE))
        wrmsr(APIC_BASE_MSR, base | APIC_BASE_ENABLE);
    base = rdmsr(APIC_BASE_MSR);
    if (base & APIC_BASE_X2APIC)
        return -1;

    lapic = (volatile uint32_t *)(uintptr_t)(base & APIC_BASE_ADDRESS_MASK);
    pic_mask_all();
    lapic_write(APIC_SVR, (lapic_read(APIC_SVR) & 0xFFU) | APIC_SVR_ENABLE);
    lapic_write(APIC_TIMER_DIVIDE, 0x3); /* divide by 16 */
    lapic_write(APIC_LVT_TIMER, 32U | APIC_TIMER_MASK);

    uint32_t calibrated = calibrate_apic();
    if (!calibrated)
        return -1;
    if (!frequency)
        frequency = 100;
    uint32_t initial = (uint32_t)(((uint64_t)calibrated * 100U) / frequency);
    if (!initial)
        initial = 1;
    tick_hz = frequency;
    ticks = 0;
    lapic_write(APIC_TIMER_INITIAL, initial);
    lapic_write(APIC_LVT_TIMER, 32U | APIC_TIMER_PERIODIC);
    serial_printf("[MinOS APIC] xAPIC base=%p calibrated=%u ticks/sec, timer=%u Hz\n",
                  (uint64_t)(uintptr_t)lapic,
                  (uint64_t)calibrated * 100U, (uint64_t)tick_hz);
    return 0;
}
