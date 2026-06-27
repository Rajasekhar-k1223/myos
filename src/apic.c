#include "apic.h"
#include "acpi.h"
#include "paging.h"
#include "kernel.h"
#include "pit.h"

#define APIC_ID_REG      0x0020
#define APIC_EOI_REG     0x00B0
#define APIC_SPURIOUS    0x00F0
#define APIC_ICR_LOW     0x0300
#define APIC_ICR_HIGH    0x0310
#define APIC_LVT_TIMER   0x0320
#define APIC_TIMER_INIT  0x0380
#define APIC_TIMER_CUR   0x0390
#define APIC_TIMER_DIV   0x03E0

static inline void apic_write(uint32_t reg, uint32_t val) {
    *((volatile uint32_t*)(local_apic_base + reg)) = val;
}

static inline uint32_t apic_read(uint32_t reg) {
    return *((volatile uint32_t*)(local_apic_base + reg));
}

uint8_t apic_get_id(void) {
    if (!local_apic_base) return bsp_apic_id;
    return (uint8_t)(apic_read(APIC_ID_REG) >> 24);
}

void apic_init(void) {
    if (!local_apic_base) return;
    
    // Enable APIC and set spurious interrupt vector to 0xFF
    apic_write(APIC_SPURIOUS, 0x1FF); // bit 8 sets APIC enabled

    // Program LINT0 to ExtINT mode to let PIC interrupts pass through!
    // LINT0 is at APIC_LVT_LINT0 (0x350).
    // Bits: Vector (0..7), Delivery Mode (8..10) = 111 (ExtINT), Mask (16) = 0
    apic_write(0x350, 0x0700); // Delivery Mode = ExtINT, Unmasked
    
    static int bsp_initialized = 0;
    if (!bsp_initialized) {
        bsp_apic_id = apic_get_id();
        bsp_initialized = 1;
        terminal_printf("[APIC] BSP ID is %d\n", bsp_apic_id);
    }
}

void apic_send_init(uint8_t apic_id) {
    apic_write(APIC_ICR_HIGH, (uint32_t)apic_id << 24);
    apic_write(APIC_ICR_LOW, 0x00004500); // INIT, Level=Assert, Trigger=Level
    // Wait for delivery to complete
    uint32_t timeout = pit_get_ticks() + 10;
    while ((apic_read(APIC_ICR_LOW) & (1 << 12)) && pit_get_ticks() < timeout) {
        // Spin until delivery status bit clears
    }
    // Note: Do NOT send INIT De-assert — it's a broadcast to ALL CPUs (including BSP)
    // and would reset the BSP too. Modern (P6+) systems don't require it.
}

void apic_send_sipi(uint8_t apic_id, uint8_t vector) {
    apic_write(APIC_ICR_HIGH, (uint32_t)apic_id << 24);
    apic_write(APIC_ICR_LOW, 0x00004600 | vector); // Start-Up (SIPI)
    
    uint32_t timeout = pit_get_ticks() + 10;
    while ((apic_read(APIC_ICR_LOW) & (1 << 12)) && pit_get_ticks() < timeout);
}

void apic_eoi(void) {
    apic_write(APIC_EOI_REG, 0);
}

void apic_timer_init(void) {
    if (!local_apic_base) return;

    // Divide by 16
    apic_write(APIC_TIMER_DIV, 0x03);

    // Set periodic mode (bit 17) and map to vector 48
    apic_write(APIC_LVT_TIMER, 48 | 0x20000);

    // Set initial count
    apic_write(APIC_TIMER_INIT, 100000); // Tweak this value for ~1ms or 10ms
}
