extern int main(int argc, char** argv);
extern void exit(int status);

void _start(void) __attribute__((naked));
void _start(void) {
    __asm__ volatile(
        "pop %eax\n"        // Pop argc into eax
        "pop %ebx\n"        // Pop argv into ebx
        "push %ebx\n"       // Push argv for main
        "push %eax\n"       // Push argc for main
        "call main\n"       // Call main(argc, argv)
        "push %eax\n"       // Push main's return value for exit
        "call exit\n"
    );
}
