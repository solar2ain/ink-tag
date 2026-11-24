/* Permanent-base provider: no token, protected-state read or authorization.
 * The Pro build selects inktag_om_activation_pro.c at the same SDK TU name. */
#include "inktag_om_activation.h"
#include <stddef.h>
bool inktag_om_activation_init(inktag_activation_t *activation) {
    if (!activation) return false;
    /* ZKEL's original immutable root is NFC UID. base deliberately has no NFC UID
     * provider; preserve the missing-ID refusal and stored password. */
    return inktag_activation_init(activation, NULL, NULL);
}
