#ifndef INKTAG_GUI_ASSETS_H
#define INKTAG_GUI_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS) && !defined(INKTAG_GUI_ROLE_FONT_ASSETS)
#error Native_fonts_require_the_role_font_contract
#endif

#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
#if !defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
#error User_fonts_require_the_native_font_contract
#endif
#if !defined(INKTAG_GUI_ATOMIC_FONT_ASSETS) || !defined(INKTAG_GUI_ASSET_READ_FAILURE_AWARE)
#error Role_fonts_require_atomic_selection_and_latched_read_failure
#endif
#include "inktag_gui_large_digits.h"
bool inktag_gui_assets_role_snapshot(inktag_gui_role_snapshot_t *);
/* Sticky byte-read failure, distinct from whether a render session is active.
 * Required inside U8g2 lookup/RLE loops, not just after a framebuffer page. */
bool inktag_gui_asset_font_read_failed(void);
#endif

/* Optional vendor-owned session state. Choose all four faces once; an IO
 * failure never changes source during the same frame. Existing ports may
 * reuse their own state with the same begin/healthy/end callbacks below. */
typedef struct { bool active, user_selected, failed; } inktag_gui_asset_render_session_t;
static inline bool inktag_gui_asset_render_begin(inktag_gui_asset_render_session_t *s, bool valid_user) {
    if (!s || s->active) return false;
    s->active = true; s->user_selected = valid_user; s->failed = false;
    return true;
}
static inline void inktag_gui_asset_render_fail(inktag_gui_asset_render_session_t *s) {
    if (s && s->active) s->failed = true;
}
static inline bool inktag_gui_asset_render_healthy(const inktag_gui_asset_render_session_t *s) {
    return s && s->active && !s->failed;
}
static inline void inktag_gui_asset_render_end(inktag_gui_asset_render_session_t *s) {
    if (s) s->active = false;
}

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
