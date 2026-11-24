"""Compare private catalog storage with the frozen wide-mask catalog, including retained optional recipes."""
import itertools
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
ORACLE = r'''
typedef struct {
    uint8_t driver_id;
    uint8_t panel_recipe;
    uint8_t transfer_format;
    uint8_t width_alignment;
    uint8_t geometry_rule;
    uint16_t color_mask;
    uint16_t max_width;
    uint16_t max_height;
} prior_driver_descriptor_t;

/*
 * This table replaces the old full-screen profile matrix. A row describes a
 * controller/recipe path and its electrical data contract; width, color and
 * presentation remain runtime configuration. Recipe zero is standard, while
 * named recipes own panel-specific reset, scan, MTP and packing behavior.
 */
static const prior_driver_descriptor_t prior_drivers[] = {
#if INKTAG_EPD_ENABLE_UC8XXX
    {EPD_SCREEN_DRIVER_UC8XXX, INKTAG_RECIPE_UC8XXX_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
    {EPD_SCREEN_DRIVER_UC8XXX, INKTAG_RECIPE_UC8XXX_HANSHOW_NOWA_213R_N,
     0, 8, GEOMETRY_HANSHOW_NOWA_213,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 128, 250},
#if INKTAG_EPD_ENABLE_UC8151
    {EPD_SCREEN_DRIVER_UC8151, INKTAG_RECIPE_UC8151_STANDARD,
     0, 8, GEOMETRY_UC8151, COLOR_BIT(EPD_SCREEN_COLOR_BWR), 152, 296},
    {EPD_SCREEN_DRIVER_UC8151, INKTAG_RECIPE_UC8151_SES_SE2213FS093,
     0, 8, GEOMETRY_SES_213, COLOR_BIT(EPD_SCREEN_COLOR_BWR), 104, 212},
    {EPD_SCREEN_DRIVER_UC8151, INKTAG_RECIPE_UC8151_SES_SE2266FS093,
     0, 8, GEOMETRY_SES_266,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 152, 296},
#endif
#if INKTAG_EPD_ENABLE_UC8159
    {EPD_SCREEN_DRIVER_UC8159, INKTAG_RECIPE_UC8159_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_4BPP, 2,
     GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
#endif
    {EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
    {EPD_SCREEN_DRIVER_UC8176, INKTAG_RECIPE_UC8176_ZKC42V_FACTORY,
     0, 8, GEOMETRY_ZKC42V,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 400, 300},
#if INKTAG_EPD_ENABLE_UC8179
    {EPD_SCREEN_DRIVER_UC8179, INKTAG_RECIPE_UC8179_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 960, 680},
#endif
    {EPD_SCREEN_DRIVER_UC8276, INKTAG_RECIPE_UC8276_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 800, 600},
    {EPD_SCREEN_DRIVER_UC8276, INKTAG_RECIPE_UC8276_ZKC42V_HE042AD7A2,
     0, 8, GEOMETRY_ZKC42V,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 400, 300},
#endif

#if INKTAG_EPD_ENABLE_SSD1XXX
    {EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 400, 300},
    {EPD_SCREEN_DRIVER_SSD1619, INKTAG_RECIPE_SSD1619_HEMA_213_BWR,
     0, 8, GEOMETRY_HEMA_213,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 128, 250},
#if INKTAG_EPD_ENABLE_SSD1675
    {EPD_SCREEN_DRIVER_SSD1675, INKTAG_RECIPE_SSD1675_STANDARD,
     0, 8, GEOMETRY_SSD1675, COLORS_BW_BWR, 152, 152},
    {EPD_SCREEN_DRIVER_SSD1675, INKTAG_RECIPE_SSD1675_HANSHOW_NOWA_154R_N,
     0, 8, GEOMETRY_SSD1675,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR), 152, 152},
#endif
#if INKTAG_EPD_ENABLE_SSD1677
    {EPD_SCREEN_DRIVER_SSD1677, INKTAG_RECIPE_SSD1677_STANDARD,
     0, 8, GEOMETRY_DYNAMIC,
     COLORS_BW_BWR
#if INKTAG_EPD_ENABLE_SSD1677_GRAY4
     | COLOR_BIT(EPD_SCREEN_COLOR_GRAY4)
#endif
     , 960, 680},
#endif
    {EPD_SCREEN_DRIVER_SSD1680, INKTAG_RECIPE_SSD1680_STANDARD,
     0, 8, GEOMETRY_SSD1680, COLORS_BW_BWR, 128, 250},
    {EPD_SCREEN_DRIVER_SSD1680, INKTAG_RECIPE_SSD1680_ZKC21V_FACTORY,
     0, 8, GEOMETRY_SSD1680,
     COLORS_BW_BWR, 128, 250},
#if INKTAG_EPD_ENABLE_SSD1683
    {EPD_SCREEN_DRIVER_SSD1683, INKTAG_RECIPE_SSD1683_STANDARD,
     0, 8, GEOMETRY_DYNAMIC, COLORS_BW_BWR, 400, 300},
    {EPD_SCREEN_DRIVER_SSD1683, INKTAG_RECIPE_SSD1683_ZKC42VM_FACTORY,
     0, 8, GEOMETRY_DYNAMIC,
     COLORS_BW_BWR, 400, 300},
#endif
#endif

#if INKTAG_EPD_ENABLE_JD79XXX
    {EPD_SCREEN_DRIVER_JD79661, INKTAG_RECIPE_JD79661_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_DYNAMIC,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 400, 300},
    {EPD_SCREEN_DRIVER_JD79661, INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_ZKC21B,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 128, 250},
    {EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4,
     GEOMETRY_DYNAMIC, COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 800, 600},
    {EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HUAWEI_398,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 768, 552},
    {EPD_SCREEN_DRIVER_JD79665AA, INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HUAWEI_398,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 768, 552},
#if INKTAG_EPD_ENABLE_JD79665CA
    {EPD_SCREEN_DRIVER_JD79665CA, INKTAG_RECIPE_JD79665CA_LENMORY_H364F067_E6,
     EPD_SCREEN_TRANSFER_INDEXED_2X2BPP, 4, GEOMETRY_LENMORY_H364F067,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRYGB), 760, 568},
#endif
    {EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_DYNAMIC,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     400, 300},
    {EPD_SCREEN_DRIVER_JD79668AA,
     INKTAG_RECIPE_JD79668AA_HANSHOW_NEBULAR_420R_N,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HANSHOW_NEBULAR_420,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     400, 300},
    {EPD_SCREEN_DRIVER_JD79668AA, INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_ZKC42B,
     COLOR_BIT(EPD_SCREEN_COLOR_BWRY), 400, 300},
    {EPD_SCREEN_DRIVER_JD79XXX, INKTAG_RECIPE_JD79XXX_STANDARD,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_DYNAMIC,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     800, 600},
    {EPD_SCREEN_DRIVER_JD79XXX,
     INKTAG_RECIPE_JD79XXX_HANSHOW_NEBULAR_213R_N,
     EPD_SCREEN_TRANSFER_INDEXED_2BPP, 4, GEOMETRY_HANSHOW_NOWA_213,
     COLOR_BIT(EPD_SCREEN_COLOR_BWR) | COLOR_BIT(EPD_SCREEN_COLOR_BWRY),
     128, 250},
#endif
};

'''
DRIVER = r'''
#include <assert.h>
#include <stdio.h>
#include "inktag_epd_screen.c"
''' + ORACLE + r'''
int main(void) {
 size_t rows=sizeof(s_drivers)/sizeof(s_drivers[0]);
 assert(rows==sizeof(prior_drivers)/sizeof(prior_drivers[0]));
 assert(sizeof(driver_descriptor_t)==(INKTAG_EPD_ENABLE_JD79665CA ? 12u : 10u));
 for(size_t i=0;i<rows;i++) {
  const driver_descriptor_t *actual=&s_drivers[i];
  const prior_driver_descriptor_t *prior=&prior_drivers[i];
  assert(actual->driver_id==prior->driver_id);
  assert(actual->panel_recipe==prior->panel_recipe);
  assert(actual->transfer_format==prior->transfer_format);
  assert(actual->width_alignment==prior->width_alignment);
  assert(actual->geometry_rule==prior->geometry_rule);
  assert(actual->color_mask==prior->color_mask);
  assert(actual->max_width==prior->max_width);
  assert(actual->max_height==prior->max_height);
  for(unsigned color=0;color<16;color++)
   assert((actual->color_mask & ((uint16_t)1u<<color))==(prior->color_mask & ((uint16_t)1u<<color)));
 }
 for(unsigned driver=0;driver<256;driver++)
  for(unsigned recipe=0;recipe<256;recipe++) {
   size_t index=rows;
   for(size_t i=0;i<rows;i++)
    if(prior_drivers[i].driver_id==driver && prior_drivers[i].panel_recipe==recipe) { index=i; break; }
   const driver_descriptor_t *actual=find_driver(driver,recipe);
   assert(actual==(index==rows?NULL:&s_drivers[index]));
  }
 printf("PASS %zu frozen catalog rows, all fields/16 mask bits and 65536 controller/recipe lookups\n",rows);
}
'''
# Exhaustively vary retained controller families; excluded engines are tested
# as rejected build configurations by test_source_boundary.py.
names=("UC8XXX", "SSD1XXX", "JD79XXX")
variants=[dict(zip(names,bits)) for bits in itertools.product((0,1),repeat=len(names))]
for name in ("UC8179", "SSD1677", "SSD1683"):
 for enabled in (0,1):
  variants.append({name:enabled})
with tempfile.TemporaryDirectory() as temp:
 out=Path(temp)
 (out/'driver.c').write_text(DRIVER,encoding='utf-8')
 for variant in variants:
  defines=[f'-DINKTAG_EPD_ENABLE_{key}={value}' for key,value in variant.items()]
  subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-Wall','-Werror',*defines,
                  '-I'+str(ROOT/'compat/common/epd'),str(out/'driver.c'),'-o',str(out/'test')],check=True)
  subprocess.run([str(out/'test')],check=True)
print(f'PASS {len(variants)} catalog feature configurations against cfff3e4; retained controller families only')
