# Bundled static calligraphy font

Ma Shan Zheng, Copyright 2018 The Ma Shan Zheng Project Authors.
SIL Open Font License 1.1; the complete notice is in `OFL-MaShanZheng.txt`.
It is also imported as visible license text in the WebUI, including the single-file build.

- Upstream: https://github.com/googlefonts/mashanzheng
- Pinned revision: `72c50ec001cea63d223d35562eeb2ba42f0fe67a`
- Source: `fonts/ttf/MaShanZheng-Regular.ttf` (5,857,936 bytes)
- Source SHA-256: `6d2546bb189c732a8ca29af9e22457b152387d158aa459e4ac2ce1e51788b7fb`
- WOFF2: 3,250,464 bytes
- WOFF2 SHA-256: `31563e1b1d107037b3ba46890fd4af7d0bb06d12dc87ab87a53c9757f7cb024a`

Converted without glyph subsetting or outline changes using FontTools 4.64.0
and Brotli 1.2.0:

```text
python -m fontTools.ttLib.woff2 compress MaShanZheng-Regular.ttf -o MaShanZheng-Regular.woff2
```

The hosted build loads this same-origin asset only when the font is requested.
The standalone build loads it from the sibling `assets/` folder. No external font CDN or
local-font enumeration permission is needed. Other existing font choices are
system-font preferences and are explicitly labelled as relying on the host.
This font rasterizes static content in the WebUI only; it never enters MCU fonts.

## Creation pixel fonts

Each asset contains row-packed, one-bit glyphs from the source strike. The
TTF sources use Pillow 13.0.0 in monochrome mode and FontTools 4.68.0 for the
Unicode cmap. WenQuanYi and Fusion use original BDF bitmaps and their metrics.
Atlas asset identifiers use InkTag names, with upstream names used for attribution
in the font picker. Fusion and Z Labs travel with the editor. The other
atlases load on demand from the WebUI's local `assets/` folder.

| Source input | Revision | SHA-256 |
| --- | --- | --- |
| `MZPXorig.ttf` | [Muzai Pixel `59e79de`](https://github.com/DWNfonts/MuzaiPixel/tree/59e79de047d344c7675944424e56bb9a1ab5503c) | `e07ef0f7b26991436f600e414ee5286164bd5ea1d779eb8914975d69af23e6f8` |
| `ZLabsPixel_12px_M_CN.ttf` | [Z Labs Pixel `Build_20260519`](https://github.com/Astro-2539/ZLabs-Pixel-12px/releases/tag/Build_20260519) | `c4bf6069b8e5e31dc42c81f310afbc322c2cbc843ceba6c871358f9f1aaae033` |
| `ChillBitmap7x.ttf` | [`ChillBitmap7x_v2.500.zip` at `f9e3b2f`](https://github.com/Warren2060/ChillBitmap/tree/f9e3b2f988a2c4107f866aa43c3d61a2b27254d5) | `a22c0fb431b99f282fc93e5a56276eb52d00b10c9d4cfc17c597a8537405dae0` |
| fusion-8px-zh-hans.bdf | [Fusion Pixel 2026.11.10](https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.11.10) | a51bff63c959664c78e0a285fda704c3eb2e3b0763fa1642662e0d16bb3aa817 |
| fusion-10px-zh-hans.bdf | Fusion Pixel 2026.11.10 | 74e76b78679bc82484821abba5db59506bb704841bd75fc12a20c58af2614f55 |
| fusion-12px-zh-hans.bdf | Fusion Pixel 2026.11.10 | 75df0e2ebe47db3dcd11444f58d9540010c0f691dfa75ede6bcc78b269c59085 |
| WenQuanYi source archive | [xfonts-wqy `1.0.0~rc1`](https://sources.debian.org/src/xfonts-wqy/1.0.0~rc1-8/) | `af655fedfb490aabbb8ce73602a08a6a847fafc78dc363ff3901aa6c3384ca5d` |

The Fusion archives contain the proportional `zh_hans` BDFs. Their notices are in `FusionPixel-NOTICES.txt`.

From the `webui/` directory, place the source TTF files, the three Fusion BDFs, and the five extracted
`wenquanyi_*.bdf` files together, then run
`python tools/generate-creation-fonts.py <source-directory>`. To recreate just
the WenQuanYi asset, extract `wenquanyi-bitmap-song-source.tar.gz`, then run
`python generate-creation-fonts.py wqy-bitmapsong --song-only --output .` with
the script copied beside the extracted directory. That path uses only the
Python standard library.
