ENTRY(probe_entry)

SECTIONS
{
  . = 0x20000000;
  .text :
  {
    KEEP(*(.probe_entry))
    *(.text*)
    *(.rodata*)
  }

  /DISCARD/ :
  {
    *(.comment*)
    *(.ARM.attributes*)
    *(.note*)
  }
}
