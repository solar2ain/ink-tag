#include "main.h"

#include <string.h>
#include "stack/ble/ble.h"
#include "inktag_tlsr_identity.h"
#include "inktag_ble_identity.h"
#include "inktag_ble_power.h"
#include "inktag_soak_telemetry.h"

static RAM uint8_t s_connected;
static RAM uint8_t s_notify_enabled;
static RAM uint8_t s_advertising_started;
static RAM uint32_t s_advertising_ready_at;
static RAM uint8_t s_mac[6];
static RAM uint8_t s_name[20] = "IKT_TL_0000";
static RAM uint8_t s_default_name[20] = "IKT_TL_0000";
static RAM uint8_t s_normal_adv[
    INKTAG_BLE_FLAGS_AD_BYTES +
    INKTAG_BLE_COMPLETE_NAME_AD_OVERHEAD +
    INKTAG_BLE_DEVICE_NAME_MAX_BYTES +
    INKTAG_BLE_MANUFACTURER_MARKER_AD_BYTES];
static RAM uint8_t s_normal_adv_length;
static RAM uint8_t s_scan_response[INKTAG_BLE_SERVICE_UUID128_AD_BYTES] = {
    17u, 0x07u,
    0xECu, 0x5Au, 0x67u, 0x1Cu, 0xC1u, 0xB6u, 0x46u, 0xFBu,
    0x8Du, 0x91u, 0x28u, 0xD8u, 0x01u, 0x00u, 0x75u, 0x62u,
};
static RAM uint8_t s_empty_scan_response;
static RAM uint8_t s_name_length = 11;
static RAM uint8_t s_default_name_length = 11;
#if INKTAG_BUILD_PRO
#include "inktag_ble_find_state_pro.inc"
#else
#define s_find_mode_active 0u
#endif
static RAM uint8_t s_adv_restart_pending;
static RAM uint32_t s_adv_restart_at;
static RAM uint8_t s_data_length_request_pending;
static RAM uint32_t s_data_length_request_at;

#define INKTAG_BLE_NOTIFY_QUEUE_DEPTH 3u
#define INKTAG_BLE_NOTIFY_MAX_BYTES 244u
#define INKTAG_BLE_NOTIFY_RETRY_MS 10u
#define INKTAG_BLE_NOTIFY_RETRY_LIMIT 500u
#define INKTAG_BLE_NOTIFY_RETRY_TIMEOUT_MS 5000u
#define INKTAG_ADV_READY_EXTRA_GUARD_MS 250u
#define INKTAG_ADV_RESTART_SETTLE_MS 100u
#define INKTAG_BLE_DATA_LENGTH_OCTETS 251u
#define INKTAG_BLE_DATA_LENGTH_SETTLE_MS 2000u
#define INKTAG_BLE_RETENTION_THRESHOLD_MS 95u
#define INKTAG_ADV_INTERVAL_UNITS_TO_MS(units) (((units) * 5u) / 8u)
#define INKTAG_NORMAL_ADV_READY_FALLBACK_MS \
    (INKTAG_ADV_INTERVAL_UNITS_TO_MS(INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS) + \
     INKTAG_ADV_READY_EXTRA_GUARD_MS)
#define INKTAG_FIND_ADV_READY_FALLBACK_MS \
    (INKTAG_ADV_INTERVAL_UNITS_TO_MS(INKTAG_FINDMY_ADV_INTERVAL_UNITS) + \
     INKTAG_ADV_READY_EXTRA_GUARD_MS)

typedef struct {
    uint8_t data[INKTAG_BLE_NOTIFY_MAX_BYTES];
    uint16_t length;
    uint8_t reliable;
    uint8_t reserved;
} inktag_ble_notify_packet_t;

/* A V2 command response is application protocol traffic, not the ATT write
 * response. Keep a bounded copy until the controller accepts it. Best-effort
 * legacy/status text may use only one slot, leaving room for a response and
 * its terminal event without growing the already large BLE FIFOs. */
static RAM inktag_ble_notify_packet_t
    s_notify_queue[INKTAG_BLE_NOTIFY_QUEUE_DEPTH];
static RAM uint8_t s_notify_head;
static RAM uint8_t s_notify_tail;
static RAM uint8_t s_notify_count;
static RAM uint16_t s_notify_head_failures;
static RAM uint8_t s_notify_disconnect_pending;
static RAM uint32_t s_notify_first_failure_at;
static RAM uint32_t s_notify_retry_at;
static RAM uint8_t s_notify_last_status;
static RAM uint16_t s_notify_retry_count;
static RAM uint16_t s_notify_drop_count;

/* The B85 stack exports the legacy scan-response packet but omits its extern
 * declaration from ll_adv.h.  bls_ll_setAdvParam() refreshes pkt_adv.advA
 * when switching back to OWN_ADDRESS_PUBLIC, but leaves this sibling packet
 * carrying the preceding random Find address and TxAdd address-type bit. */
extern rf_packet_adv_t pkt_scan_rsp;

static _attribute_ram_code_ void notify_queue_clear(void) {
    s_notify_head = 0u;
    s_notify_tail = 0u;
    s_notify_count = 0u;
    s_notify_head_failures = 0u;
    s_notify_disconnect_pending = 0u;
    s_notify_first_failure_at = 0u;
    s_notify_retry_at = 0u;
}

static void notify_count_retry(void) {
    if (s_notify_retry_count != UINT16_MAX) ++s_notify_retry_count;
}

static void notify_count_drop(void) {
    if (s_notify_drop_count != UINT16_MAX) ++s_notify_drop_count;
}

static void notify_pop(void) {
    s_notify_tail = (uint8_t)((s_notify_tail + 1u) %
                              INKTAG_BLE_NOTIFY_QUEUE_DEPTH);
    --s_notify_count;
    s_notify_head_failures = 0u;
    s_notify_first_failure_at = 0u;
    s_notify_retry_at = 0u;
}

static void notify_process(void) {
    inktag_ble_notify_packet_t *packet;
    ble_sts_t status;
    uint32_t now;

    if (!s_connected || !s_notify_enabled || s_notify_count == 0u) return;
    now = clock_time();
    if (s_notify_retry_at != 0u &&
        (int32_t)(now - s_notify_retry_at) < 0) return;

    packet = &s_notify_queue[s_notify_tail];
    status = bls_att_pushNotifyData(INKTAG_EPD_DP_H, packet->data,
                                   packet->length);
    if (status == BLE_SUCCESS) {
        /* Success means the controller owns exactly one copy. Never retry an
         * accepted packet: doing so would duplicate a V2 transaction reply. */
        notify_pop();
        return;
    }

    s_notify_last_status = status;
    notify_count_retry();
    if (!packet->reliable) {
        notify_count_drop();
        notify_pop();
        return;
    }
    if (s_notify_head_failures == 0u) s_notify_first_failure_at = now;
    if (s_notify_head_failures != UINT16_MAX) ++s_notify_head_failures;
    if (s_notify_head_failures >= INKTAG_BLE_NOTIFY_RETRY_LIMIT ||
        (int32_t)(now - s_notify_first_failure_at) >=
            (int32_t)(INKTAG_BLE_NOTIFY_RETRY_TIMEOUT_MS *
                      CLOCK_16M_SYS_TIMER_CLK_1MS)) {
        /* A broken notification path cannot carry an application error. End
         * the connection so the client gets an explicit transport failure
         * instead of mistaking its ATT write response for a V2 reply. */
        notify_count_drop();
        s_notify_disconnect_pending = 1u;
        return;
    }
    s_notify_retry_at = now + INKTAG_BLE_NOTIFY_RETRY_MS *
                              CLOCK_16M_SYS_TIMER_CLK_1MS;
}

RAM uint8_t blt_rxfifo_b[288 * 8];
RAM my_fifo_t blt_rxfifo = {288, 8, 0, 0, blt_rxfifo_b};
RAM uint8_t blt_txfifo_b[288 * 8];
RAM my_fifo_t blt_txfifo = {288, 8, 0, 0, blt_txfifo_b};

static _attribute_ram_code_ void rf_power_callback(uint8_t event, uint8_t *packet, int length) {
    (void)event; (void)packet; (void)length;
    rf_set_power_level_index(RF_POWER_P3p01dBm);
}

static _attribute_ram_code_ void advertising_callback(uint8_t event, uint8_t *packet, int length) {
    (void)event; (void)packet; (void)length;
    /* TERMINATE can be followed by an event from the old SDK ADV set before
     * the cooperative public-set rebuild. Only that rebuild's owner may
     * consume its pending work; an old event must not cancel recovery. */
    if (s_adv_restart_pending) return;
    /* BLT_EV_FLAG_ADV is raised for a real advertising event, unlike the
     * immediate return from bls_ll_setAdvEnable().  GR and OM likewise defer
     * display restoration until advertising has actually started. */
    s_advertising_started = 1u;
    s_advertising_ready_at = 0u;
    s_adv_restart_pending = 0u;
    s_adv_restart_at = 0u;
}

static _attribute_ram_code_ void connect_callback(uint8_t event, uint8_t *packet, int length) {
    (void)event; (void)packet; (void)length;
    inktag_soak_telemetry_connection_started();
#if INKTAG_BUILD_PRO
#include "inktag_ble_find_connection_pro.inc"
#endif
    s_connected = 1;
    /* A connection is direct proof that at least one connectable advertising
     * event completed. Some TLSR8359 SDK builds do not deliver
     * BLT_EV_FLAG_ADV even though the controller is advertising normally. */
    s_advertising_started = 1u;
    s_advertising_ready_at = 0u;
#if INKTAG_BUILD_PRO
    s_find_mode_active = 0u;
    s_find_switch_deadline = 0u;
#endif
    s_notify_enabled = 0;
    s_data_length_request_pending = 0u;
    s_data_length_request_at = 0u;
    notify_queue_clear();
    inktag_app_set_connected(true);
    bls_l2cap_requestConnParamUpdate(8, 12, 0, 2000);
}

static _attribute_ram_code_ void suspend_enter_callback(
    uint8_t event, uint8_t *packet, int length) {
    (void)event; (void)packet; (void)length;
    /* This SDK event is emitted at a real controller sleep transition. It is
     * not inferred from merely enabling a suspend mask. */
#if INKTAG_BUILD_PRO
    if (inktag_nfc_resume()) bls_pm_setWakeupSource(PM_WAKEUP_PAD);
#endif
    inktag_soak_telemetry_sleep_entered();
}

static _attribute_ram_code_ void disconnect_callback(uint8_t event, uint8_t *packet, int length) {
    (void)event; (void)packet; (void)length;
    s_connected = 0;
    /* The B85 slave stack reports ADV again after a disconnect before the
     * public set is reliably transmitting.  The raw link state is therefore
     * not a readiness signal.  Do not rebuild from inside the terminate
     * callback either: give it a short bounded settle interval, then recreate
     * the public set before the first one-second automatic ADV event. */
    s_advertising_started = 0u;
    s_advertising_ready_at = 0u;
    s_adv_restart_pending = 1u;
    s_adv_restart_at = clock_time() +
        INKTAG_ADV_RESTART_SETTLE_MS * CLOCK_16M_SYS_TIMER_CLK_1MS;
#if INKTAG_BUILD_PRO
    s_find_mode_active = 0u;
    s_find_switch_deadline = 0u;
#endif
    s_data_length_request_pending = 0u;
    s_data_length_request_at = 0u;
    s_notify_enabled = 0;
    notify_queue_clear();
    inktag_app_set_notify(false);
    inktag_app_set_connected(false);
    inktag_app_disconnected();
}

static uint8_t hex_digit(uint8_t value) {
    value &= 0x0Fu;
    return (uint8_t)(value < 10u ? '0' + value : 'A' + value - 10u);
}

static void build_default_name(void) {
    static const uint8_t prefix[] = "IKT_TL_";
    memcpy(s_default_name, prefix, sizeof(prefix) - 1u);
    s_default_name[7] = hex_digit((uint8_t)(s_mac[1] >> 4));
    s_default_name[8] = hex_digit(s_mac[1]);
    s_default_name[9] = hex_digit((uint8_t)(s_mac[0] >> 4));
    s_default_name[10] = hex_digit(s_mac[0]);
    s_default_name_length = 11u;
    memcpy(s_name, s_default_name, s_default_name_length);
    s_name_length = s_default_name_length;
}

static void copy_name_buffers(const uint8_t *name, uint8_t length);

static void apply_default_or_saved_name(void) {
#if INKTAG_BUILD_PRO
#include "inktag_ble_saved_name_apply_pro.inc"
#else
    /* Keep an old Pro override in its record, without applying it in InkTag. */
    (void)inktag_settings_apply_default_name(s_default_name, s_default_name_length);
    copy_name_buffers(s_default_name, s_default_name_length);
#endif
}

static void copy_name_buffers(const uint8_t *name, uint8_t length) {
    extern void inktag_att_set_name(const uint8_t *name, uint8_t length);
    if (!inktag_ble_device_name_valid(name, length)) return;
    memset(s_name, 0, sizeof(s_name));
    memcpy(s_name, name, length);
    s_name_length = length;
    inktag_att_set_name(name, length);
}

static void rebuild_normal_advertisement(void) {
    uint8_t offset = 0u;

    /* Keep this byte-exact with the common legacy-ADV size proof. The complete
     * name is primary so passive/name-only scanners never depend on receiving
     * a second packet. The four-byte InkTag marker remains a stable fallback. */
    s_normal_adv[offset++] = 2u;
    s_normal_adv[offset++] = 0x01u;
    s_normal_adv[offset++] = 0x06u;
    s_normal_adv[offset++] = (uint8_t)(s_name_length + 1u);
    s_normal_adv[offset++] = 0x09u;
    memcpy(&s_normal_adv[offset], s_name, s_name_length);
    offset = (uint8_t)(offset + s_name_length);
    s_normal_adv[offset++] = 5u;
    s_normal_adv[offset++] = 0xFFu;
    s_normal_adv[offset++] = 0xF7u;
    s_normal_adv[offset++] = 0x04u;
    s_normal_adv[offset++] = 0x02u;
    s_normal_adv[offset++] = 0x03u;
    s_normal_adv_length = offset;
}

static uint8_t ordinary_address_type(void) {
    return inktag_tlsr_identity_address_type() == 1u ?
           OWN_ADDRESS_RANDOM : OWN_ADDRESS_PUBLIC;
}

static void restore_public_scan_response_identity(void) {
    memcpy(pkt_scan_rsp.advA, s_mac, sizeof(s_mac));
    pkt_scan_rsp.header.txAddr = ordinary_address_type() == OWN_ADDRESS_RANDOM;
}

#if INKTAG_BUILD_PRO
#include "inktag_ble_advertising_pro.inc"
#else
static bool set_advertising_mode(bool find_mode) {
    ble_sts_t status;
    if (find_mode) return false;
    status = bls_ll_setAdvEnable(0);
    if (status != BLE_SUCCESS) return false;
    rebuild_normal_advertisement();
    if (ordinary_address_type() == OWN_ADDRESS_RANDOM)
        status = blc_ll_setRandomAddr(s_mac);
    if (status == BLE_SUCCESS)
        status = bls_ll_setAdvData(s_normal_adv, s_normal_adv_length);
    if (status == BLE_SUCCESS)
        status = bls_ll_setScanRspData(s_scan_response, sizeof(s_scan_response));
    if (status == BLE_SUCCESS) {
        status = bls_ll_setAdvParam(INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS,
            INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS,
            ADV_TYPE_CONNECTABLE_UNDIRECTED, ordinary_address_type(),
            0u, NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
        if (status == BLE_SUCCESS) restore_public_scan_response_identity();
    }
    if (status != BLE_SUCCESS) return false;
    s_advertising_started = 0u;
    s_advertising_ready_at = 0u;
    if (bls_ll_setAdvEnable(1) != BLE_SUCCESS) return false;
    if (s_adv_restart_pending)
        s_advertising_ready_at = clock_time() +
            INKTAG_NORMAL_ADV_READY_FALLBACK_MS * CLOCK_16M_SYS_TIMER_CLK_1MS;
    else s_advertising_started = 1u;
    return true;
}
#endif

#if INKTAG_BUILD_PRO
#include "inktag_ble_name_write_pro.inc"
#else
void inktag_ble_set_name(const uint8_t *name, uint8_t length) { (void)name; (void)length; }
#endif

void inktag_ble_apply_settings_name(void) {
    apply_default_or_saved_name();
    rebuild_normal_advertisement();
    (void)bls_ll_setAdvData(s_normal_adv, s_normal_adv_length);
}

#if INKTAG_BUILD_PRO
#include "inktag_ble_find_identity_pro.inc"
#else
bool inktag_ble_apply_find_config(const inktag_findmy_config_t *config,
    const uint8_t selected_public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]) {
    (void)config; (void)selected_public_key; return false;
}
#endif

void inktag_ble_process(void) {
    uint32_t now;
    bool next_find;
    bool advertising_started;
    if (s_notify_disconnect_pending) {
        (void)bls_ll_terminateConnection(HCI_ERR_CONN_TERM_BY_LOCAL_HOST);
        return;
    }
    notify_process();
    now = clock_time();
    if (s_data_length_request_pending &&
        (int32_t)(now - s_data_length_request_at) >= 0) {
        /* The B85 stack starts every connection at the legacy 27-byte LL
         * payload even though the 288-byte ACL FIFOs support 251 bytes.
         * Windows is not required to initiate DLE.  Do not start another LL
         * control procedure in the raw connect callback: the SDK's connection
         * parameter procedure is still settling there and silently rejects
         * the competing request.  The Telink slave guidance uses this same
         * two-second post-connect settling interval.  CCC proves the InkTag
         * GATT path is ready, so negotiate before the client's bulk transfer. */
        (void)blc_ll_exchangeDataLength(LL_LENGTH_REQ,
                                        INKTAG_BLE_DATA_LENGTH_OCTETS);
        s_data_length_request_pending = 0u;
        s_data_length_request_at = 0u;
    }
    if (s_adv_restart_pending) {
        if (s_connected) {
            s_adv_restart_pending = 0u;
            s_adv_restart_at = 0u;
        } else if ((int32_t)(now - s_adv_restart_at) >= 0) {
            /* Rebuild even when blc_ll_getCurrentState() says ADV: the old
             * set can remain logically ADV while no packets are emitted. */
            if (set_advertising_mode(false)) {
                s_adv_restart_pending = 0u;
                s_adv_restart_at = 0u;
            } else {
                s_adv_restart_at = now + 500u *
                                         CLOCK_16M_SYS_TIMER_CLK_1MS;
            }
        }
        if (s_adv_restart_pending) return;
    }
    /* Always evaluate the common readiness helper, including when Find is
     * disabled. On SDK builds that omit the first post-disconnect
     * BLT_EV_FLAG_ADV, it promotes an accepted ordinary advertising set after
     * the interval-aware guard and releases the temporary awake hold. Putting
     * !s_find_enabled first in this condition short-circuited the helper and
     * stranded a Find-disabled Nebular after an immediate V2 disconnect. */
    advertising_started = inktag_ble_advertising_started();
#if INKTAG_BUILD_PRO
#include "inktag_ble_window_advertising_pro.inc"
#include "inktag_ble_find_schedule_pro.inc"
#endif


}

void inktag_ble_init(void) {
    /* Match the common ordinary-discovery contract. Find replaces this with
     * its complete 31-byte Apple frame; fixed Recovery owns a separate name. */
    if (!inktag_tlsr_identity_init(s_mac)) return;
    s_advertising_started = 0u;
    s_advertising_ready_at = 0u;
#if INKTAG_BUILD_PRO
    s_find_mode_active = 0u;
    s_find_switch_deadline = 0u;
#endif
    s_adv_restart_pending = 0u;
    s_adv_restart_at = 0u;
    notify_queue_clear();
    s_notify_last_status = BLE_SUCCESS;
    s_notify_retry_count = 0u;
    s_notify_drop_count = 0u;
    build_default_name();
    apply_default_or_saved_name();
    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(s_mac);
    blc_ll_initAdvertising_module(s_mac);
    if (ordinary_address_type() == OWN_ADDRESS_RANDOM &&
        blc_ll_setRandomAddr(s_mac) != BLE_SUCCESS) return;
    blc_ll_initConnection_module();
    blc_ll_initSlaveRole_module();
    blc_ll_initPowerManagement_module();
    blc_gap_peripheral_init();
    extern void my_att_init(void);
    my_att_init();
    blc_l2cap_register_handler(blc_l2cap_packet_receive);
    /* This product protocol is deliberately unbonded.  Do not pull in the
     * complete SMP implementation merely to re-select its reset default. */
    rebuild_normal_advertisement();
    bls_ll_setAdvData(s_normal_adv, s_normal_adv_length);
    (void)bls_ll_setScanRspData(s_scan_response, sizeof(s_scan_response));
    if (bls_ll_setAdvParam(INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS,
            INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS,
            ADV_TYPE_CONNECTABLE_UNDIRECTED, ordinary_address_type(), 0, NULL,
            BLT_ENABLE_ADV_ALL, ADV_FP_NONE) == BLE_SUCCESS) {
        restore_public_scan_response_identity();
    }
    rf_power_callback(0, NULL, 0);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT,
                                  rf_power_callback);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER,
                                  suspend_enter_callback);
    bls_app_registerEventCallback(BLT_EV_FLAG_ADV, advertising_callback);
    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, connect_callback);
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, disconnect_callback);
    /* Register the real advertising-event callback before enabling the set.
     * Recovery confirmation and boot display restore intentionally wait for
     * that event, so missing the first one can strand a valid trial image. */
    if (bls_ll_setAdvEnable(1) == BLE_SUCCESS) {
        s_advertising_ready_at =
            clock_time() + INKTAG_NORMAL_ADV_READY_FALLBACK_MS *
                               CLOCK_16M_SYS_TIMER_CLK_1MS;
    }
    blc_pm_setDeepsleepRetentionThreshold(INKTAG_BLE_RETENTION_THRESHOLD_MS,
                                         INKTAG_BLE_RETENTION_THRESHOLD_MS);
    blc_pm_setDeepsleepRetentionEarlyWakeupTiming(240);
    blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);
    blc_att_setRxMtuSize(250);
}

bool inktag_ble_notify(const uint8_t *data, uint16_t length,
                       inktag_ble_notify_kind_t kind) {
    inktag_ble_notify_packet_t *packet;
    bool reliable = kind == INKTAG_BLE_NOTIFY_RELIABLE;
    if (!s_connected || !s_notify_enabled || !data || length == 0u ||
        length > INKTAG_BLE_NOTIFY_MAX_BYTES) return false;
    /* At most one best-effort packet may wait. Two slots remain reserved for
     * a command response and the operation event which can immediately follow
     * it. Reliable overflow is converted into a bounded disconnect below. */
    if ((!reliable && s_notify_count != 0u) ||
        s_notify_count == INKTAG_BLE_NOTIFY_QUEUE_DEPTH) {
        notify_count_drop();
        if (reliable) s_notify_disconnect_pending = 1u;
        return false;
    }
    packet = &s_notify_queue[s_notify_head];
    packet->length = length;
    packet->reliable = reliable ? 1u : 0u;
    memcpy(packet->data, data, length);
    s_notify_head = (uint8_t)((s_notify_head + 1u) %
                              INKTAG_BLE_NOTIFY_QUEUE_DEPTH);
    ++s_notify_count;
    return true;
}

bool inktag_ble_notifications_drained(void) {
    return s_notify_count == 0u && s_notify_disconnect_pending == 0u;
}

void inktag_ble_notify_diagnostics(uint8_t *last_status,
                                   uint16_t *retry_count,
                                   uint16_t *drop_count,
                                   uint8_t *pending_count) {
    if (last_status) *last_status = s_notify_last_status;
    if (retry_count) *retry_count = s_notify_retry_count;
    if (drop_count) *drop_count = s_notify_drop_count;
    if (pending_count) *pending_count = s_notify_count;
}

bool inktag_ble_connected(void) { return s_connected != 0; }
#if INKTAG_BUILD_PRO
#include "inktag_ble_idle_advertising_pro.inc"
#endif
bool inktag_ble_requires_awake(void) {
    /* Disabling/reprogramming a set is not complete merely because the stack
     * accepted bls_ll_setAdvEnable(). Stay awake until a real ADV callback or
     * the interval-aware fallback promotes the accepted set. */
    return s_adv_restart_pending != 0u ||
           (!s_connected && !s_advertising_started &&
            s_advertising_ready_at != 0u);
}
bool inktag_ble_advertising_started(void) {
    if (!s_advertising_started && s_advertising_ready_at != 0u &&
        (int32_t)(clock_time() - s_advertising_ready_at) >= 0) {
        s_advertising_started = 1u;
        s_advertising_ready_at = 0u;
    }
    return s_advertising_started != 0;
}

bool inktag_ble_find_window_active(void) {
    return s_find_mode_active != 0u;
}
uint8_t *inktag_ble_mac(void) { return s_mac; }

int inktag_ble_epd_write(void *packet) {
    rf_packet_att_write_t *request = (rf_packet_att_write_t *)packet;
    uint16_t length = request->l2capLen >= 3u ? request->l2capLen - 3u : 0u;
    if (length) inktag_app_packet(&request->value, length);
    return 0;
}

int inktag_ble_ccc_write(void *packet) {
    rf_packet_att_write_t *request = (rf_packet_att_write_t *)packet;
    s_notify_enabled = request->l2capLen >= 5u && request->value == 1u;
    if (s_notify_enabled) {
        s_data_length_request_pending = 1u;
        s_data_length_request_at = clock_time() +
            INKTAG_BLE_DATA_LENGTH_SETTLE_MS * CLOCK_16M_SYS_TIMER_CLK_1MS;
    } else {
        s_data_length_request_pending = 0u;
        s_data_length_request_at = 0u;
        notify_queue_clear();
    }
    inktag_app_set_notify(s_notify_enabled != 0);
    return 0;
}

#if !INKTAG_BUILD_PRO
const uint8_t *inktag_ble_basic_name(uint8_t *length) {
    *length = s_default_name_length;
    return s_default_name;
}
#endif
