#pragma once

/* Product-wide baseline for long-lived connectable advertising.
 * BLE advertising intervals use 0.625 ms units, so 1600 is one second.
 * A port may use a faster interval only for a separately bounded discovery
 * phase with an explicit timeout and exact-board power evidence. */
#define INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS 1600u
