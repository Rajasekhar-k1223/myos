#include "syscall.h"
#include "idt.h"
#include "kernel.h"
#include "task.h"
#include "elf.h"
#include "fat16.h"
#include "pmm.h"
#include "paging.h"
#include "io.h"
#include "string.h"
#include "tar.h"
#include "pipe.h"

// Write characters to COM1 serial port for debugging
void com1_write(const char* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        while ((inb(0x3F8 + 5) & 0x20) == 0);
        outb(0x3F8, buf[i]);
    }
}
struct open_file {
    uint8_t used;
    int is_pipe;
    pipe_t* pipe_ptr;
    int pipe_read_end;
    const uint8_t* ptr;
    size_t size;
    size_t offset;
};
static struct open_file open_files[16] = {0};

// System call handler
static void syscall_handler(struct registers* regs) {
    extern void syscall_handler_dbg(struct registers* regs);
    syscall_handler_dbg(regs);
    
    uint32_t syscall_no = regs->eax;

    switch (syscall_no) {
        case 0: { // sys_print (legacy null-terminated string)
            const char* msg = (const char*)regs->ebx;
            if (!msg) break;
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            terminal_writestring(msg);
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            com1_write(msg, strlen(msg));
            break;
        }
        case 1: { // sys_exit
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            terminal_writestring("[Syscall] Process exited.\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            com1_write("[sys_exit]\n", 11);
            task_exit();
            break;
        }
        case 2: { // sys_fork
            regs->eax = task_fork(regs);
            break;
        }
        case 3: { // sys_exec
            const char* path = (const char*)regs->ebx;
            regs->eax = task_exec(path, regs);
            break;
        }
        case 4: { // sys_read_file(name, buf, max_size) -> bytes_read
            const char* name     = (const char*)regs->ebx;
            uint8_t*    buf      = (uint8_t*)regs->ecx;
            uint32_t    max_size = regs->edx;
            if (!name || !buf) { regs->eax = (uint32_t)-1; break; }
            int n = fat16_read_file(name, buf, max_size);
            regs->eax = (uint32_t)n;
            break;
        }
        case 5: { // sys_write_file(name, buf, size) -> bytes_written
            const char*     name = (const char*)regs->ebx;
            const uint8_t*  buf  = (const uint8_t*)regs->ecx;
            uint32_t        size = regs->edx;
            if (!name || !buf) { regs->eax = (uint32_t)-1; break; }
            int n = fat16_write_file(name, buf, size);
            regs->eax = (uint32_t)n;
            break;
        }
        case 6: { // sys_mmap(size) -> user_vaddr
            uint32_t size = regs->ebx;
            if (size == 0) { regs->eax = 0; break; }

            static uint32_t user_heap_vaddr = 0x40000000;
            uint32_t ret   = user_heap_vaddr;
            uint32_t pages = (size + 4095) / 4096;

            terminal_printf("[mmap] size=%u pages=%u vaddr=0x%x\n", size, pages, ret);

            for (uint32_t i = 0; i < pages; i++) {
                void* frame = pmm_alloc_frame();
                if (!frame) {
                    terminal_writestring("[mmap] OOM!\n");
                    regs->eax = 0;
                    goto mmap_done;
                }
                memset(frame, 0, 4096);
                paging_map_page(user_heap_vaddr, (uint32_t)frame, 7); // user,rw,present
                user_heap_vaddr += 4096;
            }
            regs->eax = ret;
            mmap_done:;
            break;
        }
        case 7: { // sys_write_buf(buf, len) — length-aware console write
            const char* buf = (const char*)regs->ebx;
            uint32_t    len = regs->ecx;
            if (!buf || len == 0) break;

            // Write to terminal in bright white so it stands out
            terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
            for (uint32_t i = 0; i < len; i++) {
                terminal_putchar(buf[i]);
            }
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            com1_write(buf, len);
            break;
        }
        case 8: { // sys_open(path, flags)
            const char* path = (const char*)regs->ebx;
            int fd = -1;
            for (int i = 3; i < 16; i++) { // Reserve 0,1,2
                if (!open_files[i].used) {
                    fd = i;
                    break;
                }
            }
            if (fd == -1) { regs->eax = -1; break; }
            
            size_t file_size;
            void* ptr = tar_get_file(path, &file_size);
            if (!ptr) {
                regs->eax = -1;
                break;
            }
            
            open_files[fd].used = 1;
            open_files[fd].is_pipe = 0;
            open_files[fd].ptr = (const uint8_t*)ptr;
            open_files[fd].size = file_size;
            open_files[fd].offset = 0;
            regs->eax = fd;
            break;
        }
        case 9: { // sys_read(fd, buf, count)
            int fd = (int)regs->ebx;
            uint8_t* buf = (uint8_t*)regs->ecx;
            uint32_t count = regs->edx;
            
            if (fd < 0 || fd >= 16 || !open_files[fd].used) {
                regs->eax = -1;
                break;
            }
            
            if (open_files[fd].is_pipe) {
                if (!open_files[fd].pipe_read_end) { regs->eax = -1; break; }
                regs->eax = pipe_read(open_files[fd].pipe_ptr, buf, count);
                break;
            }
            
            size_t remain = open_files[fd].size - open_files[fd].offset;
            size_t to_read = (count < remain) ? count : remain;
            
            if (to_read > 0) {
                memcpy(buf, open_files[fd].ptr + open_files[fd].offset, to_read);
                open_files[fd].offset += to_read;
            }
            regs->eax = to_read;
            break;
        }
        case 10: { // sys_close(fd)
            int fd = (int)regs->ebx;
            if (fd >= 0 && fd < 16 && open_files[fd].used) {
                if (open_files[fd].is_pipe) {
                    if (open_files[fd].pipe_read_end) pipe_close_read(open_files[fd].pipe_ptr);
                    else pipe_close_write(open_files[fd].pipe_ptr);
                }
                open_files[fd].used = 0;
                regs->eax = 0;
            } else {
                regs->eax = -1;
            }
            break;
        }
        case 11: { // sys_getpid()
            regs->eax = task_getpid();
            break;
        }
        case 12: { // sys_waitpid(pid)
            int pid = (int)regs->ebx;
            regs->eax = task_waitpid(pid);
            break;
        }
        case 13: { // sys_sbrk(increment)
            int inc = (int)regs->ebx;
            static uint32_t current_brk = 0x50000000;
            if (inc == 0) {
                regs->eax = current_brk;
            } else {
                uint32_t old_brk = current_brk;
                uint32_t new_brk = current_brk + inc;
                
                // If expanding, map new pages
                if (inc > 0) {
                    uint32_t start_page = (old_brk + 0xFFF) & ~0xFFF;
                    uint32_t end_page = (new_brk + 0xFFF) & ~0xFFF;
                    for (uint32_t p = start_page; p < end_page; p += 4096) {
                        void* frame = pmm_alloc_frame();
                        if (frame) {
                            memset(frame, 0, 4096);
                            paging_map_page(p, (uint32_t)frame, 7);
                        }
                    }
                }
                current_brk = new_brk;
                regs->eax = old_brk;
            }
            break;
        }
        case 14: { // sys_sleep(ms)
            uint32_t ms = regs->ebx;
            task_sleep(ms);
            regs->eax = 0;
            break;
        }
        case 15: { // sys_gettimeofday(tv)
            // Just return system ticks for now
            extern uint32_t pit_get_ticks(void);
            regs->eax = pit_get_ticks() * 10; // Convert 100Hz ticks to ms roughly
            break;
        }
        case 16: { // sys_stat(path, statbuf)
            const char* path = (const char*)regs->ebx;
            uint32_t* statbuf = (uint32_t*)regs->ecx;
            if (path && statbuf) {
                size_t sz = 0;
                if (tar_get_file(path, &sz)) {
                    statbuf[0] = sz; // size
                    regs->eax = 0;
                    break;
                }
            }
            regs->eax = -1;
            break;
        }
        case 17: { // sys_pipe(pipefd)
            int* pipefd = (int*)regs->ebx;
            if (!pipefd) { regs->eax = -1; break; }
            int fd1 = -1, fd2 = -1;
            for (int i = 3; i < 16; i++) {
                if (!open_files[i].used) {
                    if (fd1 == -1) fd1 = i;
                    else if (fd2 == -1) { fd2 = i; break; }
                }
            }
            if (fd1 == -1 || fd2 == -1) { regs->eax = -1; break; }
            
            pipe_t* p = (pipe_t*)pmm_alloc_frame();
            if (!p) { regs->eax = -1; break; }
            pipe_create(p);
            
            open_files[fd1].used = 1; open_files[fd1].is_pipe = 1;
            open_files[fd1].pipe_ptr = p; open_files[fd1].pipe_read_end = 1;
            
            open_files[fd2].used = 1; open_files[fd2].is_pipe = 1;
            open_files[fd2].pipe_ptr = p; open_files[fd2].pipe_read_end = 0;
            
            pipefd[0] = fd1;
            pipefd[1] = fd2;
            regs->eax = 0;
            break;
        }
        case 18: { // sys_dup2(oldfd, newfd)
            int oldfd = (int)regs->ebx;
            int newfd = (int)regs->ecx;
            if (oldfd < 0 || oldfd >= 16 || !open_files[oldfd].used) { regs->eax = -1; break; }
            if (newfd < 0 || newfd >= 16) { regs->eax = -1; break; }
            if (oldfd == newfd) { regs->eax = newfd; break; }
            
            if (open_files[newfd].used) {
                if (open_files[newfd].is_pipe) {
                    if (open_files[newfd].pipe_read_end) pipe_close_read(open_files[newfd].pipe_ptr);
                    else pipe_close_write(open_files[newfd].pipe_ptr);
                }
            }
            open_files[newfd] = open_files[oldfd];
            regs->eax = newfd;
            break;
        }
        case 19: { // sys_write(fd, buf, count)
            int fd = (int)regs->ebx;
            const uint8_t* buf = (const uint8_t*)regs->ecx;
            uint32_t count = regs->edx;
            
            if (fd == 1 || fd == 2) {
                if (!open_files[fd].used) {
                    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
                    for (uint32_t i = 0; i < count; i++) terminal_putchar(buf[i]);
                    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    com1_write((const char*)buf, count);
                    regs->eax = count;
                    break;
                }
            }
            if (fd < 0 || fd >= 16 || !open_files[fd].used) {
                regs->eax = -1;
                break;
            }
            if (open_files[fd].is_pipe) {
                if (open_files[fd].pipe_read_end) { regs->eax = -1; break; } // Cannot write to read end
                regs->eax = pipe_write(open_files[fd].pipe_ptr, buf, count);
                break;
            }
            // Cannot write to TAR files, so if it's a file, just fail or return 0 for now.
            // Unless it's a pipe.
            regs->eax = -1;
            break;
        }
        case 20: { // sys_kill(pid, sig)
            int pid = (int)regs->ebx;
            int sig = (int)regs->ecx;
            if (sig < 0 || sig > 31) { regs->eax = -1; break; }
            int found = 0;
            for (int i=0; i<MAX_TASKS; i++) {
                if (tasks[i].id == (uint32_t)pid && tasks[i].state != TASK_DEAD) {
                    tasks[i].pending_signals |= (1 << sig);
                    if (tasks[i].state == TASK_SLEEPING) tasks[i].state = TASK_READY; // Wake up
                    found = 1;
                    break;
                }
            }
            regs->eax = found ? 0 : -1;
            break;
        }
        case 21: { // sys_sigaction(sig, handler)
            int sig = (int)regs->ebx;
            uint32_t handler = regs->ecx;
            if (sig < 0 || sig > 31) { regs->eax = -1; break; }
            task_t* cur = task_current();
            cur->sig_handlers[sig] = handler;
            regs->eax = 0;
            break;
        }
        case 22: { // sys_sigreturn()
            // We are called by the signal trampoline to return to normal execution.
            task_t* cur = task_current();
            // The user stack points to a struct containing: [signum, old_eip, old_eflags]
            // Let's assume the trampoline called us with arguments on the stack, or we just read the stack.
            // Wait, an easier way: the kernel stored the old IRET frame somewhere?
            // Actually, we can just read from the user stack!
            // EBP/ESP points to the user stack inside the kernel due to syscall interrupt.
            // User stack is at regs->useresp.
            uint32_t* ustack = (uint32_t*)regs->useresp;
            // ustack[0] = return address of sys_sigreturn (which is in the trampoline)
            // ustack[1] = old EIP
            // ustack[2] = old EFLAGS
            // ustack[3] = old ESP
            // Restore!
            regs->eip = ustack[1];
            regs->eflags = ustack[2];
            regs->useresp = ustack[3];
            cur->in_signal_handler = 0;
            break;
        }
        case 23: { // sys_ioctl(fd, request, arg)
            int      _fd  = (int)regs->ebx;   (void)_fd;
            uint32_t _req = regs->ecx;
            void*    _arg = (void*)(uintptr_t)regs->edx;
            if (_req == 0x5413) { // TIOCGWINSZ
                uint16_t* ws = (uint16_t*)(uintptr_t)regs->edx;
                if (ws) { ws[0]=25; ws[1]=80; ws[2]=1024; ws[3]=768; }
                regs->eax = 0;
            } else if (_req == 0x5414) { // TIOCSWINSZ
                regs->eax = 0;
            } else if (_req == 0x5401) { // TCGETS
                if (_arg) memset(_arg, 0, 60);
                regs->eax = 0;
            } else if (_req == 0x5402 || _req == 0x5403) { // TCSETS / TCSETSW
                regs->eax = 0;
            } else if (_req == 0x541B) { // FIONREAD
                uint32_t* av = (uint32_t*)(uintptr_t)regs->edx;
                if (av) *av = 0;
                regs->eax = 0;
            } else if (_req == 0x5421) { // FIONBIO
                regs->eax = 0;
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }
        case 26: { // sys_msgget
            extern int ipc_msgget(int, int);
            regs->eax = (uint32_t)ipc_msgget((int)regs->ebx, (int)regs->ecx);
            break;
        }
        case 27: { // sys_msgsnd
            extern int ipc_msgsnd(int, const void*, uint32_t, int);
            regs->eax = (uint32_t)ipc_msgsnd((int)regs->ebx,
                            (const void*)(uintptr_t)regs->ecx, regs->edx, 0);
            break;
        }
        case 28: { // sys_msgrcv
            extern int ipc_msgrcv(int, void*, uint32_t, long, int);
            regs->eax = (uint32_t)ipc_msgrcv((int)regs->ebx,
                            (void*)(uintptr_t)regs->ecx, regs->edx, 0, 0);
            break;
        }
        case 29: { // sys_shmget
            extern int shm_get(int, uint32_t, int);
            regs->eax = (uint32_t)shm_get((int)regs->ebx, regs->ecx, (int)regs->edx);
            break;
        }
        case 30: { // sys_shmat
            extern void* shm_at(int, int);
            regs->eax = (uint32_t)(uintptr_t)shm_at((int)regs->ebx, (int)regs->ecx);
            break;
        }
        case 31: { // sys_socket(protocol)
            extern int raw_socket_open(int);
            regs->eax = (uint32_t)raw_socket_open((int)regs->ebx);
            break;
        }
        case 32: { // sys_send(fd, buf, len)
            extern int raw_socket_send(int, const uint8_t*, uint32_t);
            regs->eax = (uint32_t)raw_socket_send((int)regs->ebx,
                            (const uint8_t*)(uintptr_t)regs->ecx, regs->edx);
            break;
        }
        case 33: { // sys_recv(fd, buf, max_len)
            extern int raw_socket_recv(int, uint8_t*, uint32_t, uint32_t);
            regs->eax = (uint32_t)raw_socket_recv((int)regs->ebx,
                            (uint8_t*)(uintptr_t)regs->ecx, regs->edx, 5000);
            break;
        }
        case 34: { // sys_sockclose(fd)
            extern void raw_socket_close(int);
            raw_socket_close((int)regs->ebx);
            regs->eax = 0;
            break;
        }
        case 35: { // sys_set_thread_area(tls_addr) — set GS base for TLS
            task_t* cur = task_current();
            if (cur) cur->tls_base = regs->ebx;
            regs->eax = 0;
            break;
        }
        case 24: { // sys_sched_setaffinity(pid, cpumask)
            uint32_t pid    = regs->ebx;
            int      cpu    = (int)regs->ecx; /* -1 = any, 0..3 = specific core */
            regs->eax = (uint32_t)-1;
            for (int i = 0; i < MAX_TASKS; i++) {
                if (tasks[i].id == pid && tasks[i].state != TASK_DEAD) {
                    tasks[i].affinity = cpu;
                    regs->eax = 0;
                    break;
                }
            }
            break;
        }
        case 25: { // sys_sched_getaffinity(pid) -> returns affinity
            uint32_t pid = regs->ebx;
            regs->eax = (uint32_t)-1;
            for (int i = 0; i < MAX_TASKS; i++) {
                if (tasks[i].id == pid && tasks[i].state != TASK_DEAD) {
                    regs->eax = (uint32_t)tasks[i].affinity;
                    break;
                }
            }
            break;
        }
        default:
            terminal_writestring("[Syscall] Unknown syscall: ");
            terminal_writedec(syscall_no);
            terminal_writestring("\n");
            break;
    }
}

void syscall_init(void) {
    register_interrupt_handler(128, syscall_handler); // INT 0x80
}
