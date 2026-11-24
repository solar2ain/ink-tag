#include <stdint.h>

#include "attm_db_128.h"
#include "co_utils.h"
#include "prf_types.h"
#include "user_custs1_def.h"

static const att_svc_desc128_t inktag_svc_uuid = INKTAG_DA_SERVICE_UUID_128;
static const uint8_t inktag_epd_uuid[ATT_UUID_128_LEN] = INKTAG_DA_EPD_UUID_128;
static const uint8_t inktag_app_ver_uuid[ATT_UUID_128_LEN] = INKTAG_DA_APP_VER_UUID_128;
static const uint8_t inktag_app_ver[] = INKTAG_VERSION_GATT_BYTES;

static const uint16_t att_decl_svc = ATT_DECL_PRIMARY_SERVICE;
static const uint16_t att_decl_char = ATT_DECL_CHARACTERISTIC;
static const uint16_t att_desc_cfg = ATT_DESC_CLIENT_CHAR_CFG;

const uint8_t custs1_services[] = {INKTAG_IDX_SVC, CUSTS1_IDX_NB};
const uint8_t custs1_services_size = ARRAY_LEN(custs1_services) - 1;
const uint16_t custs1_att_max_nb = CUSTS1_IDX_NB;

const struct attm_desc_128 custs1_att_db[CUSTS1_IDX_NB] =
{
    [INKTAG_IDX_SVC] = {(uint8_t *)&att_decl_svc, ATT_UUID_128_LEN, PERM(RD, ENABLE),
                        sizeof(inktag_svc_uuid), sizeof(inktag_svc_uuid), (uint8_t *)&inktag_svc_uuid},
    [INKTAG_IDX_EPD_CHAR] = {(uint8_t *)&att_decl_char, ATT_UUID_16_LEN, PERM(RD, ENABLE), 0, 0, NULL},
    [INKTAG_IDX_EPD_VAL] = {inktag_epd_uuid, ATT_UUID_128_LEN,
                            PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE) | PERM(WRITE_COMMAND, ENABLE) | PERM(NTF, ENABLE),
                            INKTAG_DA_MAX_DATA_LEN, 0, NULL},
    [INKTAG_IDX_EPD_NTF_CFG] = {(uint8_t *)&att_desc_cfg, ATT_UUID_16_LEN,
                                PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                sizeof(uint16_t), 0, NULL},
    [INKTAG_IDX_APP_VER_CHAR] = {(uint8_t *)&att_decl_char, ATT_UUID_16_LEN, PERM(RD, ENABLE), 0, 0, NULL},
    [INKTAG_IDX_APP_VER_VAL] = {inktag_app_ver_uuid, ATT_UUID_128_LEN, PERM(RD, ENABLE),
                                sizeof(inktag_app_ver), sizeof(inktag_app_ver), (uint8_t *)&inktag_app_ver},
};
