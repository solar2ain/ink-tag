import { DisplayModeV2, TestOperationV2 } from './generatedCommandsV2'

export type TimeTestDisplayMode = typeof DisplayModeV2.CALENDAR | typeof DisplayModeV2.CLOCK

function assertDisplayMode(mode: number): asserts mode is TimeTestDisplayMode {
  if (mode !== DisplayModeV2.CALENDAR && mode !== DisplayModeV2.CLOCK) {
    throw new RangeError('测试页面必须是日历或时钟')
  }
}

export function encodeTimeShiftTest(deltaSeconds: number, mode: TimeTestDisplayMode) {
  assertDisplayMode(mode)
  if (!Number.isInteger(deltaSeconds) || deltaSeconds === 0 || Math.abs(deltaSeconds) > 366 * 86400) {
    throw new RangeError('测试时间偏移必须是 366 天以内的非零整秒数')
  }
  const payload = new Uint8Array(6)
  payload[0] = TestOperationV2.TIME_SHIFT
  payload[1] = mode
  new DataView(payload.buffer).setInt32(2, deltaSeconds, true)
  return payload
}

export function encodeTimeSetTest(value: Date, mode: TimeTestDisplayMode, weekStart: number) {
  assertDisplayMode(mode)
  if (Number.isNaN(value.getTime())) throw new RangeError('请输入有效的测试日期时间')
  if (!Number.isInteger(weekStart) || weekStart < 0 || weekStart > 6) {
    throw new RangeError('每周起始日必须为 0..6')
  }
  const timestamp = Math.floor(value.getTime() / 1000)
  const timezoneMinutes = -value.getTimezoneOffset()
  if (timezoneMinutes < -840 || timezoneMinutes > 840) {
    throw new RangeError('时区必须在 UTC-14:00 到 UTC+14:00 之间')
  }
  const localTimestamp = timestamp + timezoneMinutes * 60
  if (timestamp < 0 || timestamp > 0xffffffff || localTimestamp < 0 || localTimestamp > 0xffffffff) {
    throw new RangeError('这个日期超出了设备时间可表示的范围')
  }
  const payload = new Uint8Array(9)
  payload[0] = TestOperationV2.TIME_SET
  payload[1] = mode
  const view = new DataView(payload.buffer)
  view.setUint32(2, timestamp, true)
  view.setInt16(6, timezoneMinutes, true)
  payload[8] = weekStart
  return payload
}

export function localDateTimeInputValue(value = new Date()) {
  const local = new Date(value.getTime() - value.getTimezoneOffset() * 60000)
  return local.toISOString().slice(0, 16)
}
