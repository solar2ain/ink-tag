"""Pulse arithmetic must retain the fixed pre-compression timer truth table."""
import os
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
DRIVER=r'''
#include <assert.h>
#include <stdio.h>
#include "inktag_status.c"
static bool prior_effect_on(uint8_t effect, uint32_t elapsed_ms) {
    switch (effect) {
        case INKTAG_STATUS_EFFECT_SOLID: return true;
        case INKTAG_STATUS_EFFECT_SLOW_FLASH:
            return (elapsed_ms % (SLOW_FLASH_HALF_MS * 2u)) < SLOW_FLASH_HALF_MS;
        case INKTAG_STATUS_EFFECT_FAST_FLASH:
            return (elapsed_ms % (FAST_FLASH_HALF_MS * 2u)) < FAST_FLASH_HALF_MS;
        case INKTAG_STATUS_EFFECT_FLASH_ONCE:
            return elapsed_ms < FAST_FLASH_HALF_MS;
        case INKTAG_STATUS_EFFECT_FLASH_TWICE:
            return elapsed_ms < FAST_FLASH_HALF_MS * 4u &&
                   (elapsed_ms % (FAST_FLASH_HALF_MS * 2u)) < FAST_FLASH_HALF_MS;
        case INKTAG_STATUS_EFFECT_FLASH_THREE:
            return elapsed_ms < FAST_FLASH_HALF_MS * 6u &&
                   (elapsed_ms % (FAST_FLASH_HALF_MS * 2u)) < FAST_FLASH_HALF_MS;
        default: return false;
    }
}

static uint32_t prior_transient_duration(const inktag_status_runtime_t *runtime,
                                   uint8_t value, uint8_t scene) {
    switch (INKTAG_STATUS_SCENE_EFFECT(value)) {
        case INKTAG_STATUS_EFFECT_FLASH_ONCE: return FAST_FLASH_HALF_MS * 2u;
        case INKTAG_STATUS_EFFECT_FLASH_TWICE: return FAST_FLASH_HALF_MS * 4u;
        case INKTAG_STATUS_EFFECT_FLASH_THREE: return FAST_FLASH_HALF_MS * 6u;
        case INKTAG_STATUS_EFFECT_SEQUENCE_SLOW:
            if (scene == INKTAG_STATUS_SCENE_STARTUP) {
                return INKTAG_STATUS_STARTUP_COLOR_MS *
                    sequence_color_count(INKTAG_STATUS_SCENE_COLOR(value),
                                         runtime->available_colors);
            }
            return SLOW_FLASH_HALF_MS * 6u;
        default: return TRANSIENT_DEFAULT_MS;
    }
}
static bool prior_periodic_tick(const inktag_status_runtime_t *runtime,
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

    selected = priority_scene(runtime, transient);
    selected_is_transient = selected == transient;
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

int main(void) {
 inktag_status_runtime_t r={0};
 for(unsigned effect=0;effect<256;effect++) {
  for(uint32_t ms=0;ms<2400;ms++) assert(effect_on(effect,ms)==prior_effect_on(effect,ms));
  const uint32_t clocks[]={UINT32_MAX,UINT32_MAX-1,UINT32_MAX-120,UINT32_MAX-240,1u<<31};
  for(unsigned n=0;n<sizeof(clocks)/sizeof(clocks[0]);n++) assert(effect_on(effect,clocks[n])==prior_effect_on(effect,clocks[n]));
 }
 for(unsigned available=0;available<8;available++)
  for(unsigned value=0;value<256;value++)
   for(unsigned scene=0;scene<256;scene++) {
    r.available_colors=available;
    assert(transient_duration(&r,value,scene)==prior_transient_duration(&r,value,scene));
   }
 const uint32_t origins[]={0u, UINT32_MAX-120u};
 for(unsigned available=0;available<8;available++)
  for(unsigned scene=0;scene<INKTAG_STATUS_SCENE_COUNT;scene++)
   for(unsigned policy=0;policy<4;policy++)
    for(unsigned origin=0;origin<2;origin++)
     for(uint32_t elapsed=0;elapsed<2400;elapsed++) {
      memset(&r,0,sizeof(r));
      r.config.policy=policy; r.active_mask=1u<<scene;
      r.available_colors=available; r.transient_scene=NO_SCENE;
      r.current_scene=scene; r.scene_started_at=origins[origin];
      inktag_status_runtime_t before=r;
      uint32_t now=origins[origin]+elapsed;
      assert(inktag_status_needs_periodic_tick(&r,now)==prior_periodic_tick(&r,now));
      assert(!memcmp(&r,&before,sizeof(r)));
     }
 puts("PASS 1689600 read-only timer cases including every ms around pulse boundaries and wrap");
 puts("PASS 615680 pulse output cases and 524288 transient duration cases against frozen cfff3e4 functions");
}
'''
with tempfile.TemporaryDirectory() as temp:
 out=Path(temp);(out/'driver.c').write_text(DRIVER,encoding='utf-8')
 for single in (0,1):
  subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-Wall','-Werror',f'-DINKTAG_STATUS_SINGLE_QUALIFIED_COLOR={single}','-I'+str(ROOT/'compat/common/status'),str(out/'driver.c'),'-o',str(out/'test')],check=True)
  subprocess.run([str(out/'test')],check=True)
