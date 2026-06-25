#ifndef TAR_H
#define TAR_H

#include <stdint.h>
#include <stddef.h>

void tar_init(uint32_t address);
void tar_ls(void);
void tar_cat(const char* filename);
void* tar_get_file(const char* filename, size_t* out_size);
int tar_get_file_at_index(int index, char* out_name);

#endif
