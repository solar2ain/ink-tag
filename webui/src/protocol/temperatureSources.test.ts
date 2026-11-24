import { describe, expect, it } from 'vitest'
import { decodeTemperatureSources, statusTemperatureTelemetry } from './temperatureSources'

describe('GET_STATUS temperature sources', () => {
  it('keeps actual DA compact status source fields unavailable without turning invalid samples into zero', () => {
    const raw = (hex: string) => Uint8Array.from(hex.match(/../g)!, (byte) => parseInt(byte, 16))
    expect(statusTemperatureTelemetry(raw('010100000000ff800000100e0000e001'), 'aesl29_da')).toEqual({ temperature: null, mcuTemperature: null, panelTemperature: null })
    expect(statusTemperatureTelemetry(raw('010108000000ff170000100e0000e001'), 'aesl29_da')).toEqual({ temperature: 23, mcuTemperature: null, panelTemperature: null })
  })
  const base = new Uint8Array(16)
  base[0] = 1
  const vendor = new Uint8Array(21)
  vendor[0] = 0xf1

  function status(flags: number, mcu: number, panel: number) {
    const result = new Uint8Array(46)
    result.set(base)
    result.set(vendor, 16)
    result.set([0x49, 0x54, 0x54, 1, flags], 37)
    const view = new DataView(result.buffer)
    view.setInt16(42, mcu, true)
    view.setInt16(44, panel, true)
    return result
  }

  it('reads signed fractional sources without interpreting the vendor trailer', () => {
    expect(decodeTemperatureSources(status(3, -40, 408))).toEqual({ mcu: -2.5, panel: 25.5 })
    expect(decodeTemperatureSources(status(2, 0, 408))).toEqual({ mcu: null, panel: 25.5 })
  })

  it('leaves old or malformed source details unavailable', () => {
    expect(decodeTemperatureSources(new Uint8Array([...base, ...vendor]))).toBeNull()
    expect(decodeTemperatureSources(status(0, 0, 408))).toBeNull()
    expect(decodeTemperatureSources(status(0x80, 0, 0))).toBeNull()
  })

  it('identifies the qualified compact nRF52811 MCU sample without inventing another source', () => {
    const compact = new Uint8Array(base)
    compact[2] = 0x08
    compact[7] = 22
    expect(statusTemperatureTelemetry(compact, ' AESL22_NRF ')).toEqual({
      temperature: 22, mcuTemperature: 22, panelTemperature: null,
    })
    for (const board of ['unknown', 'gr551x_zkc42vm', 'nrf52832_priceetag', 'hs_nowa154']) {
      expect(statusTemperatureTelemetry(compact, board)).toEqual({
        temperature: 22, mcuTemperature: null, panelTemperature: null,
      })
    }
    compact[2] = 0
    expect(statusTemperatureTelemetry(compact, 'aesl22_nrf')).toEqual({
      temperature: null, mcuTemperature: null, panelTemperature: null,
    })
    compact[2] = 0x08
    compact[7] = 0xf9
    expect(statusTemperatureTelemetry(compact, 'aesl22_nrf')?.mcuTemperature).toBe(-7)
  })

  it('preserves explicit source validity and does not infer sources from malformed suffixes', () => {
    const explicit = status(2, 0, 408)
    explicit[2] = 0x08
    explicit[7] = 25
    expect(statusTemperatureTelemetry(explicit, 'aesl22_nrf')).toEqual({
      temperature: 25, mcuTemperature: null, panelTemperature: 25.5,
    })
    expect(statusTemperatureTelemetry(status(0, 0, 0), 'aesl22_nrf')?.mcuTemperature).toBeNull()
    expect(statusTemperatureTelemetry(status(0x80, 0, 0), 'aesl22_nrf')?.mcuTemperature).toBeNull()
    expect(statusTemperatureTelemetry(new Uint8Array(15), 'aesl22_nrf')).toBeNull()
  })
})
