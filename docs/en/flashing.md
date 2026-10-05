# Full flashing

A full flash is for first installation and recovery and overwrites the factory firmware. Back up the original firmware first, then verify that the board ID in the filename exactly matches the device.

## General procedure

1. Download the full image and `SHA256SUMS.txt` from the same GitHub Release.
2. Verify SHA-256. Never flash a file that fails verification.
3. Connect power, GND, SWDIO, SWCLK, and RST/BOOT when required by that board.
4. Run the qualified script or official tool and wait for erase, program, and verify to succeed.
5. Release BOOT, disconnect the probe, or power-cycle as instructed, then test BLE and the display.

Vendor tool and driver licenses differ. Release kits therefore provide official download links, tested versions, and wrappers rather than repackaging proprietary installers by default.

Linux or macOS is supported only when the support matrix says Qualified. A script being present is not proof of hardware qualification.

## 0.2 candidate platforms

| Platform | 0.2 candidate boards | Windows first-flash path | Linux/macOS |
|---|---|---|---|
| Goodix | `gr5331_zkc21v` and GR5513/GR5515 ZKC42V/ZKC42VM | GProgrammer; GR551x identification also uses J-Link | Hardware validation pending |
| OnMicro | `om6626_zkc21b` and `om6626_zkc42b` | J-Link Commander 7.88j plus the OM6626 SDK device pack | Hardware validation pending |
| Telink | Hanshow Nowa 154R-N and Nebular 420R-N | CH340/UART-to-SWS, Python 3, `pyserial`, and the pinned reviewed pvvx tool | Hardware validation pending |

Nordic and Dialog boards are deferred in this 0.2 candidate. Their documentation entries do not imply that a current flashable asset is attached.

## Windows one-command entry points

Download and extract `InkTag-flash-tools-v0.2.0.zip`, then download the exact Full asset from the same candidate. Each wrapper waits for the target by default and supports `-h`. A local stock backup is mandatory, and the Full filename, length, and SHA-256 are checked against `firmware/manifest.json` before programming.

Goodix uses the official GProgrammer `GR5xxx_console.exe`. GR5513/GR5515 package detection also requires [SEGGER J-Link 7.88j](https://www.segger.com/downloads/jlink/):

```powershell
.\InkTag-flash-tools\windows\flash-goodix.ps1 -Board gr5331_zkc21v `
  -Firmware .\InkTag-gr5331_zkc21v-v0.2.0-full.bin `
  -StockBackup .\stock-flash.bin -ConsoleExe C:\path\GR5xxx_console.exe
```

OM6626 uses J-Link 7.88j and the device pack from the public [OM6626 SDK mirror](https://gitee.com/jtcom/OM6626). Do not merge the Full ZIP members or select one manually; the wrapper preserves the qualified state → code → device-information sequence:

```powershell
.\InkTag-flash-tools\windows\flash-om6626.ps1 -Board om6626_zkc21b `
  -Firmware .\InkTag-om6626_zkc21b-v0.2.0-full.zip `
  -StockBackup .\stock-flash.bin -OmSdkRoot C:\path\OM6626.SDK
```

Telink requires Python 3, `pyserial`, a CH340/3.3 V UART-to-SWS adapter, and [pvvx/TlsrComSwireWriter](https://github.com/pvvx/TlsrComSwireWriter) pinned to commit `6455f50f25dd264ac6820ecacfd64b90c6c80f3e`. Both 0.2 Hanshow boards remain experimental candidates, so the layout acknowledgement is explicit and full readback verification stays enabled:

```powershell
python -m pip install pyserial
.\InkTag-flash-tools\windows\flash-telink.ps1 -Board hs_nowa154 `
  -Firmware .\InkTag-hs_nowa154-v0.2.0-full.bin `
  -StockBackup C:\path\verified-backup -ToolPath C:\path\TLSR825xComFlasher.py `
  -AcknowledgeExperimentalLayout
```

The 0.2 candidate does not claim Linux or macOS first-flash qualification. PowerShell 7 being able to parse a wrapper is not equivalent to a hardware pass.

## Pre-flash checklist

- Resolve the exact `boardId` in `firmware/manifest.json`, then verify the downloaded SHA-256.
- Preserve a complete factory backup from this physical unit. Do not substitute a backup from another device of the same model.
- Use a regulated supply and common ground. An e-paper refresh normally requires more current than MCU boot and BLE advertising.
- Assert BOOT/RST only when the manual requires it. After migration, normal InkTag maintenance must permit direct debugger attachment.
- Close applications that may own the same probe, serial port, or Bluetooth device.

## Success and recovery

A successful program operation is not a complete device pass. Confirm erase, program, readback verification, normal boot, exact BLE identity, clear, and one rendered page. After an interrupted write, avoid repeated blind full erases; reconnect through the platform Recovery/first-flash path. When a target is not detected, check VTref, power, ground, wiring, and contact before reducing interface speed.

`flash/windows/` is the public flashing entry in this candidate. `flash/runtime/` is the constrained runtime used by those wrappers and must not be invoked or edited independently. If a board with an attached Full image lacks a standalone script with help and safety validation, treat that as an audit blocker. A vendor GUI that can write the image manually is not evidence for advertised one-command flashing.
