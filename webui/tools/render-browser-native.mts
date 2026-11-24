import { mkdirSync, writeFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { renderFirmwareNativePage, type NativePageKind } from '../src/device-gui/nativePageRenderer'

const [widthValue, heightValue, pageValue, scaleValue, freeValue, outputValue, revisionValue = 'v0', editionValue = 'free'] = process.argv.slice(2)
const width = Number(widthValue)
const height = Number(heightValue)
const page = pageValue as NativePageKind
const scale = Number(scaleValue)
const freeMark = freeValue === '1'
if (!Number.isInteger(width) || !Number.isInteger(height) || !['device-info', 'calendar', 'clock'].includes(page) || !Number.isInteger(scale) || !outputValue || !['v0', 'v1'].includes(revisionValue) || !['free', 'pro'].includes(editionValue)) {
  throw new Error('usage: render-browser-native.mts WIDTH HEIGHT device-info|calendar|clock SCALE_ENUM FREE_MARK OUTPUT.ppm [v0|v1] [free|pro]')
}

const frame = renderFirmwareNativePage({
  width,
  height,
  page,
  nativeUiScale: scale,
  freeMark,
  layoutRevision: revisionValue as 'v0' | 'v1',
  edition: editionValue as 'free' | 'pro',
})
const palette = [
  [255, 255, 255],
  [23, 26, 24],
  [200, 45, 40],
] as const
const rgb = new Uint8Array(width * height * 3)
frame.pixels.forEach((pixel, index) => rgb.set(palette[pixel] ?? palette[0], index * 3))
const output = resolve(outputValue)
mkdirSync(dirname(output), { recursive: true })
writeFileSync(output, Buffer.concat([Buffer.from(`P6\n${width} ${height}\n255\n`), Buffer.from(rgb)]))
