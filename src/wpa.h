#ifndef WPA_H
#define WPA_H
#include <stdint.h>
int  wpa_authenticate(const char* psk);
int  wpa_derive_pmk(const char* passphrase, const char* ssid, uint8_t* pmk_out, int store);
void wpa_derive_ptk(const uint8_t* pmk,
                    const uint8_t* aa, const uint8_t* spa,
                    const uint8_t* anonce, const uint8_t* snonce,
                    uint8_t* ptk_out, uint32_t ptk_len);
#endif
