#ifndef TEST_NRF_CRYPTO_SDK_COMMON_H
#define TEST_NRF_CRYPTO_SDK_COMMON_H
#include <stddef.h>
#include <string.h>
#include "nrf_error.h"
/* SHA-256's only dependency on sdk_common.h; crypto implementations are real. */
#define VERIFY_PARAM_NOT_NULL(value) do { if ((value) == NULL) return NRF_ERROR_NULL; } while (0)
#endif
