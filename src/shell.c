#include "shell.h"
#include "kernel.h"
#include "string.h"
#include "pmm.h"
#include "pit.h"
#include "tar.h"
#include "io.h"

#define SHELL_BUFFER_SIZE 256
#define HISTORY_SIZE       8

static char shell_buffer[SHELL_BUFFER_SIZE];
static size_t shell_index = 0;

/* Simple command history (circular) */
static char history[HISTORY_SIZE][SHELL_BUFFER_SIZE];
static int  history_count = 0;
static int  history_pos   = -1; /* -1 = not browsing */

static void print_prompt(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("root@myos:~# ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_help(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("myOS Shell — available commands\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  help    — this message\n");
    terminal_writestring("  clear   — clear the screen\n");
    terminal_writestring("  echo    — print text  (e.g. echo hello)\n");
    terminal_writestring("  info    — system information\n");
    terminal_writestring("  mem     — memory usage\n");
    terminal_writestring("  uptime  — seconds since boot\n");
    terminal_writestring("  ls      — list files on virtual pendrive\n");
    terminal_writestring("  cat     — read a file (e.g. cat hello.txt)\n");
    terminal_writestring("  reboot  — restart the machine\n");
    terminal_writestring("  halt    — halt the CPU\n");
}

static void cmd_info(void) {
    uint32_t total_kb = (pmm_get_max_frames() * 4);
    uint32_t used_kb  = (pmm_get_used_frames() * 4);

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("myOS — 32-bit Hobby OS\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("Architecture : i686 (32-bit protected mode)\n");
    terminal_writestring("RAM detected : ");
    terminal_writedec(total_kb / 1024);
    terminal_writestring(" MB (");
    terminal_writedec(total_kb);
    terminal_writestring(" KB)\n");
    terminal_writestring("RAM in use   : ");
    terminal_writedec(used_kb);
    terminal_writestring(" KB\n");
    terminal_writestring("Uptime       : ");
    terminal_writedec(pit_get_seconds());
    terminal_writestring("s (");
    terminal_writedec(pit_get_ticks());
    terminal_writestring(" ticks @ 100Hz)\n");
    terminal_writestring("Paging       : ENABLED — identity mapped 16 MB\n");
    terminal_writestring("Kernel heap  : 1 MB\n");
}

static void cmd_mem(void) {
    uint32_t max  = pmm_get_max_frames();
    uint32_t used = pmm_get_used_frames();
    uint32_t free_f = (max > used) ? (max - used) : 0;

    terminal_writestring("Physical memory frames (4 KB each):\n");
    terminal_writestring("  Total : ");
    terminal_writedec(max);
    terminal_writestring(" frames (");
    terminal_writedec(max * 4);
    terminal_writestring(" KB)\n");
    terminal_writestring("  Used  : ");
    terminal_writedec(used);
    terminal_writestring(" frames (");
    terminal_writedec(used * 4);
    terminal_writestring(" KB)\n");
    terminal_writestring("  Free  : ");
    terminal_writedec(free_f);
    terminal_writestring(" frames (");
    terminal_writedec(free_f * 4);
    terminal_writestring(" KB)\n");
}

static void cmd_uptime(void) {
    uint32_t secs  = pit_get_seconds();
    uint32_t ticks = pit_get_ticks();
    uint32_t h = secs / 3600;
    uint32_t m = (secs % 3600) / 60;
    uint32_t s = secs % 60;

    terminal_writestring("Uptime: ");
    terminal_writedec(h); terminal_writestring("h ");
    terminal_writedec(m); terminal_writestring("m ");
    terminal_writedec(s); terminal_writestring("s");
    terminal_writestring("  (");
    terminal_writedec(ticks);
    terminal_writestring(" ticks)\n");
}

static void cmd_reboot(void) {
    terminal_writestring("Rebooting...\n");
    /* Pulse PS/2 controller reset line */
    uint8_t v = 0x02;
    while (v & 0x02) v = inb(0x64);
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_halt(void) {
    terminal_writestring("System halted. Safe to power off.\n");
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

static void history_push(const char* cmd) {
    if (cmd[0] == '\0') return;
    int slot = history_count % HISTORY_SIZE;
    strcpy(history[slot], cmd);
    history_count++;
}

static void shell_execute(void) {
    terminal_putchar('\n');
    shell_buffer[shell_index] = '\0';
    history_push(shell_buffer);
    history_pos = -1;

    if (shell_index == 0) {
        /* empty — just reprint prompt */
    } else if (strcmp(shell_buffer, "help") == 0) {
        cmd_help();
    } else if (strcmp(shell_buffer, "clear") == 0) {
        terminal_clear();
    } else if (strcmp(shell_buffer, "info") == 0) {
        cmd_info();
    } else if (strcmp(shell_buffer, "mem") == 0) {
        cmd_mem();
    } else if (strcmp(shell_buffer, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(shell_buffer, "ls") == 0) {
        tar_ls();
    } else if (strcmp(shell_buffer, "reboot") == 0) {
        cmd_reboot();
    } else if (strcmp(shell_buffer, "halt") == 0) {
        cmd_halt();
    } else if (strncmp(shell_buffer, "echo ", 5) == 0) {
        terminal_writestring(&shell_buffer[5]);
        terminal_putchar('\n');
    } else if (strncmp(shell_buffer, "cat ", 4) == 0) {
        tar_cat(&shell_buffer[4]);
    } else {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("Command not found: ");
        terminal_writestring(shell_buffer);
        terminal_putchar('\n');
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    shell_index = 0;
    print_prompt();
}

/* Erase the current line from the prompt onwards */
static void shell_clear_line(void) {
    while (shell_index > 0) {
        terminal_putchar('\b');
        shell_index--;
    }
}

void shell_handle_keypress(char c) {
    if (c == '\n') {
        shell_execute();
    } else if (c == '\b') {
        if (shell_index > 0) {
            shell_index--;
            terminal_putchar('\b');
        }
    } else if (c == '\t') {
        /* Tab: basic command completion stub (no-op for now) */
    } else if (c >= ' ' && c <= '~') {
        if (shell_index < SHELL_BUFFER_SIZE - 1) {
            shell_buffer[shell_index++] = c;
            terminal_putchar(c);
        }
    }
    (void)history; (void)history_count; (void)history_pos;
    (void)shell_clear_line;
}

void shell_init(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("\nWelcome to myOS!\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("Type 'help' for available commands.\n\n");
    print_prompt();
}
