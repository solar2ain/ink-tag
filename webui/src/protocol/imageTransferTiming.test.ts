import { expect, it } from 'vitest'
import { ImageTransferTiming, decodeTlsrNotifyCounters, observeImageTiming } from './imageTransferTiming'

it('separates bounded queue/native/notify timing without retaining packet history', () => {
  const timing = new ImageTransferTiming()
  timing.record('requestQueue', 2, 10)
  timing.record('writeQueue', 3, 13)
  timing.record('attWrite', 15, 28)
  timing.record('reply', 60, 70)
  timing.record('handler', 1, 71)
  timing.record('requestQueue', 2, 77)
  expect(timing.summary()).toContain('dispatchGap=5.0/5.0ms,n=1')
  for (let i = 0; i < 10000; i++) timing.record('attWrite', 15, 80 + i)
  expect(timing.summary()).toContain('attWrite=15.0/15.0ms,n=10001')
  expect(timing.summary()).toContain('reply=60.0/60.0ms,n=1')
  expect((timing as any).stats.size).toBe(6)
  const before = timing.summary()
  timing.record('reply', NaN, 10)
  timing.record('attWrite', -1, 10)
  expect(timing.summary()).toBe(before)
})

it('does not let instrumentation change the transport outcome', () => {
  expect(() => observeImageTiming(() => { throw Error('observer') }, 'attWrite', 5)).not.toThrow()
})

it('decodes only the present TLSR v4 notification counters without inventing zero', () => {
  const packet = new Uint8Array(36)
  packet[0] = 1; packet[16] = 4; packet[21] = 2; packet[22] = 0x34; packet[23] = 0x12
  packet[24] = 0x78; packet[25] = 0x56; packet[26] = 3
  expect(decodeTlsrNotifyCounters(packet)).toEqual({ lastStatus: 2, retries: 0x1234, drops: 0x5678, pending: 3 })
  expect(decodeTlsrNotifyCounters(packet.subarray(0, 26))).toBeNull()
  packet[16] = 3
  expect(decodeTlsrNotifyCounters(packet)).toBeNull()
})
