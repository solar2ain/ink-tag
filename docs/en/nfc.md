# Control a tag and send images with NFC

[简体中文](../zh-CN/nfc.md)

Connect the tag in WebUI, check NFC in device interactions, and confirm that the tag has the matching PRO firmware and an active PRO license. Link reading, text commands and image transfer depend on the board and firmware.

## Open the device link

Unlock the phone, enable NFC, and hold its NFC antenna against the tag. Open the detected device link in a browser that supports Web Bluetooth. Select the tag when the browser asks for Bluetooth permission.

## Send a text command

Use NFC Tools or another writer that supports NDEF text. Add one text record, enter one command, and touch the phone to the tag when prompted to write.

| Example | Action |
| --- | --- |
| `calendar` | Show the built-in calendar |
| `clock` | Show the built-in clock |
| `device` | Show device information |
| `slot-3` | Show the image stored in slot 3 |
| `Sunset` | Show a saved image by name. Name the image in WebUI, then write that name as the text record. Requires firmware with image-name support |
| `time#<current UTC Unix seconds>` | Set the clock on supported firmware while keeping the device's time zone and page |

Enter the saved image name by itself, without a command prefix. Watch the tag for completion. The phone's write confirmation can arrive while the display is still refreshing. For clock sync, D3-Pro InkTag can send the phone's current time.

## Use NFC in WebUI

### Send commands

Open **Dashboard → Quick controls → NFC control**. WebUI shows the controls available in your browser.

- **Chrome on Android.** Turn on NFC, open WebUI over HTTPS, and select a command. Grant NFC permission when prompted, then hold the phone against the target tag. Tap one tag at a time. To send images from your phone, use D3-Pro InkTag as described below.
- **Chrome or Edge on a computer.** Open WebUI over HTTPS or on a local address that your browser trusts. Place the tag on the PN532/PCR532 reader and select a command. Choose the reader's serial port if prompted. The browser may reuse a port you previously authorized.

The **Calendar** and **Clock** shortcuts include the current UTC timestamp and local time zone offset when they write the command. For example, `calendar#1795276800,+480` specifies UTC+8. For older firmware, send `calendar` or `clock` from **Custom** to keep the date, time, and time zone already on the tag. **Clock** asks for confirmation because frequent refreshes can increase power use. You can also enter an image slot number or use **Custom** to send an image name that your firmware recognizes.

After WebUI confirms the write, check the tag's screen to see whether the command ran.

### Send an image with a desktop reader

1. Open the **Pictures** page, choose an image, and set the size, orientation, and colors for the tag's display.
2. Place the tag on the reader, select **NFC image transfer**, and choose the serial port if prompted.
3. WebUI reads the tag's display settings and sends the image once they match. Wait for the display to refresh after the transfer. The tag displays the image directly.

Progress follows the data acknowledged by the tag. The displayed speed covers the data transfer phase and uses the compressed byte count when compression is active. If the transfer is interrupted or times out, check the tag before retrying.

## Send an image from Android

1. Get **D3-Pro InkTag** from the **InkTag QQ group 418446983** and check the group's notes for the version that matches your firmware. [Group access](pro.md#get-pro-firmware)
2. Select the tag's display size and color mode, choose an image, and preview the crop and orientation.
3. Start NFC transfer and hold the phone antenna against the tag until the app reports that the transfer has finished. Check the actual display configuration in WebUI if the app reports a size or color mismatch.
4. Wait for the tag to refresh. If the connection is unstable, try removing a thick or metal phone case and realigning the antenna.

See [supported devices](supported-devices.md) for board details. You can also send pictures over Bluetooth from [WebUI](webui.md).

## Measured transfer times

Type 2 and Type 4 are NFC Forum tag types. They describe how a phone or reader communicates with the tag.

- **NFC Forum Type 2 Tag** uses NFC-A for reading and writing. [NFC Forum Type 2 specification](https://nfc-forum.org/build/specifications/type-2-tag-specification/)
- **NFC Forum Type 4 Tag** communicates using the ISO Data Exchange Protocol (ISO-DEP). [NFC Forum Type 4 specification](https://nfc-forum.org/build/specifications/type-4-tag-specification/)

Hanshow TLSR8359 tags typically use Type 4. ZKONG tags with Goodix or OnMicro controllers typically use Type 2. These measurements show compressed image bytes and the time from the first image data to the last acknowledgement. Tag detection, setup, and screen refresh take additional time.

| Tested device | PN532/PCR532 reader | Android phone with D3-Pro InkTag |
| --- | --- | --- |
| Hanshow TLSR8359, Type 4, 4.2-inch | About 4.6 kB in **1.6 s** | About 7.4 kB in **2.5–2.8 s** |
| Hanshow Nebular-213R, Type 4, 2.13-inch | About 4.6 kB in **3.9 s** | Timing pending for this board |
| ZKONG Goodix, Type 2, 4.2-inch | About 4.6 kB in **8.9–9.0 s** | The same 4.6 kB image took **19.7–23.4 s**. One 29.2 kB photo took **126 s** |
| ZKONG OnMicro, Type 2, 4.2-inch | About 2.2 kB in **5.0 s** | About 14.3 kB in **71.7 s** |

**ZKONG Type 2 image transfer is slow. Use it for simple images that compress well on small screens. For other images, use Bluetooth.**

Complex images usually compress less and take longer to send even on the same screen. Once the app confirms that the tag accepted the refresh, you can move the phone away and wait for the display to update. The original iOS D3-Pro also transferred images successfully to the tested 4.2-inch Type 4 tag. The table lists only the timed Android runs.

## Use an NFC reader from the command line

The repository and full CLI ZIP include the PN532/PCR532 tool. It supports compatible Type 2 and Type 4 InkTag PRO tags, including link reading, text commands, time sync, and image transfer. [PN532/PCR532 setup and commands](../../tools/nfc/README.en.md)
