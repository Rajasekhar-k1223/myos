sed -i '/context_switch(&tasks\[old\].ctx, &tasks\[next\].ctx);/i \
    if (tasks[next].ctx.eip == 0xb0000) {\
        extern void terminal_printf(const char* fmt, ...);\
        terminal_printf("\\n[FATAL] tasks[%d].ctx.eip is 0xb0000!\\n", next);\
        for (;;) asm("hlt");\
    }' src/task.c
