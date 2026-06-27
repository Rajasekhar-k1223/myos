#ifndef PKG_H
#define PKG_H

#include <stdint.h>

typedef struct {
    char name[32];
    char version[16];
    char description[128];
    int  installed;
} pkg_info_t;

void pkg_init(void);
int  pkg_update(void); /* Fetch package list */
int  pkg_install(const char* pkg_name);
int  pkg_remove(const char* pkg_name);
int  pkg_list(pkg_info_t* out_list, int max_pkgs);

#endif
