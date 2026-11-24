#ifndef INKTAG_OM_APP_H
#define INKTAG_OM_APP_H
#include <stdbool.h>
#include <stdint.h>

#define INKTAG_OM_EVENT_SERVICE_TIMER_MS 50u

/* Normal firmware links only the LZO image backend. Historical codec profile
 * numbers remain reserved: 0 RLE, 1 both, 2 LZO. RLE also requires explicit
 * shared PACKBITS enablement and a separately supplied decoder build input.
 * Raw image uploads and all non-image object encodings remain available. */
#ifndef INKTAG_OM_IMAGE_CODEC_PROFILE
#define INKTAG_OM_IMAGE_CODEC_PROFILE 2
#endif
#if INKTAG_OM_IMAGE_CODEC_PROFILE < 0 || INKTAG_OM_IMAGE_CODEC_PROFILE > 2
#error Invalid OM image codec profile
#endif

void inktag_om_app_start(void);
void inktag_om_app_process(void);
void inktag_om_app_epd_maintenance(void);
/* Current verified common policy; never a persisted/cached Pro boolean. */
bool inktag_om_app_epd_control_allowed(void);
/* Live product permission for custom actuators, separate from stock inputs. */
bool inktag_om_app_custom_input_allowed(void);
/* Observe an already-completed checked controller read; never performs IO. */
void inktag_om_app_epd_temperature_read(uint8_t command, const uint8_t *raw,
                                         uint8_t length, bool read_ok);
/* Called only after pure configuration preflight, while the foreground owns
 * the command. Parks the old mapping before its durable replacement. */
void inktag_om_app_configuration_park(void);
/* Called after all passive AUX routes validate, before it configures Timer1. */
uint32_t inktag_om_app_monotonic_ms(void);

#endif
