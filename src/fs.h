#pragma once
#include <stdint.h>

typedef struct {
    char name[16];
    uint32_t size;
} fs_file_info_t;

int fs_init(void);
void fs_format(void);
int fs_write_file(const char* name, const char* data);
int fs_read_file(const char* name, char* out_data);
int fs_list_files(fs_file_info_t* files);
