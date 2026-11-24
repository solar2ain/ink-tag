/* DA storage API v2: only consumed settings operations. */
#ifndef INKTAG_SETTINGS_JOURNAL_H
#define INKTAG_SETTINGS_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_settings_records.h"

#define INKTAG_SETTINGS_JOURNAL_PAGE_SIZE 4096u
#define INKTAG_SETTINGS_JOURNAL_PAGE_COUNT 2u
#define INKTAG_SETTINGS_JOURNAL_SIZE \
    (INKTAG_SETTINGS_JOURNAL_PAGE_SIZE * INKTAG_SETTINGS_JOURNAL_PAGE_COUNT)
#define INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD 420u
typedef bool (*inktag_settings_journal_read_fn)(
    void *context, uint32_t address, void *data, uint16_t length);
typedef bool (*inktag_settings_journal_write_fn)(
    void *context, uint32_t address, const void *data, uint16_t length);
typedef bool (*inktag_settings_journal_erase_fn)(
    void *context, uint32_t address);

typedef struct {
    void *context;
    uint32_t start_address;
    /* A target accepts only records for capabilities it actually implements.
     * DA therefore cannot acquire activation, Find or NFC state merely because
     * those stable IDs exist in the common registry. */
    uint32_t allowed_records;
    inktag_settings_journal_read_fn read;
    inktag_settings_journal_write_fn write;
    inktag_settings_journal_erase_fn erase;
} inktag_settings_journal_io_t;

typedef struct {
    inktag_settings_journal_io_t io;
    uint32_t generation;
    uint16_t write_offset;
    uint8_t active_page;
    uint8_t initialized : 1;
    uint8_t tail_clean : 1;
    uint8_t reserved_flags : 6;
} inktag_settings_journal_t;

typedef enum {
    INKTAG_SETTINGS_JOURNAL_READ_ERROR = 0,
    INKTAG_SETTINGS_JOURNAL_READ_MISSING = 1,
    INKTAG_SETTINGS_JOURNAL_READ_FOUND = 2,
} inktag_settings_journal_read_result_t;

/* create_if_empty is intended for blank factory media. A platform making an
 * incompatible Full-only transition passes false and seeds a committed empty
 * page in its Full container, so an application-only update cannot silently
 * reinterpret or erase the previous layout. */
/* Both page headers must be readable before selecting a generation. IO
 * failure is neither an invalid older page nor permission to format. */
bool inktag_settings_journal_init(
    inktag_settings_journal_t *store,
    const inktag_settings_journal_io_t *io,
    bool create_if_empty);
bool inktag_settings_journal_read(
    inktag_settings_journal_t *store, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length);
/* Security-sensitive records must not conflate a missing record with a
 * failed Flash read or an unclean journal tail. */
inktag_settings_journal_read_result_t inktag_settings_journal_read_result(
    inktag_settings_journal_t *store, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length);
bool inktag_settings_journal_write(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length);
bool inktag_settings_journal_delete(
    inktag_settings_journal_t *store, uint16_t record_id);


/* The same whole-generation contract, also for a provably absent record.
 * A failed first insertion preserves clean absence, not an ambiguous tail. */
bool inktag_settings_journal_put_record_atomic(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length);











/* New scope: omit only image-owned records 4/30. QR/password/NFC and every
 * other record survive, including when no QR record has ever been written. */
bool inktag_settings_journal_clear_images(
    inktag_settings_journal_t *store);
/* DA settings reset: omit ordinary settings records; preserve opaque ID5. */
bool inktag_settings_journal_clear_ordinary_settings_v2(
    inktag_settings_journal_t *store);



#endif
