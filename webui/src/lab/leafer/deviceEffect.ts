import { decodeStoredImage, encodeImage } from '../../protocol/image'
import { canvasSizeForScreen, type RgbaFrame } from '../../../../packages/inktag-client-core/src/screen'
import { ditherFrame } from '../../image/processing'
import type { ScreenConfig } from '../../types'

export const previewPaper = '#e6e2d9'
/** Presentation only. The encoded/downloaded image keeps actual white. */
export function paperPreview(frame: RgbaFrame): RgbaFrame {
  const data = new Uint8ClampedArray(frame.data)
  for (let i = 0; i < data.length; i += 4) if (data[i] === 255 && data[i + 1] === 255 && data[i + 2] === 255) {
    data[i] = 230; data[i + 1] = 226; data[i + 2] = 217
  }
  return { width: frame.width, height: frame.height, data }
}

/** No transport or device identity: reuse the send codec for a matching profile. */
export function deviceEffect(source: RgbaFrame, color: number, screen?: ScreenConfig | null) {
  const size = screen && canvasSizeForScreen(screen)
  if (screen && screen.color === color && size?.width === source.width && size.height === source.height) {
    const encoded = encodeImage(source, screen)
    const payload = new Uint8Array(encoded.transfers.reduce((sum, transfer) => sum + transfer.data.length, 0))
    let offset = 0
    for (const transfer of encoded.transfers) { payload.set(transfer.data, offset); offset += transfer.data.length }
    return { frame: decodeStoredImage(payload, screen, encoded.format, encoded.transfers.length), profileMatched: true }
  }
  // An arbitrary canvas palette does not qualify any particular screen driver.
  return { frame: ditherFrame(source, { color, algorithm: 'none', strength: 1, contrast: 1, heavyInk: false }), profileMatched: false }
}
