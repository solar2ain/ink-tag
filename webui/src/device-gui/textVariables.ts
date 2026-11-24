import { firmwareSolarToLunar, firmwareTransformTime, lunarDateStrings, lunarMonthStrings, lunarStemStrings, lunarBranchStrings, lunarZodiacStrings } from './lunarFirmware'
import type { DeviceGuiSampleData, DeviceGuiWidget } from './model'
import type { TextPart } from './textDelivery'
import { resolveV1Countdown } from './countdownData'

const digits = '0123456789'
export const textVariables = [
  { id: 'date', label: '日期', possibleCharacters: digits + '年月日' },
  { id: 'year', label: '年份', possibleCharacters: digits },
  { id: 'month', label: '月份', possibleCharacters: digits },
  { id: 'day', label: '日', possibleCharacters: digits },
  { id: 'time', label: '时间', possibleCharacters: digits + ':' },
  { id: 'weekday', label: '星期', possibleCharacters: '星期日一二三四五六' },
  { id: 'lunar', label: '农历日期', possibleCharacters: lunarDateStrings.join('') + lunarMonthStrings.join('') + '闰' },
  { id: 'lunarYear', label: '农历年 / 生肖', possibleCharacters: lunarStemStrings.join('') + lunarBranchStrings.join('') + lunarZodiacStrings.join('') + '年' },
  { id: 'lunarMonth', label: '农历月份（不含月字）', possibleCharacters: lunarMonthStrings.join('') + '闰' },
  { id: 'lunarDay', label: '农历日', possibleCharacters: lunarDateStrings.join('') },
  { id: 'lunarYearName', label: '农历干支年', possibleCharacters: lunarStemStrings.join('') + lunarBranchStrings.join('') },
  { id: 'lunarZodiac', label: '生肖', possibleCharacters: lunarZodiacStrings.join('') },
  { id: 'weekdayName', label: '星期序号（不含星期）', possibleCharacters: '日一二三四五六' },
  { id: 'week', label: '周数', possibleCharacters: digits },
  { id: 'countdown', label: '倒计时天数', possibleCharacters: digits + '-' },
  { id: 'countdownName', label: '倒计时事件 / 节气' },
  { id: 'countdownDays', label: '事件 / 节气剩余天数', possibleCharacters: digits },
  { id: 'temperature', label: '温度', possibleCharacters: digits + '-℃' },
  { id: 'voltage', label: '电压', possibleCharacters: digits + '-.V' },
  { id: 'battery', label: '电量百分比', possibleCharacters: digits + '%' },
  { id: 'bluetoothName', label: '蓝牙名' },
  { id: 'deviceName', label: '设备名称' },
  { id: 'boardName', label: '板型' },
] as const

export function parseTextTemplate(text: string): TextPart[] {
  const parts: TextPart[] = [], pattern = /\{\{\s*([a-zA-Z]+)\s*\}\}/g
  let start = 0
  for (const match of text.matchAll(pattern)) {
    if (match.index! > start) parts.push({ type: 'literal', text: text.slice(start, match.index) })
    const variable = textVariables.find((item) => item.id === match[1])
    parts.push({ type: 'variable', id: match[1], possibleCharacters: variable && 'possibleCharacters' in variable ? variable.possibleCharacters : undefined })
    start = match.index! + match[0].length
  }
  if (start < text.length) parts.push({ type: 'literal', text: text.slice(start) })
  return parts
}

export function widgetPreviewSample(widget: DeviceGuiWidget, base?: DeviceGuiSampleData): DeviceGuiSampleData {
  const sample = { now: new Date(2026, 8, 25, 14, 28), voltage: 3.01, temperature: 24, batteryPercent: 81,
    activated: false, deviceName: 'InkTag', bluetoothName: 'IKT_GR_3A57', boardName: 'zkc42v', screenInfo: '', ...base }
  const local = widget.nativeElement?.sample ?? widget.data.sample
  if (!local) return sample
  const time = local.timestamp === undefined ? undefined : firmwareTransformTime(local.timestamp)
  return { ...sample, ...local, voltage: local.voltage === undefined ? sample.voltage : local.voltage / 1000,
    now: time ? new Date(time.year, time.month - 1, time.day, time.hour, time.minute, time.second) : sample.now }
}

export function resolveTextTemplate(text: string, widget: DeviceGuiWidget, base?: DeviceGuiSampleData) {
  const sample = widgetPreviewSample(widget, base), now = sample.now
  const year = now.getFullYear(), month = now.getMonth() + 1, day = now.getDate()
  const lunar = firmwareSolarToLunar(year, month, day)
  const utc = new Date(Date.UTC(year, month - 1, day)); utc.setUTCDate(utc.getUTCDate() + 4 - (utc.getUTCDay() || 7))
  const week = Math.ceil(((utc.getTime() - Date.UTC(utc.getUTCFullYear(), 0, 1)) / 86400000 + 1) / 7)
  const target = widget.data.countdownDate?.split('-').map(Number)
  const countdown = resolveV1Countdown(Math.trunc(Date.UTC(year, month - 1, day) / 1000), widget.data.countdownTarget)
  const values: Record<string, string | number> = { date: `${year}年${month}月${day}日`, year, month, day,
    time: `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`,
    weekday: `星期${'日一二三四五六'[now.getDay()]}`, lunar: `${lunar.isLeap ? '闰' : ''}${lunarMonthStrings[lunar.month]}${lunarDateStrings[lunar.day]}`,
    lunarYear: `${lunarStemStrings[lunar.year % 10]}${lunarBranchStrings[lunar.year % 12]}年${lunarZodiacStrings[lunar.year % 12]}`,
    week, countdown: target?.length === 3 ? Math.ceil((Date.UTC(target[0], target[1] - 1, target[2]) - Date.UTC(year, month - 1, day)) / 86400000) : countdown?.days ?? '',
    lunarMonth: `${lunar.isLeap ? '闰' : ''}${lunarMonthStrings[lunar.month].replace(/月$/, '')}`,
    lunarDay: lunarDateStrings[lunar.day], lunarYearName: `${lunarStemStrings[lunar.year % 10]}${lunarBranchStrings[lunar.year % 12]}`,
    lunarZodiac: lunarZodiacStrings[lunar.year % 12], weekdayName: '日一二三四五六'[now.getDay()],
    countdownName: countdown?.name ?? '', countdownDays: countdown?.days ?? '',
    temperature: sample.temperature == null ? '--℃' : `${Math.round(sample.temperature)}℃`,
    voltage: sample.voltage == null ? '--V' : `${sample.voltage.toFixed(1)}V`, battery: `${sample.batteryPercent}%`,
    bluetoothName: sample.bluetoothName, deviceName: sample.deviceName, boardName: sample.boardName }
  return text.replace(/\{\{\s*([a-zA-Z]+)\s*\}\}/g, (token, id: string) => String(values[id] ?? token))
}
