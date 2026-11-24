#ifndef INKTAG_TC32_STRING_H
#define INKTAG_TC32_STRING_H
#ifdef INKTAG_TELINK_TYPES_READY
#include "common/string.h"
#else
#include <stddef.h>
void *memmove(void *dest, const void *src, size_t length);
void *memset(void *dest, int value, size_t length);
void *memcpy(void *dest, const void *src, size_t length);
void *memchr(const void *data, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
size_t strlen(const char *text);
char *strncpy(char *dest, const char *src, size_t length);
#endif
#endif
