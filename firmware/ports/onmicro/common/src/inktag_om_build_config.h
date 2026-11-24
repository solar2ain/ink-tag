#ifndef INKTAG_OM_BUILD_CONFIG_H
#define INKTAG_OM_BUILD_CONFIG_H

/*
 * The vendor BLE preset enables its development console globally.  InkTag's
 * release image has no UART console, so trim source-built diagnostics after
 * loading the preset.  The precompiled BLE library keeps its own ABI/config.
 */
#include "features.h"
#include "inktag_om_profile.h"

/* Pilot the fixed factory-font layout; other native packages stay pixel-sized. */
#define INKTAG_GUI_FACTORY_FONT_LAYOUT 1

#undef CONFIG_ASSERT
#undef CONFIG_DEBUG
#undef CONFIG_TRACE_IO
#undef CONFIG_LOG
#undef CONFIG_LOG_TO_SHELL
#undef CONFIG_SHELL
#undef CONFIG_SHELL_USART0

/* base keeps ordinary native admission; private measurement is not compiled. */
#if defined(INKTAG_CODE_GUARDS) && INKTAG_CODE_GUARDS
#error "Private protection cannot be selected by the base profile"
#endif
#define INKTAG_CODE_GUARDS 0

#endif
