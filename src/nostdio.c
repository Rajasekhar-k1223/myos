#include <stdio.h>
#include <stddef.h>

/*
 * Several vendored single-header libraries (nanosvg.h, fontstash.h) include
 * <stdio.h> unconditionally and compile in a handful of file-loading
 * convenience functions (nsvgParseFromFile, fons__loadFile) as part of
 * their public surface, even though this kernel only ever feeds them
 * in-memory buffers from the initrd tar -- there's no POSIX filesystem
 * here for fopen() to open anything from. Those dead functions still need
 * fopen/fclose/fseek/ftell/fread to satisfy the linker. One shared
 * definition here (rather than one per vendored-library wrapper .c file)
 * avoids duplicate-symbol link errors. They're intentionally non-functional
 * stubs for intentionally-unused code paths, not a claim that file I/O
 * works in this kernel.
 */
FILE* fopen(const char* path, const char* mode) { (void)path; (void)mode; return 0; }
int fclose(FILE* f) { (void)f; return 0; }
int fseek(FILE* f, long off, int whence) { (void)f; (void)off; (void)whence; return -1; }
long ftell(FILE* f) { (void)f; return 0; }
size_t fread(void* buf, size_t sz, size_t n, FILE* f) { (void)buf; (void)sz; (void)n; (void)f; return 0; }
