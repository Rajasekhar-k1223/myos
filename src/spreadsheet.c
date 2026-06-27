#include "spreadsheet.h"
#include "wm.h"
#include "string.h"

int sprintf(char* str, const char* fmt, ...);

#define SS_COLS     8
#define SS_ROWS     20
#define CELL_W      72
#define CELL_H      18
#define ROW_HDR_W   24
#define COL_HDR_H   18
#define CONTENT_X   ROW_HDR_W
#define CONTENT_Y   COL_HDR_H

/* Per-spreadsheet state — one global instance */
static window_t* ss_win  = NULL;
static char  cells[SS_ROWS][SS_COLS][16];
static int   sel_row  = 0;
static int   sel_col  = 0;
static char  edit_buf[32];
static int   editing  = 0;

/* ── helpers ──────────────────────────────────────────────────── */

static int cell_to_int(int row, int col) {
    if (row < 0 || row >= SS_ROWS || col < 0 || col >= SS_COLS) return 0;
    const char* s = cells[row][col];
    if (!s[0]) return 0;
    int neg = 0, i = 0, v = 0;
    if (s[i] == '-') { neg = 1; i++; }
    for (; s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
    return neg ? -v : v;
}

/* Parse a cell reference like "A1" → col/row (0-based). Returns 1 on success. */
static int parse_cellref(const char* s, int* col, int* row) {
    if (*s < 'A' || *s > 'H') return 0;
    *col = *s - 'A'; s++;
    if (*s < '1' || *s > '9') return 0;
    *row = 0;
    while (*s >= '0' && *s <= '9') { *row = *row * 10 + (*s - '0'); s++; }
    (*row)--;
    return (*row >= 0 && *row < SS_ROWS);
}

/* Parse a range "A1:B5" → (c0,r0,c1,r1) 0-based. Returns 1 on success. */
static int parse_range(const char* s, int* c0, int* r0, int* c1, int* r1) {
    if (!parse_cellref(s, c0, r0)) return 0;
    while (*s && *s != ':') s++;
    if (*s != ':') return 0; s++;
    if (!parse_cellref(s, c1, r1)) return 0;
    if (*c0 > *c1) { int t = *c0; *c0 = *c1; *c1 = t; }
    if (*r0 > *r1) { int t = *r0; *r0 = *r1; *r1 = t; }
    return 1;
}

/* Parse integer or cell reference from p, advance p, return value. */
static int parse_val(const char** p) {
    const char* s = *p;
    while (*s == ' ') s++;
    if ((*s >= 'A' && *s <= 'H') && (*(s+1) >= '1' && *(s+1) <= '9')) {
        int c, r;
        if (parse_cellref(s, &c, &r)) {
            while (*s && (*s == '-' || (*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z'))) s++;
            *p = s;
            return cell_to_int(r, c);
        }
    }
    int neg = 0, v = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    *p = s;
    return neg ? -v : v;
}

/* Evaluate a formula stored in buf; result written into out (max 15 chars). */
static void eval_formula(const char* buf, char* out) {
    if (buf[0] != '=') { strncpy(out, buf, 15); out[15] = '\0'; return; }

    const char* p = buf + 1;
    while (*p == ' ') p++;

    /* Aggregate functions: SUM, AVG, MIN, MAX, COUNT */
    typedef struct { const char* name; int len; } fn_t;
    static const fn_t fns[] = {{"SUM",5},{"AVG",5},{"MIN",5},{"MAX",5},{"COUNT",7},{0,0}};
    for (int fi = 0; fns[fi].name; fi++) {
        const char* fn = fns[fi].name;
        int nl = (int)strlen(fn);
        int match = 1;
        for (int i = 0; i < nl && match; i++) if (p[i] != fn[i]) match = 0;
        if (!match || p[nl] != '(') continue;
        const char* rng = p + nl + 1;
        int c0, r0, c1, r1;
        if (!parse_range(rng, &c0, &r0, &c1, &r1)) goto bad;
        int sum = 0, cnt = 0, mn = 0x7FFFFFFF, mx = -0x7FFFFFFF - 1;
        int first = 1;
        for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) {
            const char* s = cells[r][c];
            if (!s[0]) continue;
            int v = cell_to_int(r, c);
            sum += v; cnt++;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            (void)first; first = 0;
        }
        if (fi == 0) sprintf(out, "%d", sum);
        else if (fi == 1) sprintf(out, cnt ? "%d" : "0", cnt ? sum/cnt : 0);
        else if (fi == 2) sprintf(out, cnt ? "%d" : "0", cnt ? mn : 0);
        else if (fi == 3) sprintf(out, cnt ? "%d" : "0", cnt ? mx : 0);
        else              sprintf(out, "%d", cnt);
        return;
    }

    /* IF(A1>0,B1,C1) — simple comparison */
    if (p[0]=='I' && p[1]=='F' && p[2]=='(') {
        const char* q = p + 3;
        int lhs = parse_val(&q);
        while (*q == ' ') q++;
        char op = *q; if (*q) q++;
        if (op == '>' && *q == '=') { op = 'G'; q++; }
        else if (op == '<' && *q == '=') { op = 'L'; q++; }
        else if (op == '!' && *q == '=') { op = 'N'; q++; }
        int rhs = parse_val(&q);
        while (*q == ' ' || *q == ',') q++;
        int v_true = parse_val(&q);
        while (*q == ' ' || *q == ',') q++;
        int v_false = parse_val(&q);
        int cond = 0;
        if (op == '>') cond = (lhs > rhs);
        else if (op == '<') cond = (lhs < rhs);
        else if (op == '=') cond = (lhs == rhs);
        else if (op == 'G') cond = (lhs >= rhs);
        else if (op == 'L') cond = (lhs <= rhs);
        else if (op == 'N') cond = (lhs != rhs);
        sprintf(out, "%d", cond ? v_true : v_false);
        return;
    }

    /* Cell arithmetic: =A1+B2, =A1*B2, =A1-B2, =42+A1 */
    {
        const char* q = p;
        int lhs = parse_val(&q);
        while (*q == ' ') q++;
        if (*q == '+' || *q == '-' || *q == '*' || *q == '/') {
            char op = *q; q++;
            int rhs = parse_val(&q);
            int res = 0;
            if (op == '+') res = lhs + rhs;
            else if (op == '-') res = lhs - rhs;
            else if (op == '*') res = lhs * rhs;
            else if (op == '/') res = rhs ? lhs / rhs : 0;
            sprintf(out, "%d", res);
            return;
        }
        /* Single cell reference or constant */
        sprintf(out, "%d", lhs);
        return;
    }
bad:
    strncpy(out, "#ERR", 15);
    out[15] = '\0';
}

/* ── render ───────────────────────────────────────────────────── */

static void spreadsheet_render(void) {
    if (!ss_win) return;
    int ww = (int)ss_win->w;
    int wh = (int)ss_win->h;

    /* Clear background */
    for (int i = 0; i < ww * wh; i++) ss_win->buffer[i] = 0x141420;

    /* Column headers (A-H) */
    for (int c = 0; c < SS_COLS; c++) {
        int px = CONTENT_X + c * CELL_W;
        for (int dy = 0; dy < COL_HDR_H && dy < wh; dy++)
            for (int dx = 0; dx < CELL_W && (px+dx) < ww; dx++)
                ss_win->buffer[dy * ww + (px+dx)] = 0x2A2A3A;
        /* grid line on right edge of header */
        for (int dy = 0; dy < COL_HDR_H && dy < wh; dy++)
            if ((px + CELL_W - 1) < ww)
                ss_win->buffer[dy * ww + (px + CELL_W - 1)] = 0x3A3A4A;
        char lbl[2] = { (char)('A' + c), '\0' };
        wm_draw_string_window(ss_win, (uint32_t)(px + CELL_W/2 - 3), 2, lbl, 0xAABBFF);
    }

    /* Row headers (1..20) */
    for (int r = 0; r < SS_ROWS; r++) {
        int py = CONTENT_Y + r * CELL_H;
        if (py >= wh) break;
        for (int dy = 0; dy < CELL_H && (py+dy) < wh; dy++)
            for (int dx = 0; dx < ROW_HDR_W && dx < ww; dx++)
                ss_win->buffer[(py+dy) * ww + dx] = 0x2A2A3A;
        /* bottom edge */
        if ((py + CELL_H - 1) < wh)
            for (int dx = 0; dx < ROW_HDR_W && dx < ww; dx++)
                ss_win->buffer[(py + CELL_H - 1) * ww + dx] = 0x3A3A4A;
        char lbl[8];
        sprintf(lbl, "%d", r+1);
        wm_draw_string_window(ss_win, 2, (uint32_t)(py+2), lbl, 0xAABBFF);
    }

    /* Cells */
    for (int r = 0; r < SS_ROWS; r++) {
        int py = CONTENT_Y + r * CELL_H;
        if (py >= wh) break;
        for (int c = 0; c < SS_COLS; c++) {
            int px = CONTENT_X + c * CELL_W;
            if (px >= ww) break;
            int selected = (r == sel_row && c == sel_col);

            /* Cell background */
            uint32_t bg = selected ? 0x1A3A6A : 0x1E1E2E;
            for (int dy = 0; dy < CELL_H && (py+dy) < wh; dy++)
                for (int dx = 0; dx < CELL_W && (px+dx) < ww; dx++)
                    ss_win->buffer[(py+dy)*ww + (px+dx)] = bg;

            /* Grid lines */
            for (int dy = 0; dy < CELL_H && (py+dy) < wh; dy++)
                if ((px+CELL_W-1) < ww)
                    ss_win->buffer[(py+dy)*ww + (px+CELL_W-1)] = 0x3A3A4A;
            for (int dx = 0; dx < CELL_W && (px+dx) < ww; dx++)
                if ((py+CELL_H-1) < wh)
                    ss_win->buffer[(py+CELL_H-1)*ww + (px+dx)] = 0x3A3A4A;

            /* Blue border for selected */
            if (selected) {
                for (int dx = 0; dx < CELL_W && (px+dx) < ww; dx++) {
                    if (py < wh)        ss_win->buffer[py*ww + (px+dx)]         = 0x4488FF;
                    if ((py+CELL_H-1) < wh) ss_win->buffer[(py+CELL_H-1)*ww + (px+dx)] = 0x4488FF;
                }
                for (int dy = 0; dy < CELL_H && (py+dy) < wh; dy++) {
                    if (px < ww)        ss_win->buffer[(py+dy)*ww + px]           = 0x4488FF;
                    if ((px+CELL_W-1) < ww) ss_win->buffer[(py+dy)*ww + (px+CELL_W-1)] = 0x4488FF;
                }
            }

            /* Text */
            char display[17];
            if (selected && editing) {
                strncpy(display, edit_buf, 15);
                display[15] = '\0';
                int l = strlen(display);
                if (l < 15) { display[l] = '_'; display[l+1] = '\0'; }
            } else if (cells[r][c][0] == '=') {
                eval_formula(cells[r][c], display);
            } else {
                strncpy(display, cells[r][c], 15);
                display[15] = '\0';
            }
            if (display[0])
                wm_draw_string_window(ss_win, (uint32_t)(px+2), (uint32_t)(py+2), display, 0xDDDDDD);
        }
    }

    /* Formula bar */
    int bar_y = CONTENT_Y + SS_ROWS * CELL_H + 4;
    if (bar_y < wh - 2) {
        char ref[4];
        ref[0] = (char)('A' + sel_col);
        sprintf(ref+1, "%d", sel_row+1);
        char bar[64];
        sprintf(bar, "%s: %s", ref, editing ? edit_buf : cells[sel_row][sel_col]);
        for (int dx = 0; dx < ww; dx++)
            if (bar_y < wh)
                ss_win->buffer[bar_y * ww + dx] = 0x111122;
        wm_draw_string_window(ss_win, 2, (uint32_t)bar_y, bar, 0x88AAFF);
    }

    wm_request_redraw();
}

/* ── public API ──────────────────────────────────────────────── */

void spreadsheet_init(window_t* win) {
    ss_win = win;
    sel_row = sel_col = 0;
    editing = 0;
    edit_buf[0] = '\0';
    for (int r = 0; r < SS_ROWS; r++)
        for (int c = 0; c < SS_COLS; c++)
            cells[r][c][0] = '\0';
    spreadsheet_render();
}

void spreadsheet_handle_key(window_t* win, char c) {
    if (!ss_win || win != ss_win) return;

    if (c == '\x10') { /* up arrow */
        if (!editing) { if (sel_row > 0) sel_row--; }
    } else if (c == '\x11') { /* down arrow */
        if (!editing) { if (sel_row < SS_ROWS-1) sel_row++; }
    } else if (c == '\x12') { /* left arrow — not used by keyboard.c currently */
        if (!editing) { if (sel_col > 0) sel_col--; }
    } else if (c == '\x13') { /* right arrow */
        if (!editing) { if (sel_col < SS_COLS-1) sel_col++; }
    } else if (c == '\t') { /* Tab → move right */
        if (!editing) { if (sel_col < SS_COLS-1) sel_col++; }
    } else if (c == '\n' || c == '\r') {
        if (editing) {
            strncpy(cells[sel_row][sel_col], edit_buf, 15);
            cells[sel_row][sel_col][15] = '\0';
            editing = 0;
            edit_buf[0] = '\0';
            if (sel_row < SS_ROWS-1) sel_row++;
        } else {
            /* Enter on non-editing: start edit */
            editing = 1;
            strncpy(edit_buf, cells[sel_row][sel_col], 31);
            edit_buf[31] = '\0';
        }
    } else if (c == 27) { /* Escape */
        editing = 0;
        edit_buf[0] = '\0';
    } else if (c == '\b') { /* backspace */
        if (editing) {
            int l = strlen(edit_buf);
            if (l > 0) edit_buf[l-1] = '\0';
        } else {
            /* Backspace on non-editing: clear cell */
            cells[sel_row][sel_col][0] = '\0';
        }
    } else if (c >= 32 && c < 127) {
        /* Printable — start/continue editing */
        editing = 1;
        int l = strlen(edit_buf);
        if (l < 31) {
            edit_buf[l]   = c;
            edit_buf[l+1] = '\0';
        }
    }

    spreadsheet_render();
}

void spreadsheet_handle_click(window_t* win, int mx, int my) {
    if (!ss_win || win != ss_win) return;
    /* Commit any pending edit */
    if (editing) {
        strncpy(cells[sel_row][sel_col], edit_buf, 15);
        cells[sel_row][sel_col][15] = '\0';
        editing = 0;
        edit_buf[0] = '\0';
    }
    /* Transform to cell coords (account for 20px title bar drawn by wm) */
    int rx = mx - (int)win->x - CONTENT_X;
    int ry = my - (int)win->y - 20 - CONTENT_Y;
    if (rx < 0 || ry < 0) return;
    int col = rx / CELL_W;
    int row = ry / CELL_H;
    if (col < 0 || col >= SS_COLS || row < 0 || row >= SS_ROWS) return;
    sel_row = row;
    sel_col = col;
    spreadsheet_render();
}
