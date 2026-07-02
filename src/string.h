#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

/* Memory */
void*  memcpy(void* restrict dest, const void* restrict src, size_t n);
void*  memmove(void* dest, const void* src, size_t n);
void*  memset(void* s, int c, size_t n);
int    memcmp(const void* a, const void* b, size_t n);

/* String inspection */
size_t strlen(const char* s);
int    strcmp(const char* s1, const char* s2);
int    strncmp(const char* s1, const char* s2, size_t n);
char*  strstr(const char* haystack, const char* needle);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);

/* String building */
char*  strcpy(char* dest, const char* src);
char*  strncpy(char* dest, const char* src, size_t n);
char*  strcat(char* dest, const char* src);
char*  strncat(char* dest, const char* src, size_t n);

/* Conversion */
long      strtol(const char* str, char** endptr, int base);
long long strtoll(const char* str, char** endptr, int base);
char*     itoa(int n, char* buf, int base);
char*     utoa(unsigned int n, char* buf, int base);

/* Sorting */
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));

/* Formatted output (sprintf.c) */
#include <stdarg.h>
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int sscanf(const char* str, const char* fmt, ...);

#endif
