import { expect, it, vi } from 'vitest'
import fixtures from '../../../tests/fixtures/client-diagnostic-parity.json'
import { clockDiagnostic } from './clockDiagnostic'
import { InkTagClient } from './client'
import { useDeviceStore } from '../store/device'

it('uses the same clock vectors and uncertainty as the CLI', () => {
  for (const sample of fixtures.clock) {
    const payload = new Uint8Array(7); const view = new DataView(payload.buffer)
    view.setUint32(0, sample.utc, true); view.setInt16(4, sample.timezoneMinutes, true); payload[6] = sample.weekStart
    const result = clockDiagnostic(payload, sample.beforeMs, sample.afterMs, sample.rttMs)
    expect(result.offsetFromHostSeconds).toBe(sample.offset); expect(result.uncertaintySeconds).toBe(sample.uncertainty)
  }
})
it('shipping diagnostics agree on voltage and independent temperature validity for all six families', async () => {
  for (const sample of fixtures.status) {
    useDeviceStore.getState().resetDevice(); useDeviceStore.setState({boardId: sample.board})
    const payload = Uint8Array.from(sample.payloadHex.match(/../g)!.map(byte => parseInt(byte, 16)))
    const client = new InkTagClient(); Object.assign(client, {requestV2: vi.fn().mockResolvedValue({payload})})
    await client.readDiagnostics()
    const state = useDeviceStore.getState()
    expect({voltage: state.voltage, temperature: state.temperature, mcuTemperature: state.mcuTemperature, panelTemperature: state.panelTemperature}).toEqual({voltage: sample.voltage, temperature: sample.temperature, mcuTemperature: sample.mcuTemperature, panelTemperature: sample.panelTemperature})
  }
  useDeviceStore.getState().resetDevice()
})
