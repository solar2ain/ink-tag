#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_settings_journal.h"

#define TEST_START 0x0003C000u
#define TEST_FLASH_SIZE INKTAG_SETTINGS_JOURNAL_SIZE
#define TEST_PAGE_MAGIC 0x31505349u
#define TEST_ALLOWED_RECORDS \
    (INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_TIME) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_SCREEN_PROFILE) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DISPLAY_STATE) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR) | \
     INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY))
#define TEST_UNKNOWN_RECORD 9u
#define TEST_MAX_LIVE_BYTES 648u

static uint8_t s_flash[TEST_FLASH_SIZE];
static bool s_allow_mutation = true;
static bool s_fail_read;
static int8_t s_unreadable_page = -1;
static uint32_t s_unreadable_address = UINT32_MAX;
static uint32_t s_mutation_checks;
static uint32_t s_fail_at_check;

static uint32_t offset_of(uint32_t address, uint16_t length) {
    assert(address >= TEST_START);
    assert(address - TEST_START <= TEST_FLASH_SIZE);
    assert(length <= TEST_FLASH_SIZE - (address - TEST_START));
    return address - TEST_START;
}

static bool flash_read(void *context, uint32_t address, void *data,
                       uint16_t length) {
    (void)context;
    if (s_fail_read || address == s_unreadable_address || s_unreadable_page ==
            (int8_t)((address - TEST_START) / INKTAG_SETTINGS_JOURNAL_PAGE_SIZE))
        return false;
    memcpy(data, &s_flash[offset_of(address, length)], length);
    return true;
}

static bool flash_write(void *context, uint32_t address, const void *data,
                        uint16_t length) {
    const uint8_t *input = (const uint8_t *)data;
    uint32_t offset = offset_of(address, length);
    uint16_t i;
    (void)context;
    ++s_mutation_checks;
    if (!s_allow_mutation || s_mutation_checks == s_fail_at_check) return false;
    assert((address & 3u) == 0u);
    assert((length & 3u) == 0u);
    for (i = 0u; i < length; ++i) {
        assert(s_flash[offset + i] == 0xFFu);
        s_flash[offset + i] &= input[i];
    }
    return true;
}

static bool flash_erase(void *context, uint32_t address) {
    (void)context;
    ++s_mutation_checks;
    if (!s_allow_mutation || s_mutation_checks == s_fail_at_check) return false;
    assert((address & (INKTAG_SETTINGS_JOURNAL_PAGE_SIZE - 1u)) == 0u);
    memset(&s_flash[offset_of(address, INKTAG_SETTINGS_JOURNAL_PAGE_SIZE)],
           0xFF, INKTAG_SETTINGS_JOURNAL_PAGE_SIZE);
    return true;
}

static inktag_settings_journal_io_t io(void) {
    inktag_settings_journal_io_t value = {
        NULL, TEST_START, TEST_ALLOWED_RECORDS,
        flash_read, flash_write, flash_erase,
    };
    return value;
}

static void seed_empty_page(void) {
    uint32_t header[4] = {TEST_PAGE_MAGIC, 1u, 0xFFFFFFFEu, 0xFFFFFFFFu};
    memset(s_flash, 0xFF, sizeof(s_flash));
    memcpy(s_flash, header, sizeof(header));
    s_allow_mutation = true;
    s_fail_read = false;
    s_unreadable_page = -1;
    s_unreadable_address = UINT32_MAX;
    s_mutation_checks = 0u;
    s_fail_at_check = 0u;
}

static void fill_payload(uint8_t *payload, uint16_t length, uint8_t type,
                         uint8_t revision) {
    uint16_t i;
    for (i = 0u; i < length; ++i)
        payload[i] = (uint8_t)(type * 17u + revision + i);
}

static uint16_t record_length(uint16_t record_id) {
    switch (record_id) {
        case INKTAG_SETTINGS_RECORD_TIME: return 16u;
        case INKTAG_SETTINGS_RECORD_SCREEN_PROFILE: return 28u;
        case INKTAG_SETTINGS_RECORD_DISPLAY_STATE: return 16u;
        case INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR: return 419u;
        case INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY: return 36u;
        case INKTAG_SETTINGS_RECORD_ACTIVATION: return 64u;
        case INKTAG_SETTINGS_RECORD_FIND_CONFIG: return 8u;
        case INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED: return 18u;
        case TEST_UNKNOWN_RECORD: return 20u;
        default: return 0u;
    }
}

static void assert_record(inktag_settings_journal_t *store, uint16_t type,
                          uint8_t revision) {
    uint8_t expected[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint8_t actual[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint16_t limit = record_length(type);
    uint16_t length = 0u;
    fill_payload(expected, limit, type, revision);
    memset(actual, 0, sizeof(actual));
    assert(inktag_settings_journal_read(
        store, type, actual, sizeof(actual), &length));
    assert(length == limit);
    assert(memcmp(actual, expected, length) == 0);
}

static bool record_present(inktag_settings_journal_t *store,
                           uint16_t record_id) {
    uint8_t payload[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint16_t length = 0u;
    return inktag_settings_journal_read(
        store, record_id, payload, sizeof(payload), &length);
}

static void test_unreadable_header_never_rolls_back_or_formats(void) {
    inktag_settings_journal_t store = {0};
    inktag_settings_journal_io_t storage_io = io();
    uint8_t old_page[INKTAG_SETTINGS_JOURNAL_PAGE_SIZE];
    uint8_t snapshot[TEST_FLASH_SIZE], before = 0x11, after = 0x22, actual;
    uint16_t length;
    seed_empty_page();
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    assert(inktag_settings_journal_write(&store,
        INKTAG_SETTINGS_RECORD_TIME, &before, 1u));
    memcpy(old_page, s_flash, sizeof(old_page));
    assert(inktag_settings_journal_put_record_atomic(&store,
        INKTAG_SETTINGS_RECORD_TIME, &after, 1u));
    assert(store.active_page == 1u && store.generation == 2u);
    /* Retain the complete previous generation, as when its erase failed
     * after the new page was committed. Neither read failure may select it. */
    memcpy(s_flash, old_page, sizeof(old_page));
    memcpy(snapshot, s_flash, sizeof(snapshot));
    uint32_t checks = s_mutation_checks;
    for (int8_t page = 0; page < 2; ++page) {
        for (uint8_t create = 0; create < 2; ++create) {
            memset(&store, 0, sizeof(store));
            s_unreadable_page = page;
            assert(!inktag_settings_journal_init(&store, &storage_io, create));
            assert(!store.initialized && s_mutation_checks == checks);
            assert(memcmp(snapshot, s_flash, sizeof(snapshot)) == 0);
        }
    }
    s_unreadable_page = -1;
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    assert(store.active_page == 1u && store.generation == 2u);
    assert(inktag_settings_journal_read(&store,
        INKTAG_SETTINGS_RECORD_TIME, &actual, 1u, &length));
    assert(actual == after && length == 1u);
    /* Readable blank peer still does not establish empty media. */
    memset(s_flash, 0xff, sizeof(old_page));
    memset(&store, 0, sizeof(store));
    s_unreadable_page = 1;
    assert(!inktag_settings_journal_init(&store, &storage_io, true));
    assert(!store.initialized && s_mutation_checks == checks);
    s_unreadable_page = -1;
}

static void test_unreadable_tail_is_not_a_readable_dirty_tail(void) {
    inktag_settings_journal_t store = {0}, rebooted = {0};
    inktag_settings_journal_io_t storage_io = io();
    uint8_t value = 0x31u, actual = 0u, snapshot[TEST_FLASH_SIZE];
    uint16_t length = 0u;
    seed_empty_page();
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    assert(inktag_settings_journal_write(&store,
        INKTAG_SETTINGS_RECORD_TIME, &value, 1u));
    memcpy(snapshot, s_flash, sizeof(snapshot));
    uint32_t checks = s_mutation_checks;
    /* The first erased header and all committed records are readable. Only
     * a later chunk of the tail fails, after the scanner located the value. */
    for (uint16_t chunk = 1u;
         (uint32_t)store.write_offset + chunk * 32u <
             INKTAG_SETTINGS_JOURNAL_PAGE_SIZE;
         ++chunk) {
        s_unreadable_address = TEST_START + store.write_offset + chunk * 32u;
        assert(inktag_settings_journal_read_result(&store,
            INKTAG_SETTINGS_RECORD_TIME, &actual, 1u, &length) ==
                INKTAG_SETTINGS_JOURNAL_READ_ERROR);
        assert(inktag_settings_journal_read_result(&store,
            INKTAG_SETTINGS_RECORD_SCREEN_PROFILE, &actual, 1u, &length) ==
                INKTAG_SETTINGS_JOURNAL_READ_ERROR);
        memset(&rebooted, 0, sizeof(rebooted));
        assert(!inktag_settings_journal_init(&rebooted, &storage_io, false));
        assert(!rebooted.initialized && s_mutation_checks == checks);
        assert(memcmp(snapshot, s_flash, sizeof(snapshot)) == 0);
    }
    s_unreadable_address = UINT32_MAX;
    assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
    assert(inktag_settings_journal_read_result(&rebooted,
        INKTAG_SETTINGS_RECORD_TIME, &actual, 1u, &length) ==
            INKTAG_SETTINGS_JOURNAL_READ_FOUND);
    assert(actual == value && length == 1u);
    assert(inktag_settings_journal_read_result(&rebooted,
        INKTAG_SETTINGS_RECORD_SCREEN_PROFILE, &actual, 1u, &length) ==
            INKTAG_SETTINGS_JOURNAL_READ_MISSING);
    /* A genuinely readable partial tail keeps the last committed value,
     * while absence cannot be proven. The existing power-cut rule remains. */
    s_flash[store.write_offset + 32u] = 0u;
    memset(&rebooted, 0, sizeof(rebooted));
    assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
    assert(!rebooted.tail_clean);
    assert(inktag_settings_journal_read_result(&rebooted,
        INKTAG_SETTINGS_RECORD_TIME, &actual, 1u, &length) ==
            INKTAG_SETTINGS_JOURNAL_READ_FOUND);
    assert(actual == value && length == 1u);
    assert(inktag_settings_journal_read_result(&rebooted,
        INKTAG_SETTINGS_RECORD_SCREEN_PROFILE, &actual, 1u, &length) ==
            INKTAG_SETTINGS_JOURNAL_READ_ERROR);
    assert(s_mutation_checks == checks);
}

static void test_owner_reset_keeps_pending_marker(void) {
    inktag_settings_journal_t store;
    inktag_settings_journal_t rebooted;
    inktag_settings_journal_io_t storage_io = io();
    uint8_t payload[64];
    uint16_t length = 0u;
    const uint8_t marker[INKTAG_SETTINGS_OWNER_RESET_PENDING_SIZE] =
        INKTAG_SETTINGS_OWNER_RESET_PENDING_MAGIC;
    storage_io.allowed_records |=
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_ACTIVATION) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_CONFIG) |
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED);
    seed_empty_page();
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    fill_payload(payload, 64u, INKTAG_SETTINGS_RECORD_ACTIVATION, 1u);
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_ACTIVATION, payload, 64u));
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_FIND_CONFIG, payload, 8u));
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED, payload, 18u));
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING,
        marker, sizeof(marker)));
    assert(inktag_settings_journal_read_result(
        &store, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
        payload, sizeof(payload), &length) == INKTAG_SETTINGS_JOURNAL_READ_MISSING);
    s_fail_read = true;
    assert(inktag_settings_journal_read_result(
        &store, INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING,
        payload, sizeof(payload), &length) == INKTAG_SETTINGS_JOURNAL_READ_ERROR);
    s_fail_read = false;
    assert(inktag_settings_journal_clear(
        &store, INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET));
    memset(&rebooted, 0, sizeof(rebooted));
    assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
    assert(inktag_settings_journal_read_result(
        &rebooted, INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING,
        payload, sizeof(payload), &length) == INKTAG_SETTINGS_JOURNAL_READ_FOUND);
    assert(length == sizeof(marker) && memcmp(payload, marker, length) == 0);
    assert(!record_present(&rebooted, INKTAG_SETTINGS_RECORD_ACTIVATION));
    assert(!record_present(&rebooted, INKTAG_SETTINGS_RECORD_FIND_CONFIG));
    assert(!record_present(&rebooted, INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED));
}

static void test_factory_reset_preserves_protected(void) {
    inktag_settings_journal_t store;
    inktag_settings_journal_t rebooted;
    inktag_settings_journal_io_t protected_io = io();
    uint8_t payload[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint8_t snapshot[TEST_FLASH_SIZE];
    const uint8_t protected_ids[] = {
        INKTAG_SETTINGS_RECORD_ACTIVATION,
        INKTAG_SETTINGS_RECORD_FIND_CONFIG,
        INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED,
    };
    const uint8_t user_ids[] = {
        INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
        INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
    };
    uint32_t successful_checks;
    uint32_t cut;
    uint8_t index;

    for (index = 0u; index < sizeof(protected_ids); ++index)
        protected_io.allowed_records |=
            INKTAG_SETTINGS_RECORD_BIT(protected_ids[index]);
    seed_empty_page();
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &protected_io, false));
    for (index = 0u; index < sizeof(user_ids); ++index) {
        uint8_t id = user_ids[index];
        fill_payload(payload, record_length(id), id, 3u);
        assert(inktag_settings_journal_write(
            &store, id, payload, record_length(id)));
    }
    for (index = 0u; index < sizeof(protected_ids); ++index) {
        uint8_t id = protected_ids[index];
        fill_payload(payload, record_length(id), id, 7u);
        assert(inktag_settings_journal_write(
            &store, id, payload, record_length(id)));
    }
    memcpy(snapshot, s_flash, sizeof(snapshot));

    s_mutation_checks = 0u;
    assert(inktag_settings_journal_clear(
        &store, INKTAG_SETTINGS_CLEAR_FACTORY_RESET));
    successful_checks = s_mutation_checks;
    assert(successful_checks > 3u);
    for (index = 0u; index < sizeof(user_ids); ++index)
        assert(!record_present(&store, user_ids[index]));
    for (index = 0u; index < sizeof(protected_ids); ++index)
        assert_record(&store, protected_ids[index], 7u);

    for (cut = 1u; cut <= successful_checks; ++cut) {
        bool clear_ok;
        bool all_user_present;
        bool all_user_missing;
        memcpy(s_flash, snapshot, sizeof(snapshot));
        memset(&store, 0, sizeof(store));
        assert(inktag_settings_journal_init(&store, &protected_io, false));
        s_mutation_checks = 0u;
        s_fail_at_check = cut;
        clear_ok = inktag_settings_journal_clear(
            &store, INKTAG_SETTINGS_CLEAR_FACTORY_RESET);
        s_fail_at_check = 0u;

        memset(&rebooted, 0, sizeof(rebooted));
        assert(inktag_settings_journal_init(&rebooted, &protected_io, false));
        all_user_present = true;
        all_user_missing = true;
        for (index = 0u; index < sizeof(user_ids); ++index) {
            bool present = record_present(&rebooted, user_ids[index]);
            all_user_present = all_user_present && present;
            all_user_missing = all_user_missing && !present;
        }
        assert(all_user_present || all_user_missing);
        if (clear_ok) assert(all_user_missing);
        for (index = 0u; index < sizeof(protected_ids); ++index)
            assert_record(&rebooted, protected_ids[index], 7u);
    }
}

static void test_atomic_replace_all(void) {
    inktag_settings_journal_t store;
    inktag_settings_journal_t rebooted;
    inktag_settings_journal_io_t storage_io = io();
    uint8_t old_time[16];
    uint8_t old_display[16];
    uint8_t new_screen[28];
    uint8_t new_identity[36];
    inktag_settings_journal_record_t records[2];
    uint8_t snapshot[TEST_FLASH_SIZE];
    uint32_t successful_checks;
    uint32_t cut;

    seed_empty_page();
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    fill_payload(old_time, sizeof(old_time), INKTAG_SETTINGS_RECORD_TIME, 1u);
    fill_payload(old_display, sizeof(old_display),
                 INKTAG_SETTINGS_RECORD_DISPLAY_STATE, 1u);
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_TIME, old_time, sizeof(old_time)));
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
        old_display, sizeof(old_display)));
    memcpy(snapshot, s_flash, sizeof(snapshot));

    fill_payload(new_screen, sizeof(new_screen),
                 INKTAG_SETTINGS_RECORD_SCREEN_PROFILE, 2u);
    fill_payload(new_identity, sizeof(new_identity),
                 INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY, 2u);
    records[0] = (inktag_settings_journal_record_t){
        INKTAG_SETTINGS_RECORD_SCREEN_PROFILE, new_screen,
        sizeof(new_screen),
    };
    records[1] = (inktag_settings_journal_record_t){
        INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY, new_identity,
        sizeof(new_identity),
    };
    s_mutation_checks = 0u;
    assert(inktag_settings_journal_replace_all(&store, records, 2u));
    successful_checks = s_mutation_checks;
    assert(successful_checks > 3u);
    assert(!record_present(&store, INKTAG_SETTINGS_RECORD_TIME));
    assert(!record_present(&store, INKTAG_SETTINGS_RECORD_DISPLAY_STATE));
    assert_record(&store, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE, 2u);
    assert_record(&store, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY, 2u);

    for (cut = 1u; cut <= successful_checks; ++cut) {
        bool replace_ok;
        bool old_set;
        bool new_set;
        memcpy(s_flash, snapshot, sizeof(snapshot));
        memset(&store, 0, sizeof(store));
        assert(inktag_settings_journal_init(&store, &storage_io, false));
        s_mutation_checks = 0u;
        s_fail_at_check = cut;
        replace_ok = inktag_settings_journal_replace_all(
            &store, records, 2u);
        s_fail_at_check = 0u;
        memset(&rebooted, 0, sizeof(rebooted));
        assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
        old_set = record_present(&rebooted, INKTAG_SETTINGS_RECORD_TIME) &&
                  record_present(&rebooted,
                                 INKTAG_SETTINGS_RECORD_DISPLAY_STATE) &&
                  !record_present(&rebooted,
                                  INKTAG_SETTINGS_RECORD_SCREEN_PROFILE) &&
                  !record_present(&rebooted,
                                  INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY);
        new_set = !record_present(&rebooted, INKTAG_SETTINGS_RECORD_TIME) &&
                  !record_present(&rebooted,
                                  INKTAG_SETTINGS_RECORD_DISPLAY_STATE) &&
                  record_present(&rebooted,
                                 INKTAG_SETTINGS_RECORD_SCREEN_PROFILE) &&
                  record_present(&rebooted,
                                 INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY);
        assert(old_set || new_set);
        if (replace_ok) assert(new_set);
    }

    memcpy(s_flash, snapshot, sizeof(snapshot));
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    assert(inktag_settings_journal_replace_all(&store, NULL, 0u));
    memset(&rebooted, 0, sizeof(rebooted));
    assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
    assert(!record_present(&rebooted, INKTAG_SETTINGS_RECORD_TIME));
    assert(!record_present(&rebooted,
                           INKTAG_SETTINGS_RECORD_DISPLAY_STATE));
}

static void test_image_index_alias_suffix_atomic(void) {
    inktag_settings_journal_t store;
    inktag_settings_journal_t rebooted;
    inktag_settings_journal_io_t image_io = io();
    uint8_t old_index[172];
    uint8_t with_aliases[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint8_t actual[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint8_t snapshot[TEST_FLASH_SIZE];
    uint16_t length;
    uint32_t successful_checks;
    uint32_t cut;
    image_io.allowed_records |=
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_IMAGE_INDEX);
    fill_payload(old_index, sizeof(old_index),
                 INKTAG_SETTINGS_RECORD_IMAGE_INDEX, 1u);
    memcpy(with_aliases, old_index, sizeof(old_index));
    fill_payload(with_aliases + sizeof(old_index),
                 sizeof(with_aliases) - sizeof(old_index),
                 INKTAG_SETTINGS_RECORD_IMAGE_INDEX, 2u);
    seed_empty_page();
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &image_io, false));
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
        old_index, sizeof(old_index)));
    memcpy(snapshot, s_flash, sizeof(snapshot));

    s_mutation_checks = 0u;
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
        with_aliases, sizeof(with_aliases)));
    successful_checks = s_mutation_checks;
    assert(successful_checks > 2u);
    for (cut = 1u; cut <= successful_checks; ++cut) {
        memcpy(s_flash, snapshot, sizeof(snapshot));
        memset(&store, 0, sizeof(store));
        assert(inktag_settings_journal_init(&store, &image_io, false));
        s_mutation_checks = 0u;
        s_fail_at_check = cut;
        (void)inktag_settings_journal_write(
            &store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
            with_aliases, sizeof(with_aliases));
        s_fail_at_check = 0u;
        memset(&rebooted, 0, sizeof(rebooted));
        assert(inktag_settings_journal_init(&rebooted, &image_io, false));
        assert(inktag_settings_journal_read(
            &rebooted, INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
            actual, sizeof(actual), &length));
        assert((length == sizeof(old_index) &&
                memcmp(actual, old_index, length) == 0) ||
               (length == sizeof(with_aliases) &&
                memcmp(actual, with_aliases, length) == 0));
    }
    memcpy(s_flash, snapshot, sizeof(snapshot));
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &image_io, false));
    assert(inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
        with_aliases, sizeof(with_aliases)));
    assert(inktag_settings_journal_clear(
        &store, INKTAG_SETTINGS_CLEAR_RESET_SETTINGS));
    assert(record_present(&store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX));
    assert(inktag_settings_journal_clear(
        &store, INKTAG_SETTINGS_CLEAR_USER_DATA));
    assert(!record_present(&store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX));
}

int main(void) {
    inktag_settings_journal_t store;
    inktag_settings_journal_t rebooted;
    inktag_settings_journal_io_t storage_io = io();
    inktag_settings_journal_io_t upgrade_io = io();
    uint8_t payload[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint8_t flash_snapshot[TEST_FLASH_SIZE];
    static const uint8_t record_ids[] = {
        INKTAG_SETTINGS_RECORD_TIME,
        INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
        INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
        INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
        INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
    };
    uint8_t index;
    uint8_t revision;
    uint32_t cut;
    uint32_t checks_before;
    uint32_t successful_clear_checks;

    test_unreadable_header_never_rolls_back_or_formats();
    test_unreadable_tail_is_not_a_readable_dirty_tail();
    test_atomic_replace_all();
    test_factory_reset_preserves_protected();
    test_owner_reset_keeps_pending_marker();
    test_image_index_alias_suffix_atomic();

    upgrade_io.allowed_records |= INKTAG_SETTINGS_RECORD_BIT(TEST_UNKNOWN_RECORD);

    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(&store, 0, sizeof(store));
    assert(!inktag_settings_journal_init(&store, &storage_io, false));

    seed_empty_page();
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &upgrade_io, false));
    assert(store.generation == 1u);

    /* Simulate a bounded record written by newer firmware. This DA build may
     * neither read nor write it, but every compaction must preserve it. */
    fill_payload(payload, record_length(TEST_UNKNOWN_RECORD),
                 TEST_UNKNOWN_RECORD, 7u);
    assert(inktag_settings_journal_write(
        &store, TEST_UNKNOWN_RECORD, payload,
        record_length(TEST_UNKNOWN_RECORD)));
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &storage_io, false));

    for (index = 0u; index < sizeof(record_ids); ++index) {
        uint8_t type = record_ids[index];
        uint16_t limit = record_length(type);
        fill_payload(payload, limit, type, 0u);
        assert(inktag_settings_journal_write(
            &store, type, payload, limit));
    }
    assert(store.write_offset == TEST_MAX_LIVE_BYTES);
    for (index = 0u; index < sizeof(record_ids); ++index)
        assert_record(&store, record_ids[index], 0u);

    /* The shared registry contains these IDs, but the DA backend mask must
     * fail closed for every absent Pro/NFC capability. */
    assert(!inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_ACTIVATION, payload, 16u));
    assert(!inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_RETIRED_FIND_SINGLE_KEY, payload, 16u));
    assert(!inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_NFC_BINDING, payload, 16u));
    assert(!inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_FIND_KEYRING_MANIFEST, payload, 16u));

    /* Replacing one independent identity record repeatedly must compact the
     * complete DA live set while preserving the unrelated QR record. */
    for (revision = 1u; revision <= 100u; ++revision) {
        uint16_t length = record_length(INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY);
        fill_payload(payload, length, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
                     revision);
        assert(inktag_settings_journal_write(
            &store, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY, payload, length));
    }
    assert(store.generation > 1u);
    assert_record(&store, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY, 100u);
    assert_record(&store, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR, 0u);

    memset(&rebooted, 0, sizeof(rebooted));
    assert(inktag_settings_journal_init(&rebooted, &upgrade_io, false));
    assert_record(&rebooted, TEST_UNKNOWN_RECORD, 7u);

    /* Each physical callback re-samples voltage. A denied program keeps the
     * previously committed time record readable after reboot. */
    checks_before = s_mutation_checks;
    s_allow_mutation = false;
    fill_payload(payload, 16u, INKTAG_SETTINGS_RECORD_TIME, 9u);
    assert(!inktag_settings_journal_write(
        &store, INKTAG_SETTINGS_RECORD_TIME, payload, 16u));
    assert(s_mutation_checks > checks_before);
    s_allow_mutation = true;
    memset(&rebooted, 0, sizeof(rebooted));
    assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
    assert_record(&rebooted, INKTAG_SETTINGS_RECORD_TIME, 0u);

    /* RESET_SETTINGS is one generation switch. Exhaust every mutation
     * callback: a reboot must select either the complete old set or the
     * complete reset set, never a mixture. */
    memcpy(flash_snapshot, s_flash, sizeof(flash_snapshot));
    s_mutation_checks = 0u;
    assert(inktag_settings_journal_clear(
        &store, INKTAG_SETTINGS_CLEAR_RESET_SETTINGS));
    successful_clear_checks = s_mutation_checks;
    assert(successful_clear_checks > 2u);
    for (cut = 1u; cut <= successful_clear_checks; ++cut) {
        uint16_t length = 0u;
        bool time_present;
        bool screen_present;
        bool identity_present;
        bool clear_ok;

        memcpy(s_flash, flash_snapshot, sizeof(s_flash));
        memset(&store, 0, sizeof(store));
        assert(inktag_settings_journal_init(&store, &storage_io, false));
        s_mutation_checks = 0u;
        s_fail_at_check = cut;
        clear_ok = inktag_settings_journal_clear(
            &store, INKTAG_SETTINGS_CLEAR_RESET_SETTINGS);
        s_fail_at_check = 0u;

        memset(&rebooted, 0, sizeof(rebooted));
        assert(inktag_settings_journal_init(&rebooted, &upgrade_io, false));
        time_present = inktag_settings_journal_read(
            &rebooted, INKTAG_SETTINGS_RECORD_TIME,
            payload, sizeof(payload), &length);
        screen_present = inktag_settings_journal_read(
            &rebooted, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
            payload, sizeof(payload), &length);
        identity_present = inktag_settings_journal_read(
            &rebooted, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
            payload, sizeof(payload), &length);
        assert((time_present && screen_present && identity_present) ||
               (!time_present && !screen_present && !identity_present));
        if (clear_ok)
            assert(!time_present && !screen_present && !identity_present);
        assert_record(&rebooted, INKTAG_SETTINGS_RECORD_DISPLAY_STATE, 0u);
        assert_record(&rebooted, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR, 0u);
        assert_record(&rebooted, TEST_UNKNOWN_RECORD, 7u);
    }

    /* A successful reset preserves content, while CLEAR_USER_DATA removes
     * display/content references but not unknown/protected future state. */
    memcpy(s_flash, flash_snapshot, sizeof(s_flash));
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    assert(inktag_settings_journal_clear(
        &store, INKTAG_SETTINGS_CLEAR_RESET_SETTINGS));
    memset(&rebooted, 0, sizeof(rebooted));
    assert(inktag_settings_journal_init(&rebooted, &upgrade_io, false));
    {
        uint16_t length = 0u;
        assert(!inktag_settings_journal_read(
            &rebooted, INKTAG_SETTINGS_RECORD_TIME,
            payload, sizeof(payload), &length));
        assert(!inktag_settings_journal_read(
            &rebooted, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
            payload, sizeof(payload), &length));
        assert(!inktag_settings_journal_read(
            &rebooted, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
            payload, sizeof(payload), &length));
    }
    assert_record(&rebooted, INKTAG_SETTINGS_RECORD_DISPLAY_STATE, 0u);
    assert_record(&rebooted, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR, 0u);
    assert(inktag_settings_journal_clear(
        &rebooted, INKTAG_SETTINGS_CLEAR_USER_DATA));
    memset(&store, 0, sizeof(store));
    assert(inktag_settings_journal_init(&store, &upgrade_io, false));
    {
        uint16_t length = 0u;
        assert(!inktag_settings_journal_read(
            &store, INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
            payload, sizeof(payload), &length));
        assert(!inktag_settings_journal_read(
            &store, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
            payload, sizeof(payload), &length));
    }
    assert_record(&store, TEST_UNKNOWN_RECORD, 7u);

    puts("InkTag Settings Journal tests passed");
    return 0;
}
