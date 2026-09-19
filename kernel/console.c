#include "console.h"
#include "apic.h"
#include "framebuffer.h"
#include "input.h"
#include "pmm.h"
#include "serial.h"

#define CONSOLE_CHAR_WIDTH 9U
#define CONSOLE_LINE_HEIGHT 18U
#define CONSOLE_MARGIN 16U
#define CONSOLE_BG 0xFF101722U
#define CONSOLE_FG COLOR_TEXT_WHITE
#define CONSOLE_PROMPT COLOR_ACCENT
#define CONSOLE_CURSOR 0xFF38BDF8U
#define COMMAND_MAX 127U

static uint32_t columns;
static uint32_t rows;
static uint32_t cursor_column;
static uint32_t cursor_row;
static char command[COMMAND_MAX + 1U];
static uint32_t command_length;
static uint64_t cursor_tick;
static int cursor_visible;

static int console_streq(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static void console_cursor(void) {
    uint32_t x = CONSOLE_MARGIN + cursor_column * CONSOLE_CHAR_WIDTH;
    uint32_t y = CONSOLE_MARGIN + cursor_row * CONSOLE_LINE_HEIGHT;
    if (cursor_visible)
        fb_fill_rect(x, y, 8, 16, CONSOLE_CURSOR);
    else
        fb_fill_rect(x, y, 8, 16, CONSOLE_BG);
}

static void console_newline(void) {
    cursor_column = 0;
    if (++cursor_row >= rows) {
        fb_scroll_up(CONSOLE_MARGIN, rows * CONSOLE_LINE_HEIGHT, 1, CONSOLE_BG);
        cursor_row = rows - 1U;
    }
}

static void console_putc(char c) {
    if (c == '\n') {
        console_newline();
        return;
    }
    if (c == '\r') return;
    if (cursor_column >= columns) console_newline();
    fb_draw_char(CONSOLE_MARGIN + cursor_column * CONSOLE_CHAR_WIDTH,
                 CONSOLE_MARGIN + cursor_row * CONSOLE_LINE_HEIGHT,
                 c, CONSOLE_FG, CONSOLE_BG);
    ++cursor_column;
}

static void console_puts(const char *text) {
    while (text && *text) console_putc(*text++);
}

static void console_put_u64(uint64_t value) {
    char digits[21];
    uint32_t length = 0;
    if (!value) {
        console_putc('0');
        return;
    }
    while (value) {
        digits[length++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (length) console_putc(digits[--length]);
}

static void console_prompt(void) {
    console_puts("MinOS> ");
    cursor_column = 7;
    command_length = 0;
    command[0] = '\0';
}

static void console_redraw_command(void) {
    uint32_t x = CONSOLE_MARGIN + 7U * CONSOLE_CHAR_WIDTH;
    uint32_t width = fb_get_width() > x ? fb_get_width() - x : 0;
    fb_fill_rect(x, CONSOLE_MARGIN + cursor_row * CONSOLE_LINE_HEIGHT,
                 width, 16, CONSOLE_BG);
    fb_draw_string(x, CONSOLE_MARGIN + cursor_row * CONSOLE_LINE_HEIGHT,
                   command, CONSOLE_FG, CONSOLE_BG);
    cursor_column = 7U + command_length;
}

static void console_command(void) {
    command[command_length] = '\0';
    console_putc('\n');
    serial_puts("[MinOS Console] command: ");
    serial_puts(command);
    serial_putc('\n');
    if (command_length == 0) {
        /* no output */
    } else if (console_streq(command, "help")) {
        console_puts("help  clear  about  mem  ticks\n");
    } else if (console_streq(command, "clear")) {
        fb_clear(CONSOLE_BG);
        cursor_column = 0;
        cursor_row = 0;
    } else if (console_streq(command, "about")) {
        console_puts("MinOS interactive kernel console\n");
        console_puts("x86_64 UEFI | framebuffer | IRQ input\n");
    } else if (console_streq(command, "mem")) {
        console_puts("usable: ");
        console_put_u64(pmm_usable_bytes() / (1024U * 1024U));
        console_puts(" MiB, reserved: ");
        console_put_u64(pmm_reserved_bytes() / (1024U * 1024U));
        console_puts(" MiB, free frames: ");
        console_put_u64(pmm_free_frames());
        console_putc('\n');
    } else if (console_streq(command, "ticks")) {
        console_puts("ticks: ");
        console_put_u64(timer_ticks());
        console_puts(" (");
        console_put_u64(timer_frequency());
        console_puts(" Hz)\n");
    } else {
        console_puts("Unknown command. Type help.\n");
    }
    console_prompt();
}

void console_init(void) {
    fb_clear(CONSOLE_BG);
    columns = fb_get_width() > CONSOLE_MARGIN ? (fb_get_width() - CONSOLE_MARGIN) / CONSOLE_CHAR_WIDTH : 1;
    rows = fb_get_height() > CONSOLE_MARGIN ? (fb_get_height() - CONSOLE_MARGIN) / CONSOLE_LINE_HEIGHT : 1;
    if (!columns) columns = 1;
    if (!rows) rows = 1;
    cursor_row = 0;
    cursor_column = 0;
    cursor_visible = 1;
    cursor_tick = timer_ticks();
    console_prompt();
    console_cursor();
}

void console_poll(void) {
    struct input_event event;
    uint64_t now = timer_ticks();
    while (input_pop(&event) == 0) {
        if (!event.pressed) continue;
        if (event.key == INPUT_KEY_ENTER) {
            cursor_visible = 0;
            console_cursor();
            console_command();
        } else if (event.key == INPUT_KEY_BACKSPACE) {
            if (command_length) {
                --command_length;
                command[command_length] = '\0';
                console_redraw_command();
            }
        } else if (event.key == INPUT_KEY_TAB) {
            uint32_t spaces = 4U - ((cursor_column - 7U) % 4U);
            while (spaces-- && command_length < COMMAND_MAX) {
                command[command_length++] = ' ';
                command[command_length] = '\0';
            }
            console_redraw_command();
        } else if (event.character >= 32 && event.character <= 126 &&
                   command_length < COMMAND_MAX) {
            command[command_length++] = event.character;
            command[command_length] = '\0';
            console_redraw_command();
        }
        cursor_visible = 1;
        cursor_tick = now;
    }
    if (now - cursor_tick >= (uint64_t)(timer_frequency() / 2U ? timer_frequency() / 2U : 1U)) {
        cursor_visible = !cursor_visible;
        cursor_tick = now;
        console_cursor();
    }
}
