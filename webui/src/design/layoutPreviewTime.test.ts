import { describe, expect, it } from 'vitest'
import { calendarMonthRows, calendarRowExample, previewDateTime, previewDefaultTimestamp, previewTimestamp } from './layoutPreviewTime'

describe('shared gallery civil time and row examples', () => {
  it('opens both galleries on September 20 without timezone conversion', () => {
    for (const timestamp of Object.values(previewDefaultTimestamp)) {
      const { date, time } = previewDateTime(timestamp)
      expect(time).toBe('14:28:00')
      expect(date).toBe('2026-09-20')
      expect(previewTimestamp(date, time)).toBe(timestamp)
    }
  })
  it('chooses genuine four/five/six-week months and selects the last day', () => {
    for (const weekStart of [0, 1] as const) for (const rows of [4, 5, 6] as const) {
      const timestamp = calendarRowExample(rows, weekStart, '23:59:59')
      expect(calendarMonthRows(timestamp, weekStart)).toBe(rows)
      expect(previewDateTime(timestamp).time).toBe('23:59:59')
      expect(previewDateTime(timestamp + 1).date.endsWith('-01')).toBe(true)
    }
  })
  it('validates leap days, date range and time rather than normalizing invalid inputs', () => {
    expect(previewTimestamp('2028-02-29', '00:00')).toBe(Date.UTC(2028, 1, 29) / 1000)
    for (const [date, time] of [['2026-02-29', '14:28'], ['2026-04-31', '14:28'], ['2026-09-01', '24:00'],
      ['2026-09-01', '12:60'], ['2026-09-01', '12:00:60'], ['', '14:28'], ['1999-12-31', '14:28'], ['2100-01-01', '14:28']]) {
      expect(previewTimestamp(date!, time!)).toBeUndefined()
    }
  })
  it('recounts rows when a custom month uses a different week start', () => {
    const timestamp = previewTimestamp('2026-03-31', '00:00')!
    expect(calendarMonthRows(timestamp, 1)).toBe(6)
    expect(calendarMonthRows(timestamp, 0)).toBe(5)
  })
})
