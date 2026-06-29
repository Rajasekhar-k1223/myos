#include "wifi.h"
#include "kernel.h"
#include "string.h"
#include "wpa.h"

static int wifi_connected = 0;

void wifi_init(void) {
    terminal_printf("[WIFI] 802.11 Framework initialized.\n");
    wifi_connected = 0;
}

int wifi_scan(wifi_network_t* networks, int max_networks) {
    terminal_printf("[WIFI] Scanning for 802.11 networks...\n");
    if (max_networks > 0) {
        strcpy(networks[0].ssid, "Home_Network_5G");
        networks[0].is_encrypted = 1;
        networks[0].signal_strength = 85;
        return 1;
    }
    return 0;
}

int wifi_connect(const char* ssid, const char* password) {
    terminal_printf("[WIFI] Attempting to connect to '%s'...\n", ssid);
    
    if (wpa_authenticate(password)) {
        wifi_connected = 1;
        terminal_printf("[WIFI] Successfully connected to %s.\n", ssid);
        return 1;
    }
    terminal_printf("[WIFI] Failed to connect: Authentication error.\n");
    return 0;
}

int wifi_disconnect(void) {
    if (wifi_connected) {
        wifi_connected = 0;
        terminal_printf("[WIFI] Disconnected.\n");
        return 1;
    }
    return 0;
}
