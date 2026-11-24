import { CircleHelp } from 'lucide-react'
import { useId, useState } from 'react'
import { resolveBoardProfile } from '../protocol/boards'

type Props = {
  connected: boolean
  temperature: number | null
  mcuTemperature: number | null
  panelTemperature: number | null
  boardId?: string
}

export function TemperatureFact({ connected, temperature, mcuTemperature, panelTemperature, boardId }: Props) {
  const [helpOpen, setHelpOpen] = useState(false)
  const [hovered, setHovered] = useState(false)
  const [focused, setFocused] = useState(false)
  const detailsOpen = helpOpen || hovered || focused
  const helpId = useId()
  const value = (sample: number | null) => connected && sample != null && Number.isFinite(sample) ? `${sample} °C` : '--'
  const platform = resolveBoardProfile(boardId ?? '')?.platform ?? ''
  // A missing/expired panel sample does not identify an unsupported sensor:
  // Nordic adapters can supply both sources, and ITT reports only validity.
  const sourceValue = (sample: number | null, unavailableLabel: string) => {
    if (!connected) return '--'
    if (sample != null && Number.isFinite(sample)) return `${sample} °C`
    return unavailableLabel || '--'
  }
  return <div className="temperature-fact">
    <dt>温度<div className="temperature-help-anchor">
      <button type="button" className="help-icon-button" aria-label="了解温度来源" aria-expanded={detailsOpen} aria-controls={helpId} aria-describedby={helpId} onMouseEnter={() => setHovered(true)} onMouseLeave={() => setHovered(false)} onFocus={() => setFocused(true)} onBlur={() => setFocused(false)} onClick={() => setHelpOpen((open) => !open)}><CircleHelp /></button>
      <div id={helpId} className="temperature-help" role="tooltip" hidden={!detailsOpen}>
        <p className="temperature-sources">屏幕 {sourceValue(panelTemperature, '')} / MCU {sourceValue(mcuTemperature, platform.startsWith('Dialog ') || platform === 'Telink TLSR8359' ? '不支持' : '')}</p>
        {connected && (temperature == null || !Number.isFinite(temperature)) && <p>尚无有效温度样本：设备可能尚未采样，或缓存已过期。</p>}
        {connected && temperature != null && Number.isFinite(temperature) && mcuTemperature == null && panelTemperature == null && <p>设备仅返回主温度，未提供独立来源读数；不将主温度猜测为屏温或 MCU 温度。</p>}
        <p>主温度取有效来源的较低值，不等同室温。这里按设备返回的有效样本显示；未读到或缓存已过期为 --，不代表屏幕没有传感器。连接或刷新时是否采样，取决于当前固件和屏幕支持。</p>
      </div>
    </div></dt>
    <dd className="temperature-values"><span aria-label="主温度">{value(temperature)}</span></dd>
  </div>
}
