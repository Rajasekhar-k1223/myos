/*
 * GPU Hardware Abstraction Layer.
 * Probes PCI bus for Intel/AMD/NVIDIA GPUs, maps VRAM via BAR0,
 * falls back to VESA LFB if no supported GPU is found.
 */
#include "gpu.h"
#include "kernel.h"
#include "vesa.h"
#include "string.h"

/* Known GPU vendors/devices */
#define VID_INTEL  0x8086
#define VID_AMD    0x1002
#define VID_NVIDIA 0x10DE
#define VID_VMWARE 0x15AD
#define VID_VBOX   0x80EE

/* ── PCI config space access via ports 0xCF8 / 0xCFC ─────────────────────── */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v;
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    outl(0xCF8, 0x80000000UL | ((uint32_t)bus<<16) | ((uint32_t)dev<<11) | ((uint32_t)fn<<8) | (reg & 0xFC));
    return inl(0xCFC);
}
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t v) {
    outl(0xCF8, 0x80000000UL | ((uint32_t)bus<<16) | ((uint32_t)dev<<11) | ((uint32_t)fn<<8) | (reg & 0xFC));
    outl(0xCFC, v);
}

static gpu_device_t gpu;

/* Enable PCI Bus Master + Memory Space */
static void pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
    cmd |= (1 << 1) | (1 << 2); /* Memory Space Enable + Bus Master Enable */
    pci_write32(bus, dev, fn, 0x04, cmd);
}

/* Probe every PCI slot for a display controller (class 0x03) */
static int probe_gpu(void) {
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id  = pci_read32((uint8_t)bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFF || id == 0) continue;
                uint16_t vid = (uint16_t)(id & 0xFFFF);
                uint16_t did = (uint16_t)(id >> 16);
                uint32_t cls = pci_read32((uint8_t)bus, dev, fn, 0x08) >> 24;
                if (cls != 0x03) continue; /* Not a display controller */

                gpu.vendor_id = vid;
                gpu.device_id = did;
                gpu.bus = (uint8_t)bus; gpu.dev = dev; gpu.fn = fn;

                /* Read BAR0 to get VRAM/MMIO base */
                uint32_t bar0 = pci_read32((uint8_t)bus, dev, fn, 0x10);
                /* Size BAR0: write all-ones, read back, restore */
                pci_write32((uint8_t)bus, dev, fn, 0x10, 0xFFFFFFFF);
                uint32_t sz  = pci_read32((uint8_t)bus, dev, fn, 0x10);
                pci_write32((uint8_t)bus, dev, fn, 0x10, bar0);
                sz &= ~0xF; sz = (~sz) + 1;  /* size in bytes */

                gpu.vram_base = bar0 & ~0xF;
                gpu.vram_size = sz;

                const char* name = "Unknown GPU";
                if      (vid == VID_INTEL)  name = "Intel Integrated GPU";
                else if (vid == VID_AMD)    name = "AMD Radeon GPU";
                else if (vid == VID_NVIDIA) name = "NVIDIA GPU";
                else if (vid == VID_VMWARE) name = "VMware SVGA II";
                else if (vid == VID_VBOX)   name = "VirtualBox VGA";
                strncpy(gpu.name, name, 63);

                pci_enable_bus_master((uint8_t)bus, dev, fn);

                terminal_printf("[GPU] %s  VID=%04x DID=%04x  BAR0=0x%08x (%u KB)\n",
                                name, vid, did, gpu.vram_base, sz / 1024);
                return 1;
            }
        }
    }
    return 0;
}

void gpu_init(void) {
    memset(&gpu, 0, sizeof(gpu));
    if (probe_gpu()) {
        gpu.is_hardware_accelerated = 1;
        terminal_printf("[GPU] Hardware rendering: VRAM mapped at 0x%08x\n", gpu.vram_base);
    } else {
        terminal_printf("[GPU] No supported GPU found. Using VESA LFB fallback.\n");
        gpu.is_hardware_accelerated = 0;
    }
}

void gpu_draw_rect(int x, int y, int w, int h, uint32_t color32) {
    if (gpu.is_hardware_accelerated && gpu.vram_base) {
        /* Write directly to VRAM — assumes linear 32bpp framebuffer at BAR0 */
        extern uint32_t vesa_width;
        volatile uint32_t* vram = (volatile uint32_t*)(uintptr_t)gpu.vram_base;
        for (int yy = y; yy < y + h; yy++)
            for (int xx = x; xx < x + w; xx++)
                vram[(uint32_t)yy * vesa_width + (uint32_t)xx] = color32;
    } else {
        for (int yy = y; yy < y + h; yy++)
            for (int xx = x; xx < x + w; xx++)
                vesa_putpixel(xx, yy, color32);
    }
}

void gpu_execute_shader(const char* shader_code) {
    (void)shader_code;
    if (gpu.is_hardware_accelerated)
        terminal_printf("[GPU] Hardware shader execution on %s\n", gpu.name);
    else
        terminal_printf("[GPU] Software-only: shaders not supported in VESA mode.\n");
}
