# TLSR8359 plaintext tools-only programmer

Deliver the complete plaintext directory `release/tools/tlsr8359_hsel/`; no outer
ZIP is required. This tools version is `0.2.2.3`; source hashes and
build IDs are separate. This directory includes, downloads and implicitly selects **no firmware**. Probe
and backup need no firmware. Flash uses a selected compatible external complete-firmware ZIP and PCB configuration, either CRC-only Base or normally signed Pro. All three BAT entries accept zero arguments and open file pickers for missing files; writing still requires confirmation. Historical combined archives remain evidence, not recommended
new tools deliveries.

Use Python 3.11+, Windows PowerShell and the CH340 driver. Keep power/SWS/common
ground stable. Run `firmware/ports/telink/tlsr8359/tools/setup.bat` to install the
independently pinned host dependencies and perform an offline check. The same
directory contains probe.ps1, backup.ps1, flash.ps1 and board configs beside the BAT files. Every entry
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

Version0.2.1.1 fixes the registered1-MiB HSEL4Q_04_20M_33 variant. Full
flashing enables the cleared upper512-KiB image extent only after exact-unit
UID, matching JEDEC and complete1-MiB clear-plan checks. Other512-KiB boards
retain their bounds. A low-only write cannot enable an uncleared upper extent.
A400x300 BWR profile normally provides20 Pro slots; the Free limit is unchanged.
App-only OTA preserves an installed base-only Factory flag; existing devices
need the corrected complete-flashing tools to replace that flag.

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
`release/programmer-docs/tlsr8359/`, freeze a version snapshot after review, then copy one way to distribution.
Unchanged MCU bytes require no rebuild or new signature.

Tools `0.2.1.2` add `generate-bin.ps1` and `flash-bin.ps1` while retaining `flash.ps1`. Generation requires an explicit complete ZIP and configuration; 0.2.1 requires an explicit MAC. Direct bin writes default to no readback; add `-Verify` for comparison and optional `-PrepareClock24` for protected stopped-clock preparation. Download the complete tools and internal dependencies together. See the [flashing guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/flash.md#tlsr-offline-bin-generation-and-direct-writing) for checkout-root commands.

The unpublished 0.2.2.3 tools support CRC-only Base Full and signed Pro Full. Keep the complete dependency tree alongside the BAT launchers.

- generate-bin.bat: offline Full ZIP + exact board TOML to initialization BIN. MAC is optional only when both actual App and Recovery contain automatic-address capability; old0.2.1 images still require an explicit unique MAC.
- flash-bin.bat: write the supplied BIN unchanged from address0, without reading old Factory/Settings/UID or retaining Pro/data. Default is no readback; add -Verify. Explicit -PrepareClock24 uses the existing UID/JEDEC and stopped-clock repair route.
- Recommended entry, flash.bat: existing flash.ps1 -AllowMissingUid. Read necessary Factory, Settings, UID/JEDEC and stock calibration where applicable; retain valid address/calibration and authenticated same-unit Pro, rebuild ordinary defaults, clear passwords/Find/history/image indexes. Base may retain a host-verified same-unit token dormant without enabling Pro functionality.

### Zero-argument entries and file selection

Double-click an entry or invoke it from any working directory. Zero-argument launches keep the result visible until you press a key. Calls with arguments return the exit code immediately after the operation.

- `flash.bat` opens the English guide and `flash-zh.bat` opens the Chinese guide. If a single `*-complete-firmware.zip` or `*-full.zip` is beside the BAT, the guide selects it for validation. If several match, choose by number. If none match, the guide opens a file dialog. Select a board config, review the validation and retention details, and press `y` to proceed. Enter alone cancels. After confirmation, enter the exact CH340 port, such as `COM13`. The read-only SWS probe then polls until the device responds or you press Ctrl+C. Contact is followed by the usual device identity and Flash checks.
- `generate-bin.bat`: select a config beside the BAT, pick a complete ZIP, then choose a new BIN save location. This is fully offline. Omitting MAC keeps the firmware's supported first-boot automatic-address behavior.
- `flash-bin.bat`: pick a generated BIN, enter the exact port and confirm. Both direct BIN and parameterized Full flashing wait for SWS detection by default. Press Ctrl+C to cancel. Direct BIN writing **does not require a board config** and does not preserve device data. Keep the matching same-name JSON for Base BINs.

Delivered TOML configs are beside the BATs. Same-directory YAML/YML/JSON presets are also supported. No automatic search in the working directory, `configs/` or other subdirectories is performed. If configs are missing, place the appropriate files beside the BAT and retry. Canonical source presets remain under `configs/`; copy the desired files beside the BAT for interactive source use, or explicitly pass any valid `-Config` path.

Only omitted inputs are requested. Cancelling a dialog, board choice or port input exits before hardware access. Firmware is never guessed and existing BIN output is never overwritten. `-Help` and `-DryRun` never open dialogs or access hardware. Automation may supply complete arguments and `-NonInteractive` to disable missing-input prompts, with the existing `-Yes` for write confirmation. `-Yes` never bypasses package, capacity, identity or readback checks.

All boards produce512KiB, addresses0x00000–0x7FFFF. Boot code, App, Recovery, configuration and defaults are already included; no additional configuration/activation write is required. On1MiB Nebular420, the upper512KiB remains outside the BIN. Gallery capacity is unchanged; valid existing upper slots may remain, and the existing allocator erases each selected slot before writing. An offline initialization BIN does not promise the preserving Full workflow's complete gallery clearing. Separate NFC EEPROM and Flash UID OTP are outside the image. BDT and other raw programmers must load the BIN at0 and use the actual image extent: a low-address download does not prove the programmer avoids whole-chip erase. flash-bin still accepts previously generated valid1MiB BINs.

Without MAC, legacy0x7F000 and typed0x7F160 remain FF, with an initialization marker at0x7F10D. Shared App/Recovery logic reuses the SDK random source, creates a legal Static Random address and persists it using resumable one-way writes; reboots retain it without requiring QR generation. An explicit MAC writes both the six-byte little-endian legacy mirror and the typed/checksummed record, with the marker cleared. Never clone a fixed-address BIN across devices.

Offline generation does not retain individual original calibration, activation or data; the existing erased-calibration fallback applies. Base flash-bin admission requires its same-name JSON, binding actual Full-manifest version/context and BIN hash without claiming publisher authentication. Signed Pro BIN-only verification remains strict and never falls back to Base. Missing automatic capability with no explicit MAC is a genuine firmware incompatibility and fails before hardware access.

The preservation wrapper may continue Base when three complete UID observations are unusable or inconsistent; it records missing UID, never invents one or restores Pro. This binds only the current port, JEDEC and live captures, not immutable same-unit identity. Short reads/transport failure, chip/capacity mismatch, mandatory unknown-calibration conflicts and actual write/readback failures still stop. Writes are never replayed and failure never automatically starts App. Original flash.ps1 remains strict by default.

This batch has offline native/fault-injection/extracted-tool validation only, with no new physical Full/BDT qualification or public publication. Historical firmware/tools remain immutable.
