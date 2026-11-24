# PN532/PCR532 image transfer

[简体中文](README.md)

Connect a PN532/PCR532 reader in serial mode. The tool detects compatible InkTag PRO Type 2 and Type 4 tags automatically. Run it from the repository root or the extracted full client ZIP. Python 3.11 or later is required.

```powershell
python -m pip install -r tools/nfc/requirements.txt
.\tools\nfc\inktag-nfc.cmd ports
.\tools\nfc\inktag-nfc.cmd send-image "photo.jpg" --port COM18
```

On macOS or Linux, use `python tools/hardware/inktag_nfc.py` in place of the Windows command and pick the serial port shown by `ports`. Hold one tag near the reader until the tool tells you to remove it.

| Command | Purpose |
| --- | --- |
| `ports` | List the computer's serial ports |
| `read` | Read the tag's NFC link or text |
| `probe` | Show the display size and colors |
| `send-image "photo.jpg"` | Convert and send an image. Add `--wait-refresh` to wait for a confirmed refresh |
| `send-cmd calendar` | Send a text command. On firmware that supports image names, send the saved name directly, for example `send-cmd Sunset` |
| `clear` | Clear the screen while keeping image slots and device settings |
| `sync-time` | Set the clock from the computer while keeping the device's time zone and page |

An NFC acknowledgement confirms receipt. Check the tag for the completed refresh.

InkTag tool code is licensed under GNU AGPL-3.0-only. See the [PN532 provider notice](provider/NOTICE.md) for the reader dependency's license.
