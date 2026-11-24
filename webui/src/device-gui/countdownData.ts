import { firmwareTransformTime, firmwareJieQiDistance, firmwareJieQiDate, firmwareMonthMaxDays, jieQiStrings } from './lunarFirmware'

/** Civil-calendar seconds, same contract as the native page renderer. */
export interface NativeCountdown { name: string; targetTimestamp: number }
export function resolveV1Countdown(timestamp: number, target?: NativeCountdown) {
  const name = typeof target?.name === 'string' ? target.name.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, '') : ''
  const day = (seconds: number) => Math.trunc(seconds / 86400)
  if (name && Number.isInteger(target!.targetTimestamp) && target!.targetTimestamp > 0
    && target!.targetTimestamp <= 0xffffffff && day(target!.targetTimestamp) >= day(timestamp)) {
    return { name, days: day(target!.targetTimestamp) - day(timestamp), source: 'custom' as const }
  }
  const time = firmwareTransformTime(timestamp)
  const term = firmwareJieQiDistance(time.year, time.month, time.day)
  if (term?.index === 24) {
    const next = firmwareJieQiDate(time.year + 1, 1, 1)
    if (next) term.days = firmwareMonthMaxDays(time.year, 12) - time.day + next.date
  }
  return term ? { name: jieQiStrings[term.index % 24]!, days: term.days, source: 'solar-term' as const } : undefined
}
