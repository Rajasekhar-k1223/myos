#include "shell.h"
#include "kernel.h"
#include "string.h"
#include "pmm.h"
#include "tar.h"

#define SHELL_BUFFER_SIZE 256
static char shell_buffer[SHELL_BUFFER_SIZE];
static size_t shell_index = 0;

static void print_prompt(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("root@myos:~# ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void shell_execute(void) {
    terminal_putchar('\n');
    shell_buffer[shell_index] = '\0'; // Null-terminate

    if (shell_index == 0) {
        // Empty command
    } else if (strcmp(shell_buffer, "help") == 0) {
        terminal_writestring("Available commands:\n");
        terminal_writestring("  help  - Show this message\n");
        terminal_writestring("  clear - Clear the screen\n");
        terminal_writestring("  echo  - Print text to the screen\n");
        terminal_writestring("  info  - Show system memory info\n");
    } else if (strcmp(shell_buffer, "clear") == 0) {
        terminal_clear();
    } else if (strncmp(shell_buffer, "echo ", 5) == 0) {
        terminal_writestring(&shell_buffer[5]);
        terminal_putchar('\n');
    } else if (strcmp(shell_buffer, "info") == 0) {
        terminal_writestring("myOS System Information\n");
        terminal_writestring("-----------------------\n");
        terminal_writestring("Architecture: i686 (32-bit)\n");
        terminal_writestring("Memory Manager: ONLINE (Paging Enabled)\n");
    } else if (strcmp(shell_buffer, "ls") == 0) {
        tar_ls();
    } else if (strncmp(shell_buffer, "cat ", 4) == 0) {
        tar_cat(&shell_buffer[4]);
    } else {
        terminal_writestring("Command not found: ");
        terminal_writestring(shell_buffer);
        terminal_putchar('\n');
    }

    shell_index = 0;
    print_prompt();
}

void shell_handle_keypress(char c) {
    if (c == '\n') {
        shell_execute();
    } else if (c == '\b') {
        if (shell_index > 0) {
            shell_index--;
            terminal_putchar('\b');
        }
    } else if (c >= ' ' && c <= '~') { // Printable characters
        if (shell_index < SHELL_BUFFER_SIZE - 1) {
            shell_buffer[shell_index++] = c;
            terminal_putchar(c);
        }
    }
}

void shell_init(void) {
    terminal_writestring("\nWelcome to myOS!\nType 'help' for a list of commands.\n\n");
    print_prompt();
}
