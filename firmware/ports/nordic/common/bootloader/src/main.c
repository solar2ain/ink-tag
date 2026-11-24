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
#include "inktag_recovery_version.h"
#include "inktag_nrf_dfu_resume.h"
#include "inktag_code_guard.h"

/* Retained private delivery identity; no new DFU protocol response. */
__attribute__((used, section(".inktag_recovery_version")))
const uint8_t g_inktag_nrf_recovery_version[8] = INKTAG_RECOVERY_INFO_BYTES;
/* Same byte returned by the actual native protocol-version request. */
#if INKTAG_BUILD_PRO
INKTAG_GUARD_DATA
#else
__attribute__((used))
#endif
const uint8_t g_inktag_nrf_recovery_protocol = INKTAG_NRF_DFU_RESUME_PROTOCOL;

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

static void dfu_status_stop(void)
{
    NRF_TIMER1->TASKS_STOP = 1u;
    NRF_TIMER2->TASKS_STOP = 1u;
    NRF_PPI->CHENCLR = INKTAG_NRF_DFU_STATUS_PPI_MASK;
    NRF_GPIOTE->CONFIG[INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL] = 0u;
    NRF_P0->DIRSET = 1uL << INKTAG_BOARD_STATUS_LED_PIN;
#if INKTAG_BOARD_STATUS_LED_ACTIVE_LOW
    NRF_P0->OUTSET = 1uL << INKTAG_BOARD_STATUS_LED_PIN;
#else
    NRF_P0->OUTCLR = 1uL << INKTAG_BOARD_STATUS_LED_PIN;
#endif
}

static void dfu_status_set(uint16_t half_period_ms)
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
        (INKTAG_BOARD_STATUS_LED_PIN << GPIOTE_CONFIG_PSEL_Pos) |
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

static void dfu_status_ready(void)
{
    dfu_status_set(INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS);
#if INKTAG_BOARD_STATUS_LED_ACTIVE_LOW
    NRF_GPIOTE->TASKS_SET[INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL] = 1u;
#else
    NRF_GPIOTE->TASKS_CLR[INKTAG_NRF_DFU_STATUS_GPIOTE_CHANNEL] = 1u;
#endif
    NRF_TIMER2->MODE = TIMER_MODE_MODE_Counter << TIMER_MODE_MODE_Pos;
    NRF_TIMER2->CC[0] = 6u;
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
#define dfu_status_set(_half_period_ms) ((void)0)
#define dfu_status_ready() ((void)0)
#define validated_install_pending() false
#define INKTAG_NRF_DFU_STATUS_DOWNLOAD_HALF_MS 0
#define INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS 0
#endif
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
            dfu_status_set(INKTAG_NRF_DFU_STATUS_DOWNLOAD_HALF_MS);
            break;
        case NRF_DFU_EVT_TRANSPORT_ACTIVATED:
        case NRF_DFU_EVT_OBJECT_RECEIVED:
#if INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED
            if (s_dfu_settings.bank_1.bank_code == NRF_DFU_BANK_VALID_APP)
            {
                dfu_status_set(INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS);
            }
            else if (s_dfu_settings.progress.firmware_image_offset != 0u)
            {
                dfu_status_set(INKTAG_NRF_DFU_STATUS_DOWNLOAD_HALF_MS);
            }
            else
            {
                /* Advertising is ready: one off phase followed by three
                 * non-blocking blue-mapped pulses, then hardware stops off. */
                dfu_status_ready();
            }
#endif
            break;
        case NRF_DFU_EVT_DFU_COMPLETED:
            /* Integrity/target validation and the authoritative DFU
             * Settings commit have completed. Activation on the next
             * boot remains installation, never application success. */
            dfu_status_set(INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS);
            break;
        case NRF_DFU_EVT_TRANSPORT_DEACTIVATED:
        case NRF_DFU_EVT_DFU_FAILED:
        case NRF_DFU_EVT_DFU_ABORTED:
            dfu_status_stop();
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
        dfu_status_set(INKTAG_NRF_DFU_STATUS_INSTALL_HALF_MS);
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
