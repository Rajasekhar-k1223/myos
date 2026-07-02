#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>

#define WIFI_SSID_MAX 32

typedef struct {
    char ssid[WIFI_SSID_MAX];
    uint8_t bssid[6];
    int signal_strength;
    int is_encrypted;
} wifi_network_t;

void wifi_init(void);
int  wifi_scan(wifi_network_t* networks, int max_networks);
int  wifi_connect(const char* ssid, const char* password);
int  wifi_disconnect(void);
int  wifi_is_connected(void);

#endif
