"""Preserve frozen panel traces and compare expanded/compact command streams."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
registry = json.loads((ROOT / ("tests/source-test-manifest.json" if (ROOT/"tests/source-test-manifest.json").is_file() else "tests/host-test-manifest.json")).read_text())
drivers = {"compat/common/epd/inktag_epd_jd.c", "compat/common/epd/inktag_epd_ssd.c"}
entries = [item for item in registry["hostC"] if drivers.intersection(item["sources"])]
cc = os.environ.get("CC", "gcc")
helper = "compat/common/epd/inktag_epd_commands.c"
generator = "build-support/gui/compact-epd-commands.py"
includes = ["-Ifirmware/common/gui", "-Ifirmware/common/ble", "-Ifirmware/common/input"]
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    projections = {}
    for name in ("inktag_epd_jd.c", "inktag_epd_ssd.c", "inktag_epd_uc.c"):
        source = "compat/common/epd/" + name
        destination = tmp / name
        subprocess.run(["python3", generator, source, str(destination)], check=True)
        projections[source] = str(destination)
    for entry in entries:
        traces = []
        for compact in (0, 1):
            binary = tmp / (entry["id"] + str(compact))
            command = [cc, "-std=c99", "-O2", "-Wall", "-Werror",
                       *includes, *entry["defines"],
                       *[projections.get(s, s) if compact else s for s in entry["sources"]],
                       *([] if helper in entry["sources"] else [helper]), "-o", str(binary)]
            subprocess.run(command, check=True)
            traces.append(subprocess.check_output([str(binary)], text=True))
        assert traces[0] == traces[1], entry["id"]
    # UC has no existing frozen init matrix. Exercise its branch combinations
    # against the retained original path, recording data transaction lengths
    # as well as bytes, commands, RESET, BUSY, reads and delays.
    base = (ROOT / "tests/test_jd79xxx_recipe_matrix.c").read_text()
    stubs = base[:base.index("typedef struct {")]
    stubs = stubs.replace("uint8_t i;\n    for (i = 0; i < length; ++i) EPD_WriteByte(data[i]);",
                          "uint8_t i;\n    event('L', length);\n    for (i = 0; i < length; ++i) EPD_WriteByte(data[i]);")
    fixture = tmp / "uc.c"
    fixture.write_text(stubs + '''
extern const epd_driver_t epd_drv_uc8xxx;
void EPD_FillRAM(uint8_t command, uint8_t value, uint32_t bytes) {
    EPD_WriteCmd(command); event('Z', bytes); event('z', value);
}
int main(void) {
    const uint8_t chips[] = {DRV_IC_UC8151, DRV_IC_UC8176, DRV_IC_UC8179, DRV_IC_UC8159};
    const uint16_t sizes[][2] = {{128,250},{400,300},{800,480}};
    for (unsigned chip=0;chip<sizeof(chips);++chip)
    for (unsigned color=COLOR_BW;color<=COLOR_BWR;++color)
    for (unsigned size=0;size<3;++size)
    for (unsigned recipe=0;recipe<3;++recipe) {
        epd_model_t epd={0};
        epd.ic=chips[chip]; epd.color=color;
        epd.width=epd.visible_width=sizes[size][0];
        epd.height=epd.visible_height=sizes[size][1];
        epd.panel_recipe=recipe;
        epd.driver_id=EPD_SCREEN_DRIVER_UC8176;
        epd.drv=&epd_drv_uc8xxx;
        s_hash=2166136261u;s_events=0;
        epd.drv->init(&epd);
        epd.drv->sleep(&epd);
        printf("%u %u %u %u %08X %u\\n",chip,color,size,recipe,s_hash,s_events);
    }
}
''')
    traces = []
    for compact in (0, 1):
        binary = tmp / ("uc" + str(compact))
        subprocess.run([cc, "-std=c99", "-O2", "-Wall", "-Werror",
                        *includes, "-DINKTAG_TELINK", "-Ifirmware/common/epd",
                        str(fixture),
                        projections["compat/common/epd/inktag_epd_uc.c"] if compact else
                        "compat/common/epd/inktag_epd_uc.c", helper, "-o", str(binary)], check=True)
        traces.append(subprocess.check_output([str(binary)], text=True))
    assert traces[0] == traces[1], "UC init/sleep transaction traces differ"
print(f"PASS compact EPD: {len(entries)} frozen JD/SSD fixtures in both modes; UC branch/transaction trace equality")
