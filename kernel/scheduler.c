#include "scheduler.h"
#include "heap.h"
#include "serial.h"
#include "apic.h"

static tcb_t *current = NULL;
static tcb_t *ready_head = NULL;
static tcb_t *ready_tail = NULL;
static tcb_t *idle_task = NULL;
static uint32_t next_pid = 1;
static uint32_t next_tid = 1;

static void idle(void) { for (;;) __asm__ volatile ("hlt"); }

void scheduler_init(void) {
    current = (tcb_t *)kmalloc(sizeof(tcb_t));
    if (!current) return;
    current->rsp = 0;
    current->rip = 0;
    current->rbp = 0;
    current->rbx = 0;
    current->r12 = current->r13 = current->r14 = current->r15 = 0;
    current->cr3 = 0;
    current->stack_base = NULL;
    current->state = TASK_STATE_RUNNING;
    current->pid = next_pid++;
    current->tid = next_tid++;
    current->next = NULL;
    ready_head = ready_tail = NULL;

    idle_task = thread_create(idle, NULL);
    if (idle_task) idle_task->state = TASK_STATE_IDLE;
    serial_puts("[MinOS Scheduler] Initialized\n");
}

tcb_t *thread_create(void (*entry)(void), void *arg) {
    tcb_t *tcb = (tcb_t *)kmalloc(sizeof(tcb_t));
    if (!tcb) return NULL;
    uint64_t *stack = (uint64_t *)kmalloc(TCB_STACK_SIZE);
    if (!stack) { kfree(tcb); return NULL; }
    uint64_t stack_top = (uint64_t)(stack + TCB_STACK_SIZE / 8);
    tcb->rsp = stack_top;
    tcb->rip = (uint64_t)(uintptr_t)entry;
    tcb->rbp = 0;
    tcb->rbx = 0;
    tcb->r12 = tcb->r13 = tcb->r14 = tcb->r15 = 0;
    tcb->cr3 = 0;
    tcb->stack_base = stack;
    tcb->state = TASK_STATE_READY;
    tcb->pid = next_pid++;
    tcb->tid = next_tid++;
    tcb->next = NULL;

    if (!ready_head) ready_head = ready_tail = tcb;
    else { ready_tail->next = tcb; ready_tail = tcb; }
    return tcb;
}

void schedule(void) {
    if (!current || !ready_head) return;
    if (current->state == TASK_STATE_RUNNING) {
        current->state = TASK_STATE_READY;
        if (!ready_head) ready_head = ready_tail = current;
        else { ready_tail->next = current; ready_tail = current; }
    }
    tcb_t *next = ready_head;
    ready_head = next->next;
    if (!ready_head) ready_tail = NULL;
    next->next = NULL;
    next->state = TASK_STATE_RUNNING;
    current = next;
    context_switch(current);
}

void thread_yield(void) {
    __asm__ volatile ("cli");
    schedule();
    __asm__ volatile ("sti");
}

void thread_exit(void) {
    if (!current) return;
    if (current->stack_base) kfree(current->stack_base);
    kfree(current);
    current = NULL;
    schedule();
}

tcb_t *current_task(void) { return current; }