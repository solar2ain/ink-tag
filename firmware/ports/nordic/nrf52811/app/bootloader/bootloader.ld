/* InkTag nRF52811 Secure DFU bootloader memory layout. */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x2A000, LENGTH = 0x4000
  RAM (rwx) : ORIGIN = 0x200022C8, LENGTH = 0x3D38
  MBR_PARAMS_PAGE (rw) : ORIGIN = 0x2E000, LENGTH = 0x1000
  BOOTLOADER_SETTINGS_PAGE (rw) : ORIGIN = 0x2F000, LENGTH = 0x1000
  UICR_BOOTLOADER (r) : ORIGIN = 0x10001014, LENGTH = 0x4
  UICR_MBR_PARAMS (r) : ORIGIN = 0x10001018, LENGTH = 0x4
}

SECTIONS
{
  .bootloader_settings_page (NOLOAD) :
  {
    KEEP(*(.bootloader_settings_page*))
  } > BOOTLOADER_SETTINGS_PAGE

  .mbr_params_page (NOLOAD) :
  {
    KEEP(*(.mbr_params_page*))
  } > MBR_PARAMS_PAGE

  .uicr_bootloader_start_address :
  {
    KEEP(*(.uicr_bootloader_start_address*))
  } > UICR_BOOTLOADER

  .uicr_mbr_params_page :
  {
    KEEP(*(.uicr_mbr_params_page*))
  } > UICR_MBR_PARAMS
}

SECTIONS
{
  .mem_section_dummy_ram : { }
  .cli_sorted_cmd_ptrs :
  {
    PROVIDE(__start_cli_sorted_cmd_ptrs = .);
    KEEP(*(.cli_sorted_cmd_ptrs))
    PROVIDE(__stop_cli_sorted_cmd_ptrs = .);
  } > RAM
  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
  .log_dynamic_data :
  {
    PROVIDE(__start_log_dynamic_data = .);
    KEEP(*(SORT(.log_dynamic_data*)))
    PROVIDE(__stop_log_dynamic_data = .);
  } > RAM
  .log_filter_data :
  {
    PROVIDE(__start_log_filter_data = .);
    KEEP(*(SORT(.log_filter_data*)))
    PROVIDE(__stop_log_filter_data = .);
  } > RAM
} INSERT AFTER .data;

SECTIONS
{
  .mem_section_dummy_rom : { }
  .sdh_soc_observers :
  {
    PROVIDE(__start_sdh_soc_observers = .);
    KEEP(*(SORT(.sdh_soc_observers*)))
    PROVIDE(__stop_sdh_soc_observers = .);
  } > FLASH
  .sdh_ble_observers :
  {
    PROVIDE(__start_sdh_ble_observers = .);
    KEEP(*(SORT(.sdh_ble_observers*)))
    PROVIDE(__stop_sdh_ble_observers = .);
  } > FLASH
  .sdh_req_observers :
  {
    PROVIDE(__start_sdh_req_observers = .);
    KEEP(*(SORT(.sdh_req_observers*)))
    PROVIDE(__stop_sdh_req_observers = .);
  } > FLASH
  .sdh_state_observers :
  {
    PROVIDE(__start_sdh_state_observers = .);
    KEEP(*(SORT(.sdh_state_observers*)))
    PROVIDE(__stop_sdh_state_observers = .);
  } > FLASH
  .sdh_stack_observers :
  {
    PROVIDE(__start_sdh_stack_observers = .);
    KEEP(*(SORT(.sdh_stack_observers*)))
    PROVIDE(__stop_sdh_stack_observers = .);
  } > FLASH
  .nrf_queue :
  {
    PROVIDE(__start_nrf_queue = .);
    KEEP(*(.nrf_queue))
    PROVIDE(__stop_nrf_queue = .);
  } > FLASH
  .nrf_balloc :
  {
    PROVIDE(__start_nrf_balloc = .);
    KEEP(*(.nrf_balloc))
    PROVIDE(__stop_nrf_balloc = .);
  } > FLASH
  .dfu_trans :
  {
    PROVIDE(__start_dfu_trans = .);
    KEEP(*(SORT(.dfu_trans*)))
    PROVIDE(__stop_dfu_trans = .);
  } > FLASH
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(SORT(.svc_data*)))
    PROVIDE(__stop_svc_data = .);
  } > FLASH
  .crypto_data :
  {
    PROVIDE(__start_crypto_data = .);
    KEEP(*(SORT(.crypto_data*)))
    PROVIDE(__stop_crypto_data = .);
  } > FLASH
  .log_const_data :
  {
    PROVIDE(__start_log_const_data = .);
    KEEP(*(SORT(.log_const_data*)))
    PROVIDE(__stop_log_const_data = .);
  } > FLASH
  .log_backends :
  {
    PROVIDE(__start_log_backends = .);
    KEEP(*(SORT(.log_backends*)))
    PROVIDE(__stop_log_backends = .);
  } > FLASH
} INSERT AFTER .text

INCLUDE "nrf_common.ld"

/* Compact SDK adapters require these private registries to stay empty. */
ASSERT(SIZEOF(.sdh_req_observers) == 0, "Recovery request observer requires full SDK adapter")
ASSERT(SIZEOF(.sdh_state_observers) == 0, "Recovery state observer requires full SDK adapter")
ASSERT(SIZEOF(.svc_data) == 0, "Recovery private SVC requires full SDK adapter")
