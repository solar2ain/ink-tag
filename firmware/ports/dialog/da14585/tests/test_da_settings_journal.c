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
static uint32_t s_read_calls;

static uint32_t offset_of(uint32_t address, uint16_t length) {
    assert(address >= TEST_START);
    assert(address - TEST_START <= TEST_FLASH_SIZE);
    assert(length <= TEST_FLASH_SIZE - (address - TEST_START));
    return address - TEST_START;
}

static bool flash_read(void *context, uint32_t address, void *data,
                       uint16_t length) {
    (void)context;
    ++s_read_calls;
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
    s_read_calls = 0u;
}

static void fill_payload(uint8_t *payload, uint16_t length, uint8_t type,
                         uint8_t revision) {
    uint16_t i;
    for (i = 0u; i < length; ++i)
        payload[i] = (uint8_t)(type * 17u + revision + i);
}

static bool record_present(inktag_settings_journal_t *store,
                           uint16_t record_id) {
    uint8_t payload[INKTAG_SETTINGS_JOURNAL_MAX_PAYLOAD];
    uint16_t length = 0u;
    return inktag_settings_journal_read(
        store, record_id, payload, sizeof(payload), &length);
}

static void test_compaction_single_pass_preserves_all_ids(void) {
    inktag_settings_journal_t store = {0}, rebooted = {0};
    inktag_settings_journal_io_t storage_io = io();
    uint8_t payload[16], actual[16];
    uint16_t length;
    seed_empty_page();
    storage_io.allowed_records = UINT32_MAX;
    assert(inktag_settings_journal_init(&store, &storage_io, false));
    for (uint16_t type = 1; type <= INKTAG_SETTINGS_RECORD_MAX; ++type) {
        memset(payload, type, sizeof(payload));
        assert(inktag_settings_journal_write(&store, type, payload, sizeof(payload)));
    }
    assert(inktag_settings_journal_delete(&store, 29));
    memset(payload, 0xA5, sizeof(payload));
    while (store.write_offset + 32u <= INKTAG_SETTINGS_JOURNAL_PAGE_SIZE)
        assert(inktag_settings_journal_write(&store, 2, payload, sizeof(payload)));
    /* A downgraded writer can only write TIME. Compaction must nevertheless
     * retain all 31 other stable IDs, including future owner/Find records. */
    store.io.allowed_records = INKTAG_SETTINGS_RECORD_BIT(2);
    const uint32_t checks = s_mutation_checks;
    s_fail_read = true;
    assert(!inktag_settings_journal_write(&store, 2, payload, sizeof(payload)));
    assert(s_mutation_checks == checks); /* no erase before successful scan */
    s_fail_read = false;
    s_read_calls = 0;
    assert(inktag_settings_journal_write(&store, 2, payload, sizeof(payload)));
    const uint32_t compaction_reads = s_read_calls;
    assert(compaction_reads < 800u); /* old per-ID rescan is > 12,000 */
    assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
    for (uint16_t type = 1; type <= INKTAG_SETTINGS_RECORD_MAX; ++type) {
        if (type == 29) {
            assert(inktag_settings_journal_read_result(&rebooted, type, actual,
                sizeof(actual), &length) == INKTAG_SETTINGS_JOURNAL_READ_MISSING);
            continue;
        }
        assert(inktag_settings_journal_read(&rebooted, type, actual,
                                           sizeof(actual), &length));
        assert(length == sizeof(actual));
        memset(payload, type == 2 ? 0xA5 : type, sizeof(payload));
        assert(!memcmp(actual, payload, sizeof(actual)));
    }
    printf("single-pass compaction: %u reads; all stable IDs/tombstone preserved\n",
           (unsigned)compaction_reads);
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
    assert(inktag_settings_journal_clear_ordinary_settings_v2(&store));
    assert(record_present(&store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX));
    assert(inktag_settings_journal_clear_images(&store));
    assert(!record_present(&store, INKTAG_SETTINGS_RECORD_IMAGE_INDEX));
}

static void assert_two_action_records(inktag_settings_journal_t *store,
                                      uint32_t removed, bool qr_present) {
    uint16_t id;
    for (id = 1u; id <= 32u; ++id) {
        uint8_t actual[4];
        uint8_t expected[4] = {(uint8_t)id, 0x5au, 0xc3u, 0x7eu};
        uint16_t length = 0u;
        bool absent = (removed & INKTAG_SETTINGS_RECORD_BIT(id)) != 0u ||
                      (id == 5u && !qr_present);
        inktag_settings_journal_read_result_t result =
            inktag_settings_journal_read_result(
                store, id, actual, sizeof(actual), &length);
        if (absent) assert(result == INKTAG_SETTINGS_JOURNAL_READ_MISSING);
        else {
            assert(result == INKTAG_SETTINGS_JOURNAL_READ_FOUND);
            assert(length == sizeof(expected));
            assert(memcmp(actual, expected, sizeof(expected)) == 0);
        }
    }
}

static void test_two_action_scopes(void) {
    bool (*actions[2])(inktag_settings_journal_t *) = {
        inktag_settings_journal_clear_images,
        inktag_settings_journal_clear_ordinary_settings_v2,
    };
    /* Independent numeric expectations: images 4/30; ordinary 2/3/6/7/11/13.
     * Never derive the expected scope from the implementation's masks. */
    const uint32_t masks[2] = {
        UINT32_C(0x20000008), UINT32_C(0x00001466),
    };
    uint8_t before[TEST_FLASH_SIZE];
    unsigned action, qr;
    inktag_settings_journal_io_t storage_io = io();
    storage_io.allowed_records = UINT32_MAX;
    for (action = 0u; action < 2u; ++action) {
        inktag_settings_journal_t blank = {0};
        assert(!actions[action](NULL));
        assert(!actions[action](&blank));
        for (qr = 0u; qr < 2u; ++qr) {
            inktag_settings_journal_t store = {0}, rebooted = {0};
            uint16_t id;
            uint32_t mutations, cut;
            seed_empty_page();
            assert(inktag_settings_journal_init(&store, &storage_io, false));
            for (id = 1u; id <= 32u; ++id) {
                uint8_t value[4] = {(uint8_t)id, 0x5au, 0xc3u, 0x7eu};
                if (id != 5u || qr != 0u)
                    assert(inktag_settings_journal_write(
                        &store, id, value, sizeof(value)));
            }
            memcpy(before, s_flash, sizeof(before));
            /* Unknown authority must be rejected without touching Flash. */
            s_mutation_checks = 0u;
            store.tail_clean = false;
            assert(!actions[action](&store));
            store.tail_clean = true;
            ++store.generation;
            assert(!actions[action](&store));
            --store.generation;
            s_unreadable_address = TEST_START;
            assert(!actions[action](&store));
            s_unreadable_address = UINT32_MAX;
            assert(s_mutation_checks == 0u);
            s_mutation_checks = 0u;
            assert(actions[action](&store));
            mutations = s_mutation_checks;
            assert_two_action_records(&store, masks[action], qr != 0u);
            for (cut = 1u; cut <= mutations; ++cut) {
                bool ok, committed;
                memcpy(s_flash, before, sizeof(s_flash));
                memset(&store, 0, sizeof(store));
                assert(inktag_settings_journal_init(&store, &storage_io, false));
                s_mutation_checks = 0u;
                s_fail_at_check = cut;
                ok = actions[action](&store);
                s_fail_at_check = 0u;
                memset(&rebooted, 0, sizeof(rebooted));
                assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
                committed = !record_present(&rebooted, action == 0u ? 4u : 2u);
                if (ok) assert(committed);
                assert_two_action_records(&rebooted,
                    committed ? masks[action] : 0u, qr != 0u);
            }
            /* A failed source read cannot commit a partial protected set. */
            memcpy(s_flash, before, sizeof(s_flash));
            memset(&store, 0, sizeof(store));
            assert(inktag_settings_journal_init(&store, &storage_io, false));
            s_fail_read = true;
            assert(!actions[action](&store));
            s_fail_read = false;
            memset(&rebooted, 0, sizeof(rebooted));
            assert(inktag_settings_journal_init(&rebooted, &storage_io, false));
            assert_two_action_records(&rebooted, 0u, qr != 0u);
        }
    }
}

int main(void) {
    test_compaction_single_pass_preserves_all_ids();
    test_unreadable_header_never_rolls_back_or_formats();
    test_unreadable_tail_is_not_a_readable_dirty_tail();
    test_image_index_alias_suffix_atomic();
    test_two_action_scopes();
    puts("DA Free Settings Journal tests passed");
    return 0;
}
