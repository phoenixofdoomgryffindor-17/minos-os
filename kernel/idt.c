#include "idt.h"
#include "kernel.h"
#include "serial.h"

struct idt_gate {
    uint16_t offset_low, selector;
    uint8_t ist, type_attr;
    uint16_t offset_mid;
    uint32_t offset_high, zero;
} __attribute__((packed));
struct idt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
static struct idt_gate idt[256] __attribute__((aligned(16)));

void exception_dispatch(uint64_t vector) __attribute__((used));
void exception_dispatch(uint64_t vector) {
    serial_printf("[MinOS IDT] Exception vector %u (%s); halting\n", vector,
        vector == 0 ? "#DE divide error" : vector == 1 ? "#DB debug" :
        vector == 3 ? "#BP breakpoint" : vector == 6 ? "#UD invalid opcode" :
        vector == 8 ? "#DF double fault" : vector == 13 ? "#GP general protection" :
        vector == 14 ? "#PF page fault" : "unknown");
    halt_loop();
}

#define ISR_NOERR(n) \
    __attribute__((naked)) void isr_##n(void) { __asm__ volatile ("cli; pushq $" #n "; movq 0(%%rsp), %%rdi; call exception_dispatch; ud2" ::: "memory"); }
#define ISR_ERR(n) \
    __attribute__((naked)) void isr_##n(void) { __asm__ volatile ("cli; pushq $" #n "; movq 0(%%rsp), %%rdi; call exception_dispatch; ud2" ::: "memory"); }
ISR_NOERR(0) ISR_NOERR(1) ISR_NOERR(3) ISR_NOERR(6)
ISR_ERR(8) ISR_ERR(13) ISR_ERR(14)

static void set_gate(uint8_t n, void (*fn)(void)) {
    uint64_t a = (uint64_t)(uintptr_t)fn;
    idt[n].offset_low = (uint16_t)a; idt[n].selector = 8;
    idt[n].ist = 0; idt[n].type_attr = 0x8E;
    idt[n].offset_mid = (uint16_t)(a >> 16);
    idt[n].offset_high = (uint32_t)(a >> 32); idt[n].zero = 0;
}

void idt_init(void) {
    for (uint32_t i = 0; i < 256; ++i)
        ((uint64_t *)&idt[i])[0] = ((uint64_t *)&idt[i])[1] = 0;
    set_gate(0, isr_0); set_gate(1, isr_1); set_gate(3, isr_3);
    set_gate(6, isr_6); set_gate(8, isr_8); set_gate(13, isr_13); set_gate(14, isr_14);
    struct idt_ptr p = { (uint16_t)(sizeof(idt) - 1), (uint64_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0\ncli" : : "m"(p) : "memory");
}
