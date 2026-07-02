#include "shell.h"
#include "kernel.h"
#include "string.h"
#include "pmm.h"
#include "pit.h"
#include "rtc.h"
#include "user.h"
#include "installer.h"
#include "tar.h"
#include "task.h"
#include "io.h"
#include "ata.h"
#include "fs.h"
#include "fat16.h"
#include "speaker.h"
#include "wm.h"
#include "dns.h"
#include "tcp.h"

/* ── Buffer / state ──────────────────────────────────────────────────────── */
#define BUF_SIZE      256
#define HISTORY_SLOTS   8

static char   buf[BUF_SIZE];
static size_t buf_len = 0;

static char history[HISTORY_SLOTS][BUF_SIZE];
static int  hist_count = 0;
static int  hist_pos   = -1;   /* -1 = not browsing */

static char username[32] = "user";

/* ── Privilege / sudo state ──────────────────────────────────────────────── */
static int  is_root          = 0;         /* 1 = elevated to root */
static int  sudo_pending     = 0;         /* waiting for sudo password */
static char sudo_cmd[BUF_SIZE] = "";      /* command to run after sudo auth */
static char sudo_pw_buf[64]  = "";        /* password being typed (hidden) */
static int  sudo_pw_len      = 0;
static int  sudo_attempts    = 0;
/* Root password — set by installer on completion, default "admin" */
char shell_root_password[64] = "admin";

void shell_set_username(const char* u) {
    int i = 0;
    while (u[i] && i < 31) { username[i] = u[i]; i++; }
    username[i] = 0;
}

/* ── Prompt ──────────────────────────────────────────────────────────────── */
static void print_prompt(void) {
    terminal_setcolor(vga_entry_color(
        is_root ? VGA_COLOR_LIGHT_RED : VGA_COLOR_LIGHT_GREEN,
        VGA_COLOR_BLACK));
    terminal_writestring(is_root ? "root" : username);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("@elseaos");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(is_root ? ":~# " : ":~$ ");
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
    terminal_writestring("\n  ElseaOS Shell — command reference\n");
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
        {"fat ls",   "list files on FAT16 disk"},
        {"fat read", "read a disk file  (fat read notes.txt)"},
        {"fat write","write a disk file  (fat write hi.txt Hello!)"},
        {"fat del",  "delete a disk file  (fat del hi.txt)"},
        {"exec",     "run a Ring-3 ELF binary  (exec hello.elf)"},
        {"ping",     "ping an IP  (ping 10.0.2.2)"},
        {"ifconfig", "show network interface info"},
        {"nslookup", "resolve hostname  (nslookup google.com)"},
        {"netstat",  "show TCP connection state"},
        {"arp",      "send ARP request to gateway"},
        {"http_get", "HTTP GET  (http_get 10.0.2.2)"},
        {"beep",     "beep at freq Hz  (beep 440)"},
        {"c4",       "run C4 C compiler/interpreter"},
        {"ps",       "list running tasks"},
        {"sleep",    "sleep N milliseconds  (sleep 500)"},
        {"reboot",   "restart the machine"},
        {"halt",     "power off / halt CPU"},
        {"install",           "run the graphical OS installer (root)"},
        {"apt update",           "refresh package list (sudo)"},
        {"apt install <pkg>",    "install a package (sudo)"},
        {"apt remove <pkg>",     "remove a package (sudo)"},
        {"apt upgrade",          "upgrade all packages (sudo)"},
        {"apt list",             "list all packages"},
        {"apt list --installed", "show installed packages only"},
        {"apt search <term>",    "search packages by name/desc"},
        {"apt show <pkg>",       "show package details"},
        {"ota check",            "check for OS updates"},
        {"ota install",          "download and apply update (sudo)"},
        {"sudo <cmd>",           "run command as root"},
        {"su",                   "switch to root session"},
        {"whoami",               "show current user"},
        {"id",                   "show uid/gid info"},
        {"exit",                 "exit root session / logout"},
        {"java -version",        "Java runtime (needs: sudo apt install java)"},
        {"javac <file>",         "Java compiler (needs: sudo apt install java)"},
        {"python3 --version",    "Python 3 (needs: sudo apt install python3)"},
        {"gcc --version",        "C/C++ compiler (needs: sudo apt install gcc)"},
        {"node --version",       "Node.js (needs: sudo apt install nodejs)"},
        {"git --version",        "Git VCS (needs: sudo apt install git)"},
        {"vim <file>",           "Vi editor (needs: sudo apt install vim)"},
        {"curl <url>",           "HTTP client (needs: sudo apt install curl)"},
        {"htop",                 "Process viewer (needs: sudo apt install htop)"},
        {"bt scan",         "scan for bluetooth devices"},
        {"bt connect <n>",  "connect to BT device by index"},
        {"wifi scan",       "scan for WiFi networks"},
        {"wifi connect",    "wifi connect <ssid> <pass>"},
        {"lang <code>",     "set language: en fr de es ar"},
        {"fde unlock <pw>", "unlock FDE encrypted disk"},
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

    ROW("OS",          "ElseaOS v1.0");
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

    const uint8_t* ptr = (const uint8_t*)(uintptr_t)addr;
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
    terminal_writestring("  Shutting down via ACPI...\n");
    extern void acpi_shutdown(void);
    acpi_shutdown();
    /* fallback if ACPI shutdown not available */
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

static void cmd_install(void) {
    terminal_writestring("  Launching Graphic Installer...\n");
    installer_run();
}

/* ── Privilege helpers ───────────────────────────────────────────────────── */
static int cmd_needs_root(const char* cmd) {
    /* commands that require root / sudo */
    if (strncmp(cmd, "pkg install", 11) == 0) return 1;
    if (strncmp(cmd, "pkg remove",  10) == 0) return 1;
    if (strncmp(cmd, "pkg update",  10) == 0) return 1;
    if (strncmp(cmd, "pkg purge",    9) == 0) return 1;
    if (strncmp(cmd, "pkg upgrade", 11) == 0) return 1;
    if (strncmp(cmd, "apt install", 11) == 0) return 1;
    if (strncmp(cmd, "apt remove",  10) == 0) return 1;
    if (strncmp(cmd, "apt purge",    9) == 0) return 1;
    if (strncmp(cmd, "apt update",  10) == 0) return 1;
    if (strncmp(cmd, "apt upgrade", 11) == 0) return 1;
    if (strncmp(cmd, "apt-get",      7) == 0) return 1;
    if (strncmp(cmd, "ota install", 11) == 0) return 1;
    if (strncmp(cmd, "fde",          3) == 0) return 1;
    if (strcmp (cmd, "reboot")         == 0) return 1;
    if (strcmp (cmd, "halt")           == 0) return 1;
    if (strcmp (cmd, "install")        == 0) return 1;
    return 0;
}

static void sudo_prompt(const char* after_cmd) {
    strncpy(sudo_cmd, after_cmd, BUF_SIZE - 1);
    sudo_cmd[BUF_SIZE - 1] = '\0';
    sudo_pw_len  = 0;
    sudo_pw_buf[0] = '\0';
    sudo_pending = 1;
    sudo_attempts = 0;
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_writestring("\n[sudo] password for ");
    terminal_writestring(username);
    terminal_writestring(": ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* ── Execute ─────────────────────────────────────────────────────────────── */
static void execute_cmd(const char* cmd);   /* forward decl */

static void execute(void) {
    terminal_putchar('\n');
    buf[buf_len] = '\0';
    hist_push(buf);
    hist_pos = -1;

    /* Trim leading spaces */
    char* cmd = buf;
    while (*cmd == ' ') cmd++;
    if (!*cmd) goto done;

    /* ── sudo prefix ── */
    if (strncmp(cmd, "sudo ", 5) == 0) {
        const char* subcmd = cmd + 5;
        while (*subcmd == ' ') subcmd++;
        if (is_root) {
            /* already root — run directly */
            execute_cmd(subcmd);
            goto done;
        }
        sudo_prompt(subcmd);
        return;   /* don't print prompt — sudo_pending will do it after auth */
    }

    /* ── su (switch to root) ── */
    if (strcmp(cmd, "su") == 0 || strcmp(cmd, "su root") == 0) {
        sudo_prompt("\x01");   /* special sentinel: just elevate, no command */
        return;
    }

    /* ── whoami ── */
    if (strcmp(cmd, "whoami") == 0) {
        terminal_printf("  %s\n", is_root ? "root" : username);
        goto done;
    }

    /* ── id ── */
    if (strcmp(cmd, "id") == 0) {
        if (is_root)
            terminal_writestring("  uid=0(root) gid=0(root) groups=0(root)\n");
        else
            terminal_printf("  uid=1000(%s) gid=1000(%s) groups=1000(%s),4(adm),27(sudo)\n",
                username, username, username);
        goto done;
    }

    /* ── exit root / logout ── */
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "logout") == 0) {
        if (is_root) {
            is_root = 0;
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            terminal_writestring("  Returned to user session.\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        } else {
            terminal_writestring("  logout\n");
        }
        goto done;
    }

    /* ── privilege gate for sensitive commands ── */
    if (!is_root && cmd_needs_root(cmd)) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_printf("  Permission denied. Use: sudo %s\n", cmd);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        goto done;
    }

    execute_cmd(cmd);
done:
    buf_len = 0;
    print_prompt();
    extern void wm_request_redraw(void);
    wm_request_redraw();
    return;
}

/* ── Installed-package runtime commands ─────────────────────────────────── */
static int pkg_is_installed(const char* name) {
    extern int pkg_list(void*, int);
    struct { char name[32]; char version[16]; char desc[128]; int inst; } pl[64];
    int pc = pkg_list(pl, 64);
    for (int i = 0; i < pc; i++)
        if (strcmp(pl[i].name, name) == 0) return pl[i].inst;
    return 0;
}

static void cmd_java(const char* args) {
    if (!pkg_is_installed("java")) {
        terminal_printf("  java: command not found\n  Install with: sudo apt install java\n");
        return;
    }
    if (strcmp(args, "-version") == 0 || strcmp(args, "--version") == 0) {
        terminal_printf("  openjdk version \"21.0.2\" 2024-01-16\n"
                        "  OpenJDK Runtime Environment (ElseaOS build 21.0.2+13)\n"
                        "  OpenJDK Server VM (ElseaOS build 21.0.2+13, mixed mode)\n");
    } else if (strncmp(args, "-jar ", 5) == 0) {
        terminal_printf("  [JVM] Loading %s ...\n  [JVM] Running on OpenJDK 21 (ElseaOS)\n", args + 5);
    } else if (*args) {
        terminal_printf("  [JVM] Compiling %s ...\n  [JVM] Done. Run with: java -jar %s\n", args, args);
    } else {
        terminal_printf("  Usage: java -version | java -jar <file.jar> | javac <File.java>\n");
    }
}

static void cmd_javac(const char* args) {
    if (!pkg_is_installed("java")) {
        terminal_printf("  javac: command not found\n  Install with: sudo apt install java\n");
        return;
    }
    if (*args)
        terminal_printf("  [javac] Compiling %s ...\n  [javac] Build successful.\n", args);
    else
        terminal_printf("  Usage: javac <SourceFile.java>\n");
}

static void cmd_python3(const char* args) {
    if (!pkg_is_installed("python3")) {
        terminal_printf("  python3: command not found\n  Install with: sudo apt install python3\n");
        return;
    }
    if (strcmp(args, "--version") == 0 || strcmp(args, "-V") == 0) {
        terminal_printf("  Python 3.12.2 (ElseaOS build)\n");
    } else if (*args) {
        terminal_printf("  [python3] Running %s ...\n", args);
    } else {
        terminal_printf("  Python 3.12.2  (ElseaOS)  Type exit() to quit.\n  >>> ");
    }
}

static void cmd_gcc(const char* args) {
    if (!pkg_is_installed("gcc")) {
        terminal_printf("  gcc: command not found\n  Install with: sudo apt install gcc\n");
        return;
    }
    if (strcmp(args, "--version") == 0) {
        terminal_printf("  gcc (ElseaOS 13.2.0) 13.2.0\n");
    } else if (*args) {
        terminal_printf("  [gcc] Compiling %s ...\n  [gcc] Linking ...\n  [gcc] Build complete.\n", args);
    } else {
        terminal_printf("  gcc: no input files\n  Usage: gcc <file.c> -o <output>\n");
    }
}

static void cmd_node(const char* args) {
    if (!pkg_is_installed("nodejs")) {
        terminal_printf("  node: command not found\n  Install with: sudo apt install nodejs\n");
        return;
    }
    if (strcmp(args, "--version") == 0 || strcmp(args, "-v") == 0) {
        terminal_printf("  v20.11.1\n");
    } else if (*args) {
        terminal_printf("  [node] Running %s ...\n", args);
    } else {
        terminal_printf("  Node.js v20.11.1  (ElseaOS)  Type .exit to quit.\n  > ");
    }
}

static void cmd_git(const char* args) {
    if (!pkg_is_installed("git")) {
        terminal_printf("  git: command not found\n  Install with: sudo apt install git\n");
        return;
    }
    if (strcmp(args, "--version") == 0) {
        terminal_printf("  git version 2.43.0 (ElseaOS)\n");
    } else if (strncmp(args, "clone ", 6) == 0) {
        terminal_printf("  Cloning into repository ...\n  remote: Counting objects: 100\n"
                        "  Receiving objects: 100%%  done.\n");
    } else if (strcmp(args, "status") == 0) {
        terminal_printf("  On branch main\n  nothing to commit, working tree clean\n");
    } else if (strcmp(args, "log") == 0 || strcmp(args, "log --oneline") == 0) {
        terminal_printf("  a1b2c3d  Initial commit\n");
    } else {
        terminal_printf("  git: %s\n  Try: git --version | git clone <url> | git status\n", *args ? args : "(no command)");
    }
}

static void cmd_vim(const char* args) {
    if (!pkg_is_installed("vim")) {
        terminal_printf("  vim: command not found\n  Install with: sudo pkg install vim\n");
        return;
    }
    terminal_printf("  VIM - Vi IMproved 9.1  (ElseaOS)\n");
    if (*args) terminal_printf("  Opening %s  [Press :q! to quit]\n", args);
}

static void cmd_curl(const char* args) {
    if (!pkg_is_installed("curl")) {
        terminal_printf("  curl: command not found\n  Install with: sudo apt install curl\n");
        return;
    }
    if (*args) {
        terminal_printf("  [curl] Fetching %s ...\n  200 OK  (simulated)\n", args);
    } else {
        terminal_printf("  curl: no URL specified.\n  Usage: curl <url>\n");
    }
}

static void cmd_htop(void) {
    if (!pkg_is_installed("htop")) {
        terminal_printf("  htop: command not found\n  Install with: sudo apt install htop\n");
        return;
    }
    terminal_printf("  htop 3.3.0  (ElseaOS)\n");
    cmd_ps();
}

/* ── Execute ─────────────────────────────────────────────────────────────── */
static void execute_cmd(const char* cmd) {

    if      (strcmp(cmd, "help")  == 0) cmd_help();
    else if (strcmp(cmd, "clear") == 0) terminal_clear();
    else if (strcmp(cmd, "info")  == 0) cmd_info();
    else if (strcmp(cmd, "c4")    == 0) {
        extern int elf_load_and_run(const char*, const char*);
        int pid = elf_load_and_run("c4.elf", NULL);
        if (pid > 0) task_waitpid(pid);
    }
    else if (strcmp(cmd, "mem")   == 0) cmd_mem();
    else if (strcmp(cmd, "uptime")== 0) cmd_uptime();
    else if (strcmp(cmd, "date")  == 0) cmd_date();
    else if (strcmp(cmd, "ls")    == 0 || strncmp(cmd, "ls ", 3) == 0) {
        extern int elf_load_and_run(const char*, const char*);
        int pid = elf_load_and_run("bin/ls.elf", cmd + 3);
        if (pid > 0) task_waitpid(pid);
    }
    else if (strcmp(cmd, "ps")    == 0) cmd_ps();
    /* ── Installed package commands ── */
    else if (strcmp(cmd, "java") == 0 || strncmp(cmd, "java ", 5) == 0)
        cmd_java(cmd[4] ? cmd + 5 : "");
    else if (strcmp(cmd, "javac") == 0 || strncmp(cmd, "javac ", 6) == 0)
        cmd_javac(cmd[5] ? cmd + 6 : "");
    else if (strcmp(cmd, "python3") == 0 || strncmp(cmd, "python3 ", 8) == 0)
        cmd_python3(cmd[7] ? cmd + 8 : "");
    else if (strcmp(cmd, "python") == 0 || strncmp(cmd, "python ", 7) == 0)
        cmd_python3(cmd[6] ? cmd + 7 : "");
    else if (strcmp(cmd, "gcc") == 0 || strncmp(cmd, "gcc ", 4) == 0)
        cmd_gcc(cmd[3] ? cmd + 4 : "");
    else if (strcmp(cmd, "g++") == 0 || strncmp(cmd, "g++ ", 4) == 0)
        cmd_gcc(cmd[3] ? cmd + 4 : "");
    else if (strcmp(cmd, "node") == 0 || strncmp(cmd, "node ", 5) == 0)
        cmd_node(cmd[4] ? cmd + 5 : "");
    else if (strcmp(cmd, "npm") == 0 || strncmp(cmd, "npm ", 4) == 0)
        cmd_node(cmd[3] ? cmd + 4 : "");
    else if (strcmp(cmd, "git") == 0 || strncmp(cmd, "git ", 4) == 0)
        cmd_git(cmd[3] ? cmd + 4 : "");
    else if (strcmp(cmd, "vim") == 0 || strncmp(cmd, "vim ", 4) == 0)
        cmd_vim(cmd[3] ? cmd + 4 : "");
    else if (strcmp(cmd, "curl") == 0 || strncmp(cmd, "curl ", 5) == 0)
        cmd_curl(cmd[4] ? cmd + 5 : "");
    else if (strcmp(cmd, "htop") == 0) cmd_htop();
    else if (strncmp(cmd, "sleep ",6) == 0) {
        uint32_t ms = (uint32_t)strtol(cmd + 6, NULL, 10);
        terminal_printf("  Sleeping %u ms...\n", ms);
        task_sleep(ms);
        terminal_writestring("  Done.\n");
    }
    else if (strcmp(cmd, "reboot")== 0) cmd_reboot();
    else if (strcmp(cmd, "halt")  == 0) cmd_halt();
    else if (strcmp(cmd, "install")== 0) cmd_install();
    else if (strncmp(cmd, "echo ",   5) == 0) {
        extern int elf_load_and_run(const char*, const char*);
        int pid = elf_load_and_run("bin/echo.elf", cmd + 5);
        if (pid > 0) task_waitpid(pid);
    }
    else if (strncmp(cmd, "cat ",    4) == 0) {
        extern int elf_load_and_run(const char*, const char*);
        int pid = elf_load_and_run("bin/cat.elf", cmd + 4);
        if (pid > 0) task_waitpid(pid);
    }
    else if (strncmp(cmd, "calc ",   5) == 0) cmd_calc(cmd + 5);
    else if (strncmp(cmd, "hexdump", 7) == 0) cmd_hexdump(cmd[7] ? cmd + 8 : "0x100000");
    else if (strncmp(cmd, "color ",  6) == 0) cmd_color(cmd + 6);
    else if (strncmp(cmd, "hdread ", 7) == 0) {
        int lba = (int)strtol(cmd + 7, NULL, 10);
        uint8_t buf[512];
        ata_read_sector(lba, buf);
        buf[511] = '\0';
        terminal_printf("  Sector %d: %s\n", lba, (char*)buf);
    }
    else if (strncmp(cmd, "hdwrite ", 8) == 0) {
        int lba = 1; // Default
        char* text = cmd + 8;
        if (*text >= '0' && *text <= '9') {
            char* p;
            lba = (int)strtol(text, &p, 10);
            text = p;
            while (*text == ' ') text++;
        }
        uint8_t buf[512] = {0};
        strcpy((char*)buf, text);
        ata_write_sector(lba, buf);
        terminal_printf("  Wrote to sector %d!\n", lba);
    }
    else if (strcmp(cmd, "lsfs") == 0) {
        fs_file_info_t files[20];
        int num = fs_list_files(files);
        terminal_printf("  %d files found:\n", num);
        for (int i = 0; i < num; i++) {
            terminal_printf("    %s (%d bytes)\n", files[i].name, files[i].size);
        }
    }
    else if (strncmp(cmd, "catfs ", 6) == 0) {
        char buf[512];
        if (fs_read_file(cmd + 6, buf) == 0) {
            terminal_printf("  %s\n", buf);
        } else {
            terminal_printf("  File not found.\n");
        }
    }
    else if (strncmp(cmd, "mkfile ", 7) == 0) {
        char name[16] = {0};
        char* p = cmd + 7;
        int i = 0;
        while (*p && *p != ' ' && i < 15) { name[i++] = *p++; }
        while (*p == ' ') p++; // Skip spaces
        
        if (fs_write_file(name, p) == 0) {
            terminal_printf("  File '%s' created.\n", name);
        } else {
            terminal_printf("  Failed to create file.\n");
        }
    }
    else if (strcmp(cmd, "formatfs") == 0) {
        fs_format();
        terminal_printf("  MyFS formatted successfully.\n");
    }
    else if (strncmp(cmd, "beep ", 5) == 0) {
        int freq = (int)strtol(cmd + 5, NULL, 10);
        if (freq < 20 || freq > 20000) freq = 1000;
        speaker_beep(freq, 200);
        terminal_printf("  Beep at %d Hz.\n", freq);
    }
    else if (strcmp(cmd, "fat ls") == 0) {
        fat16_file_info_t files[64];
        int n = fat16_list_files(files, 64);
        if (n < 0) {
            terminal_writestring("  FAT16 not ready.\n");
        } else if (n == 0) {
            terminal_writestring("  (no files on disk)\n");
        } else {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            terminal_writestring("  NAME              SIZE\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            terminal_writestring("  ─────────────── ────────\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            for (int i = 0; i < n; i++)
                terminal_printf("  %-16s %u bytes\n", files[i].name, files[i].size);
            terminal_printf("\n  %d file(s)\n", n);
        }
    }
    else if (strncmp(cmd, "fat read ", 9) == 0) {
        static uint8_t fat_rbuf[8192];
        int r = fat16_read_file(cmd + 9, fat_rbuf, sizeof(fat_rbuf));
        if (r < 0) {
            terminal_writestring("  File not found.\n");
        } else {
            terminal_writestring((char*)fat_rbuf);
            terminal_putchar('\n');
        }
    }
    else if (strncmp(cmd, "fat write ", 10) == 0) {
        /* fat write <name> <content> */
        const char* p = cmd + 10;
        char fname[16] = {0};
        int i = 0;
        while (*p && *p != ' ' && i < 15) fname[i++] = *p++;
        while (*p == ' ') p++;
        int r = fat16_write_file(fname, (const uint8_t*)p, strlen(p));
        if (r < 0)
            terminal_writestring("  Write failed (disk full?).\n");
        else
            terminal_printf("  Wrote %d bytes to '%s'.\n", r, fname);
    }
    else if (strncmp(cmd, "fat del ", 8) == 0) {
        if (fat16_delete_file(cmd + 8) == 0)
            terminal_printf("  Deleted '%s'.\n", cmd + 8);
        else
            terminal_writestring("  File not found.\n");
    }
    else if (strncmp(cmd, "exec ", 5) == 0) {
        extern int elf_load_and_run(const char*, const char*);
        int pid = elf_load_and_run(cmd + 5, NULL);
        if (pid > 0) task_waitpid(pid);
    }
    // ── http_get — supports both IP and hostname ──────────────────────────────
    else if (strncmp(cmd, "http_get ", 9) == 0) {
        char* target = cmd + 9;
        uint32_t dest_ip = 0;

        // Try DNS resolution (handles both raw IPs and hostnames)
        if (!dns_resolve(target, &dest_ip)) {
            terminal_printf("  Cannot resolve '%s'.\n", target);
            return;
        }

        terminal_printf("  Connecting to %d.%d.%d.%d:80...\n",
            (dest_ip >> 24)&0xFF, (dest_ip >> 16)&0xFF,
            (dest_ip >>  8)&0xFF,  dest_ip & 0xFF);

        if (tcp_connect(dest_ip, 80) < 0) return;

        // Build minimal HTTP/1.0 request
        char req[256];
        snprintf(req, sizeof(req),
            "GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", target);
        tcp_send_data((uint8_t*)req, strlen(req));

        // Wait up to 5 s for data
        uint32_t timeout = pit_get_ticks() + 5000;
        while (!tcp_has_data && pit_get_ticks() < timeout) {}

        if (tcp_has_data) {
            terminal_writestring("\n--- HTTP RESPONSE ---\n");
            terminal_writestring((char*)tcp_recv_buffer);
            terminal_writestring("\n---------------------\n");
        } else {
            terminal_writestring("  No response received.\n");
        }
        tcp_close(0);
    }

    // ── nslookup — DNS hostname to IP lookup ─────────────────────────────────
    else if (strncmp(cmd, "nslookup ", 9) == 0) {
        char* hostname = cmd + 9;
        uint32_t ip = 0;
        terminal_printf("  Resolving '%s'...\n", hostname);
        if (dns_resolve(hostname, &ip)) {
            terminal_printf("  %s -> %d.%d.%d.%d\n", hostname,
                (ip >> 24)&0xFF, (ip >> 16)&0xFF, (ip >> 8)&0xFF, ip & 0xFF);
        } else {
            terminal_printf("  DNS resolution failed for '%s'.\n", hostname);
        }
    }

    // ── netstat — show current TCP connection status ──────────────────────────
    else if (strcmp(cmd, "netstat") == 0) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_writestring("\n  Active Network Connections:\n");
        terminal_writestring("  ─────────────────────────────────────────\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        if (tcp_is_connected) {
            terminal_printf("  TCP  ESTABLISHED   RX=%u bytes\n", tcp_recv_len);
        } else {
            terminal_writestring("  TCP  IDLE\n");
        }
        terminal_writestring("  UDP  Ready (port handler dispatch active)\n");
        terminal_writestring("  DNS  Cache ready (8.8.8.8)\n");
        terminal_putchar('\n');
    }

    else if (strncmp(cmd, "c4", 2) == 0) {
        terminal_printf("Executing c4.elf (C Compiler)...\n");
        extern int elf_load_and_run(const char*, const char*);
        int pid = elf_load_and_run("c4.elf", NULL);
        if (pid > 0) task_waitpid(pid);
    } else if (strncmp(cmd, "ping ", 5) == 0) {
        // Parse IP
        char* ip_str = cmd + 5;
        uint32_t a = 0, b = 0, c = 0, d = 0;
        int i = 0, part = 0, val = 0;
        uint32_t ip = 0;
        while (ip_str[i]) {
            if (ip_str[i] == '.') {
                ip |= (val << (24 - part * 8));
                part++; val = 0;
            } else if (ip_str[i] >= '0' && ip_str[i] <= '9') {
                val = val * 10 + (ip_str[i] - '0');
            }
            i++;
        }
        ip |= val;
        (void)a; (void)b; (void)c; (void)d;
        terminal_printf("  PING %d.%d.%d.%d — 3 packets\n",
            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        extern int arp_resolve(uint32_t, uint8_t*);
        extern void icmp_send_echo(uint32_t, uint8_t*);
        extern volatile int icmp_got_reply;
        extern volatile uint32_t icmp_reply_src;
        uint8_t mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        if (!arp_resolve(ip, mac)) {
            terminal_printf("  ARP failed — host unreachable.\n");
        } else {
            int _replied = 0;
            for (int _seq = 1; _seq <= 3; _seq++) {
                icmp_got_reply = 0;
                icmp_send_echo(ip, mac);
                uint32_t _dl = pit_get_ticks() + 2000;
                while (!icmp_got_reply && pit_get_ticks() < _dl) {}
                if (icmp_got_reply) {
                    terminal_printf("  Reply from %d.%d.%d.%d: seq=%d alive\n",
                        (ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF,_seq);
                    _replied++;
                } else {
                    terminal_printf("  Request timeout for seq=%d\n", _seq);
                }
            }
            terminal_printf("  3 sent, %d received, %d%% loss\n", _replied, (3-_replied)*100/3);
        }
    } else if (strncmp(cmd, "arp", 3) == 0) {
        terminal_writestring("  Sending ARP request for 10.0.2.2 (gateway)...\n");
        extern void arp_send_request(uint32_t);
        arp_send_request(0x0202000A); // 10.0.2.2
        terminal_writestring("  ARP request sent. Check serial log for replies.\n");
    } else if (strcmp(cmd, "ifconfig") == 0) {
        extern uint8_t* rtl8139_get_mac(void);
        uint8_t* mac = rtl8139_get_mac();
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_writestring("\n  eth0    RTL8139 Network Interface\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        terminal_writestring("  ─────────────────────────────────────────\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_printf("  IPv4:  10.0.2.15/24\n");
        terminal_printf("  MAC:   %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        terminal_printf("  GW:    10.0.2.2\n");
        terminal_printf("  DNS:   8.8.8.8\n");
        terminal_printf("  MTU:   1500 bytes\n");
        terminal_putchar('\n');
    } else if (strcmp(cmd, "dns") == 0) {
        terminal_writestring("  Usage: nslookup <hostname>\n");
    } else if (strcmp(cmd, "ext2 mount") == 0) {
        extern void ext2_init(void);
        extern int ext2_is_mounted(void);
        ext2_init();
        terminal_printf(ext2_is_mounted() ? "  EXT2 mounted at /mnt/ext2\n" : "  EXT2 not found.\n");
    } else if (strcmp(cmd, "ext2 ls") == 0) {
        extern int ext2_ls(char names[][13], int max);
        char names[64][13];
        int n = ext2_ls(names, 64);
        if (n < 0) { terminal_printf("  EXT2 not mounted.\n"); }
        else if (n == 0) { terminal_printf("  (empty)\n"); }
        else {
            terminal_printf("  EXT2 root directory (%d entries):\n", n);
            for (int i = 0; i < n; i++) terminal_printf("    %s\n", names[i]);
        }
    } else if (strncmp(cmd, "ext2 read ", 10) == 0) {
        extern int ext2_read_file(const char*, uint8_t*, uint32_t);
        static uint8_t _e2buf[4096];
        int r = ext2_read_file(cmd + 10, _e2buf, sizeof(_e2buf) - 1);
        if (r < 0) terminal_printf("  File not found: %s\n", cmd + 10);
        else { _e2buf[r] = '\0'; terminal_printf("%s\n", _e2buf); }
    } else if (strncmp(cmd, "nvme read ", 10) == 0) {
        extern int nvme_read_sector(uint64_t lba, void* buf);
        static uint8_t _nvbuf[512];
        uint64_t lba = (uint64_t)strtol(cmd + 10, NULL, 10);
        if (nvme_read_sector(lba, _nvbuf)) {
            terminal_printf("  NVMe LBA %llu:\n", lba);
            for (int i = 0; i < 64; i++) {
                if (i % 16 == 0) terminal_printf("  %03x: ", i);
                terminal_printf("%02x ", _nvbuf[i]);
                if (i % 16 == 15) terminal_putchar('\n');
            }
        } else { terminal_printf("  NVMe read failed.\n"); }
    } else if (strcmp(cmd, "lsusb") == 0) {
        extern void usb_list_devices(void);
        usb_list_devices();
    } else if (strcmp(cmd, "fat32 ls") == 0) {
        extern int fat32_init(void);
        extern int fat32_ls_dir(const char*, char[][13], int);
        if (!fat32_init()) { terminal_printf("  FAT32: no volume found.\n"); }
        else {
            char _names[32][13];
            int _n = fat32_ls_dir("/", _names, 32);
            for (int _i = 0; _i < _n; _i++) terminal_printf("  %s\n", _names[_i]);
            terminal_printf("  %d files.\n", _n);
        }
    } else if (strncmp(cmd, "fat32 read ", 11) == 0) {
        extern int fat32_init(void);
        extern int fat32_read_file(const char*, uint8_t*, uint32_t);
        if (!fat32_init()) { terminal_printf("  FAT32: no volume found.\n"); }
        else {
            static uint8_t _fbuf[4096];
            int _r = fat32_read_file(cmd + 11, _fbuf, sizeof(_fbuf) - 1);
            if (_r < 0) terminal_printf("  File not found.\n");
            else { _fbuf[_r] = '\0'; terminal_printf("%s\n", (char*)_fbuf); }
        }
    } else if (strcmp(cmd, "dhcp") == 0) {
        extern int dhcp_discover(void);
        extern uint32_t my_ip, gateway_ip, subnet_mask, dns_server_ip;
        terminal_printf("  Sending DHCP DISCOVER...\n");
        if (dhcp_discover()) {
            uint8_t* _a = (uint8_t*)&my_ip;
            uint8_t* _g = (uint8_t*)&gateway_ip;
            uint8_t* _d = (uint8_t*)&dns_server_ip;
            uint8_t* _m = (uint8_t*)&subnet_mask;
            terminal_printf("  IP:      %d.%d.%d.%d\n", _a[0],_a[1],_a[2],_a[3]);
            terminal_printf("  Mask:    %d.%d.%d.%d\n", _m[0],_m[1],_m[2],_m[3]);
            terminal_printf("  Gateway: %d.%d.%d.%d\n", _g[0],_g[1],_g[2],_g[3]);
            terminal_printf("  DNS:     %d.%d.%d.%d\n", _d[0],_d[1],_d[2],_d[3]);
        } else {
            terminal_printf("  DHCP failed — no server responded.\n");
        }
    } else if (strcmp(cmd, "nvme fat ls") == 0) {
        /* mount FAT16 BPB from NVMe LBA 0 and list root directory */
        extern int nvme_read_sector(uint64_t lba, void* buf);
        static uint8_t _nvbpb[512];
        if (!nvme_read_sector(0, _nvbpb)) {
            terminal_printf("  NVMe read failed.\n");
        } else {
            /* Parse BPB manually — bytes per sector, sectors per cluster etc. */
            uint16_t bytes_per_sec  = _nvbpb[11] | (_nvbpb[12] << 8);
            uint8_t  sec_per_clust  = _nvbpb[13];
            uint16_t rsvd_sectors   = _nvbpb[14] | (_nvbpb[15] << 8);
            uint8_t  num_fats       = _nvbpb[16];
            uint16_t root_entries   = _nvbpb[17] | (_nvbpb[18] << 8);
            uint16_t secs_per_fat   = _nvbpb[22] | (_nvbpb[23] << 8);
            (void)sec_per_clust; (void)secs_per_fat;
            terminal_printf("  NVMe FAT16  bytes/sec=%u  rsvd=%u  FATs=%u  root=%u\n",
                            bytes_per_sec, rsvd_sectors, num_fats, root_entries);
            /* Root dir starts at LBA: rsvd + num_fats * secs_per_fat */
            uint32_t root_lba = rsvd_sectors + num_fats * secs_per_fat;
            uint32_t root_size = (root_entries * 32 + 511) / 512;
            terminal_printf("  Root dir: LBA %u, %u sectors\n", root_lba, root_size);
            static uint8_t _nvdir[512];
            int _listed = 0;
            for (uint32_t _sl = 0; _sl < root_size && _listed < 32; _sl++) {
                if (!nvme_read_sector(root_lba + _sl, _nvdir)) break;
                for (int _e = 0; _e < 512/32 && _listed < 32; _e++) {
                    uint8_t* _de = _nvdir + _e * 32;
                    if (_de[0] == 0x00) goto _done_nvme_ls;
                    if (_de[0] == 0xE5 || _de[11] == 0x0F) continue; /* del / LFN */
                    char _fn[13]; int _fi = 0;
                    for (int _c = 0; _c < 8 && _de[_c] != ' '; _c++) _fn[_fi++] = _de[_c];
                    if (_de[8] != ' ') { _fn[_fi++] = '.'; for (int _c = 8; _c < 11 && _de[_c] != ' '; _c++) _fn[_fi++] = _de[_c]; }
                    _fn[_fi] = '\0';
                    uint32_t _fsz = _de[28] | (_de[29]<<8) | (_de[30]<<16) | (_de[31]<<24);
                    terminal_printf("  %s  (%u bytes)\n", _fn, _fsz);
                    _listed++;
                }
            }
            _done_nvme_ls:
            terminal_printf("  %d entries.\n", _listed);
        }
    } else if (strncmp(cmd, "wav play ", 9) == 0) {
        extern int wav_parse(const uint8_t*, uint32_t, void*);
        extern int wav_play(const void*);
        size_t _wsz = 0;
        const uint8_t* _wd = (const uint8_t*)tar_get_file(cmd + 9, &_wsz);
        if (!_wd) { terminal_printf("  File not found: %s\n", cmd + 9); }
        else {
            static uint8_t _winfo[64];
            int r = wav_parse(_wd, (uint32_t)_wsz, _winfo);
            if (r == 0) { wav_play(_winfo); terminal_printf("  Playing '%s'...\n", cmd + 9); }
            else terminal_writestring("  WAV parse failed.\n");
        }
    } else if (strcmp(cmd, "wav stop") == 0) {
        extern void sb16_stop(void);
        sb16_stop();
        terminal_writestring("  Audio stopped.\n");
    } else if (strcmp(cmd, "mount") == 0) {
        extern void vfs_print_mounts(void);
        vfs_print_mounts();
    } else if (strncmp(cmd, "vls", 3) == 0) {
        extern int vfs_ls(const char*, char[][64], int);
        const char* path = (cmd[3] == ' ') ? cmd + 4 : "/";
        char names[32][64];
        int n = vfs_ls(path, names, 32);
        if (n < 0) terminal_printf("  vls: no mount for '%s'\n", path);
        else { for (int i = 0; i < n; i++) terminal_printf("  %s\n", names[i]); terminal_printf("  %d entries.\n", n); }
    } else if (strncmp(cmd, "fat32 write ", 12) == 0) {
        extern int fat32_write_file(const char*, const uint8_t*, uint32_t);
        const char* p = cmd + 12;
        char fname[16] = {0}; int i = 0;
        while (*p && *p != ' ' && i < 15) fname[i++] = *p++;
        while (*p == ' ') p++;
        int r = fat32_write_file(fname, (const uint8_t*)p, (uint32_t)strlen(p));
        terminal_printf(r < 0 ? "  fat32 write failed.\n" : "  Wrote %d bytes to '%s'.\n", r, fname);
    } else if (strncmp(cmd, "fat32 mkdir ", 12) == 0) {
        extern int fat32_mkdir(const char*);
        int r = fat32_mkdir(cmd + 12);
        terminal_printf(r == 0 ? "  Created '%s'.\n" : "  mkdir failed.\n", cmd + 12);
    } else if (strcmp(cmd, "ipcs") == 0) {
        extern void ipc_print_all(void);
        ipc_print_all();
    } else if (strncmp(cmd, "usb read ", 9) == 0) {
        extern int usb_msc_read_sector(uint32_t, void*);
        uint32_t lba = 0; const char* p = cmd + 9;
        while (*p >= '0' && *p <= '9') { lba = lba * 10 + (*p - '0'); p++; }
        static uint8_t usb_buf[512];
        if (usb_msc_read_sector(lba, usb_buf) == 0) {
            for (int i = 0; i < 512; i++) { terminal_printf("%02x ", usb_buf[i]); if ((i+1)%16==0) terminal_printf("\n"); }
        } else terminal_printf("  USB read failed.\n");
    } else if (strncmp(cmd, "raw open ", 9) == 0) {
        extern int raw_socket_open(int);
        int proto = 0; const char* p = cmd + 9;
        while (*p >= '0' && *p <= '9') { proto = proto * 10 + (*p - '0'); p++; }
        int fd = raw_socket_open(proto);
        terminal_printf(fd >= 0 ? "  Opened raw socket fd=%d proto=%d\n" : "  Failed to open raw socket\n", fd, proto);
    } else if (strncmp(cmd, "raw close ", 10) == 0) {
        extern void raw_socket_close(int);
        int fd = 0; const char* p = cmd + 10;
        while (*p >= '0' && *p <= '9') { fd = fd * 10 + (*p - '0'); p++; }
        raw_socket_close(fd);
        terminal_printf("  Closed raw socket fd=%d\n", fd);
    } else if (strncmp(cmd, "pkg update",    10) == 0
            || strncmp(cmd, "apt update",    10) == 0
            || strncmp(cmd, "apt-get update",14) == 0) {
        terminal_writestring("  Hit:1 http://pkg.elseaos.org elseaos InRelease\n");
        extern int pkg_update(void);
        pkg_update();
        terminal_writestring("  Reading package lists... Done\n");
        terminal_writestring("  Building dependency tree... Done\n");

    } else if (strncmp(cmd, "pkg upgrade",    11) == 0
            || strncmp(cmd, "apt upgrade",    11) == 0
            || strncmp(cmd, "apt-get upgrade",15) == 0) {
        terminal_writestring("  Reading package lists... Done\n");
        terminal_writestring("  Building dependency tree... Done\n");
        terminal_writestring("  Calculating upgrade... Done\n");
        terminal_writestring("  0 upgraded, 0 newly installed, 0 to remove.\n");

    } else if (strncmp(cmd, "pkg install ",    12) == 0
            || strncmp(cmd, "apt install ",    12) == 0
            || strncmp(cmd, "apt-get install ",16) == 0) {
        /* extract package name — skip past "apt install " or "apt-get install " etc */
        const char* pname = cmd;
        while (*pname && *pname != ' ') pname++;   /* skip verb */
        while (*pname == ' ') pname++;              /* skip spaces */
        while (*pname && *pname != ' ') pname++;   /* skip subverb */
        while (*pname == ' ') pname++;              /* skip spaces -> package name */
        /* strip flags like -y */
        if (*pname == '-') { while (*pname && *pname != ' ') pname++; while (*pname == ' ') pname++; }
        terminal_printf("  Reading package lists... Done\n");
        terminal_printf("  Building dependency tree... Done\n");
        terminal_printf("  The following NEW packages will be installed:\n    %s\n", pname);
        terminal_printf("  0 upgraded, 1 newly installed, 0 to remove.\n");
        terminal_printf("  Need to get 0 B of archives.\n");
        terminal_printf("  Selecting previously unselected package %s.\n", pname);
        terminal_printf("  Unpacking %s ...\n", pname);
        extern int pkg_install(const char*);
        pkg_install(pname);
        terminal_printf("  Setting up %s ... Done\n", pname);
        terminal_printf("  Processing triggers for man-db ... Done\n");

    } else if (strncmp(cmd, "pkg remove ",     11) == 0
            || strncmp(cmd, "pkg purge ",      10) == 0
            || strncmp(cmd, "apt remove ",     11) == 0
            || strncmp(cmd, "apt purge ",      10) == 0
            || strncmp(cmd, "apt-get remove ", 15) == 0
            || strncmp(cmd, "apt-get purge ",  14) == 0) {
        const char* pname = cmd;
        while (*pname && *pname != ' ') pname++;
        while (*pname == ' ') pname++;
        while (*pname && *pname != ' ') pname++;
        while (*pname == ' ') pname++;
        terminal_printf("  Reading package lists... Done\n");
        terminal_printf("  The following packages will be REMOVED:\n    %s\n", pname);
        extern int pkg_remove(const char*);
        pkg_remove(pname);
        terminal_printf("  Removing %s ... Done\n", pname);

    } else if (strcmp(cmd, "pkg list") == 0
            || strcmp(cmd, "apt list")  == 0
            || strcmp(cmd, "apt-cache pkgnames") == 0
            || strcmp(cmd, "dpkg --list") == 0) {
        extern int pkg_list(void*, int);
        struct { char name[32]; char version[16]; char desc[128]; int inst; } pl[64];
        int pc = pkg_list(pl, 64);
        terminal_printf("  Listing... Done\n");
        for (int i = 0; i < pc; i++)
            terminal_printf("  %-22s %-8s %s  [%s]\n",
                pl[i].name, pl[i].version, pl[i].desc,
                pl[i].inst ? "installed" : "available");

    } else if (strcmp(cmd, "apt list --installed") == 0
            || strcmp(cmd, "pkg list --installed") == 0
            || strcmp(cmd, "dpkg -l")   == 0
            || strcmp(cmd, "dpkg -l --") == 0) {
        extern int pkg_list(void*, int);
        struct { char name[32]; char version[16]; char desc[128]; int inst; } pl[64];
        int pc = pkg_list(pl, 64);
        terminal_printf("  Listing installed packages...\n");
        int found = 0;
        for (int i = 0; i < pc; i++) {
            if (pl[i].inst) {
                terminal_printf("  ii  %-20s %-8s %s\n", pl[i].name, pl[i].version, pl[i].desc);
                found++;
            }
        }
        if (!found) terminal_printf("  (no packages installed)\n");

    } else if (strncmp(cmd, "pkg search ",      11) == 0
            || strncmp(cmd, "apt search ",      11) == 0
            || strncmp(cmd, "apt-cache search ",17) == 0) {
        const char* term = cmd + (cmd[0]=='p' ? 11 : cmd[4]=='s' ? 11 : 17);
        extern int pkg_list(void*, int);
        struct { char name[32]; char version[16]; char desc[128]; int inst; } pl[64];
        int pc = pkg_list(pl, 64);
        terminal_printf("  Sorting... Done\n  Full-Text Search... Done\n");
        int found = 0;
        for (int i = 0; i < pc; i++) {
            /* simple substring search in name and desc */
            int nm=0, di=0;
            for (int k=0; pl[i].name[k]; k++)
                if (pl[i].name[k]==term[0]) { nm=1; break; }
            for (int k=0; pl[i].desc[k]; k++)
                if (pl[i].desc[k]==term[0]) { di=1; break; }
            /* just show all that start with the same letter for simplicity */
            if (nm || di) {
                terminal_printf("  %-22s - %s\n", pl[i].name, pl[i].desc);
                found++;
            }
        }
        if (!found) terminal_printf("  No results for '%s'\n", term);

    } else if (strncmp(cmd, "pkg show ",       9) == 0
            || strncmp(cmd, "apt show ",       9) == 0
            || strncmp(cmd, "apt-cache show ", 15) == 0) {
        const char* pname = cmd + (cmd[0]=='p' ? 9 : cmd[4]=='s' ? 9 : 15);
        extern int pkg_list(void*, int);
        struct { char name[32]; char version[16]; char desc[128]; int inst; } pl[64];
        int pc = pkg_list(pl, 64);
        for (int i = 0; i < pc; i++) {
            if (strcmp(pl[i].name, pname) == 0) {
                terminal_printf("  Package: %s\n  Version: %s\n  Status: %s\n  Description: %s\n",
                    pl[i].name, pl[i].version,
                    pl[i].inst ? "install ok installed" : "install ok not-installed",
                    pl[i].desc);
                goto apt_done;
            }
        }
        terminal_printf("  E: No packages found matching '%s'\n", pname);
        apt_done:;
    } else if (strcmp(cmd, "ota check") == 0) {
        extern int ota_check_update(void);
        ota_check_update();
    } else if (strcmp(cmd, "ota install") == 0) {
        extern int ota_download_and_install(void);
        ota_download_and_install();
    } else if (strcmp(cmd, "bt scan") == 0) {
        extern void bluetooth_scan(void);
        bluetooth_scan();
    } else if (strncmp(cmd, "bt connect ", 11) == 0) {
        extern int bluetooth_connect(int);
        int idx = 0; const char* p = cmd + 11;
        while (*p >= '0' && *p <= '9') { idx = idx * 10 + (*p - '0'); p++; }
        bluetooth_connect(idx);
    } else if (strncmp(cmd, "bt disconnect ", 14) == 0) {
        extern int bluetooth_disconnect(int);
        int idx = 0; const char* p = cmd + 14;
        while (*p >= '0' && *p <= '9') { idx = idx * 10 + (*p - '0'); p++; }
        bluetooth_disconnect(idx);
    } else if (strcmp(cmd, "wifi scan") == 0) {
        extern int wifi_scan(void*, int);
        struct { char ssid[32]; uint8_t bssid[6]; int sig; int enc; } nets[8];
        int n = wifi_scan(nets, 8);
        for (int i = 0; i < n; i++)
            terminal_printf("  [%d] %-24s  signal=%d  %s\n", i, nets[i].ssid, nets[i].sig, nets[i].enc ? "WPA2" : "Open");
    } else if (strncmp(cmd, "wifi connect ", 13) == 0) {
        extern int wifi_connect(const char*, const char*);
        /* parse "ssid password" */
        const char* p = cmd + 13;
        char ssid[33] = {0}; int si = 0;
        while (*p && *p != ' ' && si < 32) { ssid[si++] = *p++; }
        while (*p == ' ') p++;
        wifi_connect(ssid, p);
    } else if (strcmp(cmd, "wifi disconnect") == 0) {
        extern int wifi_disconnect(void);
        wifi_disconnect();
    } else if (strncmp(cmd, "lang ", 5) == 0) {
        extern void i18n_set_language(const char*);
        i18n_set_language(cmd + 5);
        terminal_printf("  Language set to: %s\n", cmd + 5);
    } else if (strncmp(cmd, "fde unlock ", 11) == 0) {
        extern int fde_unlock_disk(const char*);
        if (fde_unlock_disk(cmd + 11))
            terminal_printf("  FDE: disk unlocked.\n");
        else
            terminal_printf("  FDE: unlock failed.\n");
    } else {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_printf("  Command not found: %s\n", cmd);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    buf_len = 0;
}

/* ── Keypress handler ────────────────────────────────────────────────────── */
/* Replace the current typed line (erases old chars, echoes new ones) */
static void shell_set_input(const char* line) {
    while (buf_len > 0) { buf_len--; terminal_putchar('\b'); }
    buf_len = 0;
    while (line[buf_len] && buf_len < BUF_SIZE - 1) {
        buf[buf_len] = line[buf_len];
        terminal_putchar(line[buf_len]);
        buf_len++;
    }
    buf[buf_len] = '\0';
}

void shell_handle_keypress(char c) {
    /* ── sudo password mode ── */
    if (sudo_pending) {
        if (c == '\n') {
            terminal_putchar('\n');
            sudo_pw_buf[sudo_pw_len] = '\0';
            if (strcmp(sudo_pw_buf, shell_root_password) == 0) {
                /* Correct password */
                sudo_pending  = 0;
                sudo_attempts = 0;
                if (sudo_cmd[0] == '\x01') {
                    /* su — just elevate */
                    is_root = 1;
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    terminal_writestring("  Switched to root. Type 'exit' to return.\n");
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                } else {
                    /* run the queued command as root */
                    int saved = is_root;
                    is_root = 1;
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                    terminal_writestring("  [sudo] Running as root: ");
                    terminal_writestring(sudo_cmd);
                    terminal_putchar('\n');
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    execute_cmd(sudo_cmd);
                    is_root = saved;
                }
            } else {
                sudo_attempts++;
                terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                terminal_writestring("  Sorry, try again.\n");
                terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                if (sudo_attempts >= 3) {
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    terminal_writestring("  sudo: 3 incorrect password attempts\n");
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    sudo_pending  = 0;
                    sudo_attempts = 0;
                } else {
                    /* Retry */
                    sudo_pw_len = 0;
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    terminal_writestring("[sudo] password for ");
                    terminal_writestring(username);
                    terminal_writestring(": ");
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    return;
                }
            }
            /* Print fresh prompt after sudo resolves */
            sudo_pw_len = 0;
            buf_len = 0;
            print_prompt();
            extern void wm_request_redraw(void);
            wm_request_redraw();
        } else if (c == '\b') {
            if (sudo_pw_len > 0) sudo_pw_len--;
            /* don't echo anything — password is hidden */
        } else if (c >= ' ' && c <= '~') {
            if (sudo_pw_len < 63) sudo_pw_buf[sudo_pw_len++] = c;
            /* echo '*' for each character */
            terminal_putchar('*');
            extern void wm_request_redraw(void);
            wm_request_redraw();
        }
        return;
    }

    if (c == '\n') {
        execute();
    } else if (c == '\b') {
        if (buf_len > 0) { buf_len--; terminal_putchar('\b'); }
    } else if (c == '\x10') { /* Up arrow — history prev */
        if (hist_count == 0) return;
        if (hist_pos == -1)
            hist_pos = hist_count - 1;
        else if (hist_pos > (int)(hist_count > HISTORY_SLOTS ? hist_count - HISTORY_SLOTS : 0))
            hist_pos--;
        shell_set_input(history[hist_pos % HISTORY_SLOTS]);
    } else if (c == '\x11') { /* Down arrow — history next */
        if (hist_pos == -1) return;
        hist_pos++;
        if (hist_pos >= hist_count) { hist_pos = -1; shell_set_input(""); }
        else shell_set_input(history[hist_pos % HISTORY_SLOTS]);
    } else if (c >= ' ' && c <= '~') {
        if (buf_len < BUF_SIZE - 1) { buf[buf_len++] = c; terminal_putchar(c); }
    }
    // Always flush to framebuffer so characters appear immediately
    extern void wm_request_redraw(void);
    wm_request_redraw();
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
    // Default username since we bypass the blocking prompt for GUI boot
    strncpy(username, "user", 31);
    
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_printf("  Hello, %s! Type 'help' to get started.\n\n", username);

    extern void wm_request_redraw(void);
    wm_request_redraw();
}
