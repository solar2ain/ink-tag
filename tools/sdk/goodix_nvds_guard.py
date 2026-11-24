"""Bind both Goodix roles to the same read-only cold NVDS init/HAL guard."""
from pathlib import Path


def patch_goodix_nvds_guard(sdk: Path) -> bool:
    soc = sdk / "platform/soc/src/gr_soc.c"
    text = soc.read_text(encoding="utf-8")
    original = text
    include = '#include "inktag_gr_nvds_guard.h"\n'
    if include not in text:
        text = include + text
    # Replace ONLY cold setup calls, never error handling, warm boot or raw API.
    for start in ("NVDS_START_ADDR", "0"):
        text = text.replace(f"uint8_t err_code = nvds_init({start}, NVDS_NUM_SECTOR);",
                            f"uint8_t err_code = inktag_gr_nvds_init({start}, NVDS_NUM_SECTOR);")
    if "uint8_t err_code = inktag_gr_nvds_init(" not in text:
        raise SystemExit(f"Missing reviewed Goodix cold-init anchor: {soc}")
    if text != original:
        soc.write_text(text, encoding="utf-8", newline="\n")
    changed = text != original
    hal = sdk / "components/libraries/hal_flash/hal_flash.c"
    text = hal.read_text(encoding="utf-8")
    original = text
    if include not in text:
        text = include + text
    for result, name, args, fail, erase in (
        ("uint32_t", "hal_flash_write", "const uint8_t *buf, ", "0", "false"),
        ("uint32_t", "hal_flash_write_r", "const uint8_t *buf, ", "0", "false"),
        ("bool", "hal_flash_erase", "", "false", "true"),
    ):
        anchor = f"{result} {name}(const uint32_t addr, {args}const uint32_t size)\n{{\n"
        guard = f"    if (!inktag_gr_nvds_mutation_allowed(addr, size, {erase})) return {fail};\n"
        power = f"    if (!inktag_gr_flash_mutation_allowed(addr, size)) return {fail};\n"
        if anchor not in text:
            raise SystemExit(f"Missing reviewed physical HAL anchor {name}: {hal}")
        first = text.index(anchor) + len(anchor)
        end = first
        while text.startswith(guard, end) or text.startswith(power, end):
            end += len(guard) if text.startswith(guard, end) else len(power)
        # The prior voltage materializer and this guard may alternate. Normalize
        # that prefix rather than accumulating copies on each synchronization.
        prefix = guard + (power if power in text[first:end] or
                          '__WEAK bool inktag_gr_flash_mutation_allowed' in text else '')
        text = text[:first] + prefix + text[end:]
    if text != original:
        hal.write_text(text, encoding="utf-8", newline="\n")
    return changed or text != original
