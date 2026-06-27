#include "firewall.h"
#include "kernel.h"

#define MAX_RULES 64
static firewall_rule_t fw_rules[MAX_RULES];
static int num_rules = 0;

void firewall_init(void) {
    num_rules = 0;
    terminal_printf("[FW] Firewall initialized.\n");
}

void firewall_add_rule(firewall_rule_t rule) {
    if (num_rules < MAX_RULES) {
        fw_rules[num_rules++] = rule;
    }
}

int firewall_check(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol, uint16_t src_port, uint16_t dst_port) {
    /* Default policy: Allow */
    int result = FIREWALL_ACTION_ALLOW;

    for (int i = 0; i < num_rules; i++) {
        firewall_rule_t* r = &fw_rules[i];
        
        int match = 1;
        if (r->src_ip != 0 && r->src_ip != src_ip) match = 0;
        if (r->dst_ip != 0 && r->dst_ip != dst_ip) match = 0;
        if (r->protocol != 0 && r->protocol != protocol) match = 0;
        if (r->src_port != 0 && r->src_port != src_port) match = 0;
        if (r->dst_port != 0 && r->dst_port != dst_port) match = 0;
        
        if (match) {
            result = r->action;
            /* First match wins */
            break;
        }
    }
    
    return result;
}
