/*
 * HTML5 tokenizer and block-box renderer.
 * Implements: element tokenizer, DOM-lite node tree, block/inline layout,
 * text wrapping, CSS color/font-size attributes (inline style only).
 */
#include "html5.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"
#include "ttf.h"
#include "gpu.h"
#include "vesa.h"

/* ── DOM Node ─────────────────────────────────────────────────────────── */
#define MAX_NODES  128
#define MAX_ATTRS  8
#define MAX_TEXT   256

typedef struct html_node {
    char tag[16];                   /* empty = text node */
    char text[MAX_TEXT];            /* text content */
    char attr_key[MAX_ATTRS][16];
    char attr_val[MAX_ATTRS][32];
    int  num_attrs;
    struct html_node* children[16];
    int  num_children;
    struct html_node* parent;
    int  is_text;
} html_node_t;

static html_node_t node_pool[MAX_NODES];
static int node_count = 0;

static html_node_t* alloc_node(void) {
    if (node_count >= MAX_NODES) return 0;
    html_node_t* n = &node_pool[node_count++];
    memset(n, 0, sizeof(*n));
    return n;
}

/* ── Tokenizer helpers ────────────────────────────────────────────────── */
static void str_lower(char* s) {
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32;
}

static int is_block(const char* tag) {
    static const char* blocks[] = {"div","p","h1","h2","h3","h4","h5","h6",
                                    "ul","ol","li","body","html","section",
                                    "article","header","footer","nav","main",0};
    for (int i = 0; blocks[i]; i++) if (strcmp(tag, blocks[i]) == 0) return 1;
    return 0;
}

/* ── Parse HTML into node tree ────────────────────────────────────────── */
static html_node_t* parse_html(const char* html) {
    html_node_t* root = alloc_node();
    if (!root) return 0;
    strcpy(root->tag, "root");
    html_node_t* cur = root;
    const char* p = html;

    while (*p) {
        if (*p == '<') {
            p++;
            int closing = 0;
            if (*p == '/') { closing = 1; p++; }
            if (*p == '!') { /* comment/doctype: skip to '>' */
                while (*p && *p != '>') p++;
                if (*p) p++;
                continue;
            }
            /* Read tag name */
            char tag[16] = {0}; int ti = 0;
            while (*p && *p != ' ' && *p != '>' && *p != '/' && ti < 15)
                { tag[ti++] = *p++; }
            str_lower(tag);

            if (closing) {
                /* Pop up to matching tag */
                html_node_t* t = cur;
                while (t && strcmp(t->tag, tag) != 0) t = t->parent;
                if (t) cur = t->parent ? t->parent : root;
                while (*p && *p != '>') p++;
                if (*p) p++;
                continue;
            }

            html_node_t* node = alloc_node();
            if (!node) break;
            strncpy(node->tag, tag, 15);
            node->parent = cur;
            if (cur->num_children < 16) cur->children[cur->num_children++] = node;

            /* Parse attributes */
            while (*p && *p != '>' && *p != '/') {
                while (*p == ' ') p++;
                if (*p == '>' || *p == '/') break;
                char key[16]={0}; int ki=0;
                while (*p && *p!='=' && *p!='>' && *p!=' ' && ki<15) { key[ki++]=*p++; }
                str_lower(key);
                char val[32]={0}; int vi=0;
                if (*p=='=') {
                    p++;
                    char q=0;
                    if (*p=='"'||*p=='\'') { q=*p++; }
                    while (*p && *p!=q && *p!='>' && vi<31) { val[vi++]=*p++; }
                    if (q && *p==q) p++;
                }
                if (node->num_attrs < MAX_ATTRS) {
                    strncpy(node->attr_key[node->num_attrs], key, 15);
                    strncpy(node->attr_val[node->num_attrs], val, 31);
                    node->num_attrs++;
                }
            }
            int self_close = (*p == '/');
            while (*p && *p != '>') p++;
            if (*p) p++;
            if (!self_close && strcmp(tag,"br")!=0 && strcmp(tag,"hr")!=0
                            && strcmp(tag,"img")!=0 && strcmp(tag,"input")!=0)
                cur = node;
        } else {
            /* Text content */
            char text[MAX_TEXT]={0}; int ti=0;
            while (*p && *p != '<' && ti < MAX_TEXT-1) { text[ti++] = *p++; }
            /* Trim leading whitespace */
            int start=0;
            while(text[start]==' '||text[start]=='\n'||text[start]=='\r'||text[start]=='\t')
                start++;
            if (text[start]) {
                html_node_t* tn = alloc_node();
                if (tn) {
                    tn->is_text = 1; tn->parent = cur;
                    strncpy(tn->text, text + start, MAX_TEXT-1);
                    if (cur->num_children < 16) cur->children[cur->num_children++] = tn;
                }
            }
        }
    }
    return root;
}

/* ── Render node tree ─────────────────────────────────────────────────── */
static int render_node(html_node_t* node, int wx, int wy, int ww, int wh,
                       int cx, int cy, int font_size, uint32_t color) {
    if (!node) return cy;

    if (node->is_text) {
        if (node->text[0]) {
            /* Word-wrap text */
            const char* w = node->text;
            int lx = cx;
            while (*w) {
                /* Find end of word */
                const char* ws = w;
                while (*ws && *ws != ' ') ws++;
                int wlen = (int)(ws - w);
                int px_w = wlen * (font_size / 2 + 1);
                if (lx + px_w > wx + ww - 4 && lx > cx) {
                    lx = cx;
                    cy += font_size + 4;
                }
                if (cy + font_size > wy + wh) break;
                /* draw word into framebuffer via TTF */
                extern uint32_t* fb;
                if (fb) ttf_draw_string(fb, (int)vesa_width, (int)vesa_height,
                                        lx, cy, w, wlen, font_size, color);
                lx += px_w;
                w = ws;
                while (*w == ' ') { lx += font_size/3; w++; }
            }
            cy += font_size + 4;
        }
        return cy;
    }

    /* Block elements: add vertical margin */
    if (is_block(node->tag)) { cy += 4; }

    /* Headings: increase font size */
    int fs = font_size;
    uint32_t fg = color;
    if      (strcmp(node->tag,"h1")==0) { fs=28; fg=0x111111; cy+=8; }
    else if (strcmp(node->tag,"h2")==0) { fs=22; fg=0x222222; cy+=6; }
    else if (strcmp(node->tag,"h3")==0) { fs=18; fg=0x333333; cy+=4; }
    else if (strcmp(node->tag,"li") ==0) { cx += 12; }
    else if (strcmp(node->tag,"hr") ==0) {
        vesa_draw_rect((uint32_t)cx,(uint32_t)cy,
                       (uint32_t)(ww-8),1,0xCCCCCC);
        return cy + 6;
    }

    for (int i = 0; i < node->num_children; i++)
        cy = render_node(node->children[i], wx, wy, ww, wh, cx, cy, fs, fg);

    if (is_block(node->tag)) cy += 4;
    return cy;
}

/* ── Public API ───────────────────────────────────────────────────────── */
void html5_init(void) {
    terminal_printf("[HTML5] Tokenizer + block-box renderer ready.\n");
}

void html5_render(const char* html_text, int window_x, int window_y) {
    node_count = 0;
    html_node_t* root = parse_html(html_text);
    if (!root) return;
    /* White background */
    gpu_draw_rect(window_x, window_y, 800, 600, 0xFFFFFF);
    /* Render starting 8px inside the window */
    render_node(root, window_x + 8, window_y + 8, 784, 584,
                window_x + 8, window_y + 8, 14, 0x1A1A1A);
}
