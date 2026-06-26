#include "music.h"
#include "sb16.h"
#include "fat16.h"
#include "string.h"
#include "kheap.h"
#include "vesa.h"
#include "kernel.h"

static window_t* music_win = 0;
static int is_playing = 0;
static uint32_t file_offset = 0;
static uint32_t data_size = 0;
static uint32_t data_start = 0;
static uint8_t* wav_buffer = 0;
static uint32_t wav_size = 0;

static void music_draw_ui(void) {
    if (!music_win) return;
    
    // Clear bg
    for (uint32_t i = 0; i < music_win->w * music_win->h; i++) {
        music_win->buffer[i] = 0x222222;
    }
    
    // Title
    wm_draw_string_window(music_win, 20, 20, "ElseaOS Music Player", 0xFFFFFF);
    
    // Status
    if (data_size == 0) {
        wm_draw_string_window(music_win, 20, 60, "No music.wav found.", 0xFF5555);
    } else {
        wm_draw_string_window(music_win, 20, 60, "File: music.wav", 0x55FF55);
        
        char progress[64];
        sprintf(progress, "Progress: %d / %d", file_offset - data_start, data_size);
        wm_draw_string_window(music_win, 20, 90, progress, 0xAAAAAA);
    }
    
    // Buttons
    // Play button
    for (int y = 140; y < 170; y++) {
        for (int x = 20; x < 100; x++) {
            music_win->buffer[y * music_win->w + x] = 0x338833;
        }
    }
    wm_draw_string_window(music_win, 45, 147, "Play", 0xFFFFFF);
    
    // Stop button
    for (int y = 140; y < 170; y++) {
        for (int x = 120; x < 200; x++) {
            music_win->buffer[y * music_win->w + x] = 0x883333;
        }
    }
    wm_draw_string_window(music_win, 145, 147, "Stop", 0xFFFFFF);
    
    wm_request_redraw();
}

void music_init(window_t* win) {
    music_win = win;
    is_playing = 0;
    
    // Load whole music.wav into memory
    // Assuming max size ~1MB for a short sound clip
    wav_size = 1024 * 1024;
    wav_buffer = (uint8_t*)kmalloc(wav_size);
    if (!wav_buffer) {
        data_size = 0;
        music_draw_ui();
        return;
    }
    
    int r = fat16_read_file("music.wav", wav_buffer, wav_size);
    if (r > 44) {
        uint8_t* header = wav_buffer;
        if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F') {
            uint32_t sample_rate = *(uint32_t*)(&header[24]);
            data_size = *(uint32_t*)(&header[40]);
            data_start = 44;
            file_offset = data_start;
            sb16_set_sample_rate(sample_rate);
        } else {
            // Raw PCM assumption
            data_size = r;
            data_start = 0;
            file_offset = 0;
            sb16_set_sample_rate(8000); // default
        }
    } else {
        data_size = 0;
    }
    
    music_draw_ui();
}

void music_handle_click(window_t* win, int mx, int my) {
    if (win != music_win) return;
    
    int rel_x = mx - win->x;
    int rel_y = my - (win->y + 20); // account for title bar
    
    if (rel_y >= 140 && rel_y <= 170) {
        if (rel_x >= 20 && rel_x <= 100) {
            // Play
            if (data_size > 0 && !is_playing) {
                is_playing = 1;
                file_offset = data_start;
                sb16_start_playback();
            }
        } else if (rel_x >= 120 && rel_x <= 200) {
            // Stop
            if (is_playing) {
                is_playing = 0;
                sb16_stop_playback();
            }
        }
    }
    music_draw_ui();
}

void music_process_audio(void) {
    if (!is_playing) return;
    
    if (sb16_needs_data) {
        sb16_needs_data = 0;
        
        uint32_t bytes_to_read = 2048; // HALF_BUFFER_SIZE
        if (file_offset + bytes_to_read > data_start + data_size) {
            is_playing = 0;
            sb16_stop_playback();
            music_draw_ui();
            return;
        }
        
        // Copy to DMA buffer
        memcpy((void*)sb16_next_buffer, wav_buffer + file_offset, bytes_to_read);
        file_offset += bytes_to_read;
        
        static int ui_counter = 0;
        if (ui_counter++ > 10) {
            music_draw_ui();
            ui_counter = 0;
        }
    }
}
