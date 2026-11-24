"""Materialization of the maintenance-only, read-only GR LED consumer."""
import shutil


def materialize_recovery_pins(repo, target, makefile, changed):
    names = ["inktag_gr_pin_config.c", "inktag_gr_pin_config.h",
             "inktag_gr_pin_recovery.c", "inktag_gr_pin_recovery.h",
             "inktag_gr_pin_recovery_runtime.c", "inktag_gr_pin_recovery_runtime.h",
             "inktag_gr_settings_map.c", "inktag_gr_settings_map.h",
             "inktag_gr_settings_store.c", "inktag_gr_settings_store.h",
             "inktag_gr_factory_seed.h"]
    sources = [(repo / "firmware/ports/goodix/common/src" / name, name) for name in names]
    sources += [(repo / "firmware/common/protocol" / name, name)
                for name in ("inktag_pin_config.c", "inktag_pin_config.h",
                             "inktag_aux_io.c", "inktag_aux_io.h", "inktag_release_v1.h")]
    sources += [(repo / "firmware/common/storage" / name, name)
                for name in ("inktag_settings_records.c", "inktag_settings_records.h")]
    sources += [(repo / "firmware/ports/goodix/common/src/goodix_pin_backend_facts.h", "goodix_pin_backend_facts.h")]
    for source, name in sources:
        dest = target / "Src/user" / name
        if not dest.exists() or dest.read_bytes() != source.read_bytes():
            shutil.copy2(source, dest)
            changed.append("Src/user/" + name)
    # No App storage writer, GUI, screen driver or peripheral initializer.
    anchor = "## Set source files and objects"
    if makefile.count(anchor) != 1:
        raise ValueError("Recovery source-list anchor changed")
    extra = "PRJ_C_MICRO_DEFINES += INKTAG_GR_PIN_CONFIG_PORT=1\n"
    extra += "PRJ_C_SRC_FILES += " + " ".join(
        "../Src/user/" + name for _, name in sources if name.endswith(".c")) + "\n"
    makefile = makefile.replace(anchor, extra + "\n" + anchor, 1)
    return makefile


def patch_recovery_pin_leds(text, *, gr551x):
    text = '#include "inktag_gr_pin_recovery_runtime.h"\n' + text
    output = "static void inktag_recovery_led_output(bool red, bool green, bool blue)\n{"
    text = text.replace(output, output + "\n    if (inktag_gr_pin_recovery_required()) {\n"
        "        inktag_gr_pin_recovery_output(red, green, blue);\n        return;\n    }", 1)
    init = ("void" if gr551x else "static void") + " inktag_recovery_led_init(void)\n{"
    # Do not initialize or repair NVDS. If its existing guard is not ready yet,
    # open fails dark; ADV_START gets one later read-only attempt below.
    text = text.replace(init, init + "\n    if (inktag_gr_pin_recovery_required()) {\n"
        "        (void)inktag_gr_pin_recovery_open();\n        return;\n    }", 1)
    ready = "void inktag_recovery_led_role_ready(void)\n{"
    text = text.replace(ready, ready + "\n    if (inktag_gr_pin_recovery_required()) {\n"
        "        bool available = inktag_gr_pin_recovery_open();\n" +
        ("        s_inktag_recovery_led_available = available;\n"
         "        s_inktag_recovery_led_mode = INKTAG_RECOVERY_LED_BOOT;\n"
         "        s_inktag_recovery_led_phase_ticks = 0u;\n"
         "        s_inktag_recovery_led_last_tick = s_systick_irq_count;\n" if gr551x else "        (void)available;\n") +
        "    }", 1)
    return text
