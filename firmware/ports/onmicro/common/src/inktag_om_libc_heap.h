#ifndef INKTAG_OM_LIBC_HEAP_H
#define INKTAG_OM_LIBC_HEAP_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Vendor newlib break must use its reserved linker arena, never the live
 * MSP interval. A successful boot-time allocation in a stack gap is not safe
 * against a later GUI call plus a BLE interrupt. No allocation on refusal. */
static inline bool inktag_om_heap_advance(size_t capacity, size_t *used,
                                         int increment, size_t *previous) {
    if (!used || !previous || *used > capacity) return false;
    size_t old = *used;
    if (increment >= 0) {
        size_t amount = (size_t)increment;
        if (amount > capacity-old) return false;
        *used = old+amount;
    } else {
        size_t amount = (size_t)(-(increment+1))+1u;
        if (amount > old) return false;
        *used = old-amount;
    }
    *previous = old;
    return true;
}
#endif
