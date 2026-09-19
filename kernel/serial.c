#include "serial.h"
#include "kernel.h"
#include <stdarg.h>

static int serial_initialized = 0;

int serial_init(void) {
    outb(COM1_PORT + 1, 0x00);    /* Disable interrupts */
    outb(COM1_PORT + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(COM1_PORT + 0, 0x01);    /* Set divisor to 1 (115200 baud) low byte */
    outb(COM1_PORT + 1, 0x00);    /* High byte */
    outb(COM1_PORT + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(COM1_PORT + 2, 0xC7);    /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
    outb(COM1_PORT + 4, 0x1E);    /* Set in loopback mode, test serial chip */
    outb(COM1_PORT + 0, 0xAE);    /* Test byte */

    /* Check if serial is faulty */
    if (inb(COM1_PORT + 0) != 0xAE) {
        /* Even if loopback fails in some VM environments, force standard mode */
        outb(COM1_PORT + 4, 0x0F);
        serial_initialized = 1;
        return 0;
    }

    /* Set normal operation mode */
    outb(COM1_PORT + 4, 0x0F);
    serial_initialized = 1;
    return 0;
}

static int is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_putc(char c) {
    if (!serial_initialized) {
        serial_init();
    }
    if (c == '\n') {
        while (!is_transmit_empty());
        outb(COM1_PORT, '\r');
    }
    while (!is_transmit_empty());
    outb(COM1_PORT, (uint8_t)c);
}

void serial_puts(const char *str) {
    if (!str) return;
    while (*str) {
        serial_putc(*str++);
    }
}

void serial_put_dec(uint64_t val) {
    char buf[32];
    int i = 0;
    if (val == 0) {
        serial_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        serial_putc(buf[j]);
    }
}

void serial_put_hex(uint64_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    serial_puts("0x");
    if (val == 0) {
        serial_putc('0');
        return;
    }
    char buf[16];
    int i = 0;
    while (val > 0) {
        buf[i++] = hex_chars[val & 0xF];
        val >>= 4;
    }
    for (int j = i - 1; j >= 0; j--) {
        serial_putc(buf[j]);
    }
}

void serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            serial_putc(*p);
            continue;
        }
        p++;
        switch (*p) {
            case 's': {
                const char *s = va_arg(args, const char *);
                serial_puts(s ? s : "(null)");
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                serial_putc(c);
                break;
            }
            case 'd':
            case 'u': {
                uint64_t u = va_arg(args, uint64_t);
                serial_put_dec(u);
                break;
            }
            case 'x':
            case 'p': {
                uint64_t x = va_arg(args, uint64_t);
                serial_put_hex(x);
                break;
            }
            case '%':
                serial_putc('%');
                break;
            default:
                serial_putc('%');
                serial_putc(*p);
                break;
        }
    }
    va_end(args);
}
