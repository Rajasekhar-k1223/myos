#include "pit.h"
#include "idt.h"
#include "io.h"

#define PIT_CHANNEL0  0x40
#define PIT_CMD       0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t pit_ticks = 0;
static uint32_t pit_hz = 100;

static void pit_callback(struct registers* r) {
    (void)r;
    pit_ticks++;
}

void pit_init(uint32_t hz) {
    pit_hz = hz;
    uint32_t divisor = PIT_BASE_FREQ / hz;

    outb(PIT_CMD, 0x36);                     /* channel 0, lobyte/hibyte, mode 3 */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    register_interrupt_handler(32, pit_callback); /* IRQ0 → vector 32 */
}

uint32_t pit_get_ticks(void)   { return pit_ticks; }
uint32_t pit_get_seconds(void) { return pit_ticks / pit_hz; }
