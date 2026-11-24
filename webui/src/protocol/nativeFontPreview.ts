import '../../../cli/src/inktag_cli/_assets/nativePreviewWasm.js'
import '../../../cli/src/inktag_cli/_assets/nativePreview.js'
import type { ScreenConfig } from '../types'
import { logicalImage } from '../../../packages/inktag-client-core/src/image'

export interface PreviewScreen {
  width: number; height: number; rotation?: number; mirrors?: number; nativeUiScale?: number
  visibleX?: number; visibleY?: number; visibleWidth?: number; visibleHeight?: number
  timestamp?: number; weekStart?: number; color?: number; pro?: boolean; brandHidden?: boolean
}
export interface NativeFontFrame { width: number; height: number; pixels: number[] }
export type FontPreviewPage = 'clock' | 'calendar' | 'device-info'
interface ReservedArea { width: number; height: number; digits: number }
interface NativeRenderer {
  defaultSizes(screen: PreviewScreen): { small: number; large: number; areas: { clock: ReservedArea; calendar: ReservedArea | null } }
  render(screen: PreviewScreen, page: FontPreviewPage, pkg?: number[], labels?: number[][]): NativeFontFrame
}
const shared = (globalThis as unknown as { InkTagNativePreview: {
  bytes(): Uint8Array; create(module?: WebAssembly.Module): NativeRenderer
} }).InkTagNativePreview
let renderer: Promise<NativeRenderer> | undefined
export function nativeFontRenderer() {
  renderer ??= WebAssembly.compile(shared.bytes() as Uint8Array<ArrayBuffer>)
    .then((module) => shared.create(module)).catch((error) => { renderer = undefined; throw error })
  return renderer
}
export function previewScreen(screen: ScreenConfig, pro = true): PreviewScreen {
  return { width: screen.width, height: screen.height, rotation: screen.transform & 3,
    mirrors: (screen.transform >> 2) & 3, nativeUiScale: screen.nativeUiScale,
    visibleX: screen.visibleX, visibleY: screen.visibleY,
    visibleWidth: screen.visibleWidth, visibleHeight: screen.visibleHeight, color: screen.color, pro }
}
export async function renderFontPreview(screen: PreviewScreen, page: FontPreviewPage, bytes?: Uint8Array,
  labels = ['InkTag Preview', 'IKT_XXXX', 'InkTag', 'MCU', '']) {
  const raw = (await nativeFontRenderer()).render(screen, page, bytes ? Array.from(bytes) : undefined,
    labels.map((s) => Array.from(new TextEncoder().encode(s))))
  // C returns raw panel coordinates, including mounting rotation/mirrors. The UI
  // shows the reading orientation, using the same inverse map as image readback.
  const data = new Uint8ClampedArray(raw.pixels.length * 4)
  raw.pixels.forEach((pixel, index) => { data[index * 4] = pixel; data[index * 4 + 3] = 255 })
  const frame = logicalImage({ width: raw.width, height: raw.height, data }, {
    width: screen.width, height: screen.height, visibleX: screen.visibleX ?? 0, visibleY: screen.visibleY ?? 0,
    visibleWidth: screen.visibleWidth ?? screen.width, visibleHeight: screen.visibleHeight ?? screen.height,
    transform: (screen.rotation ?? 0) | ((screen.mirrors ?? 0) << 2),
  })
  return { width: frame.width, height: frame.height,
    pixels: Array.from({ length: frame.width * frame.height }, (_, index) => frame.data[index * 4]) }
}
