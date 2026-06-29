sed -i 's/p = "char/printf("reached keyword parse\\n"); p = "char/' src/compiler/c4.c
sed -i 's/cycle = 0;/printf("starting execution loop\\n"); cycle = 0;/' src/compiler/c4.c
