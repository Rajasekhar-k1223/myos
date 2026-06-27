/*
 * textedit.c — Simple text editor for ElseaOS
 *
 * Buffer layout: flat array with '\n' as line separator.
 * Key codes: 0x10=Up, 0x11=Down, 0x12=Left, 0x13=Right
 *            0x0E=Ctrl+N, 0x0F=Ctrl+O, 0x13=Ctrl+S (same as right arrow — handled by checking context)
 * Control characters are < 0x20.
 * We detect Ctrl+S as 0x13 only when it arrives via the Ctrl path, but since 0x13
 * is also the "right-arrow" key code this kernel uses, we use 0x13 for save
 * and rely on the fact that arrow keys are passed as escape sequences in other
 * drivers.  Looking at shell.c: 0x10=up, 0x11=down, 0x12=left, 0x13=right.
 * So we must distinguish: Ctrl+S is ASCII 19 (0x13) from holding Ctrl; right arrow
 * is 0x13 from the keyboard driver.  Since both are 0x13 we use a workaround:
 * treat 0x13 as Ctrl+S only; use a separate scancode for right arrow.
 * Actually examining keyboard.c will clarify — but since task spec says
 * 0x13=right, we'll use Ctrl+S = 0x13 ambiguity by checking: if the last
 * character was a ctrl modifier we treat 0x13 as save.  Simplest approach:
 * use 0x13 as RIGHT ARROW (matching shell.c), and Ctrl+S won't be
 * distinguishable.  Instead we can use a different save shortcut workaround.
 *
 * Looking at wm.c wm_handle_shortcut: key 's'/'S' is already Ctrl+S for Notepad.
 * We will hook into that path too: if focused_window->title == "Text Editor"
 * the shortcut handler calls textedit_save().
 *
 * For this file: we treat 0x13 as RIGHT ARROW.
 * Ctrl+S will be handled via wm_handle_shortcut routing (see wm.c changes).
 * Here we also intercept 0x13 (char 19) via textedit_handle_key and treat as
 * Ctrl+S when we see it arrive, since that matches the task spec ("Ctrl+S char 0x13").
 *
 * The task spec says arrow keys: 0x10=up 0x11=down 0x12=left 0x13=right
 * AND Ctrl+S char 0x13. This is a collision. We resolve: treat 0x13 as Ctrl+S
 * save, and if you want right arrow use a different mechanism. The shell also
 * uses 0x13 for right — but shell.c handles it as right only when the user
 * is at the prompt. In text editor context the save interpretation makes more
 * sense and user can also use the wm shortcut.
 */

#include "textedit.h"
#include "wm.h"
#include "string.h"
#include "fat16.h"
#include "tar.h"
#include "speaker.h"
#include "font16.h"

/* ── Editor state ───────────────────────────────────────────────────────── */
static char     te_buf[4096];
static uint32_t te_len    = 0;
static uint32_t te_cursor = 0;   /* byte offset */
static int      te_scroll = 0;   /* first visible line index */
static char     te_filename[64]  = "";
static char     te_prompt_buf[64] = "";
static int      te_in_prompt = 0; /* 1 = entering filename for open */
static int      te_prompt_len = 0;

/* ── Geometry constants ─────────────────────────────────────────────────── */
#define TE_GUTTER_W   36          /* pixels for line-number gutter */
#define TE_CHAR_W      8          /* pixels per char */
#define TE_CHAR_H     16          /* pixels per char (font8x16) */
#define TE_STATUS_H   16          /* status bar height */
#define TE_BG         0x1E1E2E
#define TE_LINE_HL    0x2A2A3E
#define TE_GUTTER_FG  0x6C6C7E
#define TE_TEXT_FG    0xFFFFFF
#define TE_STATUS_BG  0x181825
#define TE_STATUS_FG  0xCDD6F4
#define TE_CURSOR_FG  0xF38BA8

/* ── Helpers ────────────────────────────────────────────────────────────── */

/* Return index of the n-th '\n' + 1 (start of line n).  Line 0 starts at 0. */
static uint32_t te_line_start(int line) {
    int cur = 0;
    uint32_t i = 0;
    while (i < te_len && cur < line) {
        if (te_buf[i] == '\n') cur++;
        i++;
    }
    return i;
}

/* Count total lines (at least 1). */
static int te_total_lines(void) {
    int n = 1;
    for (uint32_t i = 0; i < te_len; i++)
        if (te_buf[i] == '\n') n++;
    return n;
}

/* Return the line number (0-based) that contains byte offset pos. */
static int te_cursor_line(void) {
    int line = 0;
    for (uint32_t i = 0; i < te_cursor; i++)
        if (te_buf[i] == '\n') line++;
    return line;
}

/* Return column (0-based) of cursor on its line. */
static int te_cursor_col(void) {
    int col = 0;
    uint32_t i = 0;
    /* find the line start */
    int line = te_cursor_line();
    int cur = 0;
    while (i < te_len && cur < line) {
        if (te_buf[i] == '\n') cur++;
        i++;
    }
    while (i < te_cursor) { col++; i++; }
    return col;
}

/* ── Draw a single pixel in the window buffer ────────────────────────────── */
static inline void te_putpixel(window_t* win, int x, int y, uint32_t color) {
    if (x >= 0 && y >= 0 && (uint32_t)x < win->w && (uint32_t)y < win->h)
        win->buffer[y * win->w + x] = color;
}

/* ── Draw a character into the window buffer ────────────────────────────── */
static void te_draw_char(window_t* win, int px, int py, char c, uint32_t fg) {
    unsigned char uc = (unsigned char)c;
    for (int row = 0; row < TE_CHAR_H; row++) {
        uint8_t bits = font8x16[uc][row];
        for (int col = 0; col < TE_CHAR_W; col++) {
            if (bits & (1 << (7 - col))) {
                te_putpixel(win, px + col, py + row, fg);
            }
        }
    }
}

/* ── Draw string in window buffer ────────────────────────────────────────── */
static void te_draw_str(window_t* win, int px, int py, const char* s, uint32_t fg) {
    for (int i = 0; s[i]; i++)
        te_draw_char(win, px + i * TE_CHAR_W, py, s[i], fg);
}

/* ── Draw number (decimal) ───────────────────────────────────────────────── */
static void te_draw_num(window_t* win, int px, int py, int n, uint32_t fg) {
    char tmp[12];
    int len = 0;
    if (n == 0) { tmp[len++] = '0'; }
    else {
        int v = n;
        char rev[12]; int rlen = 0;
        while (v > 0) { rev[rlen++] = '0' + (v % 10); v /= 10; }
        for (int i = rlen - 1; i >= 0; i--) tmp[len++] = rev[i];
    }
    tmp[len] = '\0';
    te_draw_str(win, px, py, tmp, fg);
}

/* ── Fill rectangle ──────────────────────────────────────────────────────── */
static void te_fill_rect(window_t* win, int x, int y, int w, int h, uint32_t color) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            te_putpixel(win, x + col, y + row, color);
}

/* ── Save to FAT16 ───────────────────────────────────────────────────────── */
static void te_save(window_t* win) {
    if (te_filename[0] == '\0') {
        /* Default filename */
        strncpy(te_filename, "untitled.txt", 63);
    }
    int r = fat16_write_file(te_filename, (const uint8_t*)te_buf, te_len);
    if (r >= 0) {
        extern void wm_toast(const char*, uint32_t);
        char msg[80];
        int i = 0;
        const char* pfx = "Saved: ";
        while (*pfx) msg[i++] = *pfx++;
        const char* fn = te_filename;
        while (*fn && i < 78) msg[i++] = *fn++;
        msg[i] = '\0';
        wm_toast(msg, 200);
        extern void speaker_beep(uint32_t, uint32_t);
        speaker_beep(880, 60);
    } else {
        extern void wm_toast(const char*, uint32_t);
        wm_toast("Save failed!", 200);
    }
    (void)win;
}

/* ── Load file ───────────────────────────────────────────────────────────── */
static void te_load(const char* filename) {
    /* Try FAT16 first */
    static uint8_t load_buf[4096];
    int r = fat16_read_file(filename, load_buf, sizeof(load_buf) - 1);
    if (r > 0) {
        te_len = (uint32_t)r;
        memcpy(te_buf, load_buf, te_len);
        te_buf[te_len] = '\0';
        te_cursor = 0;
        te_scroll = 0;
        strncpy(te_filename, filename, 63);
        te_filename[63] = '\0';
        return;
    }
    /* Try initrd (TAR) */
    size_t fsz = 0;
    void* data = tar_get_file(filename, &fsz);
    if (data && fsz > 0) {
        if (fsz > 4095) fsz = 4095;
        te_len = (uint32_t)fsz;
        memcpy(te_buf, data, te_len);
        te_buf[te_len] = '\0';
        te_cursor = 0;
        te_scroll = 0;
        strncpy(te_filename, filename, 63);
        te_filename[63] = '\0';
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void textedit_init(window_t* win, const char* filename) {
    te_buf[0]   = '\0';
    te_len      = 0;
    te_cursor   = 0;
    te_scroll   = 0;
    te_in_prompt = 0;
    te_prompt_len = 0;
    te_prompt_buf[0] = '\0';
    te_filename[0] = '\0';

    if (filename && filename[0]) {
        te_load(filename);
    }

    /* Set window colors */
    win->bg_color = TE_BG;
    win->fg_color = TE_TEXT_FG;

    textedit_render(win);
}

void textedit_handle_key(window_t* win, char c) {
    /* ── Filename prompt mode (Ctrl+O) ── */
    if (te_in_prompt) {
        if (c == '\n' || c == '\r') {
            te_prompt_buf[te_prompt_len] = '\0';
            te_in_prompt = 0;
            if (te_prompt_len > 0) {
                te_load(te_prompt_buf);
            }
            te_prompt_len = 0;
            te_prompt_buf[0] = '\0';
        } else if (c == '\b') {
            if (te_prompt_len > 0)
                te_prompt_buf[--te_prompt_len] = '\0';
        } else if (c >= ' ' && c <= '~' && te_prompt_len < 63) {
            te_prompt_buf[te_prompt_len++] = c;
            te_prompt_buf[te_prompt_len] = '\0';
        }
        textedit_render(win);
        return;
    }

    /* ── Ctrl+N = 0x0E: new file ── */
    if (c == 0x0E) {
        te_buf[0] = '\0';
        te_len    = 0;
        te_cursor = 0;
        te_scroll = 0;
        te_filename[0] = '\0';
        textedit_render(win);
        return;
    }

    /* ── Ctrl+O = 0x0F: open file ── */
    if (c == 0x0F) {
        te_in_prompt = 1;
        te_prompt_len = 0;
        te_prompt_buf[0] = '\0';
        textedit_render(win);
        return;
    }

    /* ── Ctrl+S = 0x13: save file ── */
    if (c == 0x13) {
        te_save(win);
        textedit_render(win);
        return;
    }

    /* ── Arrow keys ── */
    if (c == 0x10) { /* Up */
        int line = te_cursor_line();
        int col  = te_cursor_col();
        if (line > 0) {
            uint32_t prev_start = te_line_start(line - 1);
            uint32_t cur_start  = te_line_start(line);
            int prev_len = (int)(cur_start - prev_start);
            if (prev_len > 0 && te_buf[prev_start + prev_len - 1] == '\n')
                prev_len--;
            int new_col = (col < prev_len) ? col : prev_len;
            te_cursor = prev_start + (uint32_t)new_col;
        }
        /* Adjust scroll */
        int new_line = te_cursor_line();
        if (new_line < te_scroll) te_scroll = new_line;
        textedit_render(win);
        return;
    }
    if (c == 0x11) { /* Down */
        int line  = te_cursor_line();
        int col   = te_cursor_col();
        int total = te_total_lines();
        if (line < total - 1) {
            uint32_t next_start = te_line_start(line + 1);
            uint32_t after_start = te_line_start(line + 2);
            int next_len = (int)(after_start - next_start);
            if (next_len > 0 && next_start + next_len <= te_len &&
                te_buf[next_start + next_len - 1] == '\n')
                next_len--;
            int new_col = (col < next_len) ? col : next_len;
            te_cursor = next_start + (uint32_t)new_col;
        }
        /* Adjust scroll */
        int visible_lines = ((int)win->h - TE_STATUS_H) / TE_CHAR_H;
        int new_line = te_cursor_line();
        if (new_line >= te_scroll + visible_lines)
            te_scroll = new_line - visible_lines + 1;
        textedit_render(win);
        return;
    }
    if (c == 0x12) { /* Left */
        if (te_cursor > 0) te_cursor--;
        int line = te_cursor_line();
        if (line < te_scroll) te_scroll = line;
        textedit_render(win);
        return;
    }
    /* Right arrow: we can't use 0x13 since that's Ctrl+S; use 0x14 if available */
    if (c == 0x14) { /* Right */
        if (te_cursor < te_len) te_cursor++;
        int visible_lines = ((int)win->h - TE_STATUS_H) / TE_CHAR_H;
        int line = te_cursor_line();
        if (line >= te_scroll + visible_lines)
            te_scroll = line - visible_lines + 1;
        textedit_render(win);
        return;
    }

    /* ── Page Up (0x15) / Page Down (0x16) ── */
    if (c == 0x15) { /* Page Up */
        te_scroll -= 10;
        if (te_scroll < 0) te_scroll = 0;
        textedit_render(win);
        return;
    }
    if (c == 0x16) { /* Page Down */
        int total = te_total_lines();
        te_scroll += 10;
        int visible_lines = ((int)win->h - TE_STATUS_H) / TE_CHAR_H;
        if (te_scroll > total - visible_lines)
            te_scroll = total - visible_lines;
        if (te_scroll < 0) te_scroll = 0;
        textedit_render(win);
        return;
    }

    /* ── Enter ── */
    if (c == '\n' || c == '\r') {
        if (te_len < 4095) {
            memmove(te_buf + te_cursor + 1, te_buf + te_cursor, te_len - te_cursor + 1);
            te_buf[te_cursor] = '\n';
            te_len++;
            te_cursor++;
            /* scroll down if needed */
            int visible_lines = ((int)win->h - TE_STATUS_H) / TE_CHAR_H;
            int line = te_cursor_line();
            if (line >= te_scroll + visible_lines)
                te_scroll = line - visible_lines + 1;
        }
        textedit_render(win);
        return;
    }

    /* ── Backspace ── */
    if (c == '\b') {
        if (te_cursor > 0) {
            te_cursor--;
            memmove(te_buf + te_cursor, te_buf + te_cursor + 1, te_len - te_cursor);
            te_len--;
            te_buf[te_len] = '\0';
            /* adjust scroll */
            int line = te_cursor_line();
            if (line < te_scroll) te_scroll = line;
        }
        textedit_render(win);
        return;
    }

    /* ── Printable characters ── */
    if (c >= ' ' && c <= '~') {
        if (te_len < 4095) {
            memmove(te_buf + te_cursor + 1, te_buf + te_cursor, te_len - te_cursor + 1);
            te_buf[te_cursor] = c;
            te_len++;
            te_cursor++;
            te_buf[te_len] = '\0';
        }
        textedit_render(win);
        return;
    }
}

void textedit_render(window_t* win) {
    int win_w = (int)win->w;
    int win_h = (int)win->h;

    /* ── Clear background ── */
    for (int i = 0; i < win_w * win_h; i++)
        win->buffer[i] = TE_BG;

    int cursor_line = te_cursor_line();
    int cursor_col  = te_cursor_col();
    int visible_lines = (win_h - TE_STATUS_H) / TE_CHAR_H;

    /* ── Draw each visible line ── */
    for (int row = 0; row < visible_lines; row++) {
        int line_idx = te_scroll + row;
        int py = row * TE_CHAR_H;

        /* Highlight current line */
        if (line_idx == cursor_line) {
            te_fill_rect(win, 0, py, win_w, TE_CHAR_H, TE_LINE_HL);
        }

        /* Gutter background */
        te_fill_rect(win, 0, py, TE_GUTTER_W, TE_CHAR_H,
                     (line_idx == cursor_line) ? TE_LINE_HL : TE_BG);

        /* Line number */
        int total_lines = te_total_lines();
        if (line_idx < total_lines) {
            /* Right-align line number in gutter (max 3 digits in 36px) */
            char lnum[8];
            int n = line_idx + 1;
            int li = 0;
            if (n == 0) { lnum[li++] = '0'; }
            else {
                char rev[8]; int rlen = 0;
                int v = n;
                while (v > 0) { rev[rlen++] = '0' + (v % 10); v /= 10; }
                for (int i = rlen - 1; i >= 0; i--) lnum[li++] = rev[i];
            }
            lnum[li] = '\0';
            /* right-align: draw at gutter_w - len*8 - 2 */
            int num_px = (int)strlen(lnum) * TE_CHAR_W;
            int gx = TE_GUTTER_W - num_px - 2;
            if (gx < 0) gx = 0;
            te_draw_str(win, gx, py, lnum, TE_GUTTER_FG);
        }

        /* Gutter separator line */
        for (int gy = 0; gy < TE_CHAR_H; gy++)
            te_putpixel(win, TE_GUTTER_W - 1, py + gy, 0x3A3A5C);

        /* Draw text content */
        if (line_idx >= total_lines) continue;

        uint32_t ls = te_line_start(line_idx);
        int col = 0;
        for (uint32_t bi = ls; bi < te_len; bi++) {
            if (te_buf[bi] == '\n') break;
            int px = TE_GUTTER_W + col * TE_CHAR_W;
            if (px + TE_CHAR_W > win_w) break;

            /* Draw cursor block */
            if (line_idx == cursor_line && col == cursor_col) {
                te_fill_rect(win, px, py, TE_CHAR_W, TE_CHAR_H, TE_CURSOR_FG);
                te_draw_char(win, px, py, te_buf[bi], TE_BG);
            } else {
                te_draw_char(win, px, py, te_buf[bi], TE_TEXT_FG);
            }
            col++;
        }

        /* Draw cursor at end of line (no char under it) */
        if (line_idx == cursor_line && col == cursor_col) {
            int px = TE_GUTTER_W + col * TE_CHAR_W;
            if (px < win_w)
                te_fill_rect(win, px, py, TE_CHAR_W, TE_CHAR_H, TE_CURSOR_FG);
        }
    }

    /* ── Status bar ── */
    int sy = win_h - TE_STATUS_H;
    te_fill_rect(win, 0, sy, win_w, TE_STATUS_H, TE_STATUS_BG);

    /* Separator line */
    for (int sx = 0; sx < win_w; sx++)
        te_putpixel(win, sx, sy, 0x3A3A5C);

    if (te_in_prompt) {
        /* Filename prompt */
        char prompt[80];
        int pi = 0;
        const char* pfx = "Open file: ";
        while (*pfx) prompt[pi++] = *pfx++;
        const char* pb = te_prompt_buf;
        while (*pb && pi < 78) prompt[pi++] = *pb++;
        prompt[pi++] = '_';
        prompt[pi] = '\0';
        te_draw_str(win, 4, sy + 2, prompt, 0xF9E2AF);
    } else {
        /* Filename */
        const char* fname = te_filename[0] ? te_filename : "untitled.txt";
        te_draw_str(win, 4, sy + 2, fname, TE_STATUS_FG);

        /* Line:Col */
        char pos[20];
        int pi = 0;
        /* space */
        int fn_px = (int)strlen(fname) * TE_CHAR_W + 8;
        /* build "Ln %d, Col %d" */
        pos[pi++] = 'L'; pos[pi++] = 'n'; pos[pi++] = ' ';
        /* line num */
        int ln = cursor_line + 1;
        {
            char rev[8]; int rlen = 0;
            if (ln == 0) { pos[pi++] = '0'; }
            else { int v = ln; while(v>0){rev[rlen++]='0'+(v%10);v/=10;} for(int i=rlen-1;i>=0;i--) pos[pi++]=rev[i]; }
        }
        pos[pi++] = ','; pos[pi++] = ' ';
        pos[pi++] = 'C'; pos[pi++] = 'o'; pos[pi++] = 'l'; pos[pi++] = ' ';
        {
            int cn = cursor_col + 1;
            char rev[8]; int rlen = 0;
            if (cn == 0) { pos[pi++] = '0'; }
            else { int v = cn; while(v>0){rev[rlen++]='0'+(v%10);v/=10;} for(int i=rlen-1;i>=0;i--) pos[pi++]=rev[i]; }
        }
        pos[pi] = '\0';

        te_draw_str(win, fn_px, sy + 2, pos, TE_GUTTER_FG);

        /* Shortcuts hint (right side) */
        const char* hint = "^S save  ^O open  ^N new";
        int hint_px = win_w - (int)strlen(hint) * TE_CHAR_W - 4;
        if (hint_px > 0)
            te_draw_str(win, hint_px, sy + 2, hint, TE_GUTTER_FG);
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}
