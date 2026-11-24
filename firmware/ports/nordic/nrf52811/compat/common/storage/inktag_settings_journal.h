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

/* Runtime IO/state only, never serialized. Vendor -fpack-struct must not
 * obscure the natural alignment of callback pointers and word fields. */
typedef struct __attribute__((aligned(4))) {
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

typedef struct __attribute__((aligned(4))) {
    inktag_settings_journal_io_t io;
    uint32_t generation;
    uint16_t write_offset;
    uint8_t active_page;
    uint8_t initialized : 1;
    uint8_t tail_clean : 1;
    uint8_t reserved_flags : 6;
} inktag_settings_journal_t;

typedef struct {
    uint16_t record_id;
    const void *data;
    uint16_t length;
} inktag_settings_journal_record_t;

typedef enum {
    INKTAG_SETTINGS_JOURNAL_READ_ERROR = 0,
    INKTAG_SETTINGS_JOURNAL_READ_MISSING = 1,
    INKTAG_SETTINGS_JOURNAL_READ_FOUND = 2,
} inktag_settings_journal_read_result_t;

typedef struct __attribute__((aligned(4))) {
    inktag_settings_journal_t *store;
    uint32_t generation;
    uint16_t write_offset;
    uint8_t page;
    uint8_t initialized : 1;
    uint8_t tail_clean : 1;
    uint8_t reserved_flags : 6;
} inktag_settings_journal_stage_t;

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
/* Bounded maintenance consumer: no initialization, repair, write or erase.
 * Only read is required; mutation callbacks are ignored. Select the committed
 * generation, verify the complete active page, and distinguish clean absence
 * from IO/corrupt/ambiguous media. No schema or record interpretation here.
 * Unlike ordinary reads, an unclean tail always refuses this snapshot. */
inktag_settings_journal_read_result_t inktag_settings_journal_readonly_result(
    const inktag_settings_journal_io_t *io, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length);
bool inktag_settings_journal_write(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length);
bool inktag_settings_journal_delete(
    inktag_settings_journal_t *store, uint16_t record_id);
/* Atomically replaces the complete logical contents with `records`. Data is
 * written to the inactive page and becomes visible only when its page header
 * is committed. This is intended for a dedicated two-page journal such as a
 * Find keyring region; using it on the main settings journal deliberately
 * drops every record omitted by the caller. A zero-record replacement creates
 * a committed empty generation. */
bool inktag_settings_journal_replace_all(
    inktag_settings_journal_t *store,
    const inktag_settings_journal_record_t *records,
    uint16_t record_count);
/* Replace one existing record while copying every other live record into a
 * new generation. The old generation remains authoritative until the new
 * page magic commits. A false result after that point is possible when the
 * old page could not be erased; callers must reconcile by reading the record
 * and must not expose a secret-bearing identity until the inactive page is
 * verified erased. */
bool inktag_settings_journal_replace_record_atomic(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length);
/* The same whole-generation contract, also for a provably absent record.
 * A failed first insertion preserves clean absence, not an ambiguous tail. */
bool inktag_settings_journal_put_record_atomic(
    inktag_settings_journal_t *store, uint16_t record_id,
    const void *data, uint16_t length);
bool inktag_settings_journal_inactive_erased(
    inktag_settings_journal_t *store);
bool inktag_settings_journal_scrub_inactive(
    inktag_settings_journal_t *store);
/* Fresh, read-only proof for a retired two-page extent: entirely FF or one
 * committed-empty generation with clean tail and wholly erased peer. No
 * create, repair or erase; callbacks retain their original bounded extent. */
bool inktag_settings_journal_empty_media_safe(
    const inktag_settings_journal_io_t *io);
/* Streaming whole-generation replacement. BEGIN erases only the inactive
 * page and writes an uncommitted generation header. Each staged record is
 * individually checksummed/committed but remains invisible to normal reads.
 * COMMIT validates the complete page and writes its page magic last. OPEN can
 * resume a clean interrupted upload after reboot without buffering the whole
 * generation in RAM. */
bool inktag_settings_journal_stage_open(
    inktag_settings_journal_t *store,
    inktag_settings_journal_stage_t *stage);
bool inktag_settings_journal_stage_begin(
    inktag_settings_journal_t *store,
    inktag_settings_journal_stage_t *stage);
bool inktag_settings_journal_stage_read(
    inktag_settings_journal_stage_t *stage, uint16_t record_id,
    void *data, uint16_t capacity, uint16_t *length);
bool inktag_settings_journal_stage_write(
    inktag_settings_journal_stage_t *stage, uint16_t record_id,
    const void *data, uint16_t length);
bool inktag_settings_journal_stage_commit(
    inktag_settings_journal_stage_t *stage);
bool inktag_settings_journal_stage_abort(
    inktag_settings_journal_stage_t *stage);
/* Rewrites a complete generation while omitting only the records selected by
 * the common lifecycle table. The new page becomes authoritative at its final
 * magic write, so a reset interrupted at any earlier mutation keeps the
 * complete previous generation. */
bool inktag_settings_journal_clear(
    inktag_settings_journal_t *store,
    inktag_settings_clear_action_t action);
/* New scope: omit only image-owned records 4/30. QR/password/NFC and every
 * other record survive, including when no QR record has ever been written. */
bool inktag_settings_journal_clear_images(
    inktag_settings_journal_t *store);
/* New settings scope, ordinary phase only: omit 2/3/6/7/11/13. The caller
 * consumes fresh W first and commits authoritative password OFF/QR last via
 * its existing backend. This function deliberately preserves ID5. */
bool inktag_settings_journal_clear_ordinary_settings_v2(
    inktag_settings_journal_t *store);
/* Full new reset scope, for a backend where clean ID5 absence is authoritative
 * OFF: ordinary settings and ID5 disappear together at the existing final
 * generation commit. Fresh W/admission remains the caller's responsibility. */
bool inktag_settings_journal_reset_settings_v2(
    inktag_settings_journal_t *store);
#endif
