#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>

void bluetooth_init(void);
void bluetooth_scan(void);
int  bluetooth_connect(int dev_idx);
int  bluetooth_send(int dev_idx, const uint8_t* data, int len);
int  bluetooth_disconnect(int dev_idx);

#endif
