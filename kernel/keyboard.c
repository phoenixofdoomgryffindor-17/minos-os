#include "keyboard.h"
#include "apic.h"
#include "input.h"
#include "kernel.h"
#include "serial.h"

#define KBD_DATA       0x60
#define KBD_STATUS     0x64
#define KBD_COMMAND    0x64
#define KBD_STATUS_OBF 0x01
#define KBD_STATUS_IBF 0x02
#define KBD_CMD_DISABLE1 0xAD
#define KBD_CMD_DISABLE2 0xA7
#define KBD_CMD_ENABLE1  0xAE
#define KBD_CMD_READ_CONFIG  0x20
#define KBD_CMD_WRITE_CONFIG 0x60
#define KBD_CMD_SELF_TEST 0xAA

static uint8_t modifiers;
static uint8_t extended;

static int wait_input_clear(void) {
    uint32_t timeout = 100000;
    while ((inb(KBD_STATUS) & KBD_STATUS_IBF) && --timeout)
        __asm__ volatile ("pause");
    return timeout != 0;
}

static int wait_output_full(void) {
    uint32_t timeout = 100000;
    while (!(inb(KBD_STATUS) & KBD_STATUS_OBF) && --timeout)
        __asm__ volatile ("pause");
    return timeout != 0;
}

static int controller_command(uint8_t command) {
    if (!wait_input_clear())
        return -1;
    outb(KBD_COMMAND, command);
    return 0;
}

static int keyboard_command(uint8_t command) {
    if (!wait_input_clear())
        return -1;
    outb(KBD_DATA, command);
    return 0;
}

static enum input_key key_for_scancode(uint8_t code) {
    static const enum input_key normal[0x40] = {
        [0x01] = INPUT_KEY_ESCAPE, [0x02] = INPUT_KEY_1, [0x03] = INPUT_KEY_2,
        [0x04] = INPUT_KEY_3, [0x05] = INPUT_KEY_4, [0x06] = INPUT_KEY_5,
        [0x07] = INPUT_KEY_6, [0x08] = INPUT_KEY_7, [0x09] = INPUT_KEY_8,
        [0x0A] = INPUT_KEY_9, [0x0B] = INPUT_KEY_0, [0x0E] = INPUT_KEY_BACKSPACE,
        [0x0F] = INPUT_KEY_TAB, [0x1C] = INPUT_KEY_ENTER, [0x39] = INPUT_KEY_SPACE,
        [0x1E] = INPUT_KEY_A, [0x30] = INPUT_KEY_B, [0x2E] = INPUT_KEY_C,
        [0x20] = INPUT_KEY_D, [0x12] = INPUT_KEY_E, [0x21] = INPUT_KEY_F,
        [0x22] = INPUT_KEY_G, [0x23] = INPUT_KEY_H, [0x17] = INPUT_KEY_I,
        [0x24] = INPUT_KEY_J, [0x25] = INPUT_KEY_K, [0x26] = INPUT_KEY_L,
        [0x32] = INPUT_KEY_M, [0x31] = INPUT_KEY_N, [0x18] = INPUT_KEY_O,
        [0x19] = INPUT_KEY_P, [0x10] = INPUT_KEY_Q, [0x13] = INPUT_KEY_R,
        [0x1F] = INPUT_KEY_S, [0x14] = INPUT_KEY_T, [0x16] = INPUT_KEY_U,
        [0x2F] = INPUT_KEY_V, [0x11] = INPUT_KEY_W, [0x2D] = INPUT_KEY_X,
        [0x15] = INPUT_KEY_Y, [0x2C] = INPUT_KEY_Z
    };
    if (code >= 0x40)
        return INPUT_KEY_NONE;
    return normal[code];
}

static enum input_key extended_key(uint8_t code) {
    if (code == 0x4B) return INPUT_KEY_LEFT;
    if (code == 0x4D) return INPUT_KEY_RIGHT;
    if (code == 0x48) return INPUT_KEY_UP;
    if (code == 0x50) return INPUT_KEY_DOWN;
    return INPUT_KEY_NONE;
}

static char key_character(enum input_key key) {
    if (key >= INPUT_KEY_A && key <= INPUT_KEY_Z)
        return (char)('a' + (key - INPUT_KEY_A));
    if (key >= INPUT_KEY_0 && key <= INPUT_KEY_9)
        return (char)('0' + (key - INPUT_KEY_0));
    if (key == INPUT_KEY_SPACE) return ' ';
    if (key == INPUT_KEY_ENTER) return '\n';
    if (key == INPUT_KEY_TAB) return '\t';
    if (key == INPUT_KEY_BACKSPACE) return '\b';
    return 0;
}

static char scancode_character(uint8_t code, uint8_t shift) {
    static const char number_shifted[] = ")!@#$%^&*(";

    if (code >= 0x02 && code <= 0x0B)
        return shift ? number_shifted[code - 0x02] : (code == 0x0B ? '0' : (char)('1' + code - 0x02));
    if (code >= 0x0C && code <= 0x0D)
        return shift ? (code == 0x0C ? '_' : '+') : (code == 0x0C ? '-' : '=');
    if (code == 0x1A) return shift ? '{' : '[';
    if (code == 0x1B) return shift ? '}' : ']';
    if (code == 0x27) return shift ? ':' : ';';
    if (code == 0x28) return shift ? '"' : '\'';
    if (code == 0x29) return shift ? '~' : '`';
    if (code == 0x2B) return shift ? '|' : '\\';
    if (code == 0x33) return shift ? '<' : ',';
    if (code == 0x34) return shift ? '>' : '.';
    if (code == 0x35) return shift ? '?' : '/';
    return 0;
}

void keyboard_irq(void) {
    uint8_t code;
    struct input_event event;

    if (!(inb(KBD_STATUS) & KBD_STATUS_OBF)) {
        pic_eoi(1);
        return;
    }
    code = inb(KBD_DATA);
    if (code == 0xE0) {
        extended = 1;
        pic_eoi(1);
        return;
    }
    if (code == 0xE1) { /* Pause sequence is intentionally ignored. */
        extended = 0;
        pic_eoi(1);
        return;
    }

    uint8_t released = code & 0x80;
    code &= 0x7F;
    if (!extended && (code == 0x2A || code == 0x36))
        modifiers = released ? (modifiers & (uint8_t)~INPUT_MOD_SHIFT)
                             : (modifiers | INPUT_MOD_SHIFT);
    else if (code == 0x1D)
        modifiers = released ? (modifiers & (uint8_t)~INPUT_MOD_CTRL)
                             : (modifiers | INPUT_MOD_CTRL);
    else if (code == 0x38)
        modifiers = released ? (modifiers & (uint8_t)~INPUT_MOD_ALT)
                             : (modifiers | INPUT_MOD_ALT);
    else {
        event.key = extended ? extended_key(code) : key_for_scancode(code);
        event.pressed = released ? 0 : 1;
        event.modifiers = modifiers;
        event.character = event.pressed ? key_character(event.key) : 0;
        if (event.pressed && !extended)
            {
                char symbol = scancode_character(code, (modifiers & INPUT_MOD_SHIFT) != 0);
                if (symbol)
                    event.character = symbol;
            }
        if ((modifiers & INPUT_MOD_SHIFT) && event.character >= 'a' &&
            event.character <= 'z')
            event.character = (char)(event.character - 'a' + 'A');
        if (event.key != INPUT_KEY_NONE)
            (void)input_push(&event);
        if (event.pressed && event.character)
            serial_printf("[MinOS Keyboard] key=%c queued\n", event.character);
    }
    extended = 0;
    pic_eoi(1);
}

int keyboard_init(void) {
    uint8_t config;
    modifiers = 0;
    extended = 0;
    input_init();

    if (controller_command(KBD_CMD_DISABLE1) || controller_command(KBD_CMD_DISABLE2))
        return -1;
    while (inb(KBD_STATUS) & KBD_STATUS_OBF)
        (void)inb(KBD_DATA);
    if (controller_command(KBD_CMD_SELF_TEST) || !wait_output_full() || inb(KBD_DATA) != 0x55)
        return -1;
    if (controller_command(KBD_CMD_READ_CONFIG) || !wait_output_full())
        return -1;
    config = inb(KBD_DATA);
    config |= 0x01;  /* keyboard IRQ1 enabled */
    config |= 0x40; /* enable 8042 translation; decoder consumes set-1 codes */
    if (controller_command(KBD_CMD_WRITE_CONFIG) || !wait_input_clear())
        return -1;
    outb(KBD_DATA, config);
    if (keyboard_command(0xFF) || !wait_output_full() || inb(KBD_DATA) != 0xFA)
        return -1;
    if (!wait_output_full() || inb(KBD_DATA) != 0xAA)
        return -1;
    if (controller_command(KBD_CMD_ENABLE1))
        return -1;
    pic_unmask_irq(1);
    serial_puts("[MinOS Keyboard] PS/2 controller ready; IRQ1 vector 33 enabled.\n");
    return 0;
}
