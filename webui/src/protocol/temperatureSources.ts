import { canonicalBoardId } from './boards'

export type TemperatureSources = { mcu: number | null; panel: number | null }

/** aesl22_nrf's compact status/DIAG temperature is sampled with sd_temp_get. */
export function compactTemperatureSources(temperature: number | null, boardId: string): TemperatureSources {
  return { mcu: canonicalBoardId(boardId) === 'aesl22_nrf' ? temperature : null, panel: null }
}

/** Optional final GET_STATUS temperature-source trailer; absent on compact firmware. */
export function decodeTemperatureSources(payload: Uint8Array): { mcu: number | null; panel: number | null } | null {
  if (payload.length < 25 || payload[0] !== 1) return null
  const offset = payload.length - 9
  if (payload[offset] !== 0x49 || payload[offset + 1] !== 0x54 || payload[offset + 2] !== 0x54 || payload[offset + 3] !== 1) return null
  const flags = payload[offset + 4]
  if (flags & ~0x03) return null
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const mcu = view.getInt16(offset + 5, true)
  const panel = view.getInt16(offset + 7, true)
  if ((!(flags & 1) && mcu !== 0) || (!(flags & 2) && panel !== 0)) return null
  return { mcu: flags & 1 ? mcu / 16 : null, panel: flags & 2 ? panel / 16 : null }
}

export function statusTemperatureTelemetry(payload: Uint8Array, boardId: string) {
  if (payload.length < 16 || payload[0] !== 1) return null
  const temperature = payload[2] & 0x08
    ? new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getInt8(7)
    : null
  const sources = decodeTemperatureSources(payload)
    ?? compactTemperatureSources(temperature, payload.length === 16 ? boardId : '')
  return { temperature, mcuTemperature: sources.mcu, panelTemperature: sources.panel }
}
