/**
 * Copyright (c) 2016 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/** @file
 *
 * @defgroup bootloader_secure_ble main.c
 * @{
 * @ingroup dfu_bootloader_api
 * @brief Bootloader project main file for secure DFU.
 *
 */

#include <stddef.h>
#include <stdint.h>
#include "inktag_board.h"
#include "crc32.h"
#include "nrf.h"
#include "nrf_mbr.h"
#include "nrf_bootloader.h"
#include "nrf_bootloader_app_start.h"
#include "nrf_bootloader_dfu_timers.h"
#include "nrf_dfu.h"
#include "nrf_dfu_settings.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "app_error.h"
#include "app_error_weak.h"
#include "nrf_bootloader_info.h"
#include "nrf_delay.h"
#include "nrf_dfu_transport.h"
#include "inktag_status.h"

#ifndef INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED
#define INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED 0
#endif

#if INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED
/* S112 reserves TIMER0 and PPI 17..31. This exact-board adapter therefore uses
 * TIMER1 for the phase clock, TIMER2 to count the six role-ready edges, GPIOTE
 * task 0 and application PPI channels 8..9. Hardware toggling and stopping keep
 * the BLE/DFU loop non-blocking. Future boards stay disabled until their pin,
 * polarity and these peripheral resources are qualified together. */
#define INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL 0u
#define INKTAG_NRF_DFU_STATUS_PPI_CHANNEL 8u
#define INKTAG_NRF_DFU_STATUS_STOP_PPI_CHANNEL 9u
#define INKTAG_NRF_DFU_STATUS_PPI_MASK \
    ((1uL << INKTAG_NRF_DFU_STATUS_PPI_CHANNEL) | \
     (1uL << INKTAG_NRF_DFU_STATUS_STOP_PPI_CHANNEL))
#define INKTAG_NRF_DFU_STATUS_DOWNLOAD_HALF_MS 240u
#define INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS 120u
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
#define DFU_LED_MASK ((1uL << INKTAG_BOARD_LED_RED_PIN) | \
                     (1uL << INKTAG_BOARD_LED_GREEN_PIN) | \
                     (1uL << INKTAG_BOARD_LED_BLUE_PIN))
#define DFU_GREEN_PIN INKTAG_BOARD_LED_GREEN_PIN
#define DFU_RED_PIN INKTAG_BOARD_LED_RED_PIN
#else
#define DFU_LED_MASK (1uL << INKTAG_BOARD_STATUS_LED_PIN)
#define DFU_GREEN_PIN INKTAG_BOARD_STATUS_LED_PIN
#define DFU_RED_PIN INKTAG_BOARD_STATUS_LED_PIN
#endif

static void dfu_status_stop(void)
{
    NRF_TIMER1->TASKS_STOP = 1u;
    NRF_TIMER2->TASKS_STOP = 1u;
    NRF_PPI->CHENCLR = INKTAG_NRF_DFU_STATUS_PPI_MASK;
    NRF_GPIOTE->CONFIG[INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL] = 0u;
    NRF_P0->DIRSET = DFU_LED_MASK;
#if INKTAG_BOARD_STATUS_LED_ACTIVE_LOW
    NRF_P0->OUTSET = DFU_LED_MASK;
#else
    NRF_P0->OUTCLR = DFU_LED_MASK;
#endif
}

static void dfu_status_set(uint16_t half_period_ms, uint8_t pin)
{
    uint32_t outinit;
    dfu_status_stop();
#if INKTAG_BOARD_STATUS_LED_ACTIVE_LOW
    outinit = GPIOTE_CONFIG_OUTINIT_Low;
#else
    outinit = GPIOTE_CONFIG_OUTINIT_High;
#endif
    NRF_GPIOTE->CONFIG[INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL] =
        (GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos) |
        (pin << GPIOTE_CONFIG_PSEL_Pos) |
        (GPIOTE_CONFIG_POLARITY_Toggle << GPIOTE_CONFIG_POLARITY_Pos) |
        (outinit << GPIOTE_CONFIG_OUTINIT_Pos);
    NRF_TIMER1->MODE = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
    NRF_TIMER1->BITMODE = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;
    NRF_TIMER1->PRESCALER = 4u; /* 1 MHz: one timer tick per microsecond. */
    NRF_TIMER1->CC[0] = half_period_ms * 1000u;
    NRF_TIMER1->SHORTS = TIMER_SHORTS_COMPARE0_CLEAR_Msk;
    NRF_TIMER1->TASKS_CLEAR = 1u;
    NRF_TIMER1->EVENTS_COMPARE[0] = 0u;
    NRF_PPI->CH[INKTAG_NRF_DFU_STATUS_PPI_CHANNEL].EEP =
        (uint32_t)(uintptr_t)&NRF_TIMER1->EVENTS_COMPARE[0];
    NRF_PPI->CH[INKTAG_NRF_DFU_STATUS_PPI_CHANNEL].TEP =
        (uint32_t)(uintptr_t)&NRF_GPIOTE->TASKS_OUT[
            INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL];
    NRF_PPI->FORK[INKTAG_NRF_DFU_STATUS_PPI_CHANNEL].TEP = 0u;
    NRF_PPI->CHENSET = 1uL << INKTAG_NRF_DFU_STATUS_PPI_CHANNEL;
    NRF_TIMER1->TASKS_START = 1u;
}

static void dfu_status_pulse(uint8_t pin, uint8_t edges)
{
    dfu_status_set(INKTAG_STATUS_READY_HALF_MS, pin);
    NRF_TIMER2->MODE = TIMER_MODE_MODE_Counter << TIMER_MODE_MODE_Pos;
    NRF_TIMER2->CC[0] = edges;
    NRF_TIMER2->SHORTS = TIMER_SHORTS_COMPARE0_STOP_Msk;
    NRF_TIMER2->TASKS_CLEAR = 1u;
    NRF_TIMER2->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER2->TASKS_START = 1u;
    NRF_PPI->FORK[INKTAG_NRF_DFU_STATUS_PPI_CHANNEL].TEP =
        (uint32_t)(uintptr_t)&NRF_TIMER2->TASKS_COUNT;
    NRF_PPI->CH[INKTAG_NRF_DFU_STATUS_STOP_PPI_CHANNEL].EEP =
        (uint32_t)(uintptr_t)&NRF_TIMER2->EVENTS_COMPARE[0];
    NRF_PPI->CH[INKTAG_NRF_DFU_STATUS_STOP_PPI_CHANNEL].TEP =
        (uint32_t)(uintptr_t)&NRF_TIMER1->TASKS_STOP;
    NRF_PPI->CHENSET = 1uL << INKTAG_NRF_DFU_STATUS_STOP_PPI_CHANNEL;
}

static void dfu_status_ready(void)
{
    dfu_status_pulse(INKTAG_BOARD_STATUS_LED_PIN, 6u);
#if INKTAG_BOARD_STATUS_LED_ACTIVE_LOW
    NRF_GPIOTE->TASKS_SET[INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL] = 1u;
#else
    NRF_GPIOTE->TASKS_CLR[INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL] = 1u;
#endif
}

static void dfu_status_startup(void)
{
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
#if INKTAG_BOARD_LED_GREEN_PIN == INKTAG_BOARD_LED_RED_PIN + 1 && \
    INKTAG_BOARD_LED_BLUE_PIN == INKTAG_BOARD_LED_RED_PIN + 2
    /* This board has three adjacent pins; avoid a redundant pin array. */
#define STARTUP_PIN(i) (INKTAG_BOARD_LED_RED_PIN + (i))
#else
    const uint8_t pins[] = {INKTAG_BOARD_LED_RED_PIN,
        INKTAG_BOARD_LED_GREEN_PIN, INKTAG_BOARD_LED_BLUE_PIN};
#define STARTUP_PIN(i) pins[i]
#endif
    dfu_status_stop();
    /* Before transport initialization: no BLE request or Flash operation is
     * blocked. Normal App boots never initialize a DFU transport. */
    for (unsigned i = 0u; i < 3u; i++) {
#if INKTAG_BOARD_STATUS_LED_ACTIVE_LOW
        NRF_P0->OUTCLR = 1uL << STARTUP_PIN(i);
        nrf_delay_ms(INKTAG_STATUS_STARTUP_COLOR_MS);
        NRF_P0->OUTSET = 1uL << STARTUP_PIN(i);
#else
        NRF_P0->OUTSET = 1uL << STARTUP_PIN(i);
        nrf_delay_ms(INKTAG_STATUS_STARTUP_COLOR_MS);
        NRF_P0->OUTCLR = 1uL << STARTUP_PIN(i);
#endif
    }
#undef STARTUP_PIN
#endif
}

static bool validated_install_pending(void)
{
    nrf_dfu_settings_t const * settings =
        (nrf_dfu_settings_t const *)(uintptr_t)BOOTLOADER_SETTINGS_ADDRESS;
    return settings->crc != UINT32_MAX &&
           settings->settings_version == NRF_DFU_SETTINGS_VERSION &&
           settings->bank_current == NRF_DFU_CURRENT_BANK_1 &&
           settings->bank_1.bank_code == NRF_DFU_BANK_VALID_APP &&
           settings->bank_1.image_size != 0u &&
           settings->crc == crc32_compute(
               (uint8_t const *)settings + sizeof(settings->crc),
               offsetof(nrf_dfu_settings_t, init_command) - sizeof(settings->crc),
               NULL);
}
#else
#define dfu_status_stop() ((void)0)
#define dfu_status_set(_half_period_ms, _pin) ((void)0)
#define dfu_status_ready() ((void)0)
#define dfu_status_pulse(_pin, _edges) ((void)0)
#define dfu_status_startup() ((void)0)
#define validated_install_pending() false
#define INKTAG_NRF_DFU_STATUS_DOWNLOAD_HALF_MS 0
#define INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS 0
#endif

ret_code_t inktag_nrf_dfu_transports_init_original(nrf_dfu_observer_t observer);
ret_code_t nrf_dfu_transports_init(nrf_dfu_observer_t observer)
{
    dfu_status_startup();
    ret_code_t result = inktag_nrf_dfu_transports_init_original(observer);
    if (result == NRF_SUCCESS) dfu_status_ready();
    return result;
}
static void on_error(void)
{
    NRF_LOG_FINAL_FLUSH();

#if NRF_MODULE_ENABLED(NRF_LOG_BACKEND_RTT)
    // To allow the buffer to be flushed by the host.
    nrf_delay_ms(100);
#endif
#ifdef NRF_DFU_DEBUG_VERSION
    NRF_BREAKPOINT_COND;
#endif
    NVIC_SystemReset();
}


void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name)
{
    NRF_LOG_ERROR("%s:%d", p_file_name, line_num);
    on_error();
}


void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
    NRF_LOG_ERROR("Received a fault! id: 0x%08x, pc: 0x%08x, info: 0x%08x", id, pc, info);
    on_error();
}


void app_error_handler_bare(uint32_t error_code)
{
    NRF_LOG_ERROR("Received an error: 0x%08x!", error_code);
    on_error();
}

/**
 * @brief Function notifies certain events in DFU process.
 */
static void dfu_observer(nrf_dfu_evt_type_t evt_type)
{
    switch (evt_type)
    {
        case NRF_DFU_EVT_DFU_STARTED:
            dfu_status_set(INKTAG_NRF_DFU_STATUS_DOWNLOAD_HALF_MS, INKTAG_BOARD_STATUS_LED_PIN);
            break;
        case NRF_DFU_EVT_TRANSPORT_ACTIVATED:
#if INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED
            if (s_dfu_settings.bank_1.bank_code == NRF_DFU_BANK_VALID_APP)
            {
                dfu_status_set(INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS, INKTAG_BOARD_STATUS_LED_PIN);
            }
            else if (s_dfu_settings.progress.command_size != 0u ||
                     s_dfu_settings.progress.firmware_image_offset != 0u)
            {
                /* Cached init can resume before the first data object, without
                 * another DFU_STARTED. Select download here, then let object
                 * completions leave the hardware timer's phase untouched. */
                dfu_status_set(INKTAG_NRF_DFU_STATUS_DOWNLOAD_HALF_MS, INKTAG_BOARD_STATUS_LED_PIN);
            }
            else
            {
                dfu_status_pulse(DFU_GREEN_PIN, 3u);
            }
#endif
            break;
        case NRF_DFU_EVT_DFU_COMPLETED:
            /* Integrity/signature validation and the authoritative DFU
             * Settings commit have completed. Activation on the next
             * boot remains installation, never application success. */
            dfu_status_set(INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS, INKTAG_BOARD_STATUS_LED_PIN);
            break;
        case NRF_DFU_EVT_TRANSPORT_DEACTIVATED:
            dfu_status_stop();
            break;
        case NRF_DFU_EVT_DFU_FAILED:
        case NRF_DFU_EVT_DFU_ABORTED:
            dfu_status_pulse(DFU_RED_PIN, 5u);
            break;
        default:
            break;
    }
}


/**@brief Function for application main entry. */
int main(void)
{
    uint32_t ret_val;

    dfu_status_stop();
    if (validated_install_pending())
    {
        dfu_status_set(INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS, INKTAG_BOARD_STATUS_LED_PIN);
    }

    /* The guarded first-flash flow owns and verifies the MBR/Bootloader UICR
     * pointers. Every normally running InkTag role must leave UICR untouched
     * so SWD/reset policy cannot change as a side effect of boot. */
    NRF_LOG_DEBUG("InkTag UICR pointers are first-flash-owned");

    // Protect MBR and bootloader code from being overwritten.
    ret_val = nrf_bootloader_flash_protect(0, MBR_SIZE);
    APP_ERROR_CHECK(ret_val);
    ret_val = nrf_bootloader_flash_protect(BOOTLOADER_START_ADDR, BOOTLOADER_SIZE);
    APP_ERROR_CHECK(ret_val);

    (void) NRF_LOG_INIT(nrf_bootloader_dfu_timer_counter_get);
    NRF_LOG_DEFAULT_BACKENDS_INIT();

    NRF_LOG_INFO("Inside main");

    ret_val = nrf_bootloader_init(dfu_observer);
    APP_ERROR_CHECK(ret_val);

    NRF_LOG_FLUSH();

    NRF_LOG_ERROR("After main, should never be reached.");
    NRF_LOG_FLUSH();

    APP_ERROR_CHECK_BOOL(false);
}

/**
 * @}
 */
