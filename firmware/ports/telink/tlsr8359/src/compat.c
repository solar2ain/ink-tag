#include "main.h"

int strncmp(const char *left, const char *right, unsigned int length) {
    while (length--) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a != b) return (int)a - (int)b;
        if (a == 0u) return 0;
    }
    return 0;
}
