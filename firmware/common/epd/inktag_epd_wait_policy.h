#ifndef INKTAG_EPD_WAIT_POLICY_H
#define INKTAG_EPD_WAIT_POLICY_H

/* Product-level guarantees for a long e-paper BUSY wait. Vendor ports may
 * service their radio or SDK more frequently, and may use GPIO interrupts or
 * an event-aware sleep instead of polling. These values are maximum latency
 * contracts, not a requirement to busy-wait at one common tick rate. */
#define INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS 100u
#define INKTAG_EPD_WAIT_CANCEL_MAX_MS 100u

#endif
