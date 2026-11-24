NRF_CHIP := nrf52832
BOARD ?= nrf52832_priceetag
NRF_PORT_MAKEFILE := firmware/ports/nordic/nrf52832/Makefile
# Arm GNU 15.2.1 whole-application A/B recompilation saves 376 Flash bytes
# without changing features or memory boundaries. This remains subject to the
# normal payload guard and exact-board regression; it does not imply fit.
NRF_EXTRA_SIZE_FLAGS := -fno-jump-tables -fipa-pta -fno-partial-inlining \
  -fno-split-wide-types -fno-caller-saves -fno-tree-scev-cprop

include firmware/ports/nordic/common/nrf52.mk
