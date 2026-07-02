#include "pkg.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"
#include "tcp.h"
#include "dns.h"

#define MAX_PKGS      64
#define PKG_HOST      "pkg.elseaos.org"
#define PKG_PORT      80
#define RECV_BUF_SIZE 4096

static pkg_info_t packages[MAX_PKGS];
static int        num_pkgs = 0;

void pkg_init(void) {
    num_pkgs = 0;
    static const struct { const char* n; const char* v; const char* d; } builtin[] = {
        /* Development */
        {"java",      "21.0",  "OpenJDK 21 — Java Runtime & Compiler"},
        {"python3",   "3.12",  "Python 3 interpreter"},
        {"gcc",       "13.2",  "GNU C/C++ compiler collection"},
        {"nodejs",    "20.11", "Node.js JavaScript runtime"},
        {"git",       "2.43",  "Distributed version control system"},
        {"cmake",     "3.28",  "Cross-platform build system"},
        {"gdb",       "14.1",  "GNU debugger"},
        {"rust",      "1.75",  "Rust language compiler (rustc + cargo)"},
        /* Editors */
        {"vim",       "9.1",   "Vi IMproved — advanced text editor"},
        {"nano",      "7.2",   "Simple terminal text editor"},
        {"emacs",     "29.2",  "GNU Emacs text editor"},
        /* System tools */
        {"busybox",   "1.36",  "Minimal Unix utilities collection"},
        {"curl",      "8.5",   "Command-line HTTP/FTP client"},
        {"wget",      "1.21",  "Non-interactive network downloader"},
        {"htop",      "3.3",   "Interactive process viewer"},
        {"tmux",      "3.3",   "Terminal multiplexer"},
        {"zip",       "3.0",   "Compression utility"},
        {"ssh",       "9.6",   "OpenSSH client & server"},
        /* Media & apps */
        {"ffmpeg",    "6.1",   "Audio/video encoder & transcoder"},
        {"vlc",       "3.0",   "VideoLAN media player"},
        {"gimp",      "2.10",  "GNU Image Manipulation Program"},
        /* Games */
        {"doom",      "1.9",   "Classic DOOM port for ElseaOS"},
        {"nethack",   "3.6",   "Roguelike dungeon-exploration game"},
        /* Scripting */
        {"lua",       "5.4",   "Lightweight scripting language"},
        {"perl",      "5.38",  "Practical Extraction and Report Language"},
        {"ruby",      "3.3",   "Ruby programming language"},
    };
    int n = (int)(sizeof(builtin)/sizeof(builtin[0]));
    for (int i = 0; i < n && i < MAX_PKGS; i++) {
        strncpy(packages[i].name,        builtin[i].n, 31);
        strncpy(packages[i].version,     builtin[i].v, 15);
        strncpy(packages[i].description, builtin[i].d, 127);
        packages[i].installed = 0;
    }
    num_pkgs = n;
    terminal_printf("[PKG] Package manager ready. %d seed packages.\n", num_pkgs);
}

static void parse_pkg_line(const char* line) {
    if (num_pkgs >= MAX_PKGS) return;
    char name[32] = {0}, ver[16] = {0}, desc[128] = {0};
    int i = 0, j = 0;
    while (line[i] && line[i] != ' ' && j < 31) { name[j] = line[i]; j++; i++; }
    while (line[i] == ' ') i++;
    j = 0;
    while (line[i] && line[i] != ' ' && j < 15) { ver[j] = line[i]; j++; i++; }
    while (line[i] == ' ') i++;
    j = 0;
    while (line[i] && line[i] != '\n' && line[i] != '\r' && j < 127) { desc[j] = line[i]; j++; i++; }
    if (!name[0]) return;
    for (int k = 0; k < num_pkgs; k++) {
        if (strcmp(packages[k].name, name) == 0) {
            strncpy(packages[k].version,     ver,  15);
            strncpy(packages[k].description, desc, 127);
            return;
        }
    }
    strncpy(packages[num_pkgs].name,        name, 31);
    strncpy(packages[num_pkgs].version,     ver,  15);
    strncpy(packages[num_pkgs].description, desc, 127);
    packages[num_pkgs].installed = 0;
    num_pkgs++;
}

int pkg_update(void) {
    terminal_printf("[PKG] Resolving %s ...\n", PKG_HOST);
    uint32_t server_ip = 0;
    if (dns_resolve(PKG_HOST, &server_ip) != 0) {
        terminal_printf("[PKG] DNS failed for %s\n", PKG_HOST);
        return 0;
    }
    terminal_printf("[PKG] Connecting to %u.%u.%u.%u:%d ...\n",
        (server_ip >> 24) & 0xFF, (server_ip >> 16) & 0xFF,
        (server_ip >>  8) & 0xFF,  server_ip & 0xFF, PKG_PORT);

    int conn = tcp_connect(server_ip, (uint16_t)PKG_PORT);
    if (conn < 0) { terminal_printf("[PKG] TCP connect failed\n"); return 0; }

    const char* req =
        "GET /packages.lst HTTP/1.0\r\n"
        "Host: " PKG_HOST "\r\n"
        "Connection: close\r\n\r\n";
    tcp_send(conn, (const uint8_t*)req, (uint32_t)strlen(req));

    uint8_t* buf = (uint8_t*)kmalloc(RECV_BUF_SIZE);
    if (!buf) { tcp_close(conn); return 0; }

    int len = tcp_recv(conn, buf, RECV_BUF_SIZE - 1, 5000);
    tcp_close(conn);

    if (len <= 0) {
        terminal_printf("[PKG] No data received\n");
        kfree(buf);
        return 0;
    }
    buf[len] = '\0';

    char* body = strstr((char*)buf, "\r\n\r\n");
    if (body) body += 4; else body = (char*)buf;

    char* line = body;
    while (*line) {
        char* nl = line;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = '\0';
        if (line[0] && line[0] != '#') parse_pkg_line(line);
        *nl = saved;
        line = (*nl) ? nl + 1 : nl;
    }
    kfree(buf);
    terminal_printf("[PKG] Repository updated. %d packages known.\n", num_pkgs);
    return 1;
}

int pkg_install(const char* pkg_name) {
    for (int i = 0; i < num_pkgs; i++) {
        if (strcmp(packages[i].name, pkg_name) == 0) {
            if (packages[i].installed) {
                terminal_printf("[PKG] '%s' already installed.\n", pkg_name);
                return 0;
            }
            uint32_t srv_ip = 0;
            if (dns_resolve(PKG_HOST, &srv_ip) != 0) { terminal_printf("[PKG] DNS failed\n"); return 0; }
            int conn = tcp_connect(srv_ip, (uint16_t)PKG_PORT);
            if (conn < 0) { terminal_printf("[PKG] TCP connect failed\n"); return 0; }

            char req[256];
            sprintf(req,
                "GET /pkg/%s-%s.pkg HTTP/1.0\r\nHost: " PKG_HOST "\r\nConnection: close\r\n\r\n",
                packages[i].name, packages[i].version);
            tcp_send(conn, (const uint8_t*)req, (uint32_t)strlen(req));

            uint8_t tmp[512];
            uint32_t total = 0;
            int n;
            while ((n = tcp_recv(conn, tmp, 512, 3000)) > 0)
                total += (uint32_t)n;
            tcp_close(conn);

            packages[i].installed = 1;
            terminal_printf("[PKG] Installed '%s' (%u bytes).\n", pkg_name, total);
            return 1;
        }
    }
    terminal_printf("[PKG] Package '%s' not found.\n", pkg_name);
    return 0;
}

int pkg_remove(const char* pkg_name) {
    for (int i = 0; i < num_pkgs; i++) {
        if (strcmp(packages[i].name, pkg_name) == 0) {
            if (!packages[i].installed) {
                terminal_printf("[PKG] '%s' not installed.\n", pkg_name);
                return 0;
            }
            packages[i].installed = 0;
            terminal_printf("[PKG] Removed '%s'.\n", pkg_name);
            return 1;
        }
    }
    return 0;
}

int pkg_list(pkg_info_t* out_list, int max_pkgs) {
    int count = (num_pkgs < max_pkgs) ? num_pkgs : max_pkgs;
    for (int i = 0; i < count; i++) out_list[i] = packages[i];
    return count;
}
