#include "inktag_gr_nvds_guard.h"
#include "custom_config.h"
#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx_nvds.h"
#define NVDS_RESERVED_SECTORS NVDS_NUM_SECTOR
#elif defined(INKTAG_GOODIX_GR5331)
#include "gr533x_nvds.h"
/* Official GR533x API consumes one additional garbage-collection sector. */
#define NVDS_RESERVED_SECTORS (NVDS_NUM_SECTOR + 1u)
#else
#error "Exact Goodix SDK required"
#endif

#define SECTOR_BYTES 4096u
#if !defined(NVDS_START_ADDR) && defined(INKTAG_GOODIX_GR5331)
/* Existing GR5331 Recovery uses the official zero/last-two-sectors API.
 * Its qualified 512 KiB map is the same 0x0027E000 reservation as App. */
#define NVDS_START_ADDR 0x0027E000u
#define NVDS_BOOT_START 0u
#else
#define NVDS_BOOT_START NVDS_START_ADDR
#endif
#define NVDS_END (NVDS_START_ADDR + NVDS_RESERVED_SECTORS * SECTOR_BYTES)
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
_Static_assert(NVDS_START_ADDR == 0x0107E000u && NVDS_NUM_SECTOR == 2u,
               "GR5513 internal layout requires exactly the final 8 KiB NVDS");
#endif
static bool s_initializing, s_ready, s_fault;

bool inktag_gr_nvds_ready(void) {
    return s_ready && !s_fault;
}

bool inktag_gr_nvds_mutation_allowed(uint32_t address, uint32_t size, bool erase) {
    uint32_t end;
    if (!size || size > UINT32_MAX - address) return false;
    end = address + size;
    if (erase) {
        if (end > UINT32_MAX - (SECTOR_BYTES - 1u)) return false;
        address &= ~(SECTOR_BYTES - 1u);
        end = (end + SECTOR_BYTES - 1u) & ~(SECTOR_BYTES - 1u);
    }
    if (address >= NVDS_END || end <= NVDS_START_ADDR) return true;
    if (s_initializing) s_fault = true;
    return inktag_gr_nvds_ready() && !s_initializing;
}

/* Keep one inspectable startup boundary shared by App and Recovery. */
__attribute__((noinline)) uint8_t inktag_gr_nvds_init(uint32_t start, uint8_t sectors) {
    if (s_fault || s_initializing || start != NVDS_BOOT_START ||
        sectors != NVDS_NUM_SECTOR) {
        s_fault = true;
        return NVDS_FAIL;
    }
    s_ready = false;
    s_initializing = true;
    uint8_t result = nvds_init(start, sectors);
    s_initializing = false;
    if (result != NVDS_SUCCESS) s_fault = true;
    s_ready = result == NVDS_SUCCESS && !s_fault;
    return s_ready ? NVDS_SUCCESS : NVDS_FAIL;
}

/* Strong replacement for the SDK weak erase/re-init error handler. An error
 * is not factory authority; never erase, retry or manufacture an OFF record. */
__attribute__((noinline)) void nvds_init_error_handler(uint8_t error) {
    (void)error;
    s_ready = false;
    s_fault = true;
}
