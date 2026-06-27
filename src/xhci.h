#pragma once
#include <stdint.h>

typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t ctrl;
} __attribute__((packed)) xhci_trb_t;

void xhci_init(void);
int  xhci_send_command(uint32_t* trb);   /* ring command doorbell; trb = 4 dwords */
int  xhci_wait_event(uint32_t* out_trb); /* poll event ring; fills out_trb[4]     */
int  xhci_poll_event(xhci_trb_t* out);  /* non-blocking poll; 1=event, 0=empty   */
void xhci_post_noop(void);              /* post a No-Op command TRB              */
void xhci_ring_cmd_doorbell(void);      /* ring doorbell register 0              */
void xhci_update_erdp(void);           /* write ERDP back to interrupter 0      */
