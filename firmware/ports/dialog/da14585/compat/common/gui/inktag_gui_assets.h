#ifndef INKTAG_GUI_ASSETS_H
#define INKTAG_GUI_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

/* Optional read-failure pilot contract. A false session must not emit another
 * framebuffer page or be reported as a successful display. The vendor latches
 * IO failure until end; begin may retry the next independent session. Ports
 * not yet adopting this callback retain their existing behavior explicitly. */
#if defined(INKTAG_GUI_ASSET_READ_FAILURE_AWARE)
bool inktag_gui_assets_healthy(void);
#else
static inline bool inktag_gui_assets_healthy(void) { return true; }
#endif

/* The renderer owns only the lifecycle and byte-reader contract. The vendor
 * port supplies the transport, while the exact board supplies the fitted
 * storage identity, pins and qualified physical region. */
#if defined(INKTAG_GUI_EXTERNAL_FONT_ASSETS) || \
    defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
bool inktag_gui_assets_begin(void);
void inktag_gui_assets_end(void);
uint8_t inktag_gui_asset_read_byte(const uint8_t *address);
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
const uint8_t *inktag_gui_asset_font(uint8_t face_id);
#endif
#else
static inline bool inktag_gui_assets_begin(void) { return true; }
static inline void inktag_gui_assets_end(void) {}
#endif

#endif
