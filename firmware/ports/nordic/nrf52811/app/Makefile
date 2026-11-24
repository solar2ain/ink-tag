NRF_CHIP := nrf52811
override NRF_APP_FLASH_LIMIT := 69632
# This independent build omits E6, SSD1677 gray4 and UC8159.
# Square-specific GUI layouts are omitted; square geometry uses the compact
# rectangular template. Existing controller/recipe admission stays enforced.
# All controller sources are retained. The
# protocol adapter returns UNSUPPORTED before persisting an omitted path.
BOARD ?= nrf52811_aesl
NRF_PORT_MAKEFILE := app/Makefile
# One Hema 128x250 BWR frame is exactly 8,000 bytes. Keeping it in the
# existing static renderer buffer avoids redrawing and rereading external-font
# glyphs once per 64-row page while remaining inside the verified S112 RAM
# window.
INKTAG_GUI_RENDER_BUFFER_SIZE := 8000
INKTAG_NRF_FIXED_IMAGE_SLOTS ?= 1
INKTAG_NRF_GUI_RAM_TABLES ?= 1
# Repeated calendar width/glyph queries otherwise evict one another and reread
# the external font over software SPI. Sixteen addresses use 96 extra RAM bytes
# on Arm; the font data cache and framebuffer retain their existing budgets.
INKTAG_NRF_GUI_GLYPH_CACHE_ENTRIES ?= 16
INKTAG_NRF_EXCLUDE_154_DRIVER ?= 1
INKTAG_NRF_LOW_BATTERY_PAGE ?= 0
# Standalone edits are retired; upload/replacement still writes name and type.
INKTAG_NRF_IMAGE_METADATA_EDIT ?= 0
# These Arm GNU 15.2.1 code-generation choices keep the complete application
# inside the 68 KiB window after the shared paged-GUI fixes. They change neither
# registered commands, controllers, panel recipes, pages nor image records.
NRF_EXTRA_SIZE_FLAGS := -fno-jump-tables -fipa-pta -fno-partial-inlining \
  -fno-split-wide-types -fno-caller-saves -fno-tree-scev-cprop

include compat/nordic/nrf52.mk
