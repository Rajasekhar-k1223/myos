#include "sysmon.h"
#include "task.h"
#include "string.h"
#include "pit.h"
#include "kheap.h"

int sprintf(char *str, const char *format, ...);

extern uint32_t pmm_get_max_frames(void);
extern uint32_t pmm_get_used_frames(void);

static window_t* my_win = 0;

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (!my_win) return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                my_win->buffer[yy * my_win->w + xx] = c;
}

static void draw_bar(int x, int y, int w, int h, int pct, uint32_t fill) {
    draw_rect(x, y, w, h, 0x333333);
    int fw = (w * pct) / 100;
    if (fw > 0) draw_rect(x, y, fw, h, fill);
}

static void sysmon_render(void) {
    if (!my_win) return;
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0x0D1117;

    wm_draw_string_window(my_win, 10, 8, "System Monitor", 0x58A6FF);

    /* Uptime */
    uint32_t secs = pit_get_seconds();
    char buf[80];
    sprintf(buf, "Uptime: %02u:%02u:%02u", secs/3600, (secs%3600)/60, secs%60);
    wm_draw_string_window(my_win, 10, 30, buf, 0xFFFFFF);

    /* RAM */
    uint32_t max_f  = pmm_get_max_frames();
    uint32_t used_f = pmm_get_used_frames();
    if (max_f == 0) max_f = 1;
    uint32_t used_mb = (used_f * 4) / 1024;
    uint32_t total_mb = (max_f * 4) / 1024;
    int ram_pct = (int)((used_f * 100) / max_f);
    sprintf(buf, "RAM: %u MB / %u MB  (%d%%)", used_mb, total_mb, ram_pct);
    wm_draw_string_window(my_win, 10, 50, buf, 0xCCCCCC);
    draw_bar(10, 68, (int)my_win->w - 20, 14, ram_pct, 0x3B82F6);

    /* CPU (estimated from task count - no HW perf counters in x86 without PMU) */
    uint32_t ntasks = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != TASK_DEAD) ntasks++;
    int cpu_est = (ntasks > 0) ? (int)(ntasks * 12) : 5;
    if (cpu_est > 95) cpu_est = 95;
    sprintf(buf, "CPU: ~%d%% (est from %u tasks)", cpu_est, ntasks);
    wm_draw_string_window(my_win, 10, 92, buf, 0xCCCCCC);
    draw_bar(10, 110, (int)my_win->w - 20, 14, cpu_est, 0x22C55E);

    /* Heap stats */
    uint32_t h_used = 0, h_free = 0, h_blks = 0;
    kheap_stats(&h_used, &h_free, &h_blks);
    uint32_t h_total = h_used + h_free;
    if (h_total == 0) h_total = 1;
    int heap_pct = (int)((h_used * 100) / h_total);
    sprintf(buf, "Heap: %u KB used / %u KB total  (%u blks)",
            h_used / 1024, h_total / 1024, h_blks);
    wm_draw_string_window(my_win, 10, 130, buf, 0xCCCCCC);
    draw_bar(10, 148, (int)my_win->w - 20, 10, heap_pct, 0xF59E0B);

    /* Process list */
    wm_draw_string_window(my_win, 10, 164, "PID   NAME            STATE", 0x8B949E);
    draw_rect(10, 180, (int)my_win->w - 20, 1, 0x30363D);

    int py = 186;
    for (int i = 0; i < MAX_TASKS && py < (int)my_win->h - 10; i++) {
        if (tasks[i].state == TASK_DEAD) continue;
        const char* st = "RUN";
        if (tasks[i].state == TASK_SLEEPING) st = "SLP";
        else if (tasks[i].state == TASK_READY) st = "RDY";
        sprintf(buf, "%3d   %-16s%s", tasks[i].id, tasks[i].name, st);
        uint32_t col = (tasks[i].state == TASK_RUNNING) ? 0x4ade80 : 0xC9D1D9;
        wm_draw_string_window(my_win, 10, py, buf, col);
        py += 18;
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void sysmon_init(window_t* win) {
    my_win = win;
    sysmon_render();
}

void sysmon_handle_click(window_t* win, int mx, int my) {
    (void)win; (void)mx; (void)my;
    sysmon_render();
}
