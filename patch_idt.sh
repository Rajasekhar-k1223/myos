sed -i 's/terminal_printf/com1_write_str/g' src/idt.c
cat << 'INNER_EOF' >> src/idt.c
void com1_write_str(const char* str, ...) {
    // simplified for patching
}
INNER_EOF
