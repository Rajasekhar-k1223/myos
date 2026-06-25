#include "shell.h"
#include "kernel.h"
#include "string.h"
#include "pmm.h"
#include "pit.h"
#include "rtc.h"
#include "tar.h"
#include "task.h"
#include "io.h"

/* ── Buffer / state ──────────────────────────────────────────────────────── */
#define BUF_SIZE      256
#define HISTORY_SLOTS   8

static char   buf[BUF_SIZE];
static size_t buf_len = 0;

static char history[HISTORY_SLOTS][BUF_SIZE];
static int  hist_count = 0;
static int  hist_pos   = -1;   /* -1 = not browsing */

static char username[32] = "user";

/* ── Prompt ──────────────────────────────────────────────────────────────── */
static void print_prompt(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring(username);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("@myos");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(":~# ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* ── History ─────────────────────────────────────────────────────────────── */
static void hist_push(const char* cmd) {
    if (!cmd[0]) return;
    strncpy(history[hist_count % HISTORY_SLOTS], cmd, BUF_SIZE - 1);
    hist_count++;
}

/* ── Individual commands ─────────────────────────────────────────────────── */
static void cmd_help(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  myOS Shell — command reference\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ────────────────────────────────────────\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    static const char* cmds[][2] = {
        {"help",    "show this message"},
        {"clear",   "clear the screen"},
        {"echo",    "print text  (echo hello world)"},
        {"info",    "system overview"},
        {"mem",     "physical memory usage"},
        {"uptime",  "time since boot"},
        {"date",    "current date and time (RTC)"},
        {"ls",      "list files on RAM disk"},
        {"cat",     "print a file  (cat readme.txt)"},
        {"hexdump", "hex dump of memory  (hexdump ADDR [LEN])"},
        {"calc",    "simple arithmetic  (calc 3+5*2)"},
        {"color",   "set terminal color  (color fg bg)"},
        {"ps",      "list running tasks"},
        {"sleep",   "sleep N milliseconds  (sleep 500)"},
        {"reboot",  "restart the machine"},
        {"halt",    "power off / halt CPU"},
    };
    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_printf("  %-10s", cmds[i][0]);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_printf("  %s\n", cmds[i][1]);
    }
    terminal_putchar('\n');
}

static void cmd_info(void) {
    char dt[20]; rtc_datetime_str(dt);
    uint32_t total_kb = pmm_get_max_frames() * 4;
    uint32_t used_kb  = pmm_get_used_frames()  * 4;

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  System Information\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ────────────────────────────────────────\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

#define ROW(k,fmt,...) \
    do { terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK)); \
         terminal_printf("  %-16s", k); \
         terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK)); \
         terminal_printf(fmt "\n", ##__VA_ARGS__); } while(0)

    ROW("OS",          "myOS v0.7");
    ROW("Architecture","i686 32-bit protected mode");
    ROW("User",        "%s", username);
    ROW("Date/Time",   "%s", dt);
    ROW("Uptime",      "%u s (%u ticks)", pit_get_seconds(), pit_get_ticks());
    ROW("RAM total",   "%u KB  (%u MB)", total_kb, total_kb / 1024);
    ROW("RAM used",    "%u KB", used_kb);
    ROW("RAM free",    "%u KB", total_kb - used_kb);
    ROW("Paging",      "identity mapped, first 16 MB");
    ROW("Kernel heap", "1 MB");
    ROW("Timer",       "PIT 100 Hz");

#undef ROW
    terminal_putchar('\n');
}

static void cmd_mem(void) {
    uint32_t max   = pmm_get_max_frames();
    uint32_t used  = pmm_get_used_frames();
    uint32_t free_ = (max > used) ? (max - used) : 0;

    /* Draw a visual bar: 40 chars wide */
    int bar = (max > 0) ? (int)((uint64_t)used * 40 / max) : 0;

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  Memory Map\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ────────────────────────────────────────\n");
    terminal_printf("  Total : %u frames  (%u KB / %u MB)\n",
                    max, max * 4, max * 4 / 1024);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_printf("  Used  : %u frames  (%u KB)\n", used, used * 4);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_printf("  Free  : %u frames  (%u KB)\n", free_, free_ * 4);
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ────────────────────────────────────────\n");

    terminal_writestring("  [");
    for (int i = 0; i < 40; i++) {
        if (i < bar) {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            terminal_writestring("\xDB");
        } else {
            terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            terminal_writestring("\xB0");
        }
    }
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("]  ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_printf("%u%%\n\n",
                    (max > 0) ? (uint32_t)((uint64_t)used * 100 / max) : 0);
}

static void cmd_uptime(void) {
    uint32_t s = pit_get_seconds();
    uint32_t h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    terminal_printf("  Uptime: %uh %um %us  (%u ticks @ 100 Hz)\n",
                    h, m, sec, pit_get_ticks());
}

static void cmd_date(void) {
    char dt[20]; rtc_datetime_str(dt);
    terminal_printf("  %s\n", dt);
}

/* Very simple left-to-right evaluator: no precedence, only + - * / */
static void cmd_calc(const char* expr) {
    char* p = (char*)expr;
    long result = strtol(p, &p, 10);
    while (*p) {
        while (*p == ' ') p++;
        char op = *p++;
        while (*p == ' ') p++;
        long rhs = strtol(p, &p, 10);
        if      (op == '+') result += rhs;
        else if (op == '-') result -= rhs;
        else if (op == '*') result *= rhs;
        else if (op == '/') { if (rhs) result /= rhs; else { terminal_writestring("  Division by zero\n"); return; } }
        else { terminal_writestring("  Unknown operator\n"); return; }
    }
    terminal_printf("  = %d\n", (int)result);
}

static void cmd_hexdump(const char* args) {
    char* p;
    uint32_t addr = (uint32_t)strtol(args, &p, 0);
    uint32_t len  = (*p) ? (uint32_t)strtol(p, NULL, 0) : 64;
    if (len > 256) len = 256;

    const uint8_t* ptr = (const uint8_t*)addr;
    for (uint32_t i = 0; i < len; i += 16) {
        terminal_printf("  %08x  ", addr + i);
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len)
                terminal_printf("%02x ", ptr[i + j]);
            else
                terminal_writestring("   ");
            if (j == 7) terminal_putchar(' ');
        }
        terminal_writestring(" |");
        for (uint32_t j = 0; j < 16 && i + j < len; j++) {
            char c = (char)ptr[i + j];
            terminal_putchar((c >= 32 && c < 127) ? c : '.');
        }
        terminal_writestring("|\n");
    }
}

static void cmd_color(const char* args) {
    char* p;
    int fg = (int)strtol(args, &p, 10);
    int bg = (int)strtol(p,    NULL, 10);
    if (fg < 0 || fg > 15 || bg < 0 || bg > 15) {
        terminal_writestring("  Usage: color <fg 0-15> <bg 0-15>\n");
        terminal_writestring("  Colors: 0=black 1=blue 2=green 3=cyan 4=red 5=magenta\n");
        terminal_writestring("          6=brown 7=lt-grey 8=dk-grey 9=lt-blue\n");
        terminal_writestring("         10=lt-green 11=lt-cyan 12=lt-red 14=yellow 15=white\n");
        return;
    }
    terminal_setcolor(vga_entry_color((enum vga_color)fg, (enum vga_color)bg));
    terminal_writestring("  Color changed.\n");
}

static const char* task_state_str(int s) {
    switch (s) {
    case 0: return "RUNNING";
    case 1: return "READY  ";
    case 2: return "SLEEP  ";
    case 3: return "DEAD   ";
    default: return "?      ";
    }
}

static void cmd_ps(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  PID  STATE    NAME\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ───  ───────  ────────────────\n");

    uint32_t n = task_count();
    for (uint32_t id = 0; id < MAX_TASKS; id++) {
        if (tasks[id].state == TASK_DEAD) continue;
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_printf("  %-4u ", tasks[id].id);
        if (tasks[id].state == 0)
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        else
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_printf("%-9s", task_state_str(tasks[id].state));
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_printf("%s\n", tasks[id].name);
    }
    terminal_printf("\n  %u task(s) total\n\n", n);
}

static void cmd_reboot(void) {
    terminal_writestring("  Rebooting...\n");
    uint8_t v = 0x02;
    while (v & 0x02) v = inb(0x64);
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

static void cmd_halt(void) {
    terminal_writestring("  System halted. Safe to power off.\n");
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

/* ── Execute ─────────────────────────────────────────────────────────────── */
static void execute(void) {
    terminal_putchar('\n');
    buf[buf_len] = '\0';
    hist_push(buf);
    hist_pos = -1;

    /* Trim leading spaces */
    char* cmd = buf;
    while (*cmd == ' ') cmd++;
    if (!*cmd) goto done;

    if      (strcmp(cmd, "help")  == 0) cmd_help();
    else if (strcmp(cmd, "clear") == 0) terminal_clear();
    else if (strcmp(cmd, "info")  == 0) cmd_info();
    else if (strcmp(cmd, "mem")   == 0) cmd_mem();
    else if (strcmp(cmd, "uptime")== 0) cmd_uptime();
    else if (strcmp(cmd, "date")  == 0) cmd_date();
    else if (strcmp(cmd, "ls")    == 0) tar_ls();
    else if (strcmp(cmd, "ps")    == 0) cmd_ps();
    else if (strncmp(cmd, "sleep ",6) == 0) {
        uint32_t ms = (uint32_t)strtol(cmd + 6, NULL, 10);
        terminal_printf("  Sleeping %u ms...\n", ms);
        task_sleep(ms);
        terminal_writestring("  Done.\n");
    }
    else if (strcmp(cmd, "reboot")== 0) cmd_reboot();
    else if (strcmp(cmd, "halt")  == 0) cmd_halt();
    else if (strncmp(cmd, "echo ",   5) == 0) { terminal_writestring(cmd + 5); terminal_putchar('\n'); }
    else if (strncmp(cmd, "cat ",    4) == 0) tar_cat(cmd + 4);
    else if (strncmp(cmd, "calc ",   5) == 0) cmd_calc(cmd + 5);
    else if (strncmp(cmd, "hexdump", 7) == 0) cmd_hexdump(cmd[7] ? cmd + 8 : "0x100000");
    else if (strncmp(cmd, "color ",  6) == 0) cmd_color(cmd + 6);
    else {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_printf("  Command not found: %s\n", cmd);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

done:
    buf_len = 0;
    print_prompt();
}

/* ── Keypress handler ────────────────────────────────────────────────────── */
void shell_handle_keypress(char c) {
    if (c == '\n') {
        execute();
    } else if (c == '\b') {
        if (buf_len > 0) { buf_len--; terminal_putchar('\b'); }
    } else if (c >= ' ' && c <= '~') {
        if (buf_len < BUF_SIZE - 1) { buf[buf_len++] = c; terminal_putchar(c); }
    }
}

/* ── First-boot wizard ───────────────────────────────────────────────────── */
static void wizard(void) {
    char dt[20]; rtc_datetime_str(dt);

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  \xC9");
    for (int i = 0; i < 54; i++) terminal_putchar('\xCD');
    terminal_writestring("\xBB\n");

    terminal_writestring("  \xBA");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("          Welcome to myOS v0.7 — First Boot          ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\xBA\n");

    terminal_writestring("  \xBA  ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_printf("  Date/Time: %-40s", dt);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\xBA\n");

    terminal_writestring("  \xC8");
    for (int i = 0; i < 54; i++) terminal_putchar('\xCD');
    terminal_writestring("\xBC\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  Enter your username: ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));

    /* Read username from keyboard one char at a time via polling */
    size_t ulen = 0;
    char ubuf[31];
    memset(ubuf, 0, sizeof(ubuf));

    /* Busy-poll the keyboard data port (IRQ1 already enabled) */
    while (1) {
        /* Wait for a key — the keyboard IRQ has already echoed it via shell_handle_keypress
           but we need our own blocking read here. We use the KBD data port directly. */
        if (!(inb(0x64) & 0x01)) continue; /* no data yet */
        uint8_t sc = inb(0x60);
        if (sc & 0x80) continue; /* key release */

        /* Minimal scancode→ASCII for username entry */
        static const char sc2a[] = {
            0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b',0,
            'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
            'a','s','d','f','g','h','j','k','l',';','\'','`',0,0,
            'z','x','c','v','b','n','m',',','.','/',0,0,0,' '
        };
        if (sc >= sizeof(sc2a)) continue;
        char c = sc2a[sc];
        if (!c) continue;

        if (c == '\n') { terminal_putchar('\n'); break; }
        if (c == '\b') {
            if (ulen > 0) { ulen--; terminal_putchar('\b'); }
            continue;
        }
        if (c >= ' ' && c <= '~' && ulen < 30) {
            ubuf[ulen++] = c;
            terminal_putchar(c);
        }
    }

    if (ulen > 0) {
        ubuf[ulen] = '\0';
        strncpy(username, ubuf, 31);
    }
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_printf("  Hello, %s! Type 'help' to get started.\n\n", username);
}

/* ── shell_init ──────────────────────────────────────────────────────────── */
void shell_init(void) {
    wizard();
    print_prompt();
}
