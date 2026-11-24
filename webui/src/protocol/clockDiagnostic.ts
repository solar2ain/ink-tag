export interface ClockDiagnostic {
  readOnly: true
  deviceUtcTimestamp: number
  deviceLocalTime: string
  timezoneMinutes: number
  weekStart: number
  hostLocalTime: string
  hostUtcBefore: string
  hostUtcAfter: string
  hostUtcMidpoint: string
  roundTripMilliseconds: number
  offsetFromHostSeconds: number | null
  uncertaintySeconds: number
  timeResolutionSeconds: 1
  hostClockStable: boolean
  deviceTimeValid: boolean
}

function localIso(timestampMs: number, minutes: number) {
  const sign = minutes < 0 ? '-' : '+'
  const absolute = Math.abs(minutes)
  const offset = `${sign}${String(Math.floor(absolute / 60)).padStart(2, '0')}:${String(absolute % 60).padStart(2, '0')}`
  return new Date(timestampMs + minutes * 60000).toISOString().slice(0, -1) + offset
}

export function clockDiagnostic(payload: Uint8Array, beforeMs: number, afterMs: number, roundTripMs: number): ClockDiagnostic {
  if (payload.length !== 7 || payload[6] > 6) throw new Error('设备时间响应无效')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const utc = view.getUint32(0, true)
  const timezoneMinutes = view.getInt16(4, true)
  if (timezoneMinutes < -840 || timezoneMinutes > 840 || ![beforeMs, afterMs, roundTripMs].every(Number.isFinite) || roundTripMs < 0) throw new Error('时间采样无效')
  const midpoint = (beforeMs + afterMs) / 2
  const hostClockStable = Math.abs(afterMs - beforeMs - roundTripMs) <= 250
  // The protocol contains integer UTC seconds. Do not claim sub-second or ppm accuracy.
  const deviceTimeValid = utc >= 946684800 && utc < 4102444800
  return {
    readOnly: true, deviceUtcTimestamp: utc, deviceLocalTime: localIso(utc * 1000, timezoneMinutes),
    timezoneMinutes, weekStart: payload[6], hostLocalTime: localIso(midpoint, -new Date(midpoint).getTimezoneOffset()),
    hostUtcBefore: new Date(beforeMs).toISOString(), hostUtcAfter: new Date(afterMs).toISOString(), hostUtcMidpoint: new Date(midpoint).toISOString(),
    roundTripMilliseconds: Math.round(roundTripMs * 1000) / 1000,
    offsetFromHostSeconds: hostClockStable && deviceTimeValid ? Math.round((utc - midpoint / 1000) * 1000) / 1000 : null,
    uncertaintySeconds: Math.round((1 + roundTripMs / 2000) * 1000) / 1000,
    timeResolutionSeconds: 1, hostClockStable, deviceTimeValid,
  }
}
