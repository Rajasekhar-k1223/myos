#include "calc.h"
#include "string.h"
int sprintf(char *str, const char *format, ...);

static char calc_buffer[128];
static int calc_buf_idx = 0;

void calc_init(window_t* win) {
    if (!win) return;
    char* msg = "Calculator v1.0\nType an expression (e.g. 5 + 3)\nand press Enter to compute!\n> ";
    for (int i = 0; msg[i]; i++) {
        wm_putchar(win, msg[i]);
    }
    calc_buf_idx = 0;
    calc_buffer[0] = '\0';
}

void calc_handle_input(window_t* win, char c) {
    if (!win) return;
    
    if (c == '\n') {
        wm_putchar(win, '\n');
        
        // Parse the expression
        int a = 0, b = 0, res = 0;
        char op = 0;
        int parsed = 0;
        
        // Simple manual parser (since we don't have sscanf yet)
        int i = 0;
        while (calc_buffer[i] == ' ') i++;
        
        // Parse a
        int sign_a = 1;
        if (calc_buffer[i] == '-') { sign_a = -1; i++; }
        while (calc_buffer[i] >= '0' && calc_buffer[i] <= '9') {
            a = a * 10 + (calc_buffer[i] - '0');
            i++;
            parsed = 1;
        }
        a *= sign_a;
        
        while (calc_buffer[i] == ' ') i++;
        
        // Parse op
        if (calc_buffer[i] == '+' || calc_buffer[i] == '-' || calc_buffer[i] == '*' || calc_buffer[i] == '/') {
            op = calc_buffer[i];
            i++;
        }
        
        while (calc_buffer[i] == ' ') i++;
        
        // Parse b
        int sign_b = 1;
        if (calc_buffer[i] == '-') { sign_b = -1; i++; }
        while (calc_buffer[i] >= '0' && calc_buffer[i] <= '9') {
            b = b * 10 + (calc_buffer[i] - '0');
            i++;
        }
        b *= sign_b;
        
        // Compute
        if (parsed && op) {
            if (op == '+') res = a + b;
            else if (op == '-') res = a - b;
            else if (op == '*') res = a * b;
            else if (op == '/') {
                if (b == 0) {
                    char* err = "Error: Div by 0\n> ";
                    for (int j = 0; err[j]; j++) wm_putchar(win, err[j]);
                    calc_buf_idx = 0;
                    calc_buffer[0] = '\0';
                    return;
                }
                res = a / b;
            }
            
            char out[32];
            sprintf(out, "= %d\n> ", res);
            for (int j = 0; out[j]; j++) wm_putchar(win, out[j]);
        } else {
            char* err = "Parse Error!\n> ";
            for (int j = 0; err[j]; j++) wm_putchar(win, err[j]);
        }
        
        // Reset buffer
        calc_buf_idx = 0;
        calc_buffer[0] = '\0';
        
    } else if (c == '\b') {
        if (calc_buf_idx > 0) {
            calc_buf_idx--;
            calc_buffer[calc_buf_idx] = '\0';
            wm_putchar(win, '\b');
        }
    } else if (c >= ' ') {
        if (calc_buf_idx < 127) {
            calc_buffer[calc_buf_idx++] = c;
            calc_buffer[calc_buf_idx] = '\0';
            wm_putchar(win, c);
        }
    }
}
