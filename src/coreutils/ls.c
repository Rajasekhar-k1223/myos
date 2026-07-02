extern int syscall0(unsigned int num);

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    // Call our new sys_ls syscall (36)
    syscall0(36);
    return 0;
}
