/** Civil display time: deliberately independent of the browser's timezone. */
// Keep historical oracle dates separate from the user-facing opening date.
export const previewReferenceTimestamp = { v0: 1788791280, v1: 1790346480 } as const
export const previewDefaultTimestamp = { v0: 1789914480, v1: 1789914480 } as const
export const previewDateLimits = { min: '2000-01-01', max: '2099-12-31' } as const
export type CalendarRows = 4 | 5 | 6

export function previewDateTime(timestamp: number) {
  const iso = new Date(timestamp * 1000).toISOString()
  return { date: iso.slice(0, 10), time: iso.slice(11, 19) }
}

export function previewTimestamp(date: string, time: string): number | undefined {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(date) || !/^\d{2}:\d{2}(:\d{2})?$/.test(time)
    || date < previewDateLimits.min || date > previewDateLimits.max) return undefined
  const normalized = time.length === 5 ? `${time}:00` : time
  const value = Date.parse(`${date}T${normalized}Z`)
  if (!Number.isFinite(value) || new Date(value).toISOString().slice(0, 19) !== `${date}T${normalized}`) return undefined
  return value / 1000
}

export function calendarMonthRows(timestamp: number, weekStart: 0 | 1): CalendarRows {
  const date = new Date(timestamp * 1000)
  const first = new Date(Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), 1)).getUTCDay()
  const days = new Date(Date.UTC(date.getUTCFullYear(), date.getUTCMonth() + 1, 0)).getUTCDate()
  return Math.ceil(((first - weekStart + 7) % 7 + days) / 7) as CalendarRows
}

/** Real months; select the last day to expose the bottom-row highlight. */
export function calendarRowExample(rows: CalendarRows, weekStart: 0 | 1, time: string) {
  const date = rows === 4 ? (weekStart === 1 ? '2021-02-28' : '2026-02-28')
    : rows === 5 ? '2026-09-30' : '2026-08-31'
  return previewTimestamp(date, time)!
}
