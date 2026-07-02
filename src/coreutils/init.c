

// Let's declare a simple sleep for now using a raw syscall.
// But wait, what syscall is sleep? In shell.c it uses task_sleep.
// Actually, let's just make it loop.

extern unsigned int syscall2(unsigned int num, unsigned int a1, unsigned int a2);
extern unsigned int syscall0(unsigned int num);

static void print(const char* s) {
    unsigned int len = 0;
    while (s[len]) len++;
    // Syscall 7: sys_write_buf
    unsigned int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(7), "b"(s), "c"(len));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    print("\n[INIT] Starting ElseaOS userspace (systemd clone)...\n");
    
    // msgget = syscall 26
    int qid = (int)syscall2(26, 1000, 0);
    if (qid >= 0) {
        print("[INIT] Created IPC Message Queue (Display Server Socket)\n");
    }
    print("[INIT] Ready to accept display server connections.\n");
    
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(2)); // fork
    if (ret == 0) {
        print("[INIT] Spawning compositor daemon...\n");
        __asm__ volatile("int $0x80" : : "a"(3), "b"("bin/compositor.elf"), "c"(0)); // exec
        print("[INIT] Failed to exec compositor!\n");
        while(1) {}
    }
    print("[INIT] Compositor started.\n");
    
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(2)); // fork
    if (ret == 0) {
        print("[INIT] Spawning audio daemon (PipeWire clone)...\n");
        __asm__ volatile("int $0x80" : : "a"(3), "b"("bin/audio.elf"), "c"(0)); // exec
        print("[INIT] Failed to exec audio daemon!\n");
        while(1) {}
    }
    print("[INIT] Audio daemon started.\n");
    
    while (1) {
        // Sleep 0ms = yield
        __asm__ volatile("int $0x80" : : "a"(14), "b"(0));
    }
    
    return 0;
}
