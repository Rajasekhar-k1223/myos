#ifndef SMP_H
#define SMP_H

#include <stdint.h>

extern volatile int ap_count;

void smp_init(void);
void ap_main(void);

#endif
