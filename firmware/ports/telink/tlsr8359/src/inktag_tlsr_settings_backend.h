#ifndef INKTAG_TLSR_SETTINGS_BACKEND_H
#define INKTAG_TLSR_SETTINGS_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_settings_journal.h"

/* Both shared members require word alignment. Preserve that requirement in
 * their RAM-only owner even under the SDK's whole-program packed ABI. */
typedef struct __attribute__((aligned(4))) {
    inktag_settings_journal_t journal;
    inktag_settings_journal_io_t io;
} inktag_tlsr_settings_backend_t;

bool inktag_tlsr_settings_backend_configure(
    inktag_tlsr_settings_backend_t *backend,
    uint32_t start_address, uint32_t allowed_records);
bool inktag_tlsr_settings_backend_pair_erased(
    inktag_tlsr_settings_backend_t *backend);

#endif
