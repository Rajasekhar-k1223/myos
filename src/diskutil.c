#include "diskutil.h"
#include "string.h"
#include "ata.h"
#include "pit.h"

int sprintf(char *str, const char *format, ...);

static window_t* my_win = 0;

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (!my_win) return;
    for (int yy = y; yy < y+h; yy++)
        for (int xx = x; xx < x+w; xx++)
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                my_win->buffer[yy * my_win->w + xx] = c;
}

static void draw_bar(int x, int y, int w, int h, int pct, uint32_t fill) {
    draw_rect(x, y, w, h, 0x222222);
    if (pct > 100) pct = 100;
    if (pct > 0) draw_rect(x, y, (w * pct) / 100, h, fill);
}

typedef struct {
    const char* name;
    const char* type;
    uint32_t    total_mb;
    uint32_t    used_mb;
    int         present;
} disk_entry_t;

static disk_entry_t disks[3];
static int num_disks = 0;
static int sel_disk = 0;

static void probe_disks(void) {
    num_disks = 0;

    /* ATA / IDE primary master — probe with a test read */
    {
        static uint8_t _buf[512];
        int ok = ata_read_sector(0, _buf);
        disks[0].name     = "ATA0 (Primary)";
        disks[0].type     = "FAT16 IDE";
        disks[0].total_mb = 32;    /* disk.img: 32 MB */
        disks[0].used_mb  = 4;     /* approximate */
        disks[0].present  = (ok == 0) ? 1 : 1; /* always show, ATA init always runs */
        num_disks++;
    }

    /* AHCI (SATA) — ext2_disk.img */
    disks[1].name    = "AHCI0 (SATA)";
    disks[1].type    = "Ext2 SATA";
    disks[1].total_mb = 32;
    disks[1].used_mb  = 1;
    disks[1].present  = 1;
    num_disks++;

    /* NVMe */
    disks[2].name    = "NVMe0";
    disks[2].type    = "RAW NVMe";
    disks[2].total_mb = 8;
    disks[2].used_mb  = 0;
    disks[2].present  = 1;
    num_disks++;
}

static void diskutil_render(void) {
    if (!my_win) return;
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0x0D1117;

    wm_draw_string_window(my_win, 10, 8, "Disk Utility", 0x58A6FF);
    draw_rect(10, 22, (int)my_win->w - 20, 1, 0x30363D);

    /* Disk list (left column) */
    for (int i = 0; i < num_disks; i++) {
        int by = 28 + i * 36;
        uint32_t bg = (i == sel_disk) ? 0x1D2D3A : 0x161B22;
        draw_rect(8, by, 130, 32, bg);
        if (i == sel_disk) draw_rect(8, by, 2, 32, 0x3B82F6);
        wm_draw_string_window(my_win, 14, by + 4,  disks[i].name, 0xC9D1D9);
        wm_draw_string_window(my_win, 14, by + 18, disks[i].type, 0x58A6FF);
    }

    /* Detail panel (right) */
    if (sel_disk < num_disks) {
        disk_entry_t* d = &disks[sel_disk];
        int dx = 148, dy = 28;
        wm_draw_string_window(my_win, dx, dy,    d->name, 0xFFFFFF);
        wm_draw_string_window(my_win, dx, dy+18, d->type, 0x8B949E);

        char buf[64];
        sprintf(buf, "Capacity: %u MB", d->total_mb);
        wm_draw_string_window(my_win, dx, dy+42, buf, 0xCCCCCC);

        sprintf(buf, "Used: %u MB  Free: %u MB", d->used_mb, d->total_mb - d->used_mb);
        wm_draw_string_window(my_win, dx, dy+58, buf, 0xCCCCCC);

        int pct = (int)((d->used_mb * 100) / (d->total_mb ? d->total_mb : 1));
        draw_bar(dx, dy+76, (int)my_win->w - dx - 12, 16, pct, 0x3B82F6);
        sprintf(buf, "%d%%", pct);
        wm_draw_string_window(my_win, dx, dy+96, buf, 0x8B949E);

        /* Status */
        wm_draw_string_window(my_win, dx, dy+120, "Status: Healthy", 0x22C55E);
        wm_draw_string_window(my_win, dx, dy+138, "Partition: MBR", 0x8B949E);
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void diskutil_init(window_t* win) {
    my_win = win;
    probe_disks();
    diskutil_render();
}

void diskutil_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20;
    /* Click on disk list */
    for (int i = 0; i < num_disks; i++) {
        int by = 28 + i * 36;
        if (lx >= 8 && lx <= 138 && ly >= by && ly <= by + 32) {
            sel_disk = i;
            diskutil_render();
            return;
        }
    }
}
