#include "sb16.h"
#include "kernel.h"
#include "pmm.h"
#include "idt.h"
#include "pit.h"
#include "string.h"

// ─── Port definitions ─────────────────────────────────────────────────────────

#define SB16_BASE       0x220
#define DSP_RESET       (SB16_BASE + 0x6)
#define DSP_READ        (SB16_BASE + 0xA)
#define DSP_WRITE       (SB16_BASE + 0xC)
#define DSP_STATUS      (SB16_BASE + 0xC)
#define DSP_DATA_AVAIL  (SB16_BASE + 0xE)

// ─── I/O primitives ───────────────────────────────────────────────────────────

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// ─── DSP helpers ─────────────────────────────────────────────────────────────

static void dsp_write(uint8_t value) {
    while (inb(DSP_STATUS) & 0x80) {}
    outb(DSP_WRITE, value);
}

static uint8_t dsp_read(void) {
    while (!(inb(DSP_DATA_AVAIL) & 0x80)) {}
    return inb(DSP_READ);
}

// ─── State ───────────────────────────────────────────────────────────────────

// Legacy double-buffer exported symbols (music.c compatibility)
static uint8_t* audio_buffer = 0;
#define AUDIO_BUFFER_SIZE  4096
#define HALF_BUFFER_SIZE   2048

volatile int     sb16_needs_data = 0;
volatile uint8_t* sb16_next_buffer = 0;
static int        current_half = 0;

// Detected flag — set to 1 in sb16_init if hardware is present
static int sb16_present = 0;

// ─── PCM playback state ───────────────────────────────────────────────────────
//
// ISA DMA requires the buffer to be below 16 MB and must not cross a 64 KB
// page boundary.  The simplest guarantee: declare a static buffer aligned to
// its own size (65536 bytes == 64 KB), so it is always naturally aligned and
// cannot straddle a boundary.
//
// NOTE: The kernel is loaded low in physical memory, so a static buffer here
// lives well below the 16 MB ceiling.

#define PCM_BUF_SIZE 65536
static uint8_t dma_buf[PCM_BUF_SIZE] __attribute__((aligned(PCM_BUF_SIZE)));

static const uint8_t* pcm_src      = 0;  // caller's sample pointer
static uint32_t       pcm_len      = 0;  // total sample count
static uint32_t       pcm_pos      = 0;  // current playback position
static int            pcm_loop     = 0;  // loop flag
static volatile int   pcm_playing  = 0;  // 1 while active

// Number of bytes we feed per DMA block (half the DMA buffer)
#define PCM_BLOCK_SIZE (PCM_BUF_SIZE / 2)

// ─── DMA programming helper ───────────────────────────────────────────────────

static void dma_program(uint32_t phys_addr, uint32_t length) {
    uint8_t  page   = (phys_addr >> 16) & 0xFF;
    uint16_t offset = (uint16_t)(phys_addr & 0xFFFF);
    uint16_t count  = (uint16_t)(length - 1);

    outb(0x0A, 0x05);                   // Mask channel 1
    outb(0x0C, 0x00);                   // Clear flip-flop
    outb(0x0B, 0x59);                   // Mode: auto-init, single, read, ch1
    outb(0x02, (uint8_t)(offset & 0xFF));
    outb(0x02, (uint8_t)(offset >> 8));
    outb(0x83, page);                   // Page register for channel 1
    outb(0x03, (uint8_t)(count & 0xFF));
    outb(0x03, (uint8_t)(count >> 8));
    outb(0x0A, 0x01);                   // Unmask channel 1
}

// Fill one half of dma_buf with the next PCM_BLOCK_SIZE bytes of audio.
// Returns 0 when playback should stop (end of samples, no loop).
static int fill_half(int half) {
    uint8_t* dst = dma_buf + (half == 0 ? 0 : PCM_BLOCK_SIZE);

    if (!pcm_playing || pcm_src == 0) {
        // Silence
        memset(dst, 128, PCM_BLOCK_SIZE);
        return 0;
    }

    uint32_t filled = 0;
    while (filled < PCM_BLOCK_SIZE) {
        if (pcm_pos >= pcm_len) {
            if (pcm_loop) {
                pcm_pos = 0;
            } else {
                // Pad remainder with silence
                memset(dst + filled, 128, PCM_BLOCK_SIZE - filled);
                pcm_playing = 0;
                return 0;
            }
        }
        uint32_t avail = pcm_len - pcm_pos;
        uint32_t need  = PCM_BLOCK_SIZE - filled;
        uint32_t copy  = (avail < need) ? avail : need;
        memcpy(dst + filled, pcm_src + pcm_pos, copy);
        pcm_pos += copy;
        filled  += copy;
    }
    return 1;
}

// ─── Initialisation ───────────────────────────────────────────────────────────

void sb16_init(void) {
    // 1. Reset DSP
    outb(DSP_RESET, 1);
    for (volatile int i = 0; i < 10000; i++) {}
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

    sb16_present = 1;

    // Turn on speaker
    dsp_write(0xD1);

    // Allocate legacy 4 KB double-buffer (for music.c / legacy API)
    audio_buffer = (uint8_t*)pmm_alloc_frame();
    for (uint32_t i = 0; i < AUDIO_BUFFER_SIZE; i++)
        audio_buffer[i] = 128;

    // Register IRQ 5 handler
    register_interrupt_handler(32 + 5, sb16_handler);
}

// ─── Legacy double-buffer API ─────────────────────────────────────────────────

void sb16_set_sample_rate(uint16_t hz) {
    dsp_write(0x41);
    dsp_write((uint8_t)(hz >> 8));
    dsp_write((uint8_t)(hz & 0xFF));
}

void sb16_start_playback(void) {
    current_half = 0;
    sb16_needs_data = 1;
    sb16_next_buffer = audio_buffer + HALF_BUFFER_SIZE;

    uint32_t phys = (uint32_t)audio_buffer;
    uint8_t  page   = (uint8_t)((phys >> 16) & 0xFF);
    uint16_t offset = (uint16_t)(phys & 0xFFFF);
    uint16_t len    = AUDIO_BUFFER_SIZE - 1;

    outb(0x0A, 5);
    outb(0x0C, 0);
    outb(0x0B, 0x59);
    outb(0x02, (uint8_t)(offset & 0xFF));
    outb(0x02, (uint8_t)(offset >> 8));
    outb(0x83, page);
    outb(0x03, (uint8_t)(len & 0xFF));
    outb(0x03, (uint8_t)(len >> 8));
    outb(0x0A, 1);

    dsp_write(0x48);
    dsp_write((uint8_t)((HALF_BUFFER_SIZE - 1) & 0xFF));
    dsp_write((uint8_t)((HALF_BUFFER_SIZE - 1) >> 8));
    dsp_write(0x1C);
}

void sb16_stop_playback(void) {
    dsp_write(0xDA);
}

// ─── IRQ 5 handler ────────────────────────────────────────────────────────────

void sb16_handler(struct registers* regs) {
    (void)regs;
    inb(SB16_BASE + 0xE);  // Acknowledge DSP interrupt

    if (pcm_playing) {
        // Auto-init DMA: hardware is now playing the other half.
        // Fill the half that just finished.
        current_half = 1 - current_half;
        fill_half(current_half);
        if (!pcm_playing) {
            // Reached end; issue halt
            dsp_write(0xDA);
        }
    } else {
        // Legacy double-buffer path
        current_half = 1 - current_half;
        if (current_half == 0)
            sb16_next_buffer = audio_buffer + HALF_BUFFER_SIZE;
        else
            sb16_next_buffer = audio_buffer;
        sb16_needs_data = 1;
    }
}

// ─── Simple PCM playback API ──────────────────────────────────────────────────

int sb16_play_pcm(const uint8_t* samples, uint32_t num_samples, int loop) {
    if (!sb16_present) return 0;
    if (!samples || num_samples == 0) return 0;

    // Stop any current playback
    if (pcm_playing) {
        dsp_write(0xDA);
        pcm_playing = 0;
    }

    // Set state
    pcm_src     = samples;
    pcm_len     = num_samples;
    pcm_pos     = 0;
    pcm_loop    = loop;
    pcm_playing = 1;

    // Pre-fill both halves of the DMA buffer
    fill_half(0);
    fill_half(1);

    // Program DMA for the full buffer (auto-init will cycle through both halves)
    uint32_t phys = (uint32_t)dma_buf;
    dma_program(phys, PCM_BUF_SIZE);

    // Set time constant for 22050 Hz: TC = 256 - (1000000 / 22050) ≈ 211
    dsp_write(0x40);
    dsp_write((uint8_t)(256 - (1000000 / 22050)));

    // Set block size to half the buffer (IRQ fires after each half)
    uint16_t block = PCM_BLOCK_SIZE - 1;
    dsp_write(0x48);
    dsp_write((uint8_t)(block & 0xFF));
    dsp_write((uint8_t)(block >> 8));

    // Start 8-bit auto-init DMA playback
    dsp_write(0x1C);

    current_half = 0;
    return 1;
}

void sb16_stop(void) {
    if (!sb16_present) return;
    pcm_playing = 0;
    pcm_src     = 0;
    dsp_write(0xDA);  // Halt DMA
}

int sb16_is_playing(void) {
    return pcm_playing;
}
