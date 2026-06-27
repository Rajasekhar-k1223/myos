#include "pkg.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"
#include "tcp.h"
#include "fs.h"

#define MAX_PKGS 32
static pkg_info_t packages[MAX_PKGS];
static int num_pkgs = 0;

void pkg_init(void) {
    num_pkgs = 0;
    terminal_printf("[PKG] Package manager initialized.\n");
    
    /* Mock initial package for testing */
    strcpy(packages[0].name, "doom");
    strcpy(packages[0].version, "1.9");
    strcpy(packages[0].description, "Classic DOOM port for ElseaOS");
    packages[0].installed = 0;
    num_pkgs = 1;
}

int pkg_update(void) {
    terminal_printf("[PKG] Fetching package lists from repository...\n");
    /* In a real implementation, this would open a TCP socket to pkg.elseaos.org */
    /* and parse a JSON/text list. For now, we mock the delay and success. */
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < 100) {} /* 1 sec delay */
    terminal_printf("[PKG] Repository updated. %d packages found.\n", num_pkgs);
    return 1;
}

int pkg_install(const char* pkg_name) {
    for (int i = 0; i < num_pkgs; i++) {
        if (strcmp(packages[i].name, pkg_name) == 0) {
            if (packages[i].installed) {
                terminal_printf("[PKG] Package '%s' is already installed.\n", pkg_name);
                return 0;
            }
            terminal_printf("[PKG] Downloading '%s'...\n", pkg_name);
            /* Mock download and install */
            uint32_t start = pit_get_ticks();
            while (pit_get_ticks() - start < 200) {} /* 2 sec delay */
            
            packages[i].installed = 1;
            terminal_printf("[PKG] Installed '%s' successfully.\n", pkg_name);
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
                terminal_printf("[PKG] Package '%s' is not installed.\n", pkg_name);
                return 0;
            }
            packages[i].installed = 0;
            terminal_printf("[PKG] Removed '%s' successfully.\n", pkg_name);
            return 1;
        }
    }
    return 0;
}

int pkg_list(pkg_info_t* out_list, int max_pkgs) {
    int count = (num_pkgs < max_pkgs) ? num_pkgs : max_pkgs;
    for (int i = 0; i < count; i++) {
        out_list[i] = packages[i];
    }
    return count;
}
