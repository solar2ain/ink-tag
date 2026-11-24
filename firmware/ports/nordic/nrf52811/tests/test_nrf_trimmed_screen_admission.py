"""Compile the shipped screen resolver with the trimmed target's switches."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "inktag_epd_screen.h"
static epd_screen_config_t make(uint8_t driver,uint8_t recipe,uint8_t color,unsigned w,unsigned h){
 epd_screen_config_t c={.schema_version=1,.driver_id=driver,.panel_recipe=recipe,.color_id=color,
 .width=w,.height=h,.visible_width=w,.visible_height=h};return c;
}
int main(void){
 epd_screen_t out;
 epd_screen_config_t c=make(EPD_SCREEN_DRIVER_SSD1619,INKTAG_RECIPE_SSD1619_HEMA_213_BWR,EPD_SCREEN_COLOR_BWR,128,250);
 c.visible_width=122;c.transform=3;assert(epd_screen_resolve(&c,&out)==EPD_SCREEN_STATUS_OK);
 const unsigned big[][5]={
  {EPD_SCREEN_DRIVER_SSD1619,0,EPD_SCREEN_COLOR_BWR,400,300},
  {EPD_SCREEN_DRIVER_SSD1683,0,EPD_SCREEN_COLOR_BWR,400,300},
  {EPD_SCREEN_DRIVER_JD79661,0,EPD_SCREEN_COLOR_BWRY,400,300},
  {EPD_SCREEN_DRIVER_JD79665AA,INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0,EPD_SCREEN_COLOR_BWRY,768,552},
  {EPD_SCREEN_DRIVER_JD79665AA,INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1,EPD_SCREEN_COLOR_BWRY,768,552},
  {EPD_SCREEN_DRIVER_UC8176,0,EPD_SCREEN_COLOR_BWR,800,480},
  {EPD_SCREEN_DRIVER_UC8179,0,EPD_SCREEN_COLOR_BWR,960,680},
  {EPD_SCREEN_DRIVER_SSD1677,0,EPD_SCREEN_COLOR_BWR,960,680}};
 for(unsigned i=0;i<sizeof(big)/sizeof(big[0]);++i){
  c=make(big[i][0],big[i][1],big[i][2],big[i][3],big[i][4]);
  assert(epd_screen_resolve(&c,&out)==EPD_SCREEN_STATUS_OK);
 }
 for(unsigned side=152;side<=200;side+=48){
  c=make(EPD_SCREEN_DRIVER_SSD1619,0,EPD_SCREEN_COLOR_BWR,side,side);
  assert(epd_screen_resolve(&c,&out)==EPD_SCREEN_STATUS_OK);
 }
 c=make(EPD_SCREEN_DRIVER_SSD1619,0,EPD_SCREEN_COLOR_BWR,240,240);
 assert(epd_screen_resolve(&c,&out)==EPD_SCREEN_STATUS_OK);
 c=make(EPD_SCREEN_DRIVER_SSD1675,0,EPD_SCREEN_COLOR_BWR,152,152);
 assert(epd_screen_resolve(&c,&out)!=EPD_SCREEN_STATUS_OK);
 const unsigned excluded[][5]={
  {EPD_SCREEN_DRIVER_UC8151,0,EPD_SCREEN_COLOR_BWR,104,212},
  {EPD_SCREEN_DRIVER_UC8159,0,EPD_SCREEN_COLOR_BWR,640,384},
  {EPD_SCREEN_DRIVER_JD79665CA,INKTAG_RECIPE_JD79665CA_LENMORY_H364F067_E6,EPD_SCREEN_COLOR_BWRYGB,760,568},
  {EPD_SCREEN_DRIVER_SSD1677,0,EPD_SCREEN_COLOR_GRAY4,800,480}};
 for(unsigned i=0;i<sizeof(excluded)/sizeof(excluded[0]);++i){
  c=make(excluded[i][0],excluded[i][1],excluded[i][2],excluded[i][3],excluded[i][4]);
  assert(epd_screen_resolve(&c,&out)!=EPD_SCREEN_STATUS_OK);
 }
 puts("PASS original2.13, 400x300 BWR/BWRY, A0/A1, large and shared 152/200/240 remain admitted; dedicated SSD1675 rejected");
}
'''
with tempfile.TemporaryDirectory() as d:
    out=Path(d);c=out/'main.c';c.write_text(SOURCE)
    subprocess.run(['cc','-std=gnu99','-O2',
        '-DINKTAG_EPD_ENABLE_SSD1675=0','-DINKTAG_EPD_ENABLE_UC8151=0','-DINKTAG_EPD_ENABLE_UC8159=0','-DINKTAG_EPD_ENABLE_SSD1677_GRAY4=0',
        '-I'+str(ROOT/'compat/common/epd'),str(c),str(ROOT/'compat/common/epd/inktag_epd_screen.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test')],check=True)
