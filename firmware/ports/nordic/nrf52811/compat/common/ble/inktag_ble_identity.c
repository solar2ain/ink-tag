#include "inktag_ble_identity.h"

bool inktag_ble_device_name_valid_shared(const uint8_t *name, uint8_t length)
{
    return inktag_ble_device_name_valid(name, length);
}
