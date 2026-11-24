#ifndef INKTAG_GR_ADC_H
#define INKTAG_GR_ADC_H

#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx_hal.h"
#else
#include "gr533x_hal.h"
#endif

/* Product callers acquire exactly 16 raw samples before existing calibration.
 * Unlike the vendor polling routine, every FIFO wait has a finite work budget. */
hal_status_t inktag_gr_adc_sample16(adc_handle_t *adc, uint16_t samples[16]);

#endif
