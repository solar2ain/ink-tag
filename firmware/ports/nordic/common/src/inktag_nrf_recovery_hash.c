/* Recovery integrity SHA only; no ECC, public key or signature provider. */
#include "inktag_nrf_integrity_hash.h"
#include <stddef.h>
#include <string.h>
#include "nrf_error.h"
#include "sha256.h"
#include "inktag_code_guard.h"
#include "inktag_nrf_integrity_hash.inc"
