# TLSR8359 plaintext tools-only programmer

Shared layout: `ota/`, `full/`, `tools/`; configurations are in `tools/configs/`. Run the commands from this board directory. The original backend is retained under `tools/internal/native/`; tools still require an explicit firmware argument.


Deliver the complete plaintext directory `release/tools/tlsr8359_hsel/`; no outer
ZIP is required. This tools version is `0.2.1.0`; source hashes and
build IDs are separate. This directory includes, downloads and implicitly selects **no firmware**. Probe
and backup need no firmware. Flash requires an explicitly selected external
signed `app-full.zip` and PCB configuration; missing arguments fail before
hardware access. Historical combined archives remain evidence, not recommended
new tools deliveries.

Use Python 3.11+, Windows PowerShell and the CH340 driver. Keep power/SWS/common
ground stable. Run `tools/internal/native/firmware/ports/telink/tlsr8359/tools/setup.ps1` to install the
independently pinned host dependencies and perform an offline check. The same
directory contains probe.ps1, backup.ps1, flash.ps1 and configs/. Every entry
supports `-Help` and offline `-DryRun`. Command examples are in README.md; replace
the example port/configuration with the actual device and use your external
firmware path. No BLE workflow is invoked.

No InkTag CLI installation is required. Default links, QR/NFC data and package
verification use fixed pure-format modules inside
`tools/hardware/inktag_programmer_formats/`, never an installed CLI. Version
0.2.0.4 supplies the default-data modules and QR dependency omitted in 0.2.0.3.
Extracted-tools validation now exercises actual complete-image preparation;
DryRun alone cannot qualify that phase. Missing default-data dependencies fail
before serial access.
Version0.2.0.5 also removes residual checkout-only CLI search-path injections
from delivered code while retaining source provenance. Default data, prepared
image bytes and programmer behavior are unchanged.
Version0.2.0.6 checks the frozen modules into the programmer source closure.
Both source and exported entries import them directly, without reading the
CLI source tree. CLI upgrades do not change programmer formats. Updating the
snapshot requires explicit regeneration from a fixed source commit and review
of the recorded source/runtime hashes.
Version0.2.0.7 allows a missing/damaged old Factory selector when independent
installed identity/configuration envelopes locate the existing Factory map.
Rebuild optional configuration from the selected preset; preserve valid BLE
identity/type, using only established legacy/verified hardware-UID defaults
when optional address records are absent/invalid, with a visible warning.
Two conflicting valid addresses still stop. Opaque stock bytes occupying
indispensable relocation/configuration/shadow destinations stop with exact
addresses and the preservation reason. They are never erased as optional data.
This is host-only: App/Recovery bytes are unchanged; no new hardware claim.
Version0.2.0.8 synchronizes the accepted HSEL3 preset to90degrees+native-X
mirror, matching WebUI/CLI defaults. The fixed0.2.0.7 delivery is unchanged.
Firmware remains an explicit external input; no MCU build or new signature.

Version0.2.1.0 exports the current maintained tool closure. Select the actual
HSEL4Q_04_20M_33 screen using `HSEL4Q_04_20M_33-SSD1683.toml` (SSD1683, BWR)
or `HSEL4Q_04_20M_33-JD79668.toml` (JD79668AA, BWRY). The YW2427 and
unsuffixed entries are retired. `screen.ic` selects the controller; there is
no BoardNote input.

The current SWS handling first preserves a registered link with confirmed
same-unit UID/JEDEC. Only a strictly identified, debug-stopped48MHz capture
can use the existing24MHz programming-clock path with readback checks.
Normal firmware startup remains48MHz. This does not qualify48MHz/921600 as
reliable Full programming, or permit copying another PCB's timing parameters.
Offline exported-directory validation adds no board/panel/hardware qualification;
old tool snapshots and firmware bytes remain unchanged.

Complete flashing asks y/N. Preserve only an existing valid, same-device native
Pro token/sequence and original identity, BLE address/type, calibration and
required boot data. No issuer, replacement token or post-flash BLE activation
is used. Clear password/QR control credentials, device Find state, settings/name,
fonts, main-Flash slots/slideshow/interaction state and old diagnostic history.
Host Find/Apple libraries and tracking history are never touched.

An additional fresh8-KiB native Settings capture is required. Before erase the
actual writer rechecks Factory/Settings against that capture and checks UID
again. Seed only the exact verified token in new Settings; erase old generations.
Clean absence/tombstone or wholly erased pages remain normal Free/stock cases.
Old development layouts, unknown non-FF user data and invalid, foreign or
incompatible authorization do not by themselves prevent complete flashing.
Unrestorable authorization is cleared and initializes Free; Pro then requires
normal activation. The existing y/N warning explains this impact, with the
actual disposition printed after preparation and before writing. Unknown data
is never described as proven empty. Match the native cold reader's selected
committed page; never revive a deleted token from an older page. An unrelated
damaged inactive page does not discard valid current Pro. Rebuild installed
host-owned pin/screen configuration slots from the explicitly selected config.
Failed/short physical reads, changed UID, uncertain necessary Factory identity
or calibration, invalid package signature/pairing and actual capacity mismatches
still refuse before erase. Never supply an old archive or external token as
this automatic preservation source. Native captures contain private
authorization data and stay in this operation's local evidence directory.

TLSR currently has no qualified persistent crash/reset-history or retained-RAM
mailbox backend. Old Settings history is omitted and the existing final
whole-chip reset is requested. No bulk SRAM/unknown-register clearing or observed
boot claim is added. Separate NFC EEPROM is not erased. Only independently
qualified Nebular420 high image storage on the same Flash is cleared; other
unqualified high addresses remain untouched and are not claimed erased.

Keep complete readback
for the first flash. A later explicit `-SkipReadbackVerify` is NOT VERIFIED.
Publisher signature, MCU/layout/capacity, internal App/Recovery pairing and
identity/calibration/native-Pro-authority protections remain. Critical new
Settings readback is never skipped. Do not borrow another unit's data.
SWS activation can reset/halt the target. Long operations stream terminal and
log progress; callers must not buffer until exit or remove completed-stage lines.

No Python runtime, driver, SDK/compiler, signer or full BLE CLI is included.
Linux/macOS shell entries support offline planning only; native programming is
not qualified. Run `python firmware/ports/telink/tlsr8359/tools/check.py` for
offline integrity/dependency checks; `--content-only` skips dependency imports.
Third-party terms are preserved in THIRD_PARTY_NOTICES.md. This is an internal
validation delivery, not all-board or public-release approval.

Maintain documentation first under the source repository's
`release/versions/<version>/`, then copy one way to distribution after review.
Unchanged MCU bytes require no rebuild or new signature.
