"""Execute shipping HH:MM clock/segment drawing; chrome is a host boundary."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(text, name):
    found = re.search(r'^static[^\n]+?\b' + name + r'\([^;{]*\)\s*\{', text, re.M)
    assert found, name
    depth, end = 1, found.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[found.start():end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include "inktag_gui_fixed_layout.h"
#define V1_TEXT_SHARED
typedef struct {int unused;} Adafruit_GFX;
typedef struct {uint8_t tm_hour,tm_min,tm_mday;} tm_t;
struct Lunar_Date {int unused;};
typedef struct {uint16_t width,height;} gui_data_t;
typedef struct {int unused;} gui_countdown_t;
typedef struct {int x,y,width,height;} v1_frame_t;
typedef enum {V1_LAYOUT_COMPACT_SQUARE,V1_LAYOUT_COMPACT_RECT,V1_LAYOUT_WIDE} v1_layout_family_t;
typedef struct {v1_layout_family_t family;} v1_layout_preset_t;
#define V1_BLACK 1u
#define V1_ALIGN_CENTER 0u
typedef struct {int left,right;} box_t;
static box_t all,hours,minutes;
static unsigned colon_count;
static int colon_x[2],colon_y[2],colon_side;
static void extend(box_t *box,int left,int right) {
    if(left<box->left)box->left=left;
    if(right>box->right)box->right=right;
}
static void stroke(int x,int width) {
    assert(width>0);extend(&all,x,x+width-1);
    extend(colon_count?&minutes:&hours,x,x+width-1);
}
static void GFX_drawFastHLine(Adafruit_GFX *g,int x,int y,int w,unsigned c) {
    (void)g;(void)y;assert(c==V1_BLACK);stroke(x,w);
}
static void GFX_drawFastVLine(Adafruit_GFX *g,int x,int y,int h,unsigned c) {
    (void)g;(void)y;assert(h>0&&c==V1_BLACK);stroke(x,1);
}
static void GFX_fillRect(Adafruit_GFX *g,int x,int y,int w,int h,unsigned c) {
    (void)g;assert(c==V1_BLACK&&w==h&&colon_count<2);
    colon_x[colon_count]=x;colon_y[colon_count++]=y;colon_side=w;extend(&all,x,x+w-1);
}
static bool GFX_pageIntersectsLogicalY(Adafruit_GFX *g,int y,int h) {(void)g;(void)y;(void)h;return true;}
/* Only chrome/layout IO is stubbed. Size fitting, digit positions, stroke
 * rasterization and fixed-cell placement execute the actual production functions. */
static v1_layout_preset_t v1_layout_preset(const gui_data_t *d) {
    return (v1_layout_preset_t){d->width<300 || d->height<300 ? V1_LAYOUT_COMPACT_RECT:V1_LAYOUT_WIDE};
}
static int v1_power_height(const gui_data_t *d) {(void)d;return 18;}
static int v1_power_inset(const gui_data_t *d) {(void)d;return 12;}
static v1_frame_t v1_wide_chrome(Adafruit_GFX *g,tm_t *t,struct Lunar_Date *l,const gui_data_t *d,bool clock,const gui_countdown_t *c) {
    (void)g;(void)t;(void)l;(void)clock;(void)c;return(v1_frame_t){12,40,(int)d->width-24,(int)d->height-60};
}
static void v1_compact_header(Adafruit_GFX *g,const gui_data_t *d,tm_t *t) {(void)g;(void)d;(void)t;}
static void v1_compact_footer(Adafruit_GFX *g,tm_t *t,struct Lunar_Date *l,const gui_data_t *d) {(void)g;(void)t;(void)l;(void)d;}
'''

TEST = r'''
static void reset(void) {
    all=hours=minutes=(box_t){INT_MAX,INT_MIN};colon_count=0;
}
int main(void) {
    Adafruit_GFX gfx={0};struct Lunar_Date lunar={0};tm_t time={0};
    const unsigned dimensions[][2]={{152,152},{200,200},{212,104},{250,128},{296,152},{400,300},{768,552}};
    for(unsigned d=0;d<sizeof(dimensions)/sizeof(*dimensions);++d) {
        gui_data_t data={dimensions[d][0],dimensions[d][1]};
        int fixed_colon=-1,fixed_y[2]={0};
        for(unsigned hour=0;hour<24;++hour)for(unsigned minute=0;minute<60;++minute) {
            time.tm_hour=hour;time.tm_min=minute;reset();
            v1_clock(&gfx,&time,&lunar,&data,NULL);assert(colon_count==2);
            int size=colon_side/2,maximum=v1_time_width(size);
            int left=((int)data.width-maximum)/2;
            assert(all.left>=left && all.right<left+maximum);
            assert(hours.right-hours.left+1>9*size+1); /* Hours always have two cells. */
            assert(minutes.right-minutes.left+1>9*size+1); /* Includes 01. */
            if(fixed_colon<0) {
                fixed_colon=colon_x[0];fixed_y[0]=colon_y[0];fixed_y[1]=colon_y[1];
            } else assert(colon_x[0]==fixed_colon&&colon_x[1]==fixed_colon&&colon_y[0]==fixed_y[0]&&colon_y[1]==fixed_y[1]);
        }
    }
    reset();v1_seven_number(&gfx,1,10,10,3,3,V1_BLACK);
    assert(hours.right-hours.left+1>9*3+1); /* Calendar 01 remains two cells. */
    puts("PASS actual nRF HH:MM clock: two-digit hours/minutes, fixed cells/colon and bounds including square fallback, date01");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    source = (ROOT / 'compat/common/gui/inktag_gui_default.inc').read_text(encoding='utf-8')
    code = PREFIX + '\n'.join(function(source, name) for name in
                              ('v1_min','v1_max','v1_time_width','v1_seven_center','v1_seven_number','v1_clock_height_size','v1_clock_field_in_body','v1_clock')) + TEST
    with tempfile.TemporaryDirectory() as temporary:
        directory = args.evidence_dir or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        unit = directory / 'actual-nrf-clock.c'
        unit.write_text(code, encoding='utf-8')
        compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('cc')
        if not compiler:
            raise RuntimeError('Actual clock C fixture emitted; host compiler unavailable, regression blocked.')
        for name, flags in (('strict', ['-O2']), ('asan-ubsan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'])):
            binary = directory / ('actual-nrf-clock-' + name)
            subprocess.run([compiler, '-std=c99', '-Ifirmware/common/gui', '-Wall', '-Wextra', '-Werror', *flags,
                            str(unit), '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
