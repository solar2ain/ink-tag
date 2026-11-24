import { describe, expect, it } from 'vitest'
import { telemetryFromMessage, temperatureRequest, voltageRequest } from './telemetry'

describe('device telemetry protocol', () => {
  it('uses the shared DIAG temperature suffix', () => {
    expect(Array.from(voltageRequest)).toEqual([0x56])
    expect(Array.from(temperatureRequest)).toEqual([0x54])
  })

  it('reads battery millivolts from the full diagnostic line', () => {
    expect(telemetryFromMessage('diag d=13 a=13 raw=0 busy=0 v=3300')).toEqual({ voltage: 3.3 })
    expect(telemetryFromMessage('diag d=13 a=13 raw=0 busy=0 v=0')).toEqual({})
  })

  it('reads signed chip temperature replies', () => {
    expect(telemetryFromMessage('temp=-7 source=die')).toEqual({ temperature: -7, mcuTemperature: -7, panelTemperature: null })
    expect(telemetryFromMessage('temp=-7 valid=1 die16=-112 die_valid=1 panel16=0 panel_valid=0')).toEqual({ temperature: -7, mcuTemperature: -7, panelTemperature: null })
    expect(telemetryFromMessage('temp=0 valid=0 die16=0 die_valid=0 panel16=0 panel_valid=0')).toEqual({ temperature: null, mcuTemperature: null, panelTemperature: null })
    expect(telemetryFromMessage('temp=999 valid=1 die16=15984 die_valid=1 panel16=0 panel_valid=0')).toEqual({})
  })

  it('keeps a compact nRF52811 diagnostic consistent and clears unavailable source details', () => {
    expect(telemetryFromMessage('diag v=3000 temp=22', 'aesl22_nrf')).toEqual({
      voltage: 3, temperature: 22, mcuTemperature: 22, panelTemperature: null,
    })
    expect(telemetryFromMessage('temp=0 valid=0', 'aesl22_nrf')).toEqual({
      temperature: null, mcuTemperature: null, panelTemperature: null,
    })
    expect(telemetryFromMessage('temp=22', 'unknown')).toEqual({
      temperature: 22, mcuTemperature: null, panelTemperature: null,
    })
    expect(telemetryFromMessage('temp=25 valid=1 die16=0 die_valid=0 panel16=408 panel_valid=1', 'aesl22_nrf')).toEqual({
      temperature: 25, mcuTemperature: null, panelTemperature: 25.5,
    })
  })

  it('does not mistake legacy time fields for temperature', () => {
    expect(telemetryFromMessage('t=1787700000 tz=8')).toEqual({})
  })
})
