#ifndef TAR_H
#define TAR_H

#include <stdint.h>

void tar_init(uint32_t address);
void tar_ls(void);
void tar_cat(const char* filename);

#endif
