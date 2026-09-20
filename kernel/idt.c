#include "idt.h"
#include "kernel.h"
#include "serial.h"
#include "apic.h"

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
    __attribute__((naked)) void isr_##n(void) { __asm__ volatile ("cli; pushq $" #n "; movq 0(%%rsp), %%rcx; andq $-16, %%rsp; subq $32, %%rsp; call exception_dispatch; ud2" ::: "memory"); }
#define ISR_ERR(n) \
    __attribute__((naked)) void isr_##n(void) { __asm__ volatile ("cli; pushq $" #n "; movq 0(%%rsp), %%rcx; andq $-16, %%rsp; subq $32, %%rsp; call exception_dispatch; ud2" ::: "memory"); }
ISR_NOERR(0) ISR_NOERR(1) ISR_NOERR(3) ISR_NOERR(6)
ISR_ERR(8) ISR_ERR(13) ISR_ERR(14)

/*
 * Hardware IRQ entry.  The CPU frame and the vector pushed by each stub are
 * below the complete register save, so the C handler can inspect the
 * interrupted state without changing what iretq will consume.
 */
__attribute__((naked, used)) void irq_common(void) {
    __asm__ volatile (
        "cld\n"
        "pushq %%rax\n"
        "pushq %%rcx\n"
        "pushq %%rdx\n"
        "pushq %%rbx\n"
        "pushq %%rbp\n"
        "pushq %%rsi\n"
        "pushq %%rdi\n"
        "pushq %%r8\n"
        "pushq %%r9\n"
        "pushq %%r10\n"
        "pushq %%r11\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
        "pushq %%r15\n"
        "movq %%rsp, %%r12\n"
        "movq %%rsp, %%rcx\n"
        "andq $-16, %%rsp\n"
        "subq $32, %%rsp\n"
        "call irq_dispatch\n"
        "movq %%r12, %%rsp\n"
        "popq %%r15\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%r11\n"
        "popq %%r10\n"
        "popq %%r9\n"
        "popq %%r8\n"
        "popq %%rdi\n"
        "popq %%rsi\n"
        "popq %%rbp\n"
        "popq %%rbx\n"
        "popq %%rdx\n"
        "popq %%rcx\n"
        "popq %%rax\n"
        "addq $8, %%rsp\n"
        "iretq\n"
        ::: "memory");
}

#define IRQ_STUB(n) \
    __attribute__((naked, used)) void irq_##n(void) { \
        __asm__ volatile ("pushq $" #n "; jmp irq_common" ::: "memory"); \
    }
IRQ_STUB(32) IRQ_STUB(33) IRQ_STUB(34) IRQ_STUB(35)
IRQ_STUB(36) IRQ_STUB(37) IRQ_STUB(38) IRQ_STUB(39)
IRQ_STUB(40) IRQ_STUB(41) IRQ_STUB(42) IRQ_STUB(43)
IRQ_STUB(44) IRQ_STUB(45) IRQ_STUB(46) IRQ_STUB(47)

void irq_dispatch(struct interrupt_frame *frame) {
    if (frame->vector == 32)
        timer_irq();
    else if (frame->vector == 33)
        keyboard_irq();
}

static void set_gate(uint8_t n, void (*fn)(void)) {
    uint64_t a = (uint64_t)(uintptr_t)fn;
    idt[n].offset_low = (uint16_t)a; idt[n].selector = 8;
    idt[n].ist = 0; idt[n].type_attr = 0x8E;
    idt[n].offset_mid = (uint16_t)(a >> 16);
    idt[n].offset_high = (uint32_t)(a >> 32); idt[n].zero = 0;
}

void idt_init(void) {
    /* Keep IF clear while the table is being built and loaded. */
    __asm__ volatile ("cli" ::: "memory");
    for (uint32_t i = 0; i < 256; ++i)
        ((uint64_t *)&idt[i])[0] = ((uint64_t *)&idt[i])[1] = 0;
    set_gate(0, isr_0); set_gate(1, isr_1); set_gate(3, isr_3);
    set_gate(6, isr_6); set_gate(8, isr_8); set_gate(13, isr_13); set_gate(14, isr_14);
    set_gate(32, irq_32); set_gate(33, irq_33); set_gate(34, irq_34); set_gate(35, irq_35);
    set_gate(36, irq_36); set_gate(37, irq_37); set_gate(38, irq_38); set_gate(39, irq_39);
    set_gate(40, irq_40); set_gate(41, irq_41); set_gate(42, irq_42); set_gate(43, irq_43);
    set_gate(44, irq_44); set_gate(45, irq_45); set_gate(46, irq_46); set_gate(47, irq_47);
    struct idt_ptr p = { (uint16_t)(sizeof(idt) - 1), (uint64_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(p) : "memory");
}
