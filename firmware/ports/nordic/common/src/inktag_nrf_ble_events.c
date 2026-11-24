/* Pinned vendor FIFO/observer source, mechanically generated in the build
 * directory. Only event-buffer placement and reentrant polling are adapted.
 * This is not another product protocol client or event queue. */
#include "app_util_platform.h"
#ifndef INKTAG_NRF_BLE_SDK_ADAPTER
#error "Nordic BLE events require the pinned build-local SDK adapter"
#endif
#include INKTAG_NRF_BLE_SDK_ADAPTER
