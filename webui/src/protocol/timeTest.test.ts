import { describe, expect, it, vi } from 'vitest'
import { DisplayModeV2, TestOperationV2 } from './generatedCommandsV2'
import { encodeTimeSetTest, encodeTimeShiftTest } from './timeTest'

describe('V2 time tests', () => {
  it('encodes signed minute and day shifts', () => {
    const minute = encodeTimeShiftTest(-60, DisplayModeV2.CLOCK)
    expect([...minute.slice(0, 2)]).toEqual([TestOperationV2.TIME_SHIFT, DisplayModeV2.CLOCK])
    expect(new DataView(minute.buffer).getInt32(2, true)).toBe(-60)

    const day = encodeTimeShiftTest(86400, DisplayModeV2.CALENDAR)
    expect(new DataView(day.buffer).getInt32(2, true)).toBe(86400)
  })

  it('encodes an absolute instant with timezone and week start', () => {
    const value = new Date('2026-12-31T23:59:00+08:00')
    const payload = encodeTimeSetTest(value, DisplayModeV2.CALENDAR, 1)
    const view = new DataView(payload.buffer)
    expect(payload[0]).toBe(TestOperationV2.TIME_SET)
    expect(view.getUint32(2, true)).toBe(Math.floor(value.getTime() / 1000))
    expect(payload[8]).toBe(1)
  })

  it('rejects zero shifts and invalid dates', () => {
    expect(() => encodeTimeShiftTest(0, DisplayModeV2.CLOCK)).toThrow(/非零/)
    expect(() => encodeTimeSetTest(new Date('invalid'), DisplayModeV2.CLOCK, 0)).toThrow(/有效/)
    for (const shift of [366 * 86400, -366 * 86400]) {
      expect(encodeTimeShiftTest(shift, DisplayModeV2.CLOCK)).toHaveLength(6)
      expect(() => encodeTimeShiftTest(shift + Math.sign(shift), DisplayModeV2.CLOCK)).toThrow('366')
    }
  })

  it('only checks UTC/local uint32 bounds instead of artificial year limits', () => {
    for (const [instant, offset, valid] of [
      ['2023-12-31T10:00:00Z', -840, true],
      ['2023-12-31T09:59:59Z', -840, true],
      ['2100-01-01T13:59:59Z', 840, true],
      ['2100-01-01T14:00:00Z', 840, true],
      ['2023-10-01T00:00:00Z', 0, true],
      ['1969-12-31T10:00:00Z', -840, false],
      ['1970-01-01T14:00:00Z', 840, true],
      ['2106-02-07T06:28:15Z', 0, true],
      ['2106-02-07T06:28:16Z', 0, false],
      ['2106-02-07T06:28:15Z', -840, false],
    ] as const) {
      const value = new Date(instant)
      vi.spyOn(value, 'getTimezoneOffset').mockReturnValue(offset)
      if (valid) expect(encodeTimeSetTest(value, DisplayModeV2.CALENDAR, 1)).toHaveLength(9)
      else expect(() => encodeTimeSetTest(value, DisplayModeV2.CALENDAR, 1)).toThrow('可表示的范围')
    }
  })
})
