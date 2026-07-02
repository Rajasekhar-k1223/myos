#include "music.h"
#include "mixer.h"
#include "sb16.h"
#include "tar.h"
#include "string.h"
#include "kheap.h"
#include "vesa.h"
#include "kernel.h"
#include "pit.h"

/* ── Playlist ─────────────────────────────────────────────────────────────── */
static const char* playlist[] = {
    "song1.wav",
    "song2.wav",
    "song3.wav",
};
#define PLAYLIST_LEN 3

static int current_track = 0;

static window_t* music_win = 0;
static int is_playing = 0;
static int is_paused  = 0;
static uint32_t file_offset = 0;
static uint32_t data_size = 0;
static uint32_t data_start = 0;
static uint8_t* wav_buffer = 0;    /* pointer into tar (not heap) */
static uint32_t play_start_ticks = 0;

/* "now playing" status string shown in the UI */
static char now_playing[64] = "No track loaded";

/* ── Load track ───────────────────────────────────────────────────────────── */
static void music_load_track(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= PLAYLIST_LEN) idx = PLAYLIST_LEN - 1;
    current_track = idx;

    /* Stop anything already running */
    if (is_playing) {
        sb16_stop_playback();
        is_playing = 0;
        is_paused  = 0;
    }

    data_size  = 0;
    wav_buffer = 0;

    const char* fname = playlist[current_track];
    size_t sz = 0;
    void* ptr = tar_get_file(fname, &sz);

    if (!ptr || sz < 44) {
        /* File missing — show graceful message */
        strncpy(now_playing, "File not found: ", 63);
        strncat(now_playing, fname, 63 - strlen(now_playing));
        now_playing[63] = '\0';
        return;
    }

    wav_buffer = (uint8_t*)ptr;
    uint8_t* hdr = wav_buffer;

    if (hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F') {
        /* Scan RIFF chunks to find "data" — don't assume fixed offsets. */
        uint32_t sample_rate = (sz >= 28) ? *(uint32_t*)(hdr + 24) : 8000;
        sb16_set_sample_rate((uint16_t)sample_rate);
        /* Walk chunks starting after the fmt chunk */
        uint32_t pos = 12;
        data_size  = 0;
        data_start = 44; /* fallback */
        while (pos + 8 <= (uint32_t)sz) {
            uint32_t csize = *(uint32_t*)(hdr + pos + 4);
            if (hdr[pos]=='d' && hdr[pos+1]=='a' && hdr[pos+2]=='t' && hdr[pos+3]=='a') {
                data_start = pos + 8;
                data_size  = csize;
                break;
            }
            pos += 8 + csize;
        }
        /* Clamp to actual file size */
        if (data_start >= (uint32_t)sz) { data_start = 44; data_size = 0; }
        if (data_start + data_size > (uint32_t)sz) data_size = (uint32_t)sz - data_start;
    } else {
        /* Assume raw 8-bit PCM @ 8 kHz */
        data_size  = (uint32_t)sz;
        data_start = 0;
        sb16_set_sample_rate(8000);
    }

    file_offset = data_start;

    strncpy(now_playing, "Loaded: ", 63);
    strncat(now_playing, fname, 63 - strlen(now_playing));
    now_playing[63] = '\0';
}

/* ── Draw UI ──────────────────────────────────────────────────────────────── */
static void music_draw_ui(void) {
    if (!music_win) return;

    /* Clear background */
    for (uint32_t i = 0; i < music_win->w * music_win->h; i++)
        music_win->buffer[i] = 0x222222;

    /* Title */
    wm_draw_string_window(music_win, 20, 10, "ElseaOS Music Player", 0xFFFFFF);

    /* Track name / status */
    wm_draw_string_window(music_win, 20, 35, now_playing, 0x55AAFF);

    /* Playback state */
    const char* state_str = is_playing ? (is_paused ? "Paused" : "Playing") : "Stopped";
    wm_draw_string_window(music_win, 20, 55, state_str, is_playing ? 0x55FF55 : 0xFF5555);

    /* Progress bar area (only when loaded) */
    if (data_size > 0) {
        /* Estimate position from elapsed ticks:
         * PIT = 100 Hz, SB16 plays ~22050 bytes/sec → 220 bytes/tick */
        uint32_t bytes_played = 0;
        if (is_playing && !is_paused) {
            uint32_t elapsed_ticks = pit_get_ticks() - play_start_ticks;
            bytes_played = elapsed_ticks * 220;
            if (bytes_played > data_size) bytes_played = data_size;
        } else {
            bytes_played = file_offset > data_start ? file_offset - data_start : 0;
        }

        char prog[64];
        uint32_t pct = data_size > 0 ? (bytes_played * 100) / data_size : 0;
        sprintf(prog, "Progress: %u%% (%u/%u bytes)", pct, bytes_played, data_size);
        wm_draw_string_window(music_win, 20, 75, prog, 0xAAAAAA);

        /* Draw a simple progress bar */
        uint32_t bar_w = music_win->w - 40;
        uint32_t fill  = (bar_w * pct) / 100;
        for (uint32_t x = 20; x < 20 + bar_w; x++) {
            uint32_t col = (x < 20 + fill) ? 0x0066CC : 0x555555;
            if (98 < (int)music_win->h && 104 < (int)music_win->h) {
                music_win->buffer[98 * music_win->w + x] = col;
                music_win->buffer[99 * music_win->w + x] = col;
                music_win->buffer[100 * music_win->w + x] = col;
                music_win->buffer[101 * music_win->w + x] = col;
                music_win->buffer[102 * music_win->w + x] = col;
            }
        }
    }

    /* Track info */
    char track_info[32];
    sprintf(track_info, "Track %d / %d", current_track + 1, PLAYLIST_LEN);
    wm_draw_string_window(music_win, 20, 112, track_info, 0xCCCCCC);

    /* ── Buttons ── */
    /* |<  Prev  (x:20..80) */
    for (int y = 130; y < 155; y++)
        for (int x = 20; x < 80; x++)
            music_win->buffer[y * music_win->w + x] = 0x555599;
    wm_draw_string_window(music_win, 35, 136, "Prev", 0xFFFFFF);

    /* Play/Pause (x:90..170) */
    uint32_t pp_col = is_playing ? 0x886600 : 0x338833;
    for (int y = 130; y < 155; y++)
        for (int x = 90; x < 170; x++)
            music_win->buffer[y * music_win->w + x] = pp_col;
    wm_draw_string_window(music_win, 107, 136, is_playing && !is_paused ? "Pause" : "Play ", 0xFFFFFF);

    /* Stop (x:180..240) */
    for (int y = 130; y < 155; y++)
        for (int x = 180; x < 240; x++)
            music_win->buffer[y * music_win->w + x] = 0x883333;
    wm_draw_string_window(music_win, 197, 136, "Stop", 0xFFFFFF);

    /* Next >| (x:250..310) */
    for (int y = 130; y < 155; y++)
        for (int x = 250; x < 310; x++)
            music_win->buffer[y * music_win->w + x] = 0x555599;
    wm_draw_string_window(music_win, 265, 136, "Next", 0xFFFFFF);

    wm_request_redraw();
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void music_init(window_t* win) {
    music_win     = win;
    is_playing    = 0;
    is_paused     = 0;
    current_track = 0;

    music_load_track(0);
    music_draw_ui();
}

void music_handle_click(window_t* win, int mx, int my) {
    if (win != music_win) return;

    int rel_x = mx - (int)win->x;
    int rel_y = my - ((int)win->y + 20); /* subtract title-bar height */

    if (rel_y >= 130 && rel_y <= 155) {

        if (rel_x >= 20 && rel_x < 80) {
            /* ── Prev ── */
            int idx = current_track - 1;
            if (idx < 0) idx = PLAYLIST_LEN - 1;
            music_load_track(idx);
            if (data_size > 0) {
                is_playing       = 1;
                is_paused        = 0;
                play_start_ticks = pit_get_ticks();
                file_offset      = data_start;
                sb16_start_playback();
                strncpy(now_playing, "Playing: ", 63);
                strncat(now_playing, playlist[current_track], 63 - strlen(now_playing));
                now_playing[63] = '\0';
            }

        } else if (rel_x >= 90 && rel_x < 170) {
            /* ── Play / Pause ── */
            if (!is_playing) {
                /* Start playback */
                if (data_size > 0) {
                    is_playing       = 1;
                    is_paused        = 0;
                    play_start_ticks = pit_get_ticks();
                    file_offset      = data_start;
                    sb16_start_playback();
                    strncpy(now_playing, "Playing: ", 63);
                    strncat(now_playing, playlist[current_track], 63 - strlen(now_playing));
                    now_playing[63] = '\0';
                }
            } else if (!is_paused) {
                /* Pause */
                is_paused = 1;
                sb16_stop_playback();
                strncpy(now_playing, "Paused:  ", 63);
                strncat(now_playing, playlist[current_track], 63 - strlen(now_playing));
                now_playing[63] = '\0';
            } else {
                /* Resume */
                is_paused        = 0;
                play_start_ticks = pit_get_ticks();
                sb16_start_playback();
                strncpy(now_playing, "Playing: ", 63);
                strncat(now_playing, playlist[current_track], 63 - strlen(now_playing));
                now_playing[63] = '\0';
            }

        } else if (rel_x >= 180 && rel_x < 240) {
            /* ── Stop ── */
            if (is_playing) {
                sb16_stop_playback();
                is_playing = 0;
                is_paused  = 0;
            }
            file_offset = data_start;
            strncpy(now_playing, "Stopped: ", 63);
            strncat(now_playing, playlist[current_track], 63 - strlen(now_playing));
            now_playing[63] = '\0';

        } else if (rel_x >= 250 && rel_x < 310) {
            /* ── Next ── */
            int idx = (current_track + 1) % PLAYLIST_LEN;
            music_load_track(idx);
            if (data_size > 0) {
                is_playing       = 1;
                is_paused        = 0;
                play_start_ticks = pit_get_ticks();
                file_offset      = data_start;
                sb16_start_playback();
                strncpy(now_playing, "Playing: ", 63);
                strncat(now_playing, playlist[current_track], 63 - strlen(now_playing));
                now_playing[63] = '\0';
            }
        }
    }

    music_draw_ui();
}

void music_process_audio(void) {
    if (!is_playing || is_paused) return;

    if (sb16_needs_data) {
        sb16_needs_data = 0;

        uint32_t bytes_to_read = 2048; /* HALF_BUFFER_SIZE */

        if (!wav_buffer || file_offset + bytes_to_read > data_start + data_size) {
            /* End of track — stop and reset */
            sb16_stop_playback();
            is_playing = 0;
            is_paused  = 0;
            file_offset = data_start;
            strncpy(now_playing, "Finished: ", 63);
            strncat(now_playing, playlist[current_track], 63 - strlen(now_playing));
            now_playing[63] = '\0';
            music_draw_ui();
            return;
        }

        /* Instead of directly writing to sb16, use the mixer! Wait, mixer_tick() handles sb16.
           Actually, music.c should just let mixer.c do its thing, or mix into sb16_next_buffer.
           Let's manually mix since music.c is the foreground app. */
        for (uint32_t i = 0; i < bytes_to_read; i++) {
            ((uint8_t*)sb16_next_buffer)[i] = wav_buffer[file_offset + i];
        }
        file_offset += bytes_to_read;

        /* Refresh UI every ~10 callbacks */
        static int ui_counter = 0;
        if (++ui_counter >= 10) {
            music_draw_ui();
            ui_counter = 0;
        }
    }
}
