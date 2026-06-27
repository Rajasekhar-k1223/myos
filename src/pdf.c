#include "pdf.h"
#include "wm.h"
#include "tar.h"
#include "kheap.h"
#include "string.h"

int sprintf(char* str, const char* fmt, ...);

#define PDF_MAX_LINES   512
#define PDF_LINE_LEN    80
#define LINE_H          14
#define MARGIN_X        8
#define MARGIN_Y        20   /* below title strip */

/* A4 at 72 dpi = 612 x 792 points */
#define PDF_PAGE_W_PT   612.0f
#define PDF_PAGE_H_PT   792.0f

static window_t* pdf_win = NULL;

/* Each extracted text item carries a pixel position */
typedef struct {
    char    text[PDF_LINE_LEN];
    int     px;   /* screen x (pixels) */
    int     py;   /* screen y (pixels) */
} pdf_line_t;

static pdf_line_t pdf_lines[PDF_MAX_LINES];
static int   pdf_line_count = 0;
static int   pdf_scroll_y   = 0;
static char  pdf_filename[64];

/* ── simple float parser (no libc atof) ─────────────────────────── */
static float pdf_atof(const char* s) {
    float result = 0.0f;
    float sign   = 1.0f;
    if (*s == '-') { sign = -1.0f; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') result = result * 10.0f + (*s++ - '0');
    if (*s == '.') {
        s++;
        float frac = 0.1f;
        while (*s >= '0' && *s <= '9') {
            result += (*s++ - '0') * frac;
            frac *= 0.1f;
        }
    }
    return result * sign;
}

/* ── hex nibble ──────────────────────────────────────────────────── */
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* ── map PDF point coordinates to screen pixels ──────────────────── */
static void pdf_coord_to_screen(float pdf_x, float pdf_y,
                                 int win_w, int win_h,
                                 float fsize,
                                 int* sx_out, int* sy_out) {
    /* scale so that A4 height fits within the usable window area */
    int usable_h = win_h - MARGIN_Y - 4;
    float scale  = (usable_h > 0) ? ((float)usable_h / PDF_PAGE_H_PT) : 1.0f;
    /* PDF origin is bottom-left; screen origin is top-left — flip y */
    int sx = MARGIN_X + (int)(pdf_x * scale);
    int sy = MARGIN_Y + (int)((PDF_PAGE_H_PT - pdf_y - fsize) * scale);
    (void)win_w;
    if (sx_out) *sx_out = sx;
    if (sy_out) *sy_out = sy;
}

/* ── emit one positioned text item ──────────────────────────────── */
static void emit_text(const char* text, int tlen,
                      float cur_x, float cur_y, float fsize,
                      int win_w, int win_h) {
    if (pdf_line_count >= PDF_MAX_LINES) return;
    if (tlen <= 0) return;

    int sx, sy;
    pdf_coord_to_screen(cur_x, cur_y, win_w, win_h, fsize, &sx, &sy);

    pdf_line_t* ln = &pdf_lines[pdf_line_count++];
    int copy = (tlen < PDF_LINE_LEN - 1) ? tlen : (PDF_LINE_LEN - 1);
    for (int i = 0; i < copy; i++) {
        char ch = text[i];
        ln->text[i] = (ch >= 32 && ch < 127) ? ch : ' ';
    }
    ln->text[copy] = '\0';
    ln->px = sx;
    ln->py = sy;
}

/* ── parse one BT...ET content block ────────────────────────────── */
static void pdf_parse_block(const char* blk, int len, int win_w, int win_h) {
    float  stack[16];
    int    sp = 0;

    /* pending text string */
    static char ptext[PDF_LINE_LEN * 4];
    int         ptlen    = 0;
    int         has_text = 0;

    float cur_x   = 0.0f;
    float cur_y   = 0.0f;
    float leading = 0.0f;
    float fsize   = 12.0f;

    const char* p   = blk;
    const char* end = blk + len;

#define PSKIP_WS() \
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++

    while (p < end) {
        PSKIP_WS();
        if (p >= end) break;

        /* ── literal string ( ... ) ───────────────────────────── */
        if (*p == '(') {
            p++;
            ptlen = 0; has_text = 1;
            int depth = 1;
            while (p < end && depth > 0) {
                char ch = *p++;
                if (ch == '\\' && p < end) {
                    char esc = *p++;
                    char out;
                    if (esc == 'n')       out = '\n';
                    else if (esc == 'r')  out = '\r';
                    else if (esc == 't')  out = '\t';
                    else                  out = esc;
                    if (out >= 32 && out < 127 && ptlen < (int)(sizeof(ptext)-1))
                        ptext[ptlen++] = out;
                } else if (ch == '(') {
                    depth++;
                    if (ptlen < (int)(sizeof(ptext)-1)) ptext[ptlen++] = ch;
                } else if (ch == ')') {
                    depth--;
                    if (depth > 0 && ptlen < (int)(sizeof(ptext)-1))
                        ptext[ptlen++] = ch;
                } else {
                    if (ptlen < (int)(sizeof(ptext)-1)) ptext[ptlen++] = ch;
                }
            }
            ptext[ptlen] = '\0';
            continue;
        }

        /* ── array [ ... ] for TJ ─────────────────────────────── */
        if (*p == '[') {
            p++;
            ptlen = 0; has_text = 1;
            while (p < end && *p != ']') {
                PSKIP_WS();
                if (p >= end || *p == ']') break;
                if (*p == '(') {
                    p++;
                    int depth = 1;
                    while (p < end && depth > 0) {
                        char ch = *p++;
                        if (ch == '\\' && p < end) {
                            char esc = *p++;
                            if (esc >= 32 && esc < 127 && ptlen < (int)(sizeof(ptext)-1))
                                ptext[ptlen++] = esc;
                        } else if (ch == '(') {
                            depth++;
                            if (ptlen < (int)(sizeof(ptext)-1)) ptext[ptlen++] = ch;
                        } else if (ch == ')') {
                            depth--;
                            if (depth > 0 && ptlen < (int)(sizeof(ptext)-1))
                                ptext[ptlen++] = ch;
                        } else {
                            if (ptlen < (int)(sizeof(ptext)-1)) ptext[ptlen++] = ch;
                        }
                    }
                } else if (*p == '<') {
                    p++;
                    while (p + 1 < end && *p != '>') {
                        int hi = hex_val(*p), lo = hex_val(*(p+1));
                        if (hi >= 0 && lo >= 0) {
                            char ch = (char)((hi << 4) | lo);
                            if (ptlen < (int)(sizeof(ptext)-1)) ptext[ptlen++] = ch;
                            p += 2;
                        } else { p++; }
                    }
                    if (p < end && *p == '>') p++;
                } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
                    /* kerning adjustment number — skip */
                    while (p < end && (*p == '-' || *p == '.' ||
                                       (*p >= '0' && *p <= '9'))) p++;
                } else {
                    p++;
                }
            }
            if (p < end && *p == ']') p++;
            ptext[ptlen] = '\0';
            continue;
        }

        /* ── hex string < ... > ───────────────────────────────── */
        if (*p == '<') {
            /* skip PDF dictionaries << ... >> */
            if (p + 1 < end && *(p+1) == '<') { p += 2; continue; }
            p++;
            ptlen = 0; has_text = 1;
            while (p + 1 < end && *p != '>') {
                int hi = hex_val(*p), lo = hex_val(*(p+1));
                if (hi >= 0 && lo >= 0) {
                    char ch = (char)((hi << 4) | lo);
                    if (ptlen < (int)(sizeof(ptext)-1)) ptext[ptlen++] = ch;
                    p += 2;
                } else { p++; }
            }
            if (p < end && *p == '>') p++;
            ptext[ptlen] = '\0';
            continue;
        }

        /* ── number ───────────────────────────────────────────── */
        if (*p == '-' || *p == '+' || *p == '.' ||
            (*p >= '0' && *p <= '9')) {
            char num[32]; int ni = 0;
            while (p < end && ni < 31 &&
                   (*p == '-' || *p == '+' || *p == '.' ||
                    (*p >= '0' && *p <= '9') || *p == 'e' || *p == 'E'))
                num[ni++] = *p++;
            num[ni] = '\0';
            if (sp < 16) stack[sp++] = pdf_atof(num);
            continue;
        }

        /* ── /Name token ──────────────────────────────────────── */
        if (*p == '/') {
            p++;
            /* skip the name; Tf will use the numeric size from the stack */
            while (p < end && *p > ' ' && *p != '/' && *p != '[' &&
                   *p != '(' && *p != '<') p++;
            continue;
        }

        /* ── operator ─────────────────────────────────────────── */
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            *p == '\'' || *p == '"') {
            char op[8]; int oi = 0;
            while (p < end && oi < 7 &&
                   ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                    *p == '*' || *p == '\'' || *p == '"'))
                op[oi++] = *p++;
            op[oi] = '\0';

            /* Td / TD: tx ty operator → move text position */
            if ((op[0]=='T' && op[1]=='d' && op[2]=='\0') ||
                (op[0]=='T' && op[1]=='D' && op[2]=='\0')) {
                if (sp >= 2) {
                    cur_x += stack[sp-2];
                    cur_y += stack[sp-1];
                    if (op[1] == 'D') leading = -stack[sp-1];
                    sp -= 2;
                } else { sp = 0; }
            }
            /* Tm: a b c d tx ty Tm → set text matrix, use tx,ty */
            else if (op[0]=='T' && op[1]=='m' && op[2]=='\0') {
                if (sp >= 6) {
                    cur_x = stack[sp-2];
                    cur_y = stack[sp-1];
                    sp -= 6;
                } else if (sp >= 2) {
                    cur_x = stack[sp-2];
                    cur_y = stack[sp-1];
                    sp = 0;
                } else { sp = 0; }
            }
            /* T*: move to next line */
            else if (op[0]=='T' && op[1]=='*' && op[2]=='\0') {
                cur_x  = 0.0f;
                cur_y -= (leading > 0.0f) ? leading : (fsize * 1.2f);
            }
            /* Tf: /FontName size Tf → capture font size */
            else if (op[0]=='T' && op[1]=='f' && op[2]=='\0') {
                if (sp >= 1) {
                    fsize = stack[sp-1];
                    if (fsize < 4.0f)  fsize = 4.0f;
                    if (fsize > 96.0f) fsize = 96.0f;
                    sp = 0;
                }
            }
            /* TL: set leading */
            else if (op[0]=='T' && op[1]=='L' && op[2]=='\0') {
                if (sp >= 1) { leading = stack[sp-1]; sp--; }
            }
            /* Tj / TJ: show string */
            else if ((op[0]=='T' && op[1]=='j' && op[2]=='\0') ||
                     (op[0]=='T' && op[1]=='J' && op[2]=='\0')) {
                if (has_text && ptlen > 0)
                    emit_text(ptext, ptlen, cur_x, cur_y, fsize, win_w, win_h);
                has_text = 0; ptlen = 0; sp = 0;
            }
            /* ' operator: move to next line and show string */
            else if (op[0]=='\'' && op[1]=='\0') {
                cur_x  = 0.0f;
                cur_y -= (leading > 0.0f) ? leading : (fsize * 1.2f);
                if (has_text && ptlen > 0)
                    emit_text(ptext, ptlen, cur_x, cur_y, fsize, win_w, win_h);
                has_text = 0; ptlen = 0; sp = 0;
            }
            /* BT/ET inside block: reset */
            else if ((op[0]=='B' && op[1]=='T') ||
                     (op[0]=='E' && op[1]=='T')) {
                cur_x = cur_y = 0.0f; sp = 0;
            }
            /* unknown operator: flush stack */
            else {
                sp = 0;
            }
            continue;
        }

        /* skip unrecognised character */
        p++;
    }
#undef PSKIP_WS
}

/* ── scan PDF data for BT...ET blocks ───────────────────────────── */
static void pdf_extract_text(const uint8_t* data, size_t size,
                              int win_w, int win_h) {
    pdf_line_count = 0;
    size_t i = 0;
    while (i < size) {
        /* Look for 'BT' preceded/followed by whitespace */
        if (i + 1 < size && data[i] == 'B' && data[i+1] == 'T' &&
            (i == 0 || data[i-1] <= ' ') &&
            (i + 2 >= size || data[i+2] <= ' ')) {
            i += 2;
            size_t blk_start = i;
            /* Find matching ET (also whitespace-bounded) */
            while (i + 1 < size) {
                if (data[i] == 'E' && data[i+1] == 'T' &&
                    (i == 0 || data[i-1] <= ' ') &&
                    (i + 2 >= size || data[i+2] <= ' '))
                    break;
                i++;
            }
            size_t blk_len = i - blk_start;
            if (blk_len > 0)
                pdf_parse_block((const char*)(data + blk_start), (int)blk_len,
                                win_w, win_h);
            if (i + 1 < size) i += 2; /* skip ET */
        } else {
            i++;
        }
    }
}

/* ── rendering ──────────────────────────────────────────────────── */

static void pdf_render(void) {
    if (!pdf_win) return;
    int ww = (int)pdf_win->w;
    int wh = (int)pdf_win->h;

    /* Background */
    for (int i = 0; i < ww * wh; i++) pdf_win->buffer[i] = 0x1C1C2C;

    /* Title bar strip */
    for (int x = 0; x < ww; x++)
        pdf_win->buffer[x] = 0x2A2A4A;
    char title_str[80];
    sprintf(title_str, "PDF: %s  [PgUp/PgDn or j/k to scroll]", pdf_filename);
    wm_draw_string_window(pdf_win, 4, 1, title_str, 0xCCDDFF);

    /* Draw each positioned text item, offset by scroll */
    for (int li = 0; li < pdf_line_count; li++) {
        int sy = pdf_lines[li].py - pdf_scroll_y;
        if (sy < MARGIN_Y || sy > wh - 4) continue;
        wm_draw_string_window(pdf_win,
                              (uint32_t)pdf_lines[li].px,
                              (uint32_t)sy,
                              pdf_lines[li].text,
                              0xDDDDDD);
    }

    /* Scroll indicator */
    {
        char pg[32];
        sprintf(pg, "Scroll %d", pdf_scroll_y);
        wm_draw_string_window(pdf_win, (uint32_t)(ww - 100), 1, pg, 0x8888AA);
    }

    wm_request_redraw();
}

/* ── public API ─────────────────────────────────────────────────── */

void pdf_init(window_t* win, const char* filename) {
    pdf_win      = win;
    pdf_scroll_y = 0;
    pdf_line_count = 0;

    strncpy(pdf_filename, filename, 63);
    pdf_filename[63] = '\0';

    size_t   size = 0;
    uint8_t* data = (uint8_t*)tar_get_file(filename, &size);

    if (!data || size == 0) {
        strncpy(pdf_lines[0].text, "Error: File not found.", PDF_LINE_LEN-1);
        pdf_lines[0].px = MARGIN_X;
        pdf_lines[0].py = MARGIN_Y + 10;
        pdf_line_count  = 1;
        pdf_render();
        return;
    }

    if (size < 4 || !(data[0]=='%' && data[1]=='P' &&
                      data[2]=='D' && data[3]=='F')) {
        strncpy(pdf_lines[0].text,
                "Error: Not a valid PDF file (missing %PDF header).",
                PDF_LINE_LEN-1);
        pdf_lines[0].px = MARGIN_X;
        pdf_lines[0].py = MARGIN_Y + 10;
        pdf_line_count  = 1;
        pdf_render();
        return;
    }

    pdf_extract_text(data, size, (int)win->w, (int)win->h);

    if (pdf_line_count == 0) {
        strncpy(pdf_lines[0].text,
                "(No readable text found in this PDF.)",
                PDF_LINE_LEN-1);
        pdf_lines[0].text[PDF_LINE_LEN-1] = '\0';
        pdf_lines[0].px = MARGIN_X;
        pdf_lines[0].py = MARGIN_Y + 10;
        pdf_line_count  = 1;
    }

    pdf_render();
}

void pdf_handle_key(window_t* win, char c) {
    if (!pdf_win || win != pdf_win) return;

    int scroll_step = LINE_H * 3;
    int page_step   = (int)pdf_win->h - MARGIN_Y - 20;
    if (page_step < 1) page_step = 1;

    if (c == 'j' || c == '\x11') {          /* down */
        pdf_scroll_y += scroll_step;
    } else if (c == 'k' || c == '\x10') {   /* up */
        pdf_scroll_y -= scroll_step;
        if (pdf_scroll_y < 0) pdf_scroll_y = 0;
    } else if (c == 17) {                   /* Ctrl+Q = Page Down */
        pdf_scroll_y += page_step;
    } else if (c == 18) {                   /* Ctrl+P = Page Up */
        pdf_scroll_y -= page_step;
        if (pdf_scroll_y < 0) pdf_scroll_y = 0;
    } else if (c == 'G') {                  /* go to end */
        int max_py = 0;
        for (int i = 0; i < pdf_line_count; i++)
            if (pdf_lines[i].py > max_py) max_py = pdf_lines[i].py;
        pdf_scroll_y = max_py - (int)pdf_win->h + MARGIN_Y + 20;
        if (pdf_scroll_y < 0) pdf_scroll_y = 0;
    } else if (c == 'g') {                  /* go to top */
        pdf_scroll_y = 0;
    } else {
        return;
    }

    pdf_render();
}
