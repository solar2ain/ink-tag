#include "inktag_image_index.h"

#include <stddef.h>
#include <string.h>
#include "inktag_resource_name.h"

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static void write16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static uint16_t map_end(const uint8_t *wire) {
    return (uint16_t)(INKTAG_IMAGE_INDEX_HEADER_BYTES +
                      INKTAG_IMAGE_INDEX_MAP_BYTES(wire[2]));
}
static bool shape(const uint8_t *wire, uint16_t length) {
    if (!wire || length < INKTAG_IMAGE_INDEX_HEADER_BYTES ||
        length > INKTAG_IMAGE_INDEX_MAX_BYTES ||
        wire[0] != INKTAG_IMAGE_INDEX_VERSION || wire[1] != 0u ||
        wire[2] == 0u || read16(wire + 4) == 0u ||
        read16(wire + 4) > INKTAG_IMAGE_INDEX_UNUSED) return false;
    return map_end(wire) <= length &&
           read16(wire + 6) == length - map_end(wire);
}
static uint16_t physical_get(const uint8_t *wire, uint8_t slot) {
    uint16_t bit = (uint16_t)slot * 10u;
    uint16_t byte = (uint16_t)(8u + bit / 8u);
    /* Ten-bit entries start at offsets 0/2/4/6, so two bytes suffice,
     * including the final entry. Byte loads avoid alignment/aliasing traps. */
    return (uint16_t)((read16(wire + byte) >> (bit % 8u)) & 0x3ffu);
}
static void physical_set(uint8_t *wire, uint8_t slot, uint16_t value) {
    uint16_t bit = (uint16_t)slot * 10u;
    uint16_t byte = (uint16_t)(8u + bit / 8u);
    uint8_t shift = (uint8_t)(bit % 8u);
    uint16_t mask = (uint16_t)(0x3ffu << shift);
    uint16_t packed = (uint16_t)((read16(wire + byte) & (uint16_t)~mask) |
                                ((value & 0x3ffu) << shift));
    write16(wire + byte, packed);
}

bool inktag_image_index_validate(const uint8_t *wire, uint16_t length,
                                uint8_t slots, uint16_t physical_limit) {
    uint16_t slot, cursor, entry;
    uint16_t previous_slot = INKTAG_IMAGE_INDEX_MAX_SLOTS;
    if (!shape(wire, length) || wire[2] != slots ||
        read16(wire + 4) != physical_limit) return false;
    for (slot = 0u; slot < slots; ++slot) {
        uint16_t physical = physical_get(wire, (uint8_t)slot), prior;
        if (physical == INKTAG_IMAGE_INDEX_UNUSED) continue;
        if (physical >= physical_limit) return false;
        for (prior = 0u; prior < slot; ++prior)
            if (physical_get(wire, (uint8_t)prior) == physical) return false;
    }
    /* Canonical unused high bits are ones, like the empty map. */
    if (((uint16_t)slots * 10u) % 8u) {
        uint8_t mask = (uint8_t)(0xffu <<
                                (((uint16_t)slots * 10u) % 8u));
        if ((wire[map_end(wire) - 1u] & mask) != mask) return false;
    }
    cursor = map_end(wire);
    for (entry = 0u; entry < wire[3]; ++entry) {
        uint8_t name_length, alias_slot;
        uint16_t prior;
        if (cursor + 2u > length) return false;
        alias_slot = wire[cursor];
        name_length = wire[cursor + 1u];
        if (alias_slot >= slots ||
            (entry && alias_slot <= previous_slot) ||
            physical_get(wire, alias_slot) == INKTAG_IMAGE_INDEX_UNUSED ||
            name_length > length - cursor - 2u ||
            !inktag_resource_name_valid(wire + cursor + 2u, name_length))
            return false;
        for (prior = map_end(wire); prior < cursor;
             prior = (uint16_t)(prior + 2u + wire[prior + 1u]))
            if (inktag_resource_name_matches(
                    wire + cursor + 2u, name_length,
                    wire + prior + 2u, wire[prior + 1u])) return false;
        previous_slot = alias_slot;
        cursor = (uint16_t)(cursor + 2u + name_length);
    }
    return cursor == length;
}

inktag_image_index_result_t inktag_image_index_reset(
    uint8_t *wire, uint16_t capacity, uint8_t slots, uint16_t physical_limit,
    uint16_t *length) {
    uint16_t bytes = (uint16_t)(8u + INKTAG_IMAGE_INDEX_MAP_BYTES(slots));
    if (!wire || !length || !slots || !physical_limit ||
        physical_limit > INKTAG_IMAGE_INDEX_UNUSED ||
        capacity > INKTAG_IMAGE_INDEX_MAX_BYTES)
        return INKTAG_IMAGE_INDEX_INVALID;
    if (capacity < bytes) return INKTAG_IMAGE_INDEX_NO_SPACE;
    memset(wire, 0xff, bytes);
    wire[0] = INKTAG_IMAGE_INDEX_VERSION;
    wire[1] = 0u;
    wire[2] = slots;
    wire[3] = 0u;
    write16(wire + 4, physical_limit);
    write16(wire + 6, 0u);
    *length = bytes;
    return INKTAG_IMAGE_INDEX_OK;
}

uint16_t inktag_image_index_get(const uint8_t *wire, uint16_t length,
                              uint8_t slot) {
    return shape(wire, length) && slot < wire[2] ?
        physical_get(wire, slot) : INKTAG_IMAGE_INDEX_UNUSED;
}
uint16_t inktag_image_index_count_below(const uint8_t *wire, uint16_t length,
                                      uint16_t slot_limit) {
    uint16_t slot, count = 0u;
    if (!shape(wire, length)) return 0u;
    if (slot_limit > wire[2]) slot_limit = wire[2];
    for (slot = 0u; slot < slot_limit; ++slot)
        if (physical_get(wire, (uint8_t)slot) != INKTAG_IMAGE_INDEX_UNUSED)
            ++count;
    return count;
}
bool inktag_image_index_first_free(const uint8_t *wire, uint16_t length,
                                  uint8_t *slot) {
    uint16_t i;
    if (!slot || !shape(wire, length)) return false;
    for (i = 0u; i < wire[2]; ++i)
        if (physical_get(wire, (uint8_t)i) == INKTAG_IMAGE_INDEX_UNUSED) {
            *slot = (uint8_t)i;
            return true;
        }
    return false;
}
static uint16_t alias_offset(const uint8_t *wire, uint16_t length,
                             uint8_t slot) {
    uint16_t cursor = map_end(wire);
    while (cursor < length && wire[cursor] < slot)
        cursor = (uint16_t)(cursor + 2u + wire[cursor + 1u]);
    return cursor;
}
const uint8_t *inktag_image_index_alias_get(
    const uint8_t *wire, uint16_t length, uint8_t slot, uint8_t *alias_length) {
    uint16_t offset;
    if (alias_length) *alias_length = 0u;
    if (!shape(wire, length) || slot >= wire[2]) return NULL;
    offset = alias_offset(wire, length, slot);
    if (offset == length || wire[offset] != slot) return NULL;
    if (alias_length) *alias_length = wire[offset + 1u];
    return wire + offset + 2u;
}
static void alias_delete(uint8_t *wire, uint16_t *length, uint8_t slot) {
    uint16_t offset = alias_offset(wire, *length, slot);
    if (offset < *length && wire[offset] == slot) {
        uint16_t bytes = (uint16_t)(2u + wire[offset + 1u]);
        memmove(wire + offset, wire + offset + bytes, *length - offset - bytes);
        *length = (uint16_t)(*length - bytes);
        --wire[3];
        write16(wire + 6, (uint16_t)(*length - map_end(wire)));
    }
}
inktag_image_index_result_t inktag_image_index_set(
    uint8_t *wire, uint16_t *length, uint8_t slot, uint16_t physical) {
    uint16_t prior;
    if (!length || !shape(wire, *length) || slot >= wire[2] ||
        (physical != INKTAG_IMAGE_INDEX_UNUSED && physical >= read16(wire + 4)))
        return INKTAG_IMAGE_INDEX_INVALID;
    if (physical != INKTAG_IMAGE_INDEX_UNUSED) {
        for (prior = 0u; prior < wire[2]; ++prior)
            if (prior != slot && physical_get(wire, (uint8_t)prior) == physical)
                return INKTAG_IMAGE_INDEX_DUPLICATE;
    } else {
        alias_delete(wire, length, slot);
    }
    physical_set(wire, slot, physical);
    return INKTAG_IMAGE_INDEX_OK;
}
inktag_image_index_result_t inktag_image_index_alias_set(
    uint8_t *wire, uint16_t capacity, uint16_t *length,
    uint8_t slot, const uint8_t *alias, uint8_t alias_length) {
    uint16_t cursor, offset, old_bytes, new_bytes, new_length;
    if (!length || !shape(wire, *length) ||
        capacity > INKTAG_IMAGE_INDEX_MAX_BYTES || capacity < *length ||
        slot >= wire[2]) return INKTAG_IMAGE_INDEX_INVALID;
    if (physical_get(wire, slot) == INKTAG_IMAGE_INDEX_UNUSED)
        return INKTAG_IMAGE_INDEX_NOT_FOUND;
    if (alias_length && !inktag_resource_name_valid(alias, alias_length))
        return INKTAG_IMAGE_INDEX_INVALID;
    offset = alias_offset(wire, *length, slot);
    old_bytes = offset < *length && wire[offset] == slot ?
                    (uint16_t)(2u + wire[offset + 1u]) : 0u;
    new_bytes = alias_length ? (uint16_t)(2u + alias_length) : 0u;
    new_length = (uint16_t)(*length - old_bytes + new_bytes);
    if (new_length > capacity) return INKTAG_IMAGE_INDEX_NO_SPACE;
    for (cursor = map_end(wire); alias_length && cursor < *length;
         cursor = (uint16_t)(cursor + 2u + wire[cursor + 1u]))
        if (wire[cursor] != slot &&
            inktag_resource_name_matches(alias, alias_length,
                wire + cursor + 2u, wire[cursor + 1u]))
            return INKTAG_IMAGE_INDEX_DUPLICATE;
    if (old_bytes && !new_bytes) --wire[3];
    if (!old_bytes && new_bytes) ++wire[3];
    memmove(wire + offset + new_bytes, wire + offset + old_bytes,
            *length - offset - old_bytes);
    if (new_bytes) {
        wire[offset] = slot;
        wire[offset + 1u] = alias_length;
        memcpy(wire + offset + 2u, alias, alias_length);
    }
    *length = new_length;
    write16(wire + 6, (uint16_t)(new_length - map_end(wire)));
    return INKTAG_IMAGE_INDEX_OK;
}
