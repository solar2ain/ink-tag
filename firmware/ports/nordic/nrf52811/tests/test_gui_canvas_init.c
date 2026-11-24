/* Actual InkTag GUI constructor versus the unchanged public graphics APIs. */

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "inktag_gui.c"
static bool fail_alloc;
void *gui_test_malloc(size_t size) {return fail_alloc ? NULL:calloc(1,size);}
static void previous(Adafruit_GFX *g,int w,int h,int ph,int color,uint8_t *buf,unsigned n) {
    if(color==2) {if(buf)GFX_begin_3c_with_buffer(g,w,h,ph,buf,n);else GFX_begin_3c(g,w,h,ph);}
    else if(color==3) {if(buf)GFX_begin_4c_with_buffer(g,w,h,ph,buf,n);else GFX_begin_4c(g,w,h,ph);}
    else {if(buf)GFX_begin_with_buffer(g,w,h,ph,buf,n);else GFX_begin(g,w,h,ph);}
}
int main(void) {
    uint8_t old_buffer[1024],new_buffer[1024];
    const unsigned capacities[]={0,1,13,32,128,512,1024};
    const unsigned colors[]={0,1,2,3,4,10};
    for(unsigned fail=0;fail<2;fail++)for(unsigned external=0;external<2;external++)
    for(unsigned c=0;c<6;c++)for(int ph=0;ph<=17;ph++)for(unsigned n=0;n<7;n++) {
        Adafruit_GFX a,b; fail_alloc=fail;
        memset(old_buffer,0xa5,sizeof old_buffer);memset(new_buffer,0xa5,sizeof new_buffer);
        previous(&a,200,37,ph,colors[c],external?old_buffer:NULL,capacities[n]);
        gui_begin_canvas(&b,200,37,ph,colors[c],external?new_buffer:NULL,capacities[n]);
        assert(!!a.buffer==!!b.buffer && !!a.color==!!b.color);
        assert((a.color && a.buffer ? a.color-a.buffer:0)==(b.color && b.buffer?b.color-b.buffer:0));
        Adafruit_GFX aa=a,bb=b;aa.buffer=bb.buffer=aa.color=bb.color=NULL;
        assert(memcmp(&aa,&bb,sizeof aa)==0);
        if(a.buffer) {
            GFX_fillScreen(&a,GFX_WHITE);GFX_fillScreen(&b,GFX_WHITE);
            unsigned size=((a.WIDTH+7)/8)*a.page_height*(a.color?2:1);
            assert(memcmp(a.buffer,b.buffer,size)==0);
        }
        GFX_end(&a);GFX_end(&b);
    }
}
