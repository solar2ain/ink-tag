#include "inktag_public_cache_retirement.h"
#include "inktag_protocol_core.h"
#include <string.h>

typedef inktag_public_cache_shape_t cache_shape_t;

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool zero(const uint8_t *p, uint16_t n) {
    uint8_t any = 0u;
    while (n--) any |= *p++;
    return any == 0u;
}
static bool public_x(const uint8_t *p) {
    uint8_t nonzero = 0u, nonff = 0u;
    for (uint8_t i = 0; i < 28; ++i) {
        nonzero |= p[i]; nonff |= (uint8_t)~p[i];
    }
    return nonzero && nonff; /* Historical codec's bounded public-X rule. */
}
static bool classify(uint8_t record, const uint8_t *p, uint16_t n,
                     cache_shape_t *shape) {
    uint8_t bank = record / 8u, index = record % 8u;
    memset(shape, 0, sizeof(*shape));
    if (n < 24u || n > INKTAG_PUBLIC_CACHE_MAX_BYTES || p[4] != 1u ||
        u32(p + n - 4u) != (inktag_crc32_update(UINT32_MAX, p, n-4u) ^ UINT32_MAX))
        return false;
    shape->length = n; shape->checksum = u32(p + n-4u);
    if (index == 0u) {
        /* 94-byte packed manifest, including 16-byte digest and final CRC. */
        if (n != 94u || u32(p) != UINT32_C(0x32524b46) ||
            p[6] != bank || p[11] != 0u || u32(p+18) == 0u) return false;
        shape->state = p[5]; shape->generation = u32(p+18);
        if (p[5] == 3u)
            return zero(p+7, 11u) && zero(p+22, 68u);
        if ((p[5] != 1u && p[5] != 2u) || p[7] != 1u ||
            p[8] < 20u || p[8] > 100u ||
            p[9] != (p[8]+7u)/8u || p[10] != (p[8]+15u)/16u ||
            u32(p+14) != 1800u || u32(p+22) == 0u ||
            zero(p+26, 8u) || zero(p+34, 8u) || !public_x(p+42)) return false;
        shape->keys = p[8]; shape->upload = u32(p+22);
        shape->keys_crc = u32(p+70);
        shape->received = (uint16_t)(p[12] | (uint16_t)p[13] << 8);
        uint16_t required = (uint16_t)((1u << p[9])-1u);
        return (shape->received & ~required) == 0u &&
               (p[5] != 2u || shape->received == required);
    }
    if (u32(p) != UINT32_C(0x32434b46) || p[5] != bank ||
        p[6] != index-1u || p[7] == 0u || (p[7] & ~3u) ||
        u32(p+8) == 0u || u32(p+12) == 0u || p[18] || p[19]) return false;
    shape->generation = u32(p+8); shape->upload = u32(p+12);
    shape->mask = p[7]; shape->counts[0] = p[16]; shape->counts[1] = p[17];
    for (uint8_t part = 0; part < 2; ++part) {
        uint8_t count = p[16+part];
        if ((p[7] & (1u << part)) ? (count == 0u || count > 8u) : count != 0u)
            return false;
    }
    if (p[7] == 3u && p[16] != 8u) return false;
    uint16_t extent = (p[7] & 2u) ? (uint16_t)(8u+p[17])*28u : (uint16_t)p[16]*28u;
    if (n != 24u+extent || (p[7] == 2u && !zero(p+20, 224u))) return false;
    for (uint8_t part = 0; part < 2; ++part)
        for (uint8_t key = 0; key < p[16+part]; ++key)
            if (!public_x(p+20u+(part*8u+key)*28u)) return false;
    return true;
}

static bool unchanged(const inktag_public_cache_io_t *io, inktag_public_cache_workspace_t *w) {
    return io->allowed(io->context) && io->authority(io->context, w->current) &&
           memcmp(w->current, w->authority, sizeof(w->current)) == 0;
}

static inktag_public_cache_result_t retire(
    const inktag_public_cache_io_t *io, inktag_public_cache_workspace_t *w, uint8_t *removed) {
    cache_shape_t *plan = w->plan;
    uint8_t *value = w->value;
    bool invalid = false, io_error = false;
    if (removed) *removed = 0;
    if (!io || !io->allowed || !io->authority || !io->read || !io->remove ||
        !io->allowed(io->context) || !io->authority(io->context, w->authority))
        return INKTAG_PUBLIC_CACHE_REFUSED;
    memset(plan, 0, sizeof(w->plan));
    for (uint8_t i = 0; i < INKTAG_PUBLIC_CACHE_RECORDS; ++i) {
        uint16_t length = sizeof(w->value);
        inktag_public_cache_read_t read = io->read(io->context, i, value, &length);
        if (read == INKTAG_PUBLIC_CACHE_IO) io_error = true;
        else if (read == INKTAG_PUBLIC_CACHE_PRESENT &&
                 !classify(i, value, length, &plan[i])) invalid = true;
        else if (read != INKTAG_PUBLIC_CACHE_PRESENT &&
                 read != INKTAG_PUBLIC_CACHE_ABSENT) io_error = true;
    }
    if (io_error) return INKTAG_PUBLIC_CACHE_IO_ERROR;
    if (invalid) return INKTAG_PUBLIC_CACHE_INVALID;
    /* Present manifests bind their chunks. Absent manifests permit fully
     * validated self-contained orphans, including interrupted retirement. */
    for (uint8_t bank = 0; bank < 2; ++bank) {
        const cache_shape_t *m = &plan[bank*8u];
        if (!m->length) continue;
        uint16_t available = 0u;
        uint32_t keys_crc = UINT32_MAX;
        for (uint8_t index = 0; index < 7; ++index) {
            const cache_shape_t *c = &plan[bank*8u+index+1u];
            if (!c->length) continue;
            if (m->state == 3u || c->generation != m->generation || c->upload != m->upload)
                return INKTAG_PUBLIC_CACHE_INVALID;
            for (uint8_t part = 0; part < 2; ++part) {
                if (!(c->mask & (1u << part))) continue;
                uint8_t first = (uint8_t)(index*16u+part*8u);
                uint8_t remaining = (uint8_t)(m->keys-first);
                if (first >= m->keys || c->counts[part] !=
                    (remaining > 8u ? 8u : remaining))
                    return INKTAG_PUBLIC_CACHE_INVALID;
                available |= (uint16_t)(1u << (index*2u+part));
            }
            if (m->state == 2u) {
                uint16_t length = sizeof(w->value);
                if (io->read(io->context, (uint8_t)(bank*8u+index+1u), value, &length)
                        != INKTAG_PUBLIC_CACHE_PRESENT)
                    return INKTAG_PUBLIC_CACHE_IO_ERROR;
                if (!classify((uint8_t)(bank*8u+index+1u), value, length, &w->shape) ||
                    memcmp(&w->shape, c, sizeof(w->shape))) return INKTAG_PUBLIC_CACHE_INVALID;
                keys_crc = inktag_crc32_update(keys_crc, value+20u,
                    (uint16_t)(c->counts[0]+c->counts[1])*28u);
            }
        }
        if (m->received & ~available) return INKTAG_PUBLIC_CACHE_INVALID;
        if (m->state == 2u && (keys_crc ^ UINT32_MAX) != m->keys_crc)
            return INKTAG_PUBLIC_CACHE_INVALID;
    }
    if (!unchanged(io, w)) return INKTAG_PUBLIC_CACHE_REFUSED;
    /* Manifests first: a power cut never leaves a manifest claiming a deleted
     * chunk, and retry needs no special persistent migration journal. */
    for (uint8_t order = 0; order < INKTAG_PUBLIC_CACHE_RECORDS; ++order) {
        uint8_t i = order < 2u ? (uint8_t)(order*8u) :
            (order < 9u ? (uint8_t)(order-1u) : order);
        if (!plan[i].length) continue;
        uint16_t length = sizeof(w->value);
        if (!unchanged(io, w)) return INKTAG_PUBLIC_CACHE_REFUSED;
        inktag_public_cache_read_t read = io->read(io->context, i, value, &length);
        if (read == INKTAG_PUBLIC_CACHE_IO) return INKTAG_PUBLIC_CACHE_IO_ERROR;
        if (read != INKTAG_PUBLIC_CACHE_PRESENT || !classify(i, value, length, &w->shape) ||
            memcmp(&w->shape, &plan[i], sizeof(w->shape))) return INKTAG_PUBLIC_CACHE_INVALID;
        io->remove(io->context, i);
        length = sizeof(w->value);
        if (io->read(io->context, i, value, &length) != INKTAG_PUBLIC_CACHE_ABSENT)
            return INKTAG_PUBLIC_CACHE_IO_ERROR;
        if (removed) ++*removed;
        if (!unchanged(io, w)) return INKTAG_PUBLIC_CACHE_REFUSED;
    }
    return INKTAG_PUBLIC_CACHE_OK;
}

inktag_public_cache_result_t inktag_public_cache_retire(
    const inktag_public_cache_io_t *io, inktag_public_cache_workspace_t *workspace,
    uint8_t *removed) {
    if (removed) *removed = 0u;
    if (!workspace) return INKTAG_PUBLIC_CACHE_REFUSED;
    inktag_public_cache_result_t result = retire(io, workspace, removed);
    /* No rolling-key or retired public material remains in a lent GUI buffer. */
    volatile uint8_t *p = (volatile uint8_t *)workspace;
    for (unsigned i=0; i<sizeof(*workspace); ++i) p[i] = 0u;
    return result;
}
