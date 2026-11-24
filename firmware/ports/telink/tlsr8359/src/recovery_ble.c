#include "recovery.h"

#include <string.h>

#include "stack/ble/ble.h"
#include "inktag_tlsr_identity.h"

static RAM uint8_t s_mac[6];
static RAM uint8_t s_name[20];
static RAM uint8_t s_data_length_pending;
static RAM uint32_t s_data_length_at;

#define RECOVERY_DATA_LENGTH_SETTLE_MS 2000u

RAM uint8_t blt_rxfifo_b[288 * 8];
RAM my_fifo_t blt_rxfifo;
RAM uint8_t blt_txfifo_b[288 * 8];
RAM my_fifo_t blt_txfifo;

static void init_runtime_data(void) {
    memset(s_name, 0, sizeof(s_name));
    memcpy(s_name, "IKT_TL_0000_OTA", 15u);
    memset(blt_rxfifo_b, 0, sizeof(blt_rxfifo_b));
    memset(blt_txfifo_b, 0, sizeof(blt_txfifo_b));
    blt_rxfifo.size = 288u;
    blt_rxfifo.num = 8u;
    blt_rxfifo.wptr = 0u;
    blt_rxfifo.rptr = 0u;
    blt_rxfifo.p = blt_rxfifo_b;
    blt_txfifo.size = 288u;
    blt_txfifo.num = 8u;
    blt_txfifo.wptr = 0u;
    blt_txfifo.rptr = 0u;
    blt_txfifo.p = blt_txfifo_b;
}

static uint8_t hex_digit(uint8_t value) {
    value &= 0x0Fu;
    return (uint8_t)(value < 10u ? '0' + value : 'A' + value - 10u);
}

/* The controller-side slave object keeps unconditional references to the
 * SDK AES-CCM entry points even when the GAP/SMP role is No_Security.  Fixed
 * Recovery has no pairing, bond database, LTK source or encrypted GATT
 * contract; publisher authentication is performed over the complete IKS1
 * image instead.  Resolve those unreachable references locally so the full
 * AES-CCM implementation is not retained.  Should an invalid peer force an
 * encrypted-link path, decryption fails closed rather than accepting data. */
void aes_ll_encryption(uint8_t *key, uint8_t *plaintext, uint8_t *result) {
    (void)key;
    (void)plaintext;
    (void)result;
}

void aes_ll_ccm_encryption_init(uint8_t *ltk, uint8_t *skdm, uint8_t *skds,
                                uint8_t *ivm, uint8_t *ivs,
                                ble_crypt_para_t *parameters) {
    (void)ltk;
    (void)skdm;
    (void)skds;
    (void)ivm;
    (void)ivs;
    (void)parameters;
}

void aes_ll_ccm_encryption(uint8_t *packet, int master,
                           ble_crypt_para_t *parameters) {
    (void)packet;
    (void)master;
    (void)parameters;
}

int aes_ll_ccm_decryption(uint8_t *packet, int master,
                          ble_crypt_para_t *parameters) {
    (void)packet;
    (void)master;
    (void)parameters;
    return 1;
}

static void build_name(void) {
    s_name[7] = hex_digit((uint8_t)(s_mac[1] >> 4));
    s_name[8] = hex_digit(s_mac[1]);
    s_name[9] = hex_digit((uint8_t)(s_mac[0] >> 4));
    s_name[10] = hex_digit(s_mac[0]);
}

void inktag_recovery_ble_request_fast_connection(void) {
    /* Match Telink's OTA sample by requesting the transfer interval only
     * after GATT discovery and the start command. Nowa's P25Q40SH loses a
     * 7.5-10 ms link during repeated Page Program operations, while both
     * boards have enough budget at 15-20 ms; keep the shared Recovery on the
     * common conservative interval until a Flash-aware scheduler is proven. */
    bls_l2cap_requestConnParamUpdate(12, 16, 0, 2000);
    /* ATT MTU does not enlarge the controller's initial27-byte LL payload.
     * As in the App, wait for the connection-parameter procedure to settle
     * before requesting251 octets. Transfer/ACKs continue meanwhile. */
    s_data_length_at = clock_time() +
        RECOVERY_DATA_LENGTH_SETTLE_MS * CLOCK_16M_SYS_TIMER_CLK_1MS;
    s_data_length_pending = 1u;
}

static void recovery_link_boundary(uint8_t event, uint8_t *packet, int length) {
    (void)packet;
    (void)length;
    s_data_length_pending = 0u;
    inktag_recovery_att_link_boundary(event == BLT_EV_FLAG_CONNECT);
}

void inktag_recovery_ble_process(void) {
    if (s_data_length_pending &&
        (int32_t)(clock_time() - s_data_length_at) >= 0) {
        s_data_length_pending = 0u;
        /* One best-effort negotiation per BEGIN; an unsupported/busy peer
         * keeps the existing fragmented transport, without a restart loop. */
        (void)blc_ll_exchangeDataLength(LL_LENGTH_REQ, 251u);
    }
}

void inktag_recovery_ble_init(void) {
    uint8_t advertising[20];
    uint8_t scan[] = {
        17, 0x07,
        0x12,0x19,0x0D,0x0C,0x0B,0x0A,0x09,0x08,
        0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00,
    };
    init_runtime_data();
    s_data_length_pending = 0u;
    if (!inktag_tlsr_identity_init(s_mac)) return;
    build_name();
    /* Keep the complete stable identity in the primary legacy advertisement,
     * matching the application. Windows may omit a scan response, so putting
     * the name there made same-device Recovery discovery needlessly flaky.
     * The Recovery service UUID remains available in the active-scan response
     * and is still required by clients before selecting this target. */
    advertising[0] = 2u;
    advertising[1] = 0x01u;
    advertising[2] = 0x06u;
    advertising[3] = 16u;
    advertising[4] = 0x09u;
    memcpy(&advertising[5], s_name, 15u);
    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(s_mac);
    blc_ll_initAdvertising_module(s_mac);
    if (inktag_tlsr_identity_address_type() == 1u &&
        blc_ll_setRandomAddr(s_mac) != BLE_SUCCESS) return;
    blc_ll_initConnection_module();
    blc_ll_initSlaveRole_module();
    blc_gap_peripheral_init();
    inktag_recovery_att_init();
    inktag_recovery_att_set_name(s_name, 15u);
    blc_l2cap_register_handler(blc_l2cap_packet_receive);
    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, recovery_link_boundary);
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, recovery_link_boundary);
    bls_ll_setAdvData(advertising, sizeof(advertising));
    bls_ll_setScanRspData(scan, sizeof(scan));
    bls_ll_setAdvParam(ADVERTISING_INTERVAL, ADVERTISING_INTERVAL + 40,
        ADV_TYPE_CONNECTABLE_UNDIRECTED,
        inktag_tlsr_identity_address_type() == 1u ? OWN_ADDRESS_RANDOM : OWN_ADDRESS_PUBLIC,
        0, NULL,
        BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
    rf_set_power_level_index(RF_POWER_P3p01dBm);
    blc_att_setRxMtuSize(250);
    if (bls_ll_setAdvEnable(1) == BLE_SUCCESS)
        inktag_recovery_ota_role_ready();
}
