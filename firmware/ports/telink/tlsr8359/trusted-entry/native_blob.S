    .section .rodata.native_loader,"a"
    .global __native_load_image_start__
    .global __native_load_image_end__
__native_load_image_start__:
    .incbin NATIVE_LOAD_BINARY
__native_load_image_end__:
