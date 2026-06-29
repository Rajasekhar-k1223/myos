sed -i 's/while (1) {/extern void elf_load_and_run(const char*); elf_load_and_run("c4.elf"); while (1) {/' src/kernel.c
