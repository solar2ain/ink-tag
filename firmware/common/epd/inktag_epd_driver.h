/* Public EPD IO/controller ABI. Compatibility facts and provenance boundary:
 * docs/research/2026-11-04-epd-io-rewrite-spec.md. Existing InkTag-only inline
 * controller dispatch remains unchanged; no whole-file license replacement. */
#ifndef __EPD_DRIVER_H__
#define __EPD_DRIVER_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "inktag_epd_config.h"
#include "inktag_epd_screen.h"
#if defined(INKTAG_GOODIX)
#include "app_log.h"
#define EPD_DEBUG(fmt, ...) APP_LOG_DEBUG("EPD: " fmt, ##__VA_ARGS__)
#elif defined(INKTAG_ONMICRO)
#include "om_log.h"
#define EPD_DEBUG(fmt, ...) OM_LOG_DEBUG("EPD: " fmt "\r\n", ##__VA_ARGS__)
#elif defined(INKTAG_DIALOG)
#define EPD_DEBUG(fmt, ...) do { } while (0)
#elif defined(INKTAG_TELINK)
#define EPD_DEBUG(fmt, ...) do { } while (0)
#else
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#define EPD_DEBUG(fmt, ...) NRF_LOG_DEBUG("EPD: " fmt "\r\n", ##__VA_ARGS__)
#endif

/* Register numbers and public color/controller identifiers are wire facts.
 * Names and allocations remain fixed; controller flows live in their modules. */
enum {
    UC8xxx_PSR = 0x00,
    UC8xxx_PWR = 0x01,
    UC8xxx_POF = 0x02,
    UC8xxx_PFS = 0x03,
    UC8xxx_PON = 0x04,
    UC8xxx_PMES = 0x05,
    UC8xxx_BTST = 0x06,
    UC8xxx_DSLP = 0x07,
    UC8xxx_DTM1 = 0x10,
    UC8xxx_DSP = 0x11,
    UC8xxx_DRF = 0x12,
    UC8xxx_DTM2 = 0x13,
    UC8xxx_LUTC = 0x20,
    UC8xxx_LUTWW = 0x21,
    UC8xxx_LUTBW = 0x22,
    UC8xxx_LUTWB = 0x23,
    UC8xxx_LUTBB = 0x24,
    UC8xxx_PLL = 0x30,
    UC8xxx_TSC = 0x40,
    UC8xxx_TSE = 0x41,
    UC8xxx_TSW = 0x42,
    UC8xxx_TSR = 0x43,
    UC8xxx_CDI = 0x50,
    UC8xxx_LPD = 0x51,
    UC8xxx_TCON = 0x60,
    UC8xxx_TRES = 0x61,
    UC8xxx_GSST = 0x65,
    UC8xxx_REV = 0x70,
    UC8xxx_FLG = 0x71,
    UC8xxx_AMV = 0x80,
    UC8xxx_VV = 0x81,
    UC8xxx_VDCS = 0x82,
    UC8xxx_PTL = 0x90,
    UC8xxx_PTIN = 0x91,
    UC8xxx_PTOUT = 0x92,
    UC8xxx_PGM = 0xA0,
    UC8xxx_APG = 0xA1,
    UC8xxx_ROTP = 0xA2,
    UC8xxx_CCSET = 0xE0,
    UC8xxx_PWS = 0xE3,
    UC8xxx_TSSET = 0xE5,
};

enum {
    SSD1xxx_GDO_CTR = 0x01,
    SSD1xxx_GDV_CTRL = 0x03,
    SSD1xxx_SDV_CTRL = 0x04,
    SSD1xxx_SOFTSTART = 0x0C,
    SSD1xxx_GSCAN_START = 0x0F,
    SSD1xxx_SLEEP_MODE = 0x10,
    SSD1xxx_ENTRY_MODE = 0x11,
    SSD1xxx_SW_RESET = 0x12,
    SSD1xxx_HV_RD_DETECT = 0x14,
    SSD1xxx_VCI_DETECT = 0x15,
    SSD1xxx_TSENSOR_CTRL = 0x18,
    SSD1xxx_TSENSOR_WRITE = 0x1A,
    SSD1xxx_TSENSOR_READ = 0x1B,
    SSD1xxx_TSENSOR_WRITE_EXT = 0x1C,
    SSD1xxx_MASTER_ACTIVATE = 0x20,
    SSD1xxx_DISP_CTRL1 = 0x21,
    SSD1xxx_DISP_CTRL2 = 0x22,
    SSD1xxx_WRITE_RAM1 = 0x24,
    SSD1xxx_WRITE_RAM2 = 0x26,
    SSD1xxx_READ_RAM = 0x27,
    SSD1xxx_VCOM_SENSE = 0x28,
    SSD1xxx_VCOM_SENSE_DURATON = 0x29,
    SSD1xxx_PRGM_VCOM_OTP = 0x2A,
    SSD1xxx_VCOM_CTRL = 0x2B,
    SSD1xxx_VCOM_VOLTAGE = 0x2C,
    SSD1xxx_READ_OTP_REG = 0x2D,
    SSD1xxx_READ_USER_ID = 0x2E,
    SSD1xxx_READ_STATUS = 0x2F,
    SSD1xxx_PRGM_WS_OTP = 0x30,
    SSD1xxx_LOAD_WS_OTP = 0x31,
    SSD1xxx_WRITE_LUT = 0x32,
    SSD1xxx_READ_LUT = 0x33,
    SSD1xxx_CRC_CALC = 0x34,
    SSD1xxx_CRC_STATUS = 0x35,
    SSD1xxx_PRGM_OTP_SELECTION = 0x36,
    SSD1xxx_OTP_SELECTION_CTRL = 0x37,
    SSD1xxx_USER_ID_CTRL = 0x38,
    SSD1xxx_OTP_PROG_MODE = 0x39,
    SSD1xxx_DUMMY_LINE = 0x3A,
    SSD1xxx_GATE_LINE_WIDTH = 0x3B,
    SSD1xxx_BORDER_CTRL = 0x3C,
    SSD1xxx_RAM_READ_CTRL = 0x41,
    SSD1xxx_RAM_XPOS = 0x44,
    SSD1xxx_RAM_YPOS = 0x45,
    SSD1xxx_AUTO_WRITE_RED_RAM = 0x46,
    SSD1xxx_AUTO_WRITE_BW_RAM = 0x47,
    SSD1xxx_RAM_XCOUNT = 0x4E,
    SSD1xxx_RAM_YCOUNT = 0x4F,
    SSD1xxx_ANALOG_BLOCK_CTRL = 0x74,
    SSD1xxx_DIGITAL_BLOCK_CTRL = 0x7E,
    SSD1xxx_NOP = 0x7F,
};

typedef enum {
    COLOR_BW = 1,
    COLOR_BWR = 2,
    COLOR_BWRY = 3,
    COLOR_GRAY4 = 4,
    COLOR_BWRYGB = 10,
} epd_color_t;

typedef enum {
    DRV_IC_IST7513 = 0x40,
    DRV_IC_UC8159 = 0x10,
    DRV_IC_UC8176 = 0x11,
    DRV_IC_UC8179 = 0x12,
    DRV_IC_UC8151 = 0x13,
    DRV_IC_SSD1619 = 0x20,
    DRV_IC_SSD1677 = 0x21,
    DRV_IC_SSD1680 = 0x22,
    DRV_IC_SSD1683 = 0x23,
    DRV_IC_SSD1675 = 0x24,
    DRV_IC_SSD1XXX = 0x2F,
    DRV_IC_JD79653A = 0x34,
    DRV_IC_JD79668 = 0x30,
    DRV_IC_JD79665 = 0x31,

    DRV_IC_JD79XXX_ZKC = 0x32,
} epd_drv_ic_t;

/* Enable only in builds which actually select whether to acquire temperature. */
#ifndef INKTAG_EPD_TEMPERATURE_OPTION
#define INKTAG_EPD_TEMPERATURE_OPTION 0
#endif

/* Screen driver IDs are the public protocol values in inktag_epd_screen.h.
 * Board wiring remains in epd_config_t and never selects this path. */
struct epd_driver;

typedef struct {
    uint8_t driver_id;
    uint8_t panel_recipe;
    epd_color_t color;
    const struct epd_driver* drv;
    epd_drv_ic_t ic;
    uint8_t transfer_format;
    uint8_t transform;
    uint16_t width;
    uint16_t height;
    uint16_t visible_x;
    uint16_t visible_y;
    uint16_t visible_width;
    uint16_t visible_height;
#if INKTAG_EPD_TEMPERATURE_OPTION
    bool skip_temperature;
#endif
} epd_model_t;

/* Default is read. Set once before initialization or a sampling call. */
#if INKTAG_EPD_TEMPERATURE_OPTION
static inline void EPD_SetTemperatureRead(epd_model_t* epd, bool read_temperature) {
    epd->skip_temperature = !read_temperature;
}
static inline bool EPD_TemperatureReadEnabled(const epd_model_t* epd) {
    return !epd->skip_temperature;
}
#else
static inline bool EPD_TemperatureReadEnabled(const epd_model_t* epd) {
    (void)epd;
    return true;
}
#endif

/* These two-colour controllers require one uninterrupted RAM stream per
 * colour plane when rendering native pages. Reopening a page window between
 * GUI chunks can scramble the SSD1680's X+/Y- address progression. */
static inline bool EPD_GuiNeedsContiguousColorPlanes(const epd_model_t* epd) {
    return epd != NULL && epd->color == COLOR_BWR &&
           (epd->ic == DRV_IC_UC8151 || epd->ic == DRV_IC_SSD1680
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
            || epd->ic == DRV_IC_SSD1XXX
#endif
#if INKTAG_EPD_ENABLE_STELLAR154
            || epd->ic == DRV_IC_JD79653A
#endif
           );
}
/* The function-table ABI is shared by every platform. Canonical BW/BWR
 * planes use0 for ink and1 for white; controllers translate RAM polarity.
 * A missing temperature callback means unavailable on the parked panel. */
typedef struct epd_driver {
    void (*init)(epd_model_t* epd);
    void (*clear)(epd_model_t* epd, bool refresh);
    void (*write_image)(epd_model_t* epd, uint8_t* black, uint8_t* color,
                        uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void (*write_ram)(epd_model_t* epd, bool begin, bool black, uint8_t* data, uint8_t len);
    void (*refresh)(epd_model_t* epd);
    void (*sleep)(epd_model_t* epd);
    int8_t (*read_temp)(epd_model_t* epd);
    bool (*read_busy)(epd_model_t* epd);
} epd_driver_t;

extern const epd_driver_t epd_drv_jd79xxx;
#if INKTAG_EPD_ENABLE_OSPREY_E6
extern const epd_driver_t epd_drv_ist7513;
bool IST7513_OperationOK(void);
#endif
bool JD79xxx_OperationOK(epd_model_t* epd);
bool JD79xxx_PrepareRam(epd_model_t* epd, bool black);
#if INKTAG_EPD_ENABLE_STELLAR154
bool UC8xxx_StellarOperationOK(epd_model_t* epd);
#endif
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
bool SSD1xxx_OperationOK(epd_model_t* epd);
#endif

static inline bool EPD_DriverOperationOK(epd_model_t* epd) {
    if (epd == NULL || epd->drv == NULL) return false;
#if INKTAG_EPD_ENABLE_STELLAR154
    if (epd->ic == DRV_IC_JD79653A) return UC8xxx_StellarOperationOK(epd);
#endif
#if INKTAG_EPD_ENABLE_HANSHOW_OLD_200
    if (epd->ic == DRV_IC_SSD1XXX) return SSD1xxx_OperationOK(epd);
#endif
#if INKTAG_EPD_ENABLE_OSPREY_E6
    if (epd->drv == &epd_drv_ist7513) return IST7513_OperationOK();
#endif
#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q || INKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154
    /* JD multi-pass and Small-Q candidates have controller-level results
     * beyond BUSY. Keep this out of every driver's function table: the
     * constrained targets must not pay one pointer per controller family. */
    if (epd->drv == &epd_drv_jd79xxx) return JD79xxx_OperationOK(epd);
#endif
    return true;
}

/* Keep inter-plane controller work out of the byte writer without growing
 * every driver's function table. Most controllers need no explicit
 * transition; the JD multi-pass recipe may refresh the completed plane and
 * select the next waveform here before write_ram sees any bytes. */
static inline bool EPD_DriverPrepareRam(epd_model_t* epd, bool black) {
    if (epd == NULL || epd->drv == NULL) return false;
#if INKTAG_EPD_ENABLE_JD79665CA
    if (epd->drv == &epd_drv_jd79xxx)
        return JD79xxx_PrepareRam(epd, black);
#else
    (void)black;
#endif
    return true;
}

static inline bool EPD_DriverWriteRam(epd_model_t* epd, bool begin, bool black,
                                      uint8_t* data, uint8_t len) {
    if (epd == NULL || epd->drv == NULL || epd->drv->write_ram == NULL)
        return false;
    if (begin && !EPD_DriverPrepareRam(epd, black))
        return false;
    epd->drv->write_ram(epd, begin, black, data, len);
    return EPD_DriverOperationOK(epd);
}

/**
 * Long panel waits must remain interruptible.  The platform service installs
 * a tiny callback while an EPD operation is active; drivers poll it from
 * delay/BUSY loops and return to the service, which performs the actual panel
 * reset and operation hand-off.
 */
typedef bool (*epd_abort_callback_t)(void);
typedef void (*epd_maintenance_callback_t)(void);

#define LOW (0x0)
#define HIGH (0x1)

#define DEFAULT (0xFF)
#define INPUT (0x0)
#define OUTPUT (0x1)
#define INPUT_PULLUP (0x2)
#define INPUT_PULLDOWN (0x3)

// Arduino-like function wrappers
void pinMode(uint32_t pin, uint32_t mode);
#if defined(INKTAG_GOODIX) || defined(INKTAG_ONMICRO) || defined(INKTAG_DIALOG) || defined(INKTAG_TELINK)
void digitalWrite(uint32_t pin, uint32_t value);
uint32_t digitalRead(uint32_t pin);
void delay(uint32_t ms);
#else
#define digitalWrite(pin, value) nrf_gpio_pin_write(pin, value)
#define digitalRead(pin) nrf_gpio_pin_read(pin)
#define delay(ms) nrf_delay_ms(ms)
#endif

// GPIO
void EPD_GPIO_Load(epd_config_t* cfg);
void EPD_GPIO_Init(void);
void EPD_GPIO_Uninit(void);
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
void EPD_BoardPrepare(void);
void EPD_BoardRelease(void);
bool EPD_BoardIsPrepared(void);
#else
#define EPD_BoardPrepare() ((void)0)
#define EPD_BoardRelease() ((void)0)
#define EPD_BoardIsPrepared() false
#endif

// SPI
void EPD_SPI_Write(uint8_t* value, uint8_t len);
void EPD_SPI_Read(uint8_t* value, uint8_t len);
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
uint32_t EPD_SPI_LastError(void);
/* First failed wait survives later POF/sleep: command in high byte;
 * low byte1=no assertion,2=held,3=cancel. FFFF means no failed edge wait. */
void EPD_BusyFailureReset(void);
uint16_t EPD_BusyFirstFailure(void);
void EPD_SPI_ClearError(void);
#endif

// EPD
void EPD_WriteCmd(uint8_t cmd);
void EPD_WriteData(uint8_t* value, uint8_t len);
/* Constant tuples: command, byte count (0..16), data. Each command/data pair
 * remains a separate synchronous bus transaction with RAM staging. */
void EPD_WriteInitCommands(const uint8_t* commands, uint16_t length);
void EPD_ReadData(uint8_t* value, uint8_t len);
/* Issue one command and read its response without releasing CS between the
 * command and data phases. Half-duplex SDA panels require this framing. */
bool EPD_ReadCommand(uint8_t cmd, uint8_t* value, uint8_t len);
#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q
/* Pilot backend: one continuous read, discarding a bounded prefix without
 * allocating a full OTP buffer. It never programs OTP or changes Flash. */
bool EPD_ReadCommandWindow(uint8_t cmd, uint16_t skip, uint8_t* value, uint8_t len);
#endif
void EPD_WriteByte(uint8_t value);
uint8_t EPD_ReadByte(void);
#define EPD_Write(cmd, ...)                  \
    do {                                     \
        uint8_t _data[] = {__VA_ARGS__};     \
        EPD_WriteCmd(cmd);                   \
        EPD_WriteData(_data, sizeof(_data)); \
    } while (0)
void EPD_FillRAM(uint8_t cmd, uint8_t value, uint32_t len);
void EPD_Reset(bool status, uint16_t duration);
/* One active-low reset pulse with independently specified guard times. Exact
 * panel recipes may require a much shorter low pulse than their settle time. */
void EPD_ResetPulse(uint16_t high_before_ms, uint16_t low_ms,
                    uint16_t high_after_ms);
/* Some JD79665CA panel recipes require two low reset pulses before loading
 * their MTP-backed waveform. Keep the electrical sequence in the port layer
 * so controller code does not reach into board-private GPIO state. */
void EPD_ResetDualPulse(uint16_t duration);
bool EPD_ReadBusy(void);
bool EPD_BusyDiagAsserted(void);
uint32_t EPD_BusyDiagAssertDelayMs(void);
uint32_t EPD_BusyDiagActiveMs(void);
void EPD_BusyDiagReset(void);
bool EPD_WaitBusyEdge(bool status, uint16_t assert_timeout, uint16_t release_timeout);
void EPD_WaitBusy(bool status, uint16_t timeout);
void EPD_SetAbortCallback(epd_abort_callback_t callback);
void EPD_SetMaintenanceCallback(epd_maintenance_callback_t callback);
bool EPD_AbortRequested(void);

#if defined(INKTAG_DIALOG)
/* DA14585 bench diagnostics distinguish intended GPIO/SPI activity from an
 * actual controller response. BUSY transitions are sampled from the input;
 * reset/DC transitions and SPI counters describe MCU-side activity. */
void EPD_DialogDiagReset(void);
bool EPD_DialogSpiReady(void);
int8_t EPD_DialogSpiLastError(void);
uint32_t EPD_DialogSpiBytes(void);
uint16_t EPD_DialogSpiTransfers(void);
uint16_t EPD_DialogResetTransitions(void);
uint16_t EPD_DialogDcTransitions(void);
uint16_t EPD_DialogBusyTransitions(void);
uint8_t EPD_DialogPinLevels(void);
#endif

#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
/* PriceETag bring-up can read UC8176 status over the panel's half-duplex
 * data pin.  Keep this board-only so the space-constrained nRF52811 image is
 * unaffected. */
bool UC8xxx_PowerReady(void);
uint8_t UC8xxx_LowPowerStatus(void);
uint8_t UC8xxx_FlagStatus(void);
uint8_t UC8xxx_Revision(void);
#endif

// LED
void EPD_LED_ON(void);
void EPD_LED_OFF(void);
void EPD_LED_Toggle(void);
void EPD_LED_BLINK(void);
void EPD_LED_SetEnabled(bool enabled);

// VDD voltage
uint16_t EPD_ReadVoltage(void);
/* Platform adapter for the product's failure-aware effective temperature.
 * False means unavailable; callers must not substitute a fabricated value. */
bool EPD_ReadEffectiveTemperature(int8_t* temperature);

/* Failure-aware JD796xx controller temperature. The returned whole-degree
 * value is suitable for the product UI; the raw quarter-degree fraction is
 * validated before conversion. */
bool JD79xxx_ReadTemperature(epd_model_t* epd, int8_t* temperature);

/* Bounded SSD1680/SSD1683 internal-sensor acquisition on an already powered,
 * exclusively owned panel. Uses the controller's wire format; false leaves
 * the output unchanged. Not a GET_STATUS/panel-powering entry point. */
bool SSD1xxx_ReadTemperature(epd_model_t* epd, int8_t* temperature);

/* Pilot standard SSD1675 152x152 acquisition while the caller owns a powered,
 * idle display bus. No RAM/refresh/LUT writes; false never becomes a fallback. */
bool SSD1675_ReadTemperature(epd_model_t* epd, int16_t* sixteenths);

epd_model_t* epd_init(const epd_screen_config_t* config);
#if INKTAG_EPD_TEMPERATURE_OPTION
epd_model_t* epd_init_with_temperature(const epd_screen_config_t* config,
                                     bool read_temperature);
#endif
epd_model_t* epd_find(const epd_screen_config_t* config);
uint8_t epd_runtime_driver_id(void);

#endif
