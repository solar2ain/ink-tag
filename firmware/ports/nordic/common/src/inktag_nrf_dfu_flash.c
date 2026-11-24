/* Project-owned Nordic Secure DFU Flash adapter.
 *
 * Recovery accepts application images only, so it needs one internal-page
 * erase operation and aligned application/settings writes. A small queue
 * around the SoftDevice flash API preserves Nordic DFU's asynchronous buffer
 * lifetime without linking the generic multi-instance fstorage dispatcher.
 * A signed init packet receives the product OTA admission check before the
 * SDK accepts its destination. Every later physical submission is preceded
 * by the lower product Flash-write check. */

#include "nrf_dfu_flash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inktag_nrf_dfu_power.h"
#include "inktag_power.h"
#include "nrf.h"
#include "nrf_dfu_types.h"
#include "nrf_error.h"
#include "nrf_fstorage.h"
#include "nrf_nvmc.h"
#include "nrf_sdh_soc.h"
#include "nrf_soc.h"
#if INKTAG_NRF_SAFE_RESUME
#include "inktag_nrf_dfu_resume.h"
#include "nrf_bootloader_info.h"
#include "nrf_dfu_utils.h"
#endif

#define SAADC_EVENT_WAIT_LIMIT 1000000u
#define FLASH_PAGE_SIZE 4096u
#define FLASH_WRITE_CHUNK_BYTES 20u
#define FLASH_QUEUE_SIZE 16u
#define FLASH_RETRY_LIMIT 8u
#define FLASH_END_ADDRESS \
    (BOOTLOADER_SETTINGS_ADDRESS + BOOTLOADER_SETTINGS_PAGE_SIZE)

typedef enum {
    FLASH_OP_WRITE,
    FLASH_OP_ERASE,
} flash_op_kind_t;

typedef struct {
    flash_op_kind_t kind;
    uint32_t address;
    uint8_t const *source;
    uint32_t length;
    uint32_t offset;
    nrf_dfu_flash_callback_t callback;
} flash_op_t;

static flash_op_t s_queue[FLASH_QUEUE_SIZE];
static uint8_t s_queue_head;
static uint8_t s_queue_count;
static uint8_t s_retry_count;
static bool s_softdevice_enabled;
static bool s_operation_accepted;

static bool wait_for_event(volatile uint32_t *event)
{
    uint32_t remaining = SAADC_EVENT_WAIT_LIMIT;
    while (*event == 0u && remaining-- != 0u) { }
    return *event != 0u;
}

static uint16_t sample_vdd_mv(void)
{
    volatile int16_t sample = 0;
    bool ok;

    NRF_SAADC->EVENTS_STARTED = 0u;
    NRF_SAADC->EVENTS_END = 0u;
    NRF_SAADC->EVENTS_STOPPED = 0u;
    NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_10bit;
    NRF_SAADC->ENABLE =
        (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);
    NRF_SAADC->CH[0].CONFIG =
        ((SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos) &
         SAADC_CH_CONFIG_RESP_Msk) |
        ((SAADC_CH_CONFIG_RESN_Bypass << SAADC_CH_CONFIG_RESN_Pos) &
         SAADC_CH_CONFIG_RESN_Msk) |
        ((SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) &
         SAADC_CH_CONFIG_GAIN_Msk) |
        ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) &
         SAADC_CH_CONFIG_REFSEL_Msk) |
        ((SAADC_CH_CONFIG_TACQ_3us << SAADC_CH_CONFIG_TACQ_Pos) &
         SAADC_CH_CONFIG_TACQ_Msk) |
        ((SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) &
         SAADC_CH_CONFIG_MODE_Msk);
    NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
    NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDD;
    NRF_SAADC->RESULT.PTR = (uint32_t)&sample;
    NRF_SAADC->RESULT.MAXCNT = 1u;

    NRF_SAADC->TASKS_START = 1u;
    ok = wait_for_event(&NRF_SAADC->EVENTS_STARTED);
    if (ok) {
        NRF_SAADC->EVENTS_STARTED = 0u;
        NRF_SAADC->TASKS_SAMPLE = 1u;
        ok = wait_for_event(&NRF_SAADC->EVENTS_END);
    }
    NRF_SAADC->TASKS_STOP = 1u;
    ok = wait_for_event(&NRF_SAADC->EVENTS_STOPPED) && ok;
    NRF_SAADC->ENABLE =
        (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);

    if (!ok || sample <= 0) return 0u;
    return (uint16_t)(((uint32_t)(uint16_t)sample * 3600u) >> 10);
}

static bool power_operation_allowed(inktag_power_operation_t operation)
{
    inktag_power_thresholds_t const thresholds =
        INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER;
    uint16_t const voltage_mv = sample_vdd_mv();

    return inktag_power_operation_allowed(
        operation, voltage_mv,
        voltage_mv >= 1500u && voltage_mv <= 6000u, &thresholds);
}

bool inktag_nrf_dfu_ota_admission_allowed(void)
{
    return power_operation_allowed(INKTAG_POWER_OPERATION_OTA);
}

bool inktag_nrf_dfu_power_allows_write(void)
{
    return power_operation_allowed(INKTAG_POWER_OPERATION_FLASH);
}

static flash_op_t *current_op(void)
{
    return (s_queue_count == 0u) ? NULL : &s_queue[s_queue_head];
}

static bool range_valid(uint32_t address, uint32_t length)
{
    return length != 0u && address >= MBR_SIZE &&
           address < FLASH_END_ADDRESS &&
           length <= FLASH_END_ADDRESS - address;
}

static void submit_current(void);

static bool application_mutation_allowed(uint32_t address)
{
#if INKTAG_NRF_SAFE_RESUME
    if (address >= nrf_dfu_bank0_start_addr() &&
        address < DFU_REGION_END(BOOTLOADER_START_ADDR)) {
        inktag_nrf_dfu_resume_touch();
        return inktag_nrf_dfu_resume_app_write_allowed();
    }
#else
    (void)address;
#endif
    return true;
}

static void complete_current(void)
{
    flash_op_t *op = current_op();
    nrf_dfu_flash_callback_t const callback = op->callback;
    void * const source = (void *)op->source;

    s_queue_head = (uint8_t)((s_queue_head + 1u) % FLASH_QUEUE_SIZE);
    s_queue_count--;
    s_retry_count = 0u;
    s_operation_accepted = false;
    if (callback != NULL) callback(source);
}

static void submit_current(void)
{
    while (!s_operation_accepted) {
        flash_op_t *op = current_op();
        ret_code_t result;

        if (op == NULL) return;
        if (!inktag_nrf_dfu_power_allows_write() ||
            !application_mutation_allowed(op->address)) {
            complete_current();
            continue;
        }
        if (op->kind == FLASH_OP_ERASE) {
            result = sd_flash_page_erase(op->address / FLASH_PAGE_SIZE);
        } else {
            uint32_t bytes = op->length - op->offset;
            if (bytes > FLASH_WRITE_CHUNK_BYTES) bytes = FLASH_WRITE_CHUNK_BYTES;
            result = sd_flash_write((uint32_t *)(op->address + op->offset),
                                    (uint32_t const *)(op->source + op->offset),
                                    bytes / sizeof(uint32_t));
        }
        if (result == NRF_SUCCESS) {
            s_operation_accepted = true;
        } else if (result == NRF_ERROR_BUSY) {
            return;
        } else {
            complete_current();
        }
    }
}

static void flash_soc_event(uint32_t event, void *context)
{
    flash_op_t *op;
    (void)context;

    if (event != NRF_EVT_FLASH_OPERATION_SUCCESS &&
        event != NRF_EVT_FLASH_OPERATION_ERROR) {
        return;
    }
    if (!s_operation_accepted) {
        submit_current();
        return;
    }

    op = current_op();
    s_operation_accepted = false;
    if (event == NRF_EVT_FLASH_OPERATION_ERROR) {
        if (++s_retry_count <= FLASH_RETRY_LIMIT) {
            submit_current();
        } else {
            complete_current();
            submit_current();
        }
        return;
    }

    s_retry_count = 0u;
    if (op->kind == FLASH_OP_WRITE) {
        uint32_t bytes = op->length - op->offset;
        if (bytes > FLASH_WRITE_CHUNK_BYTES) bytes = FLASH_WRITE_CHUNK_BYTES;
        op->offset += bytes;
        if (op->offset != op->length) {
            submit_current();
            return;
        }
    }
    complete_current();
    submit_current();
}

NRF_SDH_SOC_OBSERVER(s_flash_observer, 0, flash_soc_event, NULL);

ret_code_t nrf_dfu_flash_init(bool sd_irq_initialized)
{
    s_softdevice_enabled = sd_irq_initialized;
    return NRF_SUCCESS;
}

static ret_code_t queue_operation(flash_op_t const *operation)
{
    uint8_t tail;
    if (s_queue_count == FLASH_QUEUE_SIZE) return NRF_ERROR_NO_MEM;
    tail = (uint8_t)((s_queue_head + s_queue_count) % FLASH_QUEUE_SIZE);
    s_queue[tail] = *operation;
    s_queue_count++;
    submit_current();
    return NRF_SUCCESS;
}

ret_code_t nrf_dfu_flash_store(uint32_t dest, void const *source,
                               uint32_t length,
                               nrf_dfu_flash_callback_t callback)
{
    flash_op_t const operation = {
        .kind = FLASH_OP_WRITE,
        .address = dest,
        .source = source,
        .length = length,
        .offset = 0u,
        .callback = callback,
    };

    if (source == NULL) return NRF_ERROR_NULL;
    if (length == 0u || (length & 3u) != 0u) return NRF_ERROR_INVALID_LENGTH;
    if ((dest & 3u) != 0u || (((uintptr_t)source) & 3u) != 0u)
        return NRF_ERROR_INVALID_ADDR;
    if (!range_valid(dest, length)) return NRF_ERROR_INVALID_ADDR;
    if (!inktag_nrf_dfu_power_allows_write()) return NRF_ERROR_FORBIDDEN;
    if (!s_softdevice_enabled) {
        if (!application_mutation_allowed(dest)) return NRF_ERROR_FORBIDDEN;
        nrf_nvmc_write_words(dest, source, length / sizeof(uint32_t));
        if (callback != NULL) callback((void *)source);
        return NRF_SUCCESS;
    }
    return queue_operation(&operation);
}

ret_code_t nrf_dfu_flash_erase(uint32_t page_addr, uint32_t num_pages,
                               nrf_dfu_flash_callback_t callback)
{
    flash_op_t const operation = {
        .kind = FLASH_OP_ERASE,
        .address = page_addr,
        .source = NULL,
        .length = FLASH_PAGE_SIZE,
        .offset = 0u,
        .callback = callback,
    };

    if (num_pages != 1u) return NRF_ERROR_INVALID_LENGTH;
    if ((page_addr & (FLASH_PAGE_SIZE - 1u)) != 0u)
        return NRF_ERROR_INVALID_ADDR;
    if (!range_valid(page_addr, FLASH_PAGE_SIZE)) return NRF_ERROR_INVALID_ADDR;
    if (!inktag_nrf_dfu_power_allows_write()) return NRF_ERROR_FORBIDDEN;
    if (!s_softdevice_enabled) {
        if (!application_mutation_allowed(page_addr)) return NRF_ERROR_FORBIDDEN;
        nrf_nvmc_page_erase(page_addr);
        if (callback != NULL) callback(NULL);
        return NRF_SUCCESS;
    }
    return queue_operation(&operation);
}

bool nrf_fstorage_is_busy(nrf_fstorage_t const *instance)
{
    (void)instance;
    return s_queue_count != 0u;
}
