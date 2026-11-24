#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "inktag_ota_reject.h"

int main(void)
{
    uint8_t response[INKTAG_OTA_REJECT_EXTENSION_WIRE_SIZE];
    const uint8_t expected[] = {
        0x02, 0x49, 0x4B, 0x01,
        INKTAG_OTA_REJECT_REASON_VOLTAGE_BELOW_MIN,
        INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
        INKTAG_OTA_REJECT_FLAG_SAMPLED_MV_VALID |
            INKTAG_OTA_REJECT_FLAG_THRESHOLD_MV_VALID |
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
        0x00, 0x00, 0x22, 0x00,
        0xD6, 0x0A,
        0x28, 0x0A,
    };
    const inktag_ota_reject_info_t info = {
        INKTAG_OTA_REJECT_REASON_VOLTAGE_BELOW_MIN,
        INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
        INKTAG_OTA_REJECT_FLAG_SAMPLED_MV_VALID |
            INKTAG_OTA_REJECT_FLAG_THRESHOLD_MV_VALID |
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
        0x00220000u,
        2774u,
        2600u,
    };

    memset(response, 0xAA, sizeof(response));
    assert(inktag_ota_reject_encode(response, sizeof(response), 0x02, &info) ==
           INKTAG_OTA_REJECT_EXTENSION_WIRE_SIZE);
    assert(memcmp(response, expected, sizeof(expected)) == 0);
    assert(inktag_ota_reject_encode(response, sizeof(response) - 1u, 0x02, &info) == 0u);
    assert(inktag_ota_reject_encode(0, sizeof(response), 0x02, &info) == 0u);
    assert(inktag_ota_reject_encode(response, sizeof(response), 0x02, 0) == 0u);
    return 0;
}
