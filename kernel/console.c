#include "console.h"
#include "apic.h"
#include "framebuffer.h"
#include "input.h"
#include "pmm.h"
#include "serial.h"
#include "vfs.h"
#include "ata.h"
#include "power.h"

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
static uint32_t command_cursor;
static uint64_t cursor_tick;
static int cursor_visible;
static char cwd[VFS_PATH_MAX + 1] = "/";
#define HISTORY_MAX 16U
static char history[HISTORY_MAX][COMMAND_MAX + 1];
static uint32_t history_count, history_index;

static uint32_t console_prompt_columns(void) {
    uint32_t length = 0;
    while (cwd[length]) ++length;
    return 8U + length;
}

static int console_streq(const char *left, const char *right) {
    while (*left && *right) {
        char a = *left;
        char b = *right;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
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

static void console_error(void) { console_puts("error\n"); }
static int console_path(const char *arg, char *out) {
    return vfs_normalize(cwd, arg ? arg : ".", out);
}
static int console_arg(char **argv, uint32_t argc, uint32_t n, char *out) {
    if (n >= argc || console_path(argv[n], out) != 0) { console_error(); return -1; }
    return 0;
}
static uint32_t console_words(char *line, char **argv, uint32_t max) {
    uint32_t n = 0;
    while (*line && n < max) {
        while (*line == ' ') ++line;
        if (!*line) break;
        argv[n++] = line;
        while (*line && *line != ' ') ++line;
        if (*line) *line++ = 0;
    }
    return n;
}
static void console_list(const char *path) {
    struct vfs_dirent entries[VFS_FILE_MAX]; uint32_t count, i;
    struct vfs_stat stat;
    if (vfs_stat_path(path, &stat) != 0) { console_error(); return; }
    if (stat.type == VFS_FILE) { console_puts(path); console_putc('\n'); return; }
    if (vfs_list(path, entries, VFS_FILE_MAX, &count) != 0) { console_error(); return; }
    for (i = 0; i < count; ++i) {
        console_puts(entries[i].name);
        if (entries[i].type == VFS_DIRECTORY) console_putc('/');
        console_putc('\n');
    }
}
static int console_append_path(const char *base, const char *name, char *out, uint32_t cap) {
    uint32_t i = 0, j;
    if (!base || !name || !out || !cap) return -1;
    while (base[i] && i + 1U < cap) out[i] = base[i], ++i;
    if (base[i] || i + 1U >= cap) return -1;
    if (i > 1U) out[i++] = '/';
    for (j = 0; name[j] && i + 1U < cap; ++j) out[i++] = name[j];
    if (name[j]) return -1;
    out[i] = '\0';
    return 0;
}
static int console_name_matches(const char *name, const char *needle) {
    uint32_t i, j;
    if (!needle || !*needle) return 1;
    for (i = 0; name[i]; ++i) {
        for (j = 0; needle[j] && name[i + j] == needle[j]; ++j) {}
        if (!needle[j]) return 1;
    }
    return 0;
}
static void console_tree_walk(const char *path, uint32_t depth) {
    struct vfs_dirent entries[VFS_FILE_MAX]; uint32_t count, i, j;
    char child[VFS_PATH_MAX + 1];
    if (depth >= 16U || vfs_list(path, entries, VFS_FILE_MAX, &count) != 0) return;
    for (i = 0; i < count; ++i) {
        for (j = 0; j < depth; ++j) console_puts("  ");
        console_puts(entries[i].name);
        if (entries[i].type == VFS_DIRECTORY) console_putc('/');
        console_putc('\n');
        if (entries[i].type == VFS_DIRECTORY &&
            console_append_path(path, entries[i].name, child, sizeof(child)) == 0)
            console_tree_walk(child, depth + 1U);
    }
}
static void console_find_walk(const char *path, const char *needle, uint32_t depth) {
    struct vfs_dirent entries[VFS_FILE_MAX]; uint32_t count, i;
    char child[VFS_PATH_MAX + 1];
    if (depth >= 16U || vfs_list(path, entries, VFS_FILE_MAX, &count) != 0) return;
    for (i = 0; i < count; ++i) {
        if (console_append_path(path, entries[i].name, child, sizeof(child)) != 0) continue;
        if (console_name_matches(entries[i].name, needle)) console_puts(child), console_putc('\n');
        if (entries[i].type == VFS_DIRECTORY) console_find_walk(child, needle, depth + 1U);
    }
}
static void console_add_history(const char *line) {
    uint32_t i;
    if (!line || !*line) return;
    if (history_count && console_streq(history[(history_count - 1U) % HISTORY_MAX], line)) return;
    if (history_count < HISTORY_MAX) ++history_count;
    for (i = history_count - 1U; i; --i) {
        uint32_t dst = i % HISTORY_MAX, src = (i - 1U) % HISTORY_MAX, j;
        for (j = 0; j <= COMMAND_MAX; ++j) history[dst][j] = history[src][j];
    }
    for (i = 0; i <= COMMAND_MAX; ++i) history[0][i] = line[i];
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
    console_puts("MinOS:");
    console_puts(cwd);
    console_puts("> ");
    cursor_column = console_prompt_columns();
    command_length = 0;
    command_cursor = 0;
    command[0] = '\0';
}

static void console_redraw_command(void) {
    uint32_t x = CONSOLE_MARGIN + console_prompt_columns() * CONSOLE_CHAR_WIDTH;
    uint32_t width = fb_get_width() > x ? fb_get_width() - x : 0;
    fb_fill_rect(x, CONSOLE_MARGIN + cursor_row * CONSOLE_LINE_HEIGHT,
                 width, 16, CONSOLE_BG);
    fb_draw_string(x, CONSOLE_MARGIN + cursor_row * CONSOLE_LINE_HEIGHT,
                   command, CONSOLE_FG, CONSOLE_BG);
    cursor_column = console_prompt_columns() + command_cursor;
}
static void console_set_command(const char *text) {
    uint32_t i = 0;
    while (text && text[i] && i < COMMAND_MAX) { command[i] = text[i]; ++i; }
    command[i] = 0; command_length = i; command_cursor = i; console_redraw_command();
}

static void console_command(void) {
    char line[COMMAND_MAX + 1], *argv[8], path[VFS_PATH_MAX + 1], path2[VFS_PATH_MAX + 1];
    uint32_t argc, i, size;
    char data[COMMAND_MAX + 1];
    command[command_length] = '\0';
    console_putc('\n');
    serial_puts("[MinOS Console] command: ");
    serial_puts(command);
    serial_putc('\n');
    for (i = 0; i <= command_length; ++i) line[i] = command[i];
    console_add_history(line);
    history_index = 0;
    argc = console_words(line, argv, 8);
    if (argc == 0) {
        /* no output */
    } else if (console_streq(argv[0], "help")) {
        console_puts("BashPlus - MinOS shell\n");
        console_puts("System: about clear help mem ticks uptime reboot shutdown\n");
        console_puts("Files:  pwd ls cd mkdir rmdir touch cat write append rm cp mv tree find df\n");
        console_puts("Shell:  echo history\n");
    } else if (console_streq(argv[0], "clear")) {
        fb_clear(CONSOLE_BG);
        cursor_column = 0;
        cursor_row = 0;
    } else if (console_streq(argv[0], "about")) {
        console_puts("MinOS interactive kernel console\n");
        console_puts("x86_64 UEFI | framebuffer | IRQ input\n");
    } else if (console_streq(argv[0], "mem")) {
        console_puts("usable: ");
        console_put_u64(pmm_usable_bytes() / (1024U * 1024U));
        console_puts(" MiB, reserved: ");
        console_put_u64(pmm_reserved_bytes() / (1024U * 1024U));
        console_puts(" MiB, free frames: ");
        console_put_u64(pmm_free_frames());
        console_putc('\n');
    } else if (console_streq(argv[0], "ticks")) {
        console_puts("ticks: ");
        console_put_u64(timer_ticks());
        console_puts(" (");
        console_put_u64(timer_frequency());
        console_puts(" Hz)\n");
    } else if (console_streq(argv[0], "uptime")) {
        console_puts("uptime: ");
        console_put_u64(timer_frequency() ? timer_ticks() / timer_frequency() : 0);
        console_puts(" seconds\n");
    } else if (console_streq(argv[0], "reboot")) {
        power_reboot();
    } else if (console_streq(argv[0], "shutdown")) {
        power_shutdown();
    } else if (console_streq(argv[0], "pwd")) {
        console_puts(cwd); console_putc('\n');
    } else if (console_streq(argv[0], "ls")) {
        if (console_arg(argv, argc, argc > 1 ? 1 : 0, path) == 0) console_list(path);
    } else if (console_streq(argv[0], "cd")) {
        if (console_arg(argv, argc, argc > 1 ? 1 : 0, path) == 0 && vfs_is_directory(path)) {
            uint32_t j; for (j = 0; path[j]; ++j) cwd[j] = path[j]; cwd[j] = 0;
        } else console_error();
    } else if (console_streq(argv[0], "mkdir") || console_streq(argv[0], "rmdir") ||
               console_streq(argv[0], "touch") || console_streq(argv[0], "rm")) {
        if (console_arg(argv, argc, 1, path) == 0) {
            int rc = console_streq(argv[0], "mkdir") ? vfs_mkdir(path) :
                     console_streq(argv[0], "rmdir") ? vfs_rmdir(path) :
                     console_streq(argv[0], "touch") ? vfs_touch(path) : vfs_remove(path);
            if (rc) console_error();
        }
    } else if (console_streq(argv[0], "cat")) {
        if (console_arg(argv, argc, 1, path) == 0) {
            if (vfs_read(path, data, sizeof(data) - 1U, &size) == 0) {
                data[size] = 0;
                console_puts(data);
                console_putc('\n');
                serial_puts("[MinOS Console] cat output: ");
                serial_puts(data);
                serial_putc('\n');
            } else {
                serial_puts("[MinOS Console] cat failed\n");
                console_error();
            }
        }
    } else if (console_streq(argv[0], "write") || console_streq(argv[0], "append")) {
        if (argc >= 3 && console_arg(argv, argc, 1, path) == 0) {
            /* The command grammar deliberately treats the remaining words as text. */
            uint32_t j, at = 0;
            for (j = 2; j < argc; ++j) {
                uint32_t k; if (j > 2 && at < sizeof(data) - 1U) data[at++] = ' ';
                for (k = 0; argv[j][k] && at < sizeof(data) - 1U; ++k) data[at++] = argv[j][k];
            }
            if (vfs_write(path, data, at, console_streq(argv[0], "append")) != 0) console_error();
        } else console_error();
    } else if (console_streq(argv[0], "cp") || console_streq(argv[0], "mv")) {
        if (argc >= 3 && console_arg(argv, argc, 1, path) == 0 && console_arg(argv, argc, 2, path2) == 0 &&
            (console_streq(argv[0], "cp") ? vfs_copy(path, path2) : vfs_move(path, path2))) console_error();
    } else if (console_streq(argv[0], "tree")) {
        if (console_arg(argv, argc, argc > 1 ? 1 : 0, path) == 0) {
            struct vfs_stat stat;
            if (vfs_stat_path(path, &stat) != 0) console_error();
            else { console_puts(path); console_putc('\n'); if (stat.type == VFS_DIRECTORY) console_tree_walk(path, 0); }
        }
    } else if (console_streq(argv[0], "find")) {
        if (console_arg(argv, argc, argc > 1 ? 1 : 0, path) == 0) {
            const char *needle = argc > 2 ? argv[2] : "";
            struct vfs_stat stat;
            if (vfs_stat_path(path, &stat) != 0) console_error();
            else if (stat.type == VFS_FILE) {
                if (console_name_matches(path, needle)) console_puts(path), console_putc('\n');
            } else console_find_walk(path, needle, 0);
        }
    } else if (console_streq(argv[0], "df")) {
        console_puts(ata_primary_master_present() ? "disk: persistent ATA\n" : "disk: volatile memory\n");
    } else if (console_streq(argv[0], "echo")) {
        for (i = 1; i < argc; ++i) { if (i > 1) console_putc(' '); console_puts(argv[i]); } console_putc('\n');
    } else if (console_streq(argv[0], "history")) {
        for (i = 0; i < history_count; ++i) { console_put_u64(i + 1U); console_puts(" "); console_puts(history[i]); console_putc('\n'); }
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
            if (command_cursor) {
                uint32_t index = command_cursor - 1U;
                while (index < command_length) {
                    command[index] = command[index + 1U];
                    ++index;
                }
                --command_cursor;
                --command_length;
                console_redraw_command();
            }
        } else if (event.key == INPUT_KEY_TAB) {
            serial_puts("[MinOS Console] Tab event consumed\n");
            uint32_t spaces = 4U - ((cursor_column - 7U) % 4U);
            while (spaces-- && command_length < COMMAND_MAX) {
                uint32_t index = command_length;
                while (index > command_cursor) {
                    command[index] = command[index - 1U];
                    --index;
                }
                command[command_cursor++] = ' ';
                ++command_length;
                command[command_length] = '\0';
            }
            console_redraw_command();
        } else if (event.key == INPUT_KEY_LEFT) {
            if (command_cursor) {
                --command_cursor;
                console_redraw_command();
            }
            serial_puts("[MinOS Console] Left arrow consumed\n");
        } else if (event.key == INPUT_KEY_RIGHT) {
            if (command_cursor < command_length) {
                ++command_cursor;
                console_redraw_command();
            }
            serial_puts("[MinOS Console] Right arrow consumed\n");
        } else if (event.key == INPUT_KEY_UP || event.key == INPUT_KEY_DOWN) {
            serial_puts("[MinOS Console] Vertical arrow consumed\n");
            if (event.key == INPUT_KEY_UP && history_count) {
                if (history_index < history_count) ++history_index;
                console_set_command(history[(history_index - 1U) % HISTORY_MAX]);
            } else if (event.key == INPUT_KEY_DOWN && history_index) {
                --history_index;
                console_set_command(history_index ? history[(history_index - 1U) % HISTORY_MAX] : "");
            }
        } else if ((event.modifiers & INPUT_MOD_CTRL) && event.key == INPUT_KEY_A) {
            command_cursor = 0; console_redraw_command();
        } else if ((event.modifiers & INPUT_MOD_CTRL) && event.key == INPUT_KEY_E) {
            command_cursor = command_length; console_redraw_command();
        } else if ((event.modifiers & INPUT_MOD_CTRL) && event.key == INPUT_KEY_U) {
            command_length = 0; command_cursor = 0; command[0] = 0; console_redraw_command();
        } else if ((event.modifiers & INPUT_MOD_CTRL) && event.key == INPUT_KEY_K) {
            command_length = command_cursor; command[command_length] = 0; console_redraw_command();
        } else if (event.character >= 32 && event.character <= 126 &&
                   command_length < COMMAND_MAX) {
            uint32_t index = command_length;
            while (index > command_cursor) {
                command[index] = command[index - 1U];
                --index;
            }
            command[command_cursor++] = event.character;
            ++command_length;
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
