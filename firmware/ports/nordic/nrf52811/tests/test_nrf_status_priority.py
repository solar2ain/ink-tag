"""Frozen 38480cc status oracle: sharing priority selection must be equivalent."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
ORACLE = r'''
static uint8_t prior_selected_scene(inktag_status_runtime_t *runtime, uint32_t now_ms) {
    uint8_t transient = runtime->transient_scene;
    if ((runtime->active_mask & LOCATE_ACTIVE) != 0u) {
        if (runtime->current_scene != LOCATE_SCENE ||
            now_ms - runtime->scene_started_at <
                INKTAG_STATUS_LOCATE_DURATION_MS) {
            return LOCATE_SCENE;
        }
        runtime->active_mask &= (uint16_t)~LOCATE_ACTIVE;
    }
    if (runtime->transient_scene != NO_SCENE) {
        if (!before(now_ms, runtime->transient_until)) {
            uint8_t expired = runtime->transient_scene;
            runtime->transient_scene = NO_SCENE;
            transient = NO_SCENE;
            if (expired == INKTAG_STATUS_SCENE_STARTUP &&
                (runtime->active_mask & READY_PENDING_MASK) != 0u) {
                runtime->transient_scene = READY_TAIL_SCENE;
                runtime->transient_until = now_ms +
                    INKTAG_STATUS_READY_HALF_MS * 7u;
                transient = READY_TAIL_SCENE;
            } else if (expired == READY_TAIL_SCENE) {
                runtime->active_mask &= (uint16_t)~READY_PENDING_MASK;
            }
        }
    }
    for (uint8_t i = 0; i < INKTAG_STATUS_SCENE_COUNT; i++) {
        uint8_t scene = s_priority[i];
        if (!policy_allows_scene(runtime->config.policy, scene)) continue;
        /* Transient feedback participates in the same priority order as an
         * active scene. In particular, a just-connected flash must not hide a
         * transfer that starts before the status timer gets another turn. */
        if (scene == transient) return scene;
        if (scene == INKTAG_STATUS_SCENE_STARTUP &&
            transient == READY_TAIL_SCENE) return READY_TAIL_SCENE;
        if ((runtime->active_mask & (1u << scene)) != 0u) return scene;
    }
    return NO_SCENE;
}

bool prior_query(const inktag_status_runtime_t *runtime,
                                       uint32_t now_ms) {
    uint8_t transient;
    uint8_t selected = NO_SCENE;
    bool selected_is_transient = false;
    uint8_t value;
    uint8_t effect;
    uint32_t elapsed;

    if (!runtime) return false;
    /* The timer owns the exact 72-second locator stop boundary,
     * including boards with no LED channels. */
    if (inktag_status_locate_active(runtime)) return true;
    if (runtime->config.policy == INKTAG_STATUS_POLICY_OFF ||
        runtime->available_colors == 0u) return false;

    transient = runtime->transient_scene;
    if (transient != NO_SCENE && !before(now_ms, runtime->transient_until)) {
        if (transient == INKTAG_STATUS_SCENE_STARTUP &&
            (runtime->active_mask & READY_PENDING_MASK) != 0u) {
            /* One service turn is still required to enter the queued role
             * tail; the query itself remains read-only. */
            return true;
        }
        transient = NO_SCENE;
    }

    for (uint8_t i = 0; i < INKTAG_STATUS_SCENE_COUNT; i++) {
        uint8_t scene = s_priority[i];
        if (!policy_allows_scene(runtime->config.policy, scene)) continue;
        if (scene == transient) {
            selected = scene;
            selected_is_transient = true;
            break;
        }
        if (scene == INKTAG_STATUS_SCENE_STARTUP &&
            transient == READY_TAIL_SCENE) {
            selected = READY_TAIL_SCENE;
            selected_is_transient = true;
            break;
        }
        if ((runtime->active_mask & (1u << scene)) != 0u) {
            selected = scene;
            break;
        }
    }
    if (selected == NO_SCENE) return false;

    value = selected == READY_TAIL_SCENE
        ? INKTAG_STATUS_SCENE_VALUE(ready_tail_color(runtime),
                                    INKTAG_STATUS_EFFECT_FLASH_THREE)
        : scene_value(runtime, selected);
    if (map_color(INKTAG_STATUS_SCENE_COLOR(value),
                  runtime->available_colors) == 0u) return false;
    effect = INKTAG_STATUS_SCENE_EFFECT(value);
    if (effect == INKTAG_STATUS_EFFECT_OFF) return false;
    if (selected_is_transient) return true;
    if (effect == INKTAG_STATUS_EFFECT_SOLID) return false;
    if (effect == INKTAG_STATUS_EFFECT_SLOW_FLASH ||
        effect == INKTAG_STATUS_EFFECT_FAST_FLASH ||
        effect == INKTAG_STATUS_EFFECT_SEQUENCE_SLOW) return true;

    elapsed = now_ms - runtime->scene_started_at;
    if (effect == INKTAG_STATUS_EFFECT_FLASH_ONCE)
        return elapsed < FAST_FLASH_HALF_MS * 2u;
    if (effect == INKTAG_STATUS_EFFECT_FLASH_TWICE)
        return elapsed < FAST_FLASH_HALF_MS * 4u;
    if (effect == INKTAG_STATUS_EFFECT_FLASH_THREE)
        return elapsed < FAST_FLASH_HALF_MS * 6u;
    return false;
}
'''
DRIVER = r'''
#include <assert.h>
#include <stdio.h>
#include "inktag_status.c"
''' + ORACLE + r'''
static uint32_t random_state=0xf07ee123u;
static uint32_t random_value(void) { random_state=random_state*1664525u+1013904223u; return random_state; }
int main(void) {
    assert(!inktag_status_needs_periodic_tick(NULL,0u));
    const uint8_t transients[]={0,1,2,3,4,5,6,7,8,9,10,READY_TAIL_SCENE,NO_SCENE};
    for (unsigned mask=0; mask<65536; mask++)
        for (unsigned phase=0;phase<32;phase++) {
            uint32_t now=random_value();
            inktag_status_runtime_t r={0}, prior, before_query;
            r.active_mask=mask; r.config.policy=(uint8_t)(phase%4);
            r.available_colors=(uint8_t)(phase%8);
            r.transient_scene=transients[(mask+phase)%sizeof(transients)];
            r.current_scene=transients[random_value()%sizeof(transients)];
            if (phase&1) r.current_scene=LOCATE_SCENE;
            r.scene_started_at=now-(random_value()%73000);
            const int32_t delta[]={-1,0,1,119,120,239,240,479,480,719,720,839,840,1079,1080};
            r.transient_until=now+delta[phase%15];
            before_query=r;
            assert(inktag_status_needs_periodic_tick(&r,now)==prior_query(&r,now));
            assert(!memcmp(&r,&before_query,sizeof(r)));
            prior=r;
            assert(selected_scene(&r,now)==prior_selected_scene(&prior,now));
            assert(!memcmp(&r,&prior,sizeof(r)));
        }
    puts("PASS 2097152 fixed status oracle cases: priorities, policy, wrap, expiry, ready tails, locator and read-only query");
}
'''
with tempfile.TemporaryDirectory() as temp:
    out=Path(temp)
    (out/'driver.c').write_text(DRIVER,encoding='utf-8')
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-Wall','-Werror',
                    '-I'+str(ROOT/'compat/common/status'),str(out/'driver.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test')],check=True)
