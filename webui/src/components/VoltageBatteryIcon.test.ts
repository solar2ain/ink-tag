import { describe, expect, it } from 'vitest'
import { batteryVisualState } from './VoltageBatteryIcon'

describe('WebUI voltage battery icon', () => {
  it('maps 2.4 V to an empty red battery', () => {
    expect(batteryVisualState(2.4)).toEqual({ level: 0, tone: 'critical' })
    expect(batteryVisualState(2.2)).toEqual({ level: 0, tone: 'critical' })
  })

  it('uses a yellow outline through the 2.6 V warning boundary', () => {
    expect(batteryVisualState(2.5).tone).toBe('warning')
    expect(batteryVisualState(2.6).tone).toBe('warning')
    expect(batteryVisualState(2.61).tone).toBe('normal')
  })

  it('fills linearly and clamps at 3.3 V', () => {
    expect(batteryVisualState(2.85).level).toBeCloseTo(0.5)
    expect(batteryVisualState(3.3)).toEqual({ level: 1, tone: 'normal' })
    expect(batteryVisualState(3.6)).toEqual({ level: 1, tone: 'normal' })
  })

  it('does not invent a charge level for missing telemetry', () => {
    expect(batteryVisualState(null)).toEqual({ level: 0, tone: 'unknown' })
    expect(batteryVisualState(Number.NaN)).toEqual({ level: 0, tone: 'unknown' })
  })
})
