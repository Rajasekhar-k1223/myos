#include "installer.h"
#include "string.h"

// Simple busy loop delay for simulated install progression
static void installer_delay() {
    volatile uint32_t count = 0x8FFFFF;
    while(count--) { __asm__ volatile("pause"); }
}

// Ensure sprintf is declared
int sprintf(char *str, const char *format, ...);

void installer_run(void) {
    // Create a massive fullscreen-like window (assuming 1024x768 resolution)
    window_t* win = wm_create_window(0, 0, 1024, 768, "ElseaOS Graphic Installer");
    if (!win) return;
    
    // We overwrite the buffer directly for custom drawing, bypassing standard theme
    
    // --- LEFT SIDE: The Showcase (Placeholder for Phoenix Logo Video) ---
    for (int y = 0; y < 768; y++) {
        for (int x = 0; x < 512; x++) {
            // Draw a subtle cinematic gradient (Deep Space Blue)
            uint32_t r = 0x00;
            uint32_t g = 0x08;
            uint32_t b = 0x1A + (y / 4);
            if (b > 255) b = 255;
            win->buffer[y * 1024 + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
    
    wm_draw_string_window(win, 180, 300, "ELSEA OS", 0xFF00AAFF);
    wm_draw_string_window(win, 180, 330, "Fly Beyond Limits", 0xFFFFFFFF);
    wm_draw_string_window(win, 150, 380, "(Cinematic Phoenix Logo Playing...)", 0xFF888888);

    // --- RIGHT SIDE: The Installer Glassmorphic Panel ---
    for (int y = 0; y < 768; y++) {
        for (int x = 512; x < 1024; x++) {
            win->buffer[y * 1024 + x] = 0xFF181C25; // Dark Slate gray background
        }
    }
    
    // Main Panel Box Dimensions
    int px = 562, py = 100, pw = 412, ph = 568;
    for (int y = py; y < py+ph; y++) {
        for (int x = px; x < px+pw; x++) {
            // Semi-transparent box
            win->buffer[y * 1024 + x] = 0xFF282D3A;
            // Draw Border
            if (y == py || y == py+ph-1 || x == px || x == px+pw-1) {
                win->buffer[y * 1024 + x] = 0xFF444C60;
            }
        }
    }
    
    // Header text
    wm_draw_string_window(win, px + 30, py + 30, "ElseaOS - Setup", 0xFFAAAAAA);
    wm_draw_string_window(win, px + 30, py + 70, "Installation Progress", 0xFFFFFFFF);
    
    // Progress Bar Background Outline
    int bx = px + 30;
    int by = py + 150;
    int bw = 352;
    int bh = 20;
    for (int y = by; y < by+bh; y++) {
        for (int x = bx; x < bx+bw; x++) {
            win->buffer[y * 1024 + x] = 0xFF11151F; // Very dark inner shadow
        }
    }
    
    wm_draw_string_window(win, px + 30, py + 220, "Installing Components:", 0xFFCCCCCC);
    
    // Components being installed
    char* components[] = {
        "Core Kernel (14 MB)",
        "Global Fonts & Translations (1.1 GB)",
        "Hardware Firmware Blobs (800 MB)",
        "Web Engine / WebKit (350 MB)",
        "GPU Drivers / Mesa3D (400 MB)"
    };
    
    // Mini Terminal Log Box
    int tx = px + 30;
    int ty = py + 420;
    int tw = 352;
    int th = 120;
    for (int y = ty; y < ty+th; y++) {
        for (int x = tx; x < tx+tw; x++) {
            win->buffer[y * 1024 + x] = 0xFF080808; // Pitch black
            if (y == ty || y == ty+th-1 || x == tx || x == tx+tw-1) {
                win->buffer[y * 1024 + x] = 0xFF333333; // Border
            }
        }
    }

    char* logs[] = {
        "[09:42:01] Flashing Core OS... [OK]",
        "[09:42:08] Unpacking 1.1GB Fonts... [OK]",
        "[09:42:15] Flashing Firmware Blobs... [OK]",
        "[09:42:21] Compiling WebKit cache... [OK]",
        "[09:42:28] Linking Graphics Drivers... [OK]",
        "[09:42:35] Installation Complete! Rebooting..."
    };

    // Main Simulation Loop
    for (int step = 0; step <= 5; step++) {
        // Render checkboxes
        for (int i = 0; i < 5; i++) {
            if (i < step) {
                wm_draw_string_window(win, px + 30, py + 260 + (i*25), "[X]", 0xFF00FF00); // Green checked
                wm_draw_string_window(win, px + 60, py + 260 + (i*25), components[i], 0xFFFFFFFF);
            } else if (i == step) {
                wm_draw_string_window(win, px + 30, py + 260 + (i*25), "[-]", 0xFF00AAFF); // Blue active
                wm_draw_string_window(win, px + 60, py + 260 + (i*25), components[i], 0xFF00AAFF);
            } else {
                wm_draw_string_window(win, px + 30, py + 260 + (i*25), "[ ]", 0xFF555555); // Gray pending
                wm_draw_string_window(win, px + 60, py + 260 + (i*25), components[i], 0xFF555555);
            }
        }
        
        // Render Progress Text
        char pct[64];
        int percent = (step * 100) / 5;
        sprintf(pct, "Overall Progress: %d%% Complete", percent);
        
        // Clear old progress text area
        for (int y = py + 120; y < py+140; y++) {
            for (int x = px + 30; x < px+380; x++) win->buffer[y * 1024 + x] = 0xFF282D3A;
        }
        wm_draw_string_window(win, px + 30, py + 120, pct, 0xFFFFFFFF);
        
        // Fill Progress Bar
        int fill_w = (bw * percent) / 100;
        for (int y = by; y < by+bh; y++) {
            for (int x = bx; x < bx+fill_w; x++) {
                uint32_t color = 0xFF00AAFF + ((x - bx) / 2); // Blue to orange gradient effect
                win->buffer[y * 1024 + x] = color;
            }
        }
        
        // Render Terminal logs
        if (step > 0) {
            wm_draw_string_window(win, tx + 10, ty + 10 + ((step-1)*16), logs[step-1], 0xFF00FF00); // Hacker green
        }

        // Force screen refresh
        wm_request_redraw();
        
        // Wait to simulate long installation of massive files
        for (int d = 0; d < 30; d++) installer_delay();
    }
}
