#ifndef MINOS_SYSCALL_H
#define MINOS_SYSCALL_H

#include <stdint.h>

#define SYSCALL_WRITE    0
#define SYSCALL_READ     1
#define SYSCALL_OPEN     2
#define SYSCALL_CLOSE    3
#define SYSCALL_EXIT     60
#define SYSCALL_YIELD    24

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
void syscall_init(void);

#endif