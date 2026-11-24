import { describe, expect, it } from 'vitest'
import { calendarCountdownNameIssue, isUnsupportedWeekStartError } from '../dashboard/calendarSettings'
import { ProtocolV2Error } from '../protocol/client'
import { StatusV2 } from '../protocol/generatedCommandsV2'

describe('calendar countdown name', () => {
  it('accepts up to four characters independently of the embedded subset', () => {
    expect(calendarCountdownNameIssue('国庆')).toBe('')
    expect(calendarCountdownNameIssue('宝宝')).toBe('')
  })

  it('rejects a fifth character before rendering', () => {
    expect(calendarCountdownNameIssue('国庆节假期')).toBe('名称最多 4 个字。')
  })

  it('leaves glyph coverage to the actual font and enforces wire constraints only', () => {
    expect(calendarCountdownNameIssue('龘')).toBe('')
    expect(calendarCountdownNameIssue('😀😀😀😀')).toContain('12 个 UTF-8 字节')
    expect(calendarCountdownNameIssue('宝\u0000')).toContain('控制字符')
  })

  it('falls back only for unsupported setting commands', () => {
    expect(isUnsupportedWeekStartError(new ProtocolV2Error(StatusV2.UNSUPPORTED))).toBe(true)
    expect(isUnsupportedWeekStartError(new ProtocolV2Error(StatusV2.UNKNOWN_COMMAND))).toBe(true)
    expect(isUnsupportedWeekStartError(new ProtocolV2Error(StatusV2.IO_ERROR))).toBe(false)
  })
})
