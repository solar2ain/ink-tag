# Choosing fonts for an e-paper display

[简体中文](creation-fonts.md)

Open **Create** in the WebUI and start a static page or open a static template. Select a text block and use the text toolbar to change its font or size. Select part of the text to style only that part. To set the font for an entire list or table, use **Appearance → Font**.

| Font | Suggested size | Best for |
| --- | --- | --- |
| [Z Labs Pixel CN](https://github.com/Astro-2539/ZLabs-Pixel-12px) | 12 px, based on an 11×12 bitmap design | The default for larger screens such as 400×300. Good for body text, timetables, and checklists. Uses mainland Chinese glyph shapes. The author reports coverage of 12,994 Han characters. |
| [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) | Native 8, 10, and 12 px | Smaller screens default to the 10 px proportional Simplified Chinese strike. Try 8 px for very short labels, 10 px for 212×104 timetables and six-row lunar calendars at 296×152, and 12 px for body text and calendar labels at 400×300. [SIL OFL 1.1](https://github.com/TakWolf/fusion-pixel-font/blob/master/LICENSE-OFL) |
| [Muzai Pixel](https://github.com/DWNfonts/MuzaiPixel) | Original-width 8×12 px | Narrow lists and mixed Chinese–Latin text. Common Han characters advance by 8 px. The bundled revision covers all 6,763 GB2312 Han characters. |
| [Chill Bitmap](https://github.com/Warren2060/ChillBitmap) | 7 px glyphs on an 8 px cell | Short menu or status labels. Check complex characters on the display and switch to the 12 px sans font if they are hard to read. |
| [WenQuanYi Bitmap Song](https://sources.debian.org/src/xfonts-wqy/1.0.0~rc1-8/) | Native 12, 13, 14, 15, and 16 px | A classic device-style Song face. Start at 12 or 13 px. |
| Ma Shan Zheng and Kai | Start at 32 px | Titles, cards, and short quotations. Kai depends on the fonts available on your system. |

Start at the native size for small text. For larger text, choose a whole-number multiple such as 24 or 36 px. The WebUI enlarges each pixel by the same amount. WenQuanYi uses its own native glyphs at each size from 12 to 16 px. Select **1:1** above the canvas to check fine strokes at the original size. In the device-effect preview, **Fit** uses the available width and height. **1:1** shows native pixels, and the plus and minus buttons move in smaller steps. For narrow layouts, shorten the text or widen the text box.

The font picker offers Fusion, Z Labs, Muzai, Chill Bitmap, and WenQuanYi for static canvases, exported PNGs, and images sent to a tag. Device-rendered dynamic pages use the device font library. Fusion and Z Labs are included in the standalone HTML. For the other fonts, extract `assets/` alongside the HTML for offline use.
