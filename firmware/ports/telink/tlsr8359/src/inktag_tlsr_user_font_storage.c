#include "../../common/src/inktag_tlsr_build_profile.h"
#ifdef INKTAG_TLSR_GUI_ASSET_HOST_TEST
#include <tlsr_gui_asset_test_platform.h>
#else
#include "main.h"
#endif
#include <string.h>
#include "inktag_tlsr_user_font_storage.h"
#include "inktag_tlsr_power.h"

static bool font_span(uint32_t offset, uint32_t length) {
    return length && offset < INKTAG_USER_FONT_SIZE &&
        length <= INKTAG_USER_FONT_SIZE - offset;
}
bool inktag_tlsr_user_font_read(uint32_t offset, uint8_t *data, uint32_t length) {
    if (!data || !font_span(offset, length)) return false;
    /* SDK read is void: bounds are proven, not electrical read integrity.
     * The native package adapter validates CRC and its header-last commit. */
    flash_read_page(INKTAG_USER_FONT_START + offset, length, data);
    return true;
}
#if INKTAG_BUILD_PRO
#include "inktag_tlsr_user_font_upload_pro.inc"
#else
bool inktag_tlsr_user_font_program(uint32_t offset, const uint8_t *data, uint32_t length) { (void)offset; (void)data; (void)length; return false; }
bool inktag_tlsr_user_font_erase(void) { return false; }
#endif
