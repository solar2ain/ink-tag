#ifndef INKTAG_GR_PIN_RECOVERY_RUNTIME_H
#define INKTAG_GR_PIN_RECOVERY_RUNTIME_H
#include <stdbool.h>
/* Returns true for every non-legacy selector, including invalid data. Such a
 * target must stay dark rather than fall back to compiled legacy LEDs. */
bool inktag_gr_pin_recovery_required(void);
/* Only reads existing, already initialized guarded settings; never seeds. */
bool inktag_gr_pin_recovery_open(void);
void inktag_gr_pin_recovery_output(bool red, bool green, bool blue);
#endif
