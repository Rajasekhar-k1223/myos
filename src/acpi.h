#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

#define MAX_CORES 16

extern uint32_t local_apic_base;
extern uint8_t apic_ids[MAX_CORES];
extern int num_cores;
extern uint8_t bsp_apic_id;

void acpi_init(void);
void acpi_shutdown(void);
void acpi_reboot(void);
void acpi_sleep_s3(void);

#endif
