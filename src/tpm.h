#ifndef TPM_H
#define TPM_H

#include <stdint.h>

void tpm_init(void);
int  tpm_is_present(void);
int  tpm_get_random(uint8_t* buf, uint32_t len);
int  tpm_seal_data(const uint8_t* in, uint32_t len, uint8_t* out);
int  tpm_unseal_data(const uint8_t* in, uint32_t len, uint8_t* out);

#endif
