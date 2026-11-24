import { creationPixelFont, creationPixelFonts, pixelFontSize, pixelFontSizes } from '../../data/creationFonts'
import { fontOptions } from '../../data/fonts'
import { calligraphyFamily } from '../../data/bundledFontMetadata'
import { deviceGuiFontOptions } from '../../device-gui/model'
import { LiveNumberInput } from './LiveNumberInput'
import type { PageMode } from '../../../../packages/inktag-editor-core/src/index'

export function CreationFontOptions({ mode }: { mode: PageMode }) {
  return <>
    <option value="" disabled>选择字体</option>
    {mode === 'static' && <optgroup label="墨水屏点阵">
      {creationPixelFonts.map(font => <option key={font.family} value={font.family}>{font.label}</option>)}
    </optgroup>}
    {mode !== 'host-dynamic' && <optgroup label="设备字库">
      {deviceGuiFontOptions.map(font => <option key={font.id} value={font.id}>{font.shortLabel}</option>)}
    </optgroup>}
    <optgroup label="大字与标题">
      {fontOptions.map(font => (
        <option key={font.id} value={font.family}>{font.label}</option>
      ))}
    </optgroup>
  </>
}

export function creationFontValue(value: string, mode: PageMode) {
  if (value === 'Ma Shan Zheng') return calligraphyFamily
  const pixel = mode === 'static' ? creationPixelFont(value) : undefined
  if (pixel) return pixel.family
  if (mode !== 'host-dynamic' && deviceGuiFontOptions.some(font => font.id === value)) return value
  return fontOptions.some(font => font.family === value) ? value : ''
}

export function selectedFontSize(value: string, current: number) {
  const pixelFont = creationPixelFont(value)
  if (pixelFont) return pixelFontSize(pixelFont, current)
  return value.includes('Ma Shan Zheng') ? Math.max(32, current) : current
}

export function CreationFontSize({ family, mode, value, label, onChange }: {
  family?: string; mode: PageMode; value: number; label: string; onChange: (value: number) => void
}) {
  const font = mode === 'static' && creationPixelFont(family)
  if (!font) return <LiveNumberInput value={value} max={240} onChange={onChange} label={label}/>
  const sizes = pixelFontSizes(font)
  return <select className="pixel-font-size" aria-label={label} value={pixelFontSize(font, value)} onChange={event => onChange(Number(event.target.value))}>
    {sizes.map(size => <option key={size} value={size}>{size}{font.sizes.some(native => native === size) ? ' · 原生' : ''}</option>)}
  </select>
}
