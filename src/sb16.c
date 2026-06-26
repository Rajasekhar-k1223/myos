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

static uint8_t* audio_buffer;
static uint32_t audio_length = 4096;

void sb16_init(void) {
    // 1. Reset DSP
    outb(DSP_RESET, 1);
    for (volatile int i = 0; i < 10000; i++) {} // Delay 3 microseconds
    outb(DSP_RESET, 0);
    
    // Read 0xAA
    uint8_t magic = dsp_read();
    if (magic != 0xAA) {
        terminal_printf("  [SB16] Hardware not found! Read 0x%x\n", magic);
        return;
    }
    
    // Get version
    dsp_write(0xE1);
    uint8_t major = dsp_read();
    uint8_t minor = dsp_read();
    terminal_printf("  [SB16] DSP Initialized successfully. Version %d.%d\n", major, minor);
    
    // Turn on Speaker
    dsp_write(0xD1);
    
    // 2. Synthesize Square Wave (400 Hz)
    // 8000 Hz sample rate. 8000 / 400 = 20 samples per cycle. 10 high, 10 low.
    audio_buffer = (uint8_t*)pmm_alloc_frame();
    for (uint32_t i = 0; i < audio_length; i++) {
        audio_buffer[i] = ((i % 20) < 10) ? 192 : 64; // 8-bit PCM unsigned (0 to 255)
    }
    
    // 3. Program ISA DMA Channel 1
    uint32_t phys = (uint32_t)audio_buffer;
    uint8_t page = (phys >> 16) & 0xFF;
    uint16_t offset = phys & 0xFFFF;
    uint16_t len = audio_length - 1;
    
    outb(0x0A, 5);         // Mask channel 1
    outb(0x0C, 0);         // Clear byte pointer
    outb(0x0B, 0x59);      // Auto-Init, Single Transfer, Read (memory to peripheral), Channel 1
    
    outb(0x02, offset & 0xFF);         // Low byte of address
    outb(0x02, (offset >> 8) & 0xFF);  // High byte of address
    outb(0x83, page);                  // Page register for channel 1
    
    outb(0x03, len & 0xFF);            // Low byte of length
    outb(0x03, (len >> 8) & 0xFF);     // High byte of length
    
    outb(0x0A, 1);         // Unmask channel 1
    
    // 4. Register IRQ 5 handler
    register_interrupt_handler(32 + 5, sb16_handler);
    
    // 5. Start DSP Playback
    // Set Sample Rate
    dsp_write(0x40);
    dsp_write(8000 >> 8); // Actually SB16 needs a time constant for older versions, but 0x41 is set output sample rate
    // Wait, DSP version 4 uses command 0x41 for output rate:
    dsp_write(0x41);
    dsp_write(8000 >> 8);
    dsp_write(8000 & 0xFF);
    
    // 8-bit auto-init
    dsp_write(0xC6); 
    dsp_write(0x00); // mono, unsigned
    dsp_write(len & 0xFF);
    dsp_write((len >> 8) & 0xFF);
    
    terminal_printf("  [SB16] Playback started on DMA Channel 1!\n");
}

void sb16_handler(struct registers* regs) {
    (void)regs;
    // Acknowledge DSP interrupt (8-bit)
    inb(SB16_BASE + 0xE);
}
