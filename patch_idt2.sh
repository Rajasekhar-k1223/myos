cat << 'INNER_EOF' >> src/idt.c

extern void com1_write(const char* buf, uint32_t len);
static int my_strlen(const char* s) { int i=0; while(s[i]) i++; return i; }
void com1_print(const char* s) { com1_write(s, my_strlen(s)); }
void com1_print_hex(uint32_t n) {
    char buf[16];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else {
        uint32_t temp = n;
        char rev[16];
        int j = 0;
        while (temp) {
            int rem = temp % 16;
            rev[j++] = rem < 10 ? '0' + rem : 'A' + rem - 10;
            temp /= 16;
        }
        while (j > 0) buf[i++] = rev[--j];
    }
    buf[i] = 0;
    com1_print("0x");
    com1_print(buf);
}
INNER_EOF
sed -i 's/terminal_printf("\\n\*\*\* EXCEPTION %s \*\*\*\\n", exception_messages\[regs->int_no\]);/terminal_printf("\\n\*\*\* EXCEPTION %s \*\*\*\\n", exception_messages\[regs->int_no\]); com1_print("\\n*** EXCEPTION "); com1_print(exception_messages\[regs->int_no\]); com1_print(" ***\\n");/g' src/idt.c

sed -i 's/terminal_printf("EIP: 0x%x, CS: 0x%x, EFLAGS: 0x%x\\n", regs->eip, regs->cs, regs->eflags);/terminal_printf("EIP: 0x%x, CS: 0x%x, EFLAGS: 0x%x\\n", regs->eip, regs->cs, regs->eflags); com1_print("EIP: "); com1_print_hex(regs->eip); com1_print("\\n");/g' src/idt.c
