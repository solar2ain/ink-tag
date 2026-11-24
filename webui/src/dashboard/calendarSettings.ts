import { ProtocolV2Error } from '../protocol/client'
import { StatusV2 } from '../protocol/generatedCommandsV2'

export function calendarCountdownNameIssue(value: string) {
  const name = value.trim()
  if (Array.from(name).length > 4) return '名称最多 4 个字。'
  if (new TextEncoder().encode(name).length > 12) return '名称最多 12 个 UTF-8 字节。'
  if (/[\u0000-\u001f\u007f]/u.test(name)) return '名称不能包含控制字符。'
  // Coverage belongs to the actual installed font, not the embedded subset.
  return ''
}

export function isUnsupportedWeekStartError(error: unknown) {
  return error instanceof ProtocolV2Error &&
    (error.status === StatusV2.UNSUPPORTED || error.status === StatusV2.UNKNOWN_COMMAND)
}
