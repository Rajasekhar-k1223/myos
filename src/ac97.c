/*
 * AC'97 / Intel HDA Audio Driver for ElseaOS
 *
 * Supports:
 *   - AC'97 (class 0x04, subclass 0x01): Intel 82801AA (vendor 0x8086, device 0x2415)
 *   - Intel HDA (class 0x04, subclass 0x03): detected but not fully implemented
 *
 * AC'97 register map:
 *   BAR0 (NAM) = Native Audio Mixer  — codec registers
 *   BAR1 (NABM) = Native Audio Bus Master — DMA control
 */

#include "ac97.h"
#include "pci.h"
#include "kernel.h"
#include "string.h"

/* ── I/O helpers ─────────────────────────────────────────────────────────── */

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t data) {
    asm volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t data) {
    asm volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ── NAM (mixer) register offsets ─────────────────────────────────────────── */
#define NAM_RESET       0x00   /* write 0 to reset */
#define NAM_MASTER_VOL  0x02   /* master volume */
#define NAM_PCM_VOL     0x18   /* PCM out volume */
#define NAM_SAMPLE_RATE 0x2C   /* PCM front DAC rate */

/* ── NABM (bus master) register offsets ──────────────────────────────────── */
#define NABM_PO_BDBAR   0x10   /* PCM Out Buffer Descriptor Base Address */
#define NABM_PO_CIV     0x14   /* Current Index Value (current entry) */
#define NABM_PO_LVI     0x15   /* Last Valid Index */
#define NABM_PO_SR      0x16   /* Status Register (16-bit) */
#define NABM_PO_PICB    0x18   /* Position in Current Buffer */
#define NABM_PO_CR      0x1B   /* Control Register */

/* PCM Out Control bits */
#define PO_CR_RPBM     0x01    /* Run / Pause Bus Master */
#define PO_CR_RR       0x02    /* Reset registers */
#define PO_CR_LVBIE    0x04    /* Last Valid Buffer Interrupt Enable */
#define PO_CR_FEIE     0x08    /* FIFO Error Interrupt Enable */
#define PO_CR_IOCE     0x10    /* Interrupt On Completion Enable */

/* Buffer Descriptor List entry (8 bytes each, up to 32 entries) */
typedef struct {
    uint32_t addr;      /* physical address of sample buffer */
    uint16_t samples;   /* number of samples in buffer */
    uint16_t flags;     /* BUP (0x4000) | IOC (0x8000) */
} __attribute__((packed)) ac97_bdl_entry_t;

#define AC97_BDL_ENTRIES    32
#define AC97_BUF_SIZE       0x8000   /* 32 KB per BDL buffer (power-of-2 required) */

/* ── Driver state ─────────────────────────────────────────────────────────── */

static int      ac97_present  = 0;
static uint16_t nam_base      = 0;
static uint16_t nabm_base     = 0;

static ac97_bdl_entry_t  bdl[AC97_BDL_ENTRIES] __attribute__((aligned(8)));
static uint8_t           ac97_dma_buf[AC97_BDL_ENTRIES * AC97_BUF_SIZE]
                         __attribute__((aligned(AC97_BUF_SIZE)));

static volatile int  ac97_playing = 0;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Spin until NAM codec is ready (bit 8 of NAM+0x26 = Primary Codec Ready) */
static void nam_wait_ready(void) {
    for (int i = 0; i < 100000; i++) {
        if (inw((uint16_t)(nam_base + 0x26)) & 0x0100) return;
    }
}

static void nam_write(uint8_t reg, uint16_t val) {
    outw((uint16_t)(nam_base + reg), val);
}

static uint16_t nam_read(uint8_t reg) {
    return inw((uint16_t)(nam_base + reg));
}

/* ── Init ─────────────────────────────────────────────────────────────────── */

int ac97_init(void) {
    pci_device_t dev;

    /* Try AC'97 (Multimedia Audio, subclass 0x01) */
    int found = pci_get_device_by_class(0x04, 0x01, 0x00, &dev);

    if (!found) {
        /* Try Intel HDA (subclass 0x03) */
        if (pci_get_device_by_class(0x04, 0x03, 0x00, &dev)) {
            terminal_printf("[AC97] Intel HDA found (vendor=%04x dev=%04x) — HDA too complex, falling back to SB16\n",
                dev.vendor, dev.device);
            return 0;
        }
        terminal_printf("[AC97] No AC'97 or HDA device found\n");
        return 0;
    }

    terminal_printf("[AC97] Found AC'97 device: vendor=%04x device=%04x bus=%u slot=%u\n",
        dev.vendor, dev.device, dev.bus, dev.slot);

    /* Enable PCI bus mastering + I/O space */
    pci_enable_bus_mastering(&dev);
    {
        /* Also enable I/O space bit (bit 0) in command register */
        uint16_t cmd = pci_read_config_16(dev.bus, dev.slot, dev.func, 0x04);
        cmd |= 0x0001; /* I/O Space Enable */
        pci_write_config_16(dev.bus, dev.slot, dev.func, 0x04, cmd);
    }

    /* Read BAR0 (NAM) and BAR1 (NABM) — I/O BARs, mask off low bits */
    uint32_t bar0 = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x10);
    uint32_t bar1 = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x14);

    nam_base  = (uint16_t)(bar0 & 0xFFFC);
    nabm_base = (uint16_t)(bar1 & 0xFFFC);

    terminal_printf("[AC97] NAM base=0x%x  NABM base=0x%x\n", nam_base, nabm_base);

    if (!nam_base || !nabm_base) {
        terminal_printf("[AC97] Invalid BARs — aborting\n");
        return 0;
    }

    /* Reset NABM PCM Out channel */
    outb((uint16_t)(nabm_base + NABM_PO_CR), PO_CR_RR);
    for (volatile int i = 0; i < 10000; i++) {}
    outb((uint16_t)(nabm_base + NABM_PO_CR), 0x00);

    /* Cold reset codec: write 0x0000 to NAM+0x00 */
    nam_write(NAM_RESET, 0x0000);
    for (volatile int i = 0; i < 100000; i++) {}

    /* Wait for codec ready */
    nam_wait_ready();

    /* Set master volume to max (0x0000 = max, 0x8000 = mute) */
    nam_write(NAM_MASTER_VOL, 0x0000);

    /* Set PCM out volume to max */
    nam_write(NAM_PCM_VOL, 0x0000);

    /* Set sample rate to 22050 Hz (if supported) */
    {
        uint16_t ext_audio = nam_read(0x28); /* Extended Audio ID */
        if (ext_audio & 0x0001) {
            /* Variable Rate Audio (VRA) supported */
            nam_write(0x2A, 0x0001); /* Enable VRA */
            nam_write(NAM_SAMPLE_RATE, 22050);
            uint16_t actual = nam_read(NAM_SAMPLE_RATE);
            terminal_printf("[AC97] Sample rate set to %u Hz\n", actual);
        } else {
            terminal_printf("[AC97] Fixed 48000 Hz rate (no VRA) — 22050 Hz resampling needed\n");
        }
    }

    ac97_present = 1;
    terminal_printf("[AC97] Initialized successfully\n");
    return 1;
}

/* ── Volume ───────────────────────────────────────────────────────────────── */

void ac97_set_volume(uint8_t vol) {
    if (!ac97_present) return;
    /* AC'97 volume: 0x00 = max, 0x3F = min per channel; 0x8000 = mute
     * Map vol 255 -> 0x0000, vol 0 -> 0x8000 (mute) */
    if (vol == 0) {
        nam_write(NAM_MASTER_VOL, 0x8000);
        nam_write(NAM_PCM_VOL,    0x8000);
    } else {
        /* Scale 1-255 → attenuation 63-0 */
        uint8_t att = (uint8_t)(63 - (((uint32_t)vol * 63) / 255));
        uint16_t reg = (uint16_t)((att << 8) | att);
        nam_write(NAM_MASTER_VOL, reg);
        nam_write(NAM_PCM_VOL,    reg);
    }
}

/* ── Playback ─────────────────────────────────────────────────────────────── */

int ac97_play_pcm(const uint8_t* samples, uint32_t num_samples) {
    if (!ac97_present) return 0;
    if (!samples || num_samples == 0) return 0;

    /* Stop any running DMA */
    outb((uint16_t)(nabm_base + NABM_PO_CR), 0x00);
    for (volatile int i = 0; i < 10000; i++) {}

    ac97_playing = 0;

    /* Build the Buffer Descriptor List.
     * We split the audio data across up to AC97_BDL_ENTRIES BDL slots.
     * Each slot can hold up to AC97_BUF_SIZE bytes.
     *
     * AC'97 BDL samples field counts 16-bit PCM samples.
     * Our input is 8-bit unsigned mono; we treat each byte as one sample
     * for the hardware (samples field = byte count / 2 rounded to even).
     */
    uint32_t remaining = num_samples;
    uint32_t offset    = 0;
    int      n_entries = 0;

    memset(bdl, 0, sizeof(bdl));

    while (remaining > 0 && n_entries < AC97_BDL_ENTRIES) {
        uint32_t chunk = remaining;
        if (chunk > AC97_BUF_SIZE) chunk = AC97_BUF_SIZE;

        uint8_t* dst = ac97_dma_buf + (uint32_t)n_entries * AC97_BUF_SIZE;
        memcpy(dst, samples + offset, chunk);

        /* Pad remainder of slot with silence (0x80 = unsigned 8-bit silence) */
        if (chunk < AC97_BUF_SIZE)
            memset(dst + chunk, 0x80, AC97_BUF_SIZE - chunk);

        bdl[n_entries].addr    = (uint32_t)dst;
        /* samples field = number of 16-bit PCM samples; chunk bytes / 2, even */
        bdl[n_entries].samples = (uint16_t)((chunk / 2) & 0xFFFE);
        bdl[n_entries].flags   = 0;

        offset    += chunk;
        remaining -= chunk;
        n_entries++;
    }

    if (n_entries == 0) return 0;

    /* Mark last entry with IOC (Interrupt On Completion) and BUP (Buffer Underrun Policy) */
    bdl[n_entries - 1].flags = 0x8000 | 0x4000; /* IOC | BUP */

    /* Write BDL base address to PCM Out BDL base register (NABM + 0x10) */
    outl((uint16_t)(nabm_base + NABM_PO_BDBAR), (uint32_t)bdl);

    /* Reset CIV to 0, set LVI to last valid entry index */
    outb((uint16_t)(nabm_base + NABM_PO_CIV), 0);
    outb((uint16_t)(nabm_base + NABM_PO_LVI), (uint8_t)(n_entries - 1));

    /* Clear status register (write 1s to clear writable bits) */
    outw((uint16_t)(nabm_base + NABM_PO_SR), 0x001C);

    /* Start DMA: set RPBM (Run/Pause Bus Master) bit in PCM Out control (NABM + 0x1B) */
    outb((uint16_t)(nabm_base + NABM_PO_CR), PO_CR_RPBM);

    ac97_playing = 1;
    terminal_printf("[AC97] DMA started: %d entries, %u samples\n", n_entries, num_samples);
    return 1;
}

/* ── Stop ─────────────────────────────────────────────────────────────────── */

void ac97_stop(void) {
    if (!ac97_present) return;
    outb((uint16_t)(nabm_base + NABM_PO_CR), 0x00);
    ac97_playing = 0;
}

/* ── Status ───────────────────────────────────────────────────────────────── */

int ac97_is_playing(void) {
    if (!ac97_present || !ac97_playing) return 0;

    /* Check PCM Out status register (NABM + 0x16):
     * bit 0 = DCH (DMA Controller Halted) — set when BDL exhausted */
    uint16_t sr = inw((uint16_t)(nabm_base + NABM_PO_SR));
    if (sr & 0x0001) {
        /* DCH set — DMA has halted (end of BDL) */
        ac97_playing = 0;
        return 0;
    }
    return 1;
}
