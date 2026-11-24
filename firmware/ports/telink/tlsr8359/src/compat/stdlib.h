#ifndef INKTAG_TC32_STDLIB_H
#define INKTAG_TC32_STDLIB_H
#include <stddef.h>
void *malloc(unsigned int size);
void free(void *pointer);
#ifndef INKTAG_TELINK_TYPES_READY
int abs(int value);
#endif
#endif
