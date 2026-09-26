#ifndef MINOS_SCHEDULER_H
#define MINOS_SCHEDULER_H

#include <stdint.h>

#define TCB_STACK_SIZE 8192

typedef enum {
    TASK_STATE_IDLE = 0,
    TASK_STATE_RUNNING,
    TASK_STATE_READY,
    TASK_STATE_BLOCKED
} task_state_t;

typedef struct task_control_block {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12, r13, r14, r15;
    uint64_t cr3;
    uint64_t *stack_base;
    task_state_t state;
    uint32_t pid;
    uint32_t tid;
    struct task_control_block *next;
} tcb_t;

void scheduler_init(void);
tcb_t *thread_create(void (*entry)(void), void *arg);
void schedule(void);
void thread_yield(void);
void thread_exit(void);
tcb_t *current_task(void);

#endif