SECTIONS
{
  . = 0;
  .fonts :
  {
    KEEP(*(.rodata.u8g2_font_wqy9_t_lunar))
    KEEP(*(.rodata.u8g2_font_wqy12_t_lunar))
    KEEP(*(.rodata.u8g2_font_helvB14_tn))
    KEEP(*(.rodata.u8g2_font_helvB18_tn))
    KEEP(*(.rodata.inktag_default_device_qr))
    KEEP(*(.rodata.solar_1_1))
    KEEP(*(.rodata.lunar_month_days))
    KEEP(*(.rodata.YearMonthBit))
  }

  /DISCARD/ :
  {
    *(.comment*)
    *(.note*)
  }
}
