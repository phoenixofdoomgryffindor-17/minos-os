#include "input.h"

#define INPUT_QUEUE_SIZE 64

static struct input_event queue[INPUT_QUEUE_SIZE];
static volatile uint32_t read_index;
static volatile uint32_t write_index;

void input_init(void) {
    read_index = 0;
    write_index = 0;
}

int input_push(const struct input_event *event) {
    uint32_t next;
    if (!event)
        return -1;
    next = (write_index + 1U) % INPUT_QUEUE_SIZE;
    if (next == read_index)
        return -1;
    queue[write_index] = *event;
    write_index = next;
    return 0;
}

int input_pop(struct input_event *event) {
    if (!event || read_index == write_index)
        return -1;
    *event = queue[read_index];
    read_index = (read_index + 1U) % INPUT_QUEUE_SIZE;
    return 0;
}
