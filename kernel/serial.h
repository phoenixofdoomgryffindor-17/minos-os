#ifndef MINOS_KERNEL_SERIAL_H
#define MINOS_KERNEL_SERIAL_H

#include <stdint.h>

#define COM1_PORT 0x3F8

int  serial_init(void);
void serial_putc(char c);
void serial_puts(const char *str);
void serial_put_dec(uint64_t val);
void serial_put_hex(uint64_t val);
void serial_printf(const char *fmt, ...);

#endif /* MINOS_KERNEL_SERIAL_H */
