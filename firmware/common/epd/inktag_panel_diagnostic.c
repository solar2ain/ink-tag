#include "inktag_panel_diagnostic.h"
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
#include <string.h>

inktag_status_v2_t inktag_panel_diag_uc_read_plan(uint8_t model,
    const inktag_panel_diag_request_t *r, inktag_panel_diag_read_plan_t *p) {
    uint16_t capacity;
    uint8_t revision;
    if (!r || !p) return INKTAG_STATUS_INVALID_REQUEST;
    memset(p, 0, sizeof(*p));
    switch (model) {
        case INKTAG_PANEL_DIAG_UC8151C:
        case INKTAG_PANEL_DIAG_UC8176: capacity = 4096; revision = 1; break;
        case INKTAG_PANEL_DIAG_UC8179: capacity = 6144; revision = 7; break;
        case INKTAG_PANEL_DIAG_UC8276: capacity = 6144; revision = 5; break;
        default: return INKTAG_STATUS_UNSUPPORTED;
    }
    if (!r->length || r->length > 64 || r->fixture ||
        (r->operation != INKTAG_PANEL_DIAG_OTP && r->offset))
        return INKTAG_STATUS_INVALID_REQUEST;
    switch (r->operation) {
        case INKTAG_PANEL_DIAG_REVISION:
            if (r->length != revision) return INKTAG_STATUS_INVALID_REQUEST;
            p->command = 0x70; break;
        case INKTAG_PANEL_DIAG_STATUS:
            if (r->length != 1) return INKTAG_STATUS_INVALID_REQUEST;
            p->command = 0x71; break;
        case INKTAG_PANEL_DIAG_OTP:
            if (r->offset > capacity - r->length) return INKTAG_STATUS_INVALID_REQUEST;
            p->command = 0xA2;
            p->skip = (uint16_t)(r->offset + 1u); /* One documented OTP dummy. */
            break;
        default: return INKTAG_STATUS_UNSUPPORTED;
    }
    p->length = r->length;
    return INKTAG_STATUS_OK;
}

inktag_status_v2_t inktag_panel_diag_ssd1680_read_plan(
    const inktag_panel_diag_request_t *r, inktag_panel_diag_read_plan_t *p) {
    if (!r || !p) return INKTAG_STATUS_INVALID_REQUEST;
    memset(p, 0, sizeof(*p));
    if (!r->length || r->length > 11u || r->fixture ||
        (r->operation != INKTAG_PANEL_DIAG_OTP && r->offset))
        return INKTAG_STATUS_INVALID_REQUEST;
    switch (r->operation) {
        case INKTAG_PANEL_DIAG_STATUS:
            if (r->length != 1u) return INKTAG_STATUS_INVALID_REQUEST;
            p->command = 0x2fu;
            break;
        case INKTAG_PANEL_DIAG_OTP:
            if (r->offset > 11u - r->length) return INKTAG_STATUS_INVALID_REQUEST;
            p->command = 0x2du;
            p->skip = r->offset; /* No dummy byte for this command. */
            break;
        /* No UC-style revision register; byte7..10 of 2D is waveform version,
         * not silicon revision. Keep REVISION unsupported rather than lie. */
        default: return INKTAG_STATUS_UNSUPPORTED;
    }
    p->length = r->length;
    return INKTAG_STATUS_OK;
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void write32(uint8_t *p, uint32_t v) {
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u * i));
}

inktag_status_v2_t inktag_panel_diag_read_plan(uint8_t model,
    const inktag_panel_diag_request_t *r, inktag_panel_diag_read_plan_t *p) {
    /* SSD1683 Rev1.0 R2F/R2D has this same bounded status/11-byte view.
     * This is not a complete OTP waveform dump or unique die identity. */
    if (model == INKTAG_PANEL_DIAG_SSD1680 || model == INKTAG_PANEL_DIAG_SSD1683)
        return inktag_panel_diag_ssd1680_read_plan(r, p);
    if (model != INKTAG_PANEL_DIAG_JD79665AA_A0)
        return inktag_panel_diag_uc_read_plan(model, r, p);
    if (!r || !p) return INKTAG_STATUS_INVALID_REQUEST;
    memset(p, 0, sizeof(*p));
    if (!r->length || r->length > 64u || r->fixture ||
        (r->operation != INKTAG_PANEL_DIAG_OTP && r->offset))
        return INKTAG_STATUS_INVALID_REQUEST;
    switch (r->operation) {
    case INKTAG_PANEL_DIAG_REVISION:
        if (r->length != 3u && r->length != 7u) return INKTAG_STATUS_INVALID_REQUEST;
        p->command = 0x70u; break;
    case INKTAG_PANEL_DIAG_TEMPERATURE:
        if (r->length != 2u) return INKTAG_STATUS_INVALID_REQUEST;
        p->command = 0x40u; break;
    case INKTAG_PANEL_DIAG_OTP:
        if ((uint32_t)r->offset + r->length > 4096u) return INKTAG_STATUS_INVALID_REQUEST;
        p->command = 0x92u; p->skip = (uint16_t)(r->offset + 1u); break;
    default: return INKTAG_STATUS_UNSUPPORTED;
    }
    p->length = r->length;
    return INKTAG_STATUS_OK;
}

inktag_status_v2_t inktag_panel_diag_describe(uint8_t model, uint8_t out[16]) {
    uint16_t mask = (uint16_t)(1u << INKTAG_PANEL_DIAG_DESCRIBE), capacity;
    uint8_t revision;
    if (!out) return INKTAG_STATUS_INVALID_REQUEST;
    switch (model) {
    case INKTAG_PANEL_DIAG_UC8151C:
    case INKTAG_PANEL_DIAG_UC8176: capacity = 4096u; revision = 1u; break;
    case INKTAG_PANEL_DIAG_UC8179: capacity = 6144u; revision = 7u; break;
    case INKTAG_PANEL_DIAG_UC8276: capacity = 6144u; revision = 5u; break;
    case INKTAG_PANEL_DIAG_SSD1680:
    case INKTAG_PANEL_DIAG_SSD1683: capacity = 11u; revision = 0u; break;
    case INKTAG_PANEL_DIAG_JD79665AA_A0: capacity = 4096u; revision = 7u; break;
    default: return INKTAG_STATUS_UNSUPPORTED;
    }
    memset(out, 0, 16u);
    mask |= (uint16_t)((1u << INKTAG_PANEL_DIAG_OTP) |
        (1u << (revision ? INKTAG_PANEL_DIAG_REVISION : INKTAG_PANEL_DIAG_STATUS)));
    if (model != INKTAG_PANEL_DIAG_JD79665AA_A0)
        mask |= (uint16_t)(1u << INKTAG_PANEL_DIAG_STATUS);
    else {
        mask |= (uint16_t)((1u << INKTAG_PANEL_DIAG_RESET) |
            (1u << INKTAG_PANEL_DIAG_TEMPERATURE) |
            (1u << INKTAG_PANEL_DIAG_POWER_CYCLE) |
            (1u << INKTAG_PANEL_DIAG_RAM_WRITE));
        out[7] = 2u; out[8] = 1u; /* temperature bytes; RAM write-only. */
        out[10] = 0u; out[11] = 3u; /* 768x552, LE16. */
        out[12] = 0x28u; out[13] = 2u;
    }
    out[0] = 1u; out[1] = model;
    out[2] = (uint8_t)mask; out[3] = (uint8_t)(mask >> 8);
    out[4] = (uint8_t)capacity; out[5] = (uint8_t)(capacity >> 8);
    out[6] = revision;
    return INKTAG_STATUS_OK;
}

enum { PROBE_RESET_HIGH=1, PROBE_RESET_LOW, PROBE_RESET_SETTLE,
       PROBE_READ, PROBE_INIT, PROBE_PON_ASSERT, PROBE_PON_RELEASE,
       PROBE_POF_ASSERT, PROBE_POF_RELEASE, PROBE_RAM, PROBE_DONE };
/* Observation flags: reset sequence, PON sent, POF sent, RAM sent,
 * RAM readback verified (never set by A0), board cutoff verified by port. */
static bool probe_write(inktag_panel_diag_probe_t *s,
    const inktag_panel_diag_io_t *io, uint8_t cmd, const uint8_t *p, uint8_t n) {
    s->last_command = cmd;
    if (!io->write(io->context, cmd, p, n)) { s->write_failures++; return false; }
    s->tx_bytes += 1u + n;
    return true;
}
static void probe_report(const inktag_panel_diag_probe_t *s,
    inktag_panel_diag_result_t *r) {
    r->length = 16u;
    r->data[0]=1u; r->data[1]=s->model; r->data[2]=s->phase; r->data[3]=s->flags;
    r->data[4]=s->initial_busy; r->data[5]=s->last_busy;
    r->data[6]=s->samples; r->data[7]=s->last_command;
    write32(r->data+8,s->tx_bytes); write32(r->data+12,s->write_failures);
}
static void probe_finish(inktag_panel_diag_probe_t *s,
    inktag_panel_diag_result_t *r, uint8_t status) {
    r->status = status;
    r->state = status ? INKTAG_PANEL_DIAG_FAILED : INKTAG_PANEL_DIAG_DONE;
    if (s->operation >= INKTAG_PANEL_DIAG_RESET &&
        s->operation != INKTAG_PANEL_DIAG_TEMPERATURE) probe_report(s,r);
}
inktag_status_v2_t inktag_panel_diag_probe_start(inktag_panel_diag_probe_t *s,
    uint8_t model, const inktag_panel_diag_request_t *r,
    const inktag_panel_diag_io_t *io, uint32_t now) {
    if (!s || !r || !io || !io->reset || !io->busy || !io->write ||
        !io->read_start || !io->read_poll) return INKTAG_STATUS_UNSUPPORTED;
    memset(s,0,sizeof(*s)); s->model=model; s->operation=r->operation;
    if (model != INKTAG_PANEL_DIAG_JD79665AA_A0) return INKTAG_STATUS_UNSUPPORTED;
    s->initial_busy=s->last_busy=io->busy(io->context);
    s->phase=PROBE_RESET_HIGH; s->mark_ms=now;
    return io->reset(io->context,true) ? INKTAG_STATUS_OK : INKTAG_STATUS_IO_ERROR;
}
static bool probe_pof(inktag_panel_diag_probe_t *s,
    const inktag_panel_diag_io_t *io, uint32_t now) {
    static const uint8_t zero=0u;
    s->flags |= 4u; /* Attempted once, including a refused/failed bus write. */
    s->phase=PROBE_POF_ASSERT; s->mark_ms=now;
    return probe_write(s,io,0x02u,&zero,1u);
}
void inktag_panel_diag_probe_poll(inktag_panel_diag_probe_t *s,
    const inktag_panel_diag_request_t *request, const inktag_panel_diag_io_t *io,
    uint32_t now, inktag_panel_diag_result_t *r) {
    uint32_t elapsed=now-s->mark_ms;
    inktag_panel_diag_read_plan_t plan;
    bool done=false;
    uint8_t written=0u;
    s->last_busy=io->busy(io->context);
    if (!s->last_busy && s->phase<=PROBE_RESET_SETTLE) s->samples|=1u;
    switch (s->phase) {
    case PROBE_RESET_HIGH:
        if (elapsed<50u) return;
        if (!io->reset(io->context,false)) break;
        s->phase=PROBE_RESET_LOW; s->mark_ms=now; return;
    case PROBE_RESET_LOW:
        if (elapsed<50u) return;
        if (!io->reset(io->context,true)) break;
        s->phase=PROBE_RESET_SETTLE; s->mark_ms=now; return;
    case PROBE_RESET_SETTLE:
        if (elapsed<50u) return;
        if (!s->last_busy) {
            if (elapsed<2000u) return;
            probe_finish(s,r,INKTAG_STATUS_TIMEOUT); return;
        }
        s->flags|=1u; r->gates|=1u;
        if (s->operation==INKTAG_PANEL_DIAG_RESET) {
            probe_finish(s,r,INKTAG_STATUS_OK); return;
        }
        if (s->operation==INKTAG_PANEL_DIAG_POWER_CYCLE ||
            s->operation==INKTAG_PANEL_DIAG_RAM_WRITE) {
            s->phase=PROBE_INIT; return;
        }
        if (inktag_panel_diag_read_plan(s->model,request,&plan)!=INKTAG_STATUS_OK ||
            !io->read_start(io->context,plan.command,plan.skip,plan.length)) break;
        s->last_command=plan.command; s->phase=PROBE_READ; return;
    case PROBE_READ:
        if (!io->read_poll(io->context,r->data+r->length,
                (uint8_t)(64u-r->length),&written,&done)) break;
        r->length=(uint8_t)(r->length+written);
        if (done) probe_finish(s,r,INKTAG_STATUS_OK);
        return;
    case PROBE_INIT: {
        static const uint8_t psr=0x0bu, geometry[]={3u,0u,2u,0x28u};
        if (!probe_write(s,io,0x00u,&psr,1u) ||
            !probe_write(s,io,0x61u,geometry,4u)) break;
        if (s->operation==INKTAG_PANEL_DIAG_RAM_WRITE) {s->phase=PROBE_RAM;return;}
        /* Only command the charge pump when the documented BUSY_N is idle. */
        if (!s->last_busy) {probe_finish(s,r,INKTAG_STATUS_INVALID_STATE);return;}
        s->flags|=2u;
        if (!probe_write(s,io,0x04u,NULL,0u)) break;
        s->phase=PROBE_PON_ASSERT; s->mark_ms=now; return;
    }
    case PROBE_PON_ASSERT:
        if (!s->last_busy) {s->samples|=2u;s->phase=PROBE_PON_RELEASE;return;}
        if (elapsed<500u) return;
        s->failure=INKTAG_STATUS_TIMEOUT; r->pon_ms=elapsed;
        if (!probe_pof(s,io,now)) break;
        return;
    case PROBE_PON_RELEASE:
        if (s->last_busy) {
            s->samples|=4u;r->gates|=2u;r->pon_ms=elapsed;
            if (!probe_pof(s,io,now)) break;
            return;
        }
        if (elapsed<5000u) return;
        r->pon_ms=elapsed;probe_finish(s,r,INKTAG_STATUS_TIMEOUT);return;
    case PROBE_POF_ASSERT:
        if (!s->last_busy) {s->samples|=8u;s->phase=PROBE_POF_RELEASE;return;}
        if (elapsed<500u) return;
        r->pof_ms=elapsed;probe_finish(s,r,INKTAG_STATUS_TIMEOUT);return;
    case PROBE_POF_RELEASE:
        if (s->last_busy) {
            s->samples|=16u;r->gates|=16u;r->pof_ms=elapsed;
            probe_finish(s,r,s->failure);return;
        }
        if (elapsed<5000u) return;
        r->pof_ms=elapsed;probe_finish(s,r,INKTAG_STATUS_TIMEOUT);return;
    case PROBE_RAM: {
        /* One fixed 768-pixel row, bounded stack, no R11/data-stop or DRF.
         * A0 has no qualified RAM readback command. Never set RAM gate4. */
        static const uint8_t window[]={0u,0u,2u,0xffu,0u,0u,0u,0u,1u};
        uint8_t row[192];memset(row,0x55,sizeof(row));
        if (!probe_write(s,io,0x83u,window,sizeof(window)) ||
            !probe_write(s,io,0x10u,row,sizeof(row))) break;
        s->flags|=8u;probe_finish(s,r,INKTAG_STATUS_OK);return;
    }
    default: break;
    }
    probe_finish(s,r,INKTAG_STATUS_IO_ERROR);
}
bool inktag_panel_diag_probe_stop(inktag_panel_diag_probe_t *s,
    const inktag_panel_diag_io_t *io, inktag_panel_diag_result_t *r) {
    bool ok=true;
    if ((s->flags&2u) && !(s->flags&4u) && io->busy(io->context)) {
        static const uint8_t zero=0u;
        s->flags|=4u;
        ok=probe_write(s,io,0x02u,&zero,1u);
    }
    /* BUSY-stuck PON cannot legally accept POF. Port must cut EN regardless. */
    if (s->operation>=INKTAG_PANEL_DIAG_RESET &&
        s->operation!=INKTAG_PANEL_DIAG_TEMPERATURE) probe_report(s,r);
    return ok;
}
void inktag_panel_diag_init(inktag_panel_diag_t *s, const inktag_panel_diag_backend_t *b) {
    memset(s, 0, sizeof(*s));
    if (b) s->backend = *b;
}
inktag_status_v2_t inktag_panel_diag_close(inktag_panel_diag_t *s) {
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (s->cleanup_failed) return INKTAG_STATUS_IO_ERROR;
    if (s->acquired) status = s->backend.cleanup(s->backend.context);
    if (status != INKTAG_STATUS_OK) {
        s->cleanup_failed = true;
        s->result.state = INKTAG_PANEL_DIAG_FAILED;
        s->result.status = (uint8_t)status;
        s->result.length = 0;
        return status;
    }
    s->acquired = false;
    s->token = 0;
    memset(&s->request, 0, sizeof(s->request));
    memset(&s->result, 0, sizeof(s->result));
    return INKTAG_STATUS_OK;
}
static void finish(inktag_panel_diag_t *s, uint8_t status) {
    inktag_status_v2_t cleanup_status = s->backend.cleanup(s->backend.context);
    if (cleanup_status != INKTAG_STATUS_OK) {
        s->cleanup_failed = true;
        status = (uint8_t)cleanup_status;
    }
    if (!s->cleanup_failed)
    s->acquired = false;
    s->result.status = status;
    s->result.state = status == INKTAG_STATUS_OK ? INKTAG_PANEL_DIAG_DONE : INKTAG_PANEL_DIAG_FAILED;
    if (status != INKTAG_STATUS_OK &&
        s->request.operation != INKTAG_PANEL_DIAG_RESET &&
        s->request.operation != INKTAG_PANEL_DIAG_POWER_CYCLE &&
        s->request.operation != INKTAG_PANEL_DIAG_RAM_WRITE) s->result.length = 0;
}
void inktag_panel_diag_tick(inktag_panel_diag_t *s, uint32_t now) {
    inktag_status_v2_t status;
    if (!s->token || s->cleanup_failed) return;
    if ((uint32_t)(now - s->opened_ms) >= 120000u) { inktag_panel_diag_close(s); return; }
    if (s->result.state != INKTAG_PANEL_DIAG_RUNNING) return;
    if ((uint32_t)(now - s->started_ms) >= 65000u) { finish(s, INKTAG_STATUS_TIMEOUT); return; }
    if ((uint32_t)(now - s->polled_ms) < 20u) return;
    s->polled_ms = now;
    status = s->backend.guard(s->backend.context, &s->request);
    if (status != INKTAG_STATUS_OK) { finish(s, (uint8_t)status); return; }
    s->backend.poll(s->backend.context, &s->result);
    if (s->result.length > INKTAG_PANEL_DIAG_MAX_DATA || s->result.status > INKTAG_STATUS_LOW_POWER) {
        finish(s, INKTAG_STATUS_IO_ERROR); return;
    }
    if (s->result.state == INKTAG_PANEL_DIAG_RUNNING) return;
    if (s->result.state != INKTAG_PANEL_DIAG_DONE ||
        s->result.length > INKTAG_PANEL_DIAG_MAX_DATA ||
        (s->request.operation != INKTAG_PANEL_DIAG_WAVEFORM &&
         s->result.length != s->request.length) ||
        (s->request.operation == INKTAG_PANEL_DIAG_WAVEFORM &&
         s->result.gates != INKTAG_PANEL_DIAG_WAVE_GATES)) {
        finish(s, s->result.status == INKTAG_STATUS_OK ? INKTAG_STATUS_IO_ERROR : s->result.status);
    } else finish(s, s->result.status);
}
inktag_status_v2_t inktag_panel_diag_dispatch(inktag_panel_diag_t *s, uint8_t opcode,
    const uint8_t *p, uint16_t n, uint32_t now, uint8_t *out, uint16_t cap, uint16_t *written) {
    uint32_t token;
    inktag_status_v2_t status;
    inktag_panel_diag_request_t request;
    if (!written) return INKTAG_STATUS_INVALID_REQUEST;
    *written = 0;
    if (!s || !p || !out || n < 6u || p[0] != INKTAG_PANEL_DIAG_OPERATION || p[1] != 1u)
        return INKTAG_STATUS_INVALID_REQUEST;
    if (!s->backend.guard || !s->backend.acquire || !s->backend.start ||
        !s->backend.poll || !s->backend.cleanup) return INKTAG_STATUS_UNSUPPORTED;
    token = read32(p + 2);
    if (!token) return INKTAG_STATUS_INVALID_REQUEST;
    inktag_panel_diag_tick(s, now);
    if (opcode == INKTAG_CMD_OPEN_DEBUG_SESSION) {
        if (n != 6u || cap < 6u) return INKTAG_STATUS_INVALID_REQUEST;
        if (s->token) return INKTAG_STATUS_BUSY;
        memset(&request, 0, sizeof(request));
        status = s->backend.guard(s->backend.context, &request);
        if (status != INKTAG_STATUS_OK) return status;
        s->token = token; s->opened_ms = now;
        memcpy(out, p, 6); *written = 6;
        return INKTAG_STATUS_OK;
    }
    if (token != s->token) return INKTAG_STATUS_DENIED;
    if (opcode == INKTAG_CMD_CLOSE_DEBUG_SESSION || opcode == INKTAG_CMD_ABORT_DEBUG_OPERATION) {
        if (n != 6u) return INKTAG_STATUS_INVALID_REQUEST;
        return inktag_panel_diag_close(s);
    }
    if (opcode == INKTAG_CMD_DEBUG_READ) {
        if (n != 6u || cap < 22u + s->result.length) return INKTAG_STATUS_INVALID_REQUEST;
        status = s->backend.guard(s->backend.context, &s->request);
        if (status != INKTAG_STATUS_OK) { inktag_panel_diag_close(s); return status; }
        memcpy(out, p, 6);
        out[6] = s->result.state; out[7] = s->result.status;
        out[8] = s->result.gates; out[9] = s->result.length;
        write32(out + 10, s->result.pon_ms); write32(out + 14, s->result.drf_ms);
        write32(out + 18, s->result.pof_ms);
        memcpy(out + 22, s->result.data, s->result.length);
        *written = (uint16_t)(22u + s->result.length); return INKTAG_STATUS_OK;
    }
    if (opcode != INKTAG_CMD_DEBUG_EXECUTE) return INKTAG_STATUS_UNSUPPORTED;
    if (s->cleanup_failed) return INKTAG_STATUS_IO_ERROR;
    if (n != 13u || cap < 6u || p[12] > 1u) return INKTAG_STATUS_INVALID_REQUEST;
    if (s->result.state != INKTAG_PANEL_DIAG_IDLE) return INKTAG_STATUS_INVALID_STATE;
    request.operation = p[6]; request.length = p[7];
    request.offset = (uint16_t)(p[8] | (uint16_t)p[9] << 8);
    request.fixture = (uint16_t)(p[10] | (uint16_t)p[11] << 8);
    if (request.operation < 1u || request.operation > 9u || request.length > 64u)
        return INKTAG_STATUS_INVALID_REQUEST;
    if (request.operation == INKTAG_PANEL_DIAG_WAVEFORM) {
        if (!p[12] || !request.fixture || request.length || request.offset)
            return INKTAG_STATUS_INVALID_REQUEST;
    } else if (request.operation >= INKTAG_PANEL_DIAG_DESCRIBE) {
        bool power = request.operation == INKTAG_PANEL_DIAG_POWER_CYCLE ||
                     request.operation == INKTAG_PANEL_DIAG_RAM_WRITE;
        if (p[12] != (uint8_t)power || request.fixture || request.offset ||
            request.length != (request.operation == INKTAG_PANEL_DIAG_TEMPERATURE ? 2u : 16u))
            return INKTAG_STATUS_INVALID_REQUEST;
    } else if (p[12] || request.fixture || !request.length ||
               (request.operation != INKTAG_PANEL_DIAG_OTP && request.offset))
        return INKTAG_STATUS_INVALID_REQUEST;
    status = s->backend.guard(s->backend.context, &request);
    if (status != INKTAG_STATUS_OK) return status;
    if (!s->backend.acquire(s->backend.context)) return INKTAG_STATUS_BUSY;
    s->acquired = true; s->request = request;
    s->result.state = INKTAG_PANEL_DIAG_RUNNING;
    s->started_ms = now; s->polled_ms = now;
    status = s->backend.start(s->backend.context, &request);
    if (status != INKTAG_STATUS_OK) { finish(s, (uint8_t)status); return status; }
    memcpy(out, p, 6); *written = 6; return INKTAG_STATUS_OK;
}
#endif
