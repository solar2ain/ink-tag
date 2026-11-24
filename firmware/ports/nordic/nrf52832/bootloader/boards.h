#ifndef INKTAG_NRF52832_PRICEETAG_BOOTLOADER_BOARDS_H
#define INKTAG_NRF52832_PRICEETAG_BOOTLOADER_BOARDS_H

/* The recovered stock BUTTON module establishes an active-low switch on
 * P0.02. Nordic's Secure DFU Bootloader samples it with sense-low, so keep the
 * line defined while the switch is open. */
#define BUTTON_PULL NRF_GPIO_PIN_PULLUP

#endif
