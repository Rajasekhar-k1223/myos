#include "pit.h"
#include "idt.h"
#include "io.h"
#include "task.h"
#include "acpi.h"
#include "apic.h"

#define PIT_CHANNEL0  0x40
#define PIT_CMD       0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t pit_ticks       = 0;
static volatile int      sched_active    = 0;
static uint32_t          pit_hz          = 100;

static void pit_callback(struct registers* r) {
    (void)r;
    if (apic_get_id() == bsp_apic_id) {
        pit_ticks++;
        if (!sched_active) return;
        task_tick(); /* decrement sleep counters */
    }
    
    if (!sched_active) return;

    /* Preempt every 2 ticks (20 ms time slice) on BSP, but on APs we just schedule always since LAPIC timer count can be tuned */
    if (apic_get_id() != bsp_apic_id || pit_ticks % 2 == 0)
        schedule();
}

void pit_init(uint32_t hz) {
    pit_hz = hz;
    uint32_t divisor = PIT_BASE_FREQ / hz;
    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    register_interrupt_handler(32, pit_callback);
    register_interrupt_handler(48, pit_callback);
}

void pit_enable_scheduling(void) {
    sched_active = 1;
}

uint32_t pit_get_ticks(void)   { return pit_ticks; }
uint32_t pit_get_seconds(void) { return pit_ticks / pit_hz; }
