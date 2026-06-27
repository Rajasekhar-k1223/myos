#include "idt.h"
#include "io.h"
#include "kernel.h"
#include "apic.h"
#include "acpi.h"
#include <stdint.h>

#define IDT_ENTRIES 256

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern void com1_print(const char*);
extern void com1_print_hex(uint32_t);

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;
static isr_t            interrupt_handlers[IDT_ENTRIES];

/* ISR/IRQ stubs defined in isr.S */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);
extern void isr128(void);

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = base & 0xFFFF;
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel     = sel;
    idt[num].always0 = 0;
    idt[num].flags   = flags;
}

static void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD,  0x11); io_wait();  /* ICW1: init + ICW4 needed */
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();  /* ICW2: master → INT 32-39 */
    outb(PIC2_DATA, 0x28); io_wait();  /* ICW2: slave  → INT 40-47 */
    outb(PIC1_DATA, 0x04); io_wait();  /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();  /* ICW3: cascade identity  */
    outb(PIC1_DATA, 0x01); io_wait();  /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0x00);    /* unmask all */
    outb(PIC2_DATA, 0x00);
}

void idt_init(void) {
    extern void* memset(void*, int, unsigned int);
    memset(interrupt_handlers, 0, sizeof(interrupt_handlers));
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

#define SET(n, fn) idt_set_gate(n, (uint32_t)(fn), 0x08, 0x8E)
    SET(0,  isr0);  SET(1,  isr1);  SET(2,  isr2);  SET(3,  isr3);
    SET(4,  isr4);  SET(5,  isr5);  SET(6,  isr6);  SET(7,  isr7);
    SET(8,  isr8);  SET(9,  isr9);  SET(10, isr10); SET(11, isr11);
    SET(12, isr12); SET(13, isr13); SET(14, isr14); SET(15, isr15);
    SET(16, isr16); SET(17, isr17); SET(18, isr18); SET(19, isr19);
    SET(20, isr20); SET(21, isr21); SET(22, isr22); SET(23, isr23);
    SET(24, isr24); SET(25, isr25); SET(26, isr26); SET(27, isr27);
    SET(28, isr28); SET(29, isr29); SET(30, isr30); SET(31, isr31);

    pic_remap();

    SET(32, irq0);  SET(33, irq1);  SET(34, irq2);  SET(35, irq3);
    SET(36, irq4);  SET(37, irq5);  SET(38, irq6);  SET(39, irq7);
    SET(40, irq8);  SET(41, irq9);  SET(42, irq10); SET(43, irq11);
    SET(44, irq12); SET(45, irq13); SET(46, irq14); SET(47, irq15);
#undef SET

    extern void irq16(void);
    idt_set_gate(48, (uint32_t)irq16, 0x08, 0x8E);

    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE);

    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
    __asm__ volatile ("sti");
}

void idt_load_ap(void) {
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
}

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

static const char* exception_names[] = {
    "Division Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 FP Exception", "Alignment Check",
    "Machine Check", "SIMD FP Exception",
};

void isr_handler(struct registers* regs) {
    if (interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](regs);
        extern void task_check_signals(struct registers* regs);
        task_check_signals(regs);
        return;
    }

    if (regs->int_no < 32) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
        terminal_writestring("\n*** EXCEPTION ");
        if (regs->int_no < 20)
            terminal_writestring(exception_names[regs->int_no]);
        terminal_writestring(" — system halted ***\n");
        com1_print("\n*** EXCEPTION ");
        com1_print_hex(regs->int_no);
        com1_print(" ***\n");
        if (regs->int_no == 14) {
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            /* Try COW/swap handler before giving up */
            extern void paging_page_fault_handler(uint32_t, uint32_t);
            paging_page_fault_handler(cr2, regs->err_code);
            /* If handler returned, the fault was resolved — return to faulting code */
            return;
        }
        com1_print("EIP: "); com1_print_hex(regs->eip); com1_print("\n");
        terminal_printf("EIP: 0x%x, CS: 0x%x, EFLAGS: 0x%x\n", regs->eip, regs->cs, regs->eflags); com1_print("EIP: "); com1_print_hex(regs->eip); com1_print("\n");
        terminal_printf("EAX: 0x%x, EBX: 0x%x, ECX: 0x%x, EDX: 0x%x\n", regs->eax, regs->ebx, regs->ecx, regs->edx);
        terminal_printf("ESP: 0x%x, EBP: 0x%x, USERESP: 0x%x, SS: 0x%x\n", regs->esp, regs->ebp, regs->useresp, regs->ss);
        
        terminal_printf("\nStack Dump (ESP=0x%x):\n", regs->esp);
        uint32_t* sp = (uint32_t*)regs->esp;
        for (int i = 0; i < 8; i++) {
            terminal_printf("  [ESP+0x%x] 0x%x\n", i * 4, sp[i]);
        }
        for (;;) __asm__ volatile ("cli; hlt");
    }
}

void irq_handler(struct registers* regs) {
    if (regs->int_no >= 32 && regs->int_no <= 47) {
        if (regs->int_no >= 40)
            outb(0xA0, 0x20); /* EOI to slave */
        outb(0x20, 0x20);     /* EOI to master */
    }
    if (regs->int_no == 48) {
        extern void apic_eoi(void);
        apic_eoi();
    }

    if (interrupt_handlers[regs->int_no])
        interrupt_handlers[regs->int_no](regs);
        
    extern void task_check_signals(struct registers* regs);
    task_check_signals(regs);
}

extern void com1_write(const char* buf, uint32_t len);
static int my_strlen(const char* s) { int i=0; while(s[i]) i++; return i; }
void com1_print(const char* s) { com1_write(s, my_strlen(s)); }

void syscall_handler_dbg(struct registers* regs) {
    com1_print("SYSCALL TRACE: eax=");
    com1_print_hex(regs->eax);
    com1_print("\n");
}
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
    buf[i] = '\0';
    com1_print("0x");
    com1_print(buf);
}
