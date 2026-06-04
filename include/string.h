#ifndef STRING_H
#define STRING_H

#include "types.h"

void*  memset(void* dest, int val, size_t count);
void*  memcpy(void* dest, const void* src, size_t count);
void*  memmove(void* dest, const void* src, size_t count);
size_t strlen(const char* str);
int    strcmp(const char* s1, const char* s2);
int    strncmp(const char* s1, const char* s2, size_t n);
char*  strcpy(char* dest, const char* src);
char*  strncpy(char* dest, const char* src, size_t n);
char*  strcat(char* dest, const char* src);
char*  strchr(const char* str, int c);
char*  strstr(const char* haystack, const char* needle);
void   itoa(int value, char* buf, int base);
void   utoa(uint32_t value, char* buf, int base);

#endif
