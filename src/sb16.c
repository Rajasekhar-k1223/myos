#include "sb16.h"
#include "kernel.h"
#include "pmm.h"
#include "idt.h"
#include "pit.h"

#define SB16_BASE 0x220
#define DSP_RESET (SB16_BASE + 0x6)
#define DSP_READ (SB16_BASE + 0xA)
#define DSP_WRITE (SB16_BASE + 0xC)
#define DSP_STATUS (SB16_BASE + 0xC)
#define DSP_DATA_AVAIL (SB16_BASE + 0xE)

static inline void outb(uint16_t port, uint8_t data) { asm volatile("outb %0, %1" : : "a"(data), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t ret; asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port)); return ret; }

static void dsp_write(uint8_t value) {
    while (inb(DSP_STATUS) & 0x80) { } // Wait for ready
    outb(DSP_WRITE, value);
}

static uint8_t dsp_read(void) {
    while (!(inb(DSP_DATA_AVAIL) & 0x80)) { } // Wait for data
    return inb(DSP_READ);
}

static uint8_t* audio_buffer = 0;
#define AUDIO_BUFFER_SIZE 4096
#define HALF_BUFFER_SIZE 2048

volatile int sb16_needs_data = 0;
volatile uint8_t* sb16_next_buffer = 0;
static int current_half = 0; // 0 = playing first half, 1 = playing second half

void sb16_init(void) {
    // 1. Reset DSP
    outb(DSP_RESET, 1);
    for (volatile int i = 0; i < 10000; i++) {} // Delay
    outb(DSP_RESET, 0);
    
    uint8_t magic = dsp_read();
    if (magic != 0xAA) {
        terminal_printf("  [SB16] Hardware not found! Read 0x%x\n", magic);
        return;
    }
    
    dsp_write(0xE1);
    uint8_t major = dsp_read();
    uint8_t minor = dsp_read();
    terminal_printf("  [SB16] DSP Initialized successfully. Version %d.%d\n", major, minor);
    
    // Turn on Speaker
    dsp_write(0xD1);
    
    // Allocate 4KB buffer (guaranteed not to cross 64KB boundary)
    audio_buffer = (uint8_t*)pmm_alloc_frame();
    for (uint32_t i = 0; i < AUDIO_BUFFER_SIZE; i++) {
        audio_buffer[i] = 128; // Silence
    }
    
    // Program ISA DMA Channel 1 for Auto-Initialize
    uint32_t phys = (uint32_t)audio_buffer;
    uint8_t page = (phys >> 16) & 0xFF;
    uint16_t offset = phys & 0xFFFF;
    uint16_t len = AUDIO_BUFFER_SIZE - 1;
    
    outb(0x0A, 5);         // Mask channel 1
    outb(0x0C, 0);         // Clear byte pointer
    outb(0x0B, 0x59);      // Auto-Init, Single Transfer, Read, Channel 1
    
    outb(0x02, offset & 0xFF);
    outb(0x02, (offset >> 8) & 0xFF);
    outb(0x83, page);
    
    outb(0x03, len & 0xFF);
    outb(0x03, (len >> 8) & 0xFF);
    
    outb(0x0A, 1);         // Unmask channel 1
    
    // Register IRQ 5 handler
    register_interrupt_handler(32 + 5, sb16_handler);
}

void sb16_set_sample_rate(uint16_t hz) {
    dsp_write(0x41);
    dsp_write(hz >> 8);
    dsp_write(hz & 0xFF);
}

void sb16_start_playback(void) {
    current_half = 0;
    sb16_needs_data = 1;
    sb16_next_buffer = audio_buffer + HALF_BUFFER_SIZE;
    
    uint16_t len = HALF_BUFFER_SIZE - 1;
    dsp_write(0x48); // 8-bit Auto-Init
    dsp_write(len & 0xFF);
    dsp_write((len >> 8) & 0xFF);
    dsp_write(0x1C); // 8-bit Auto-Init Command
}

void sb16_stop_playback(void) {
    dsp_write(0xDA); // Halt DMA
}

void sb16_handler(struct registers* regs) {
    (void)regs;
    inb(SB16_BASE + 0xE); // Acknowledge DSP interrupt

    // The DSP just finished playing one half.
    // It's currently playing the OTHER half.
    // So we need data for the half it just finished.
    current_half = 1 - current_half;
    
    if (current_half == 0) {
        // Just finished second half, now playing first half.
        // We need data for the second half.
        sb16_next_buffer = audio_buffer + HALF_BUFFER_SIZE;
    } else {
        // Just finished first half, now playing second half.
        // We need data for the first half.
        sb16_next_buffer = audio_buffer;
    }
    
    sb16_needs_data = 1;
}
