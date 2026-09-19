#ifndef MINOS_IDT_H
#define MINOS_IDT_H

#include <stdint.h>

struct interrupt_frame {
    uint64_t rax, rcx, rdx, rbx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
};

void idt_init(void);
void irq_dispatch(struct interrupt_frame *frame);

#endif
