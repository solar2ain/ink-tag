/** Integer-for-integer TypeScript port of firmware/common/gui/inktag_calendar.c. */

export interface FirmwareTime {
  year: number
  month: number
  day: number
  hour: number
  minute: number
  second: number
  weekday: number
}

export interface FirmwareLunarDate {
  year: number
  month: number
  day: number
  isLeap: number
}

export const lunarMonthStrings = ['----', '正月', '二月', '三月', '四月', '五月', '六月', '七月', '八月', '九月', '十月', '冬月', '腊月'] as const
export const lunarMonthLeapStrings = [' ', '闰'] as const
export const lunarDateStrings = ['----', '初一', '初二', '初三', '初四', '初五', '初六', '初七', '初八', '初九', '初十', '十一', '十二', '十三', '十四', '十五', '十六', '十七', '十八', '十九', '二十', '廿一', '廿二', '廿三', '廿四', '廿五', '廿六', '廿七', '廿八', '廿九', '三十'] as const
export const lunarDayStrings = ['日', '一', '二', '三', '四', '五', '六'] as const
export const lunarZodiacStrings = ['猴', '鸡', '狗', '猪', '鼠', '牛', '虎', '兔', '龙', '蛇', '马', '羊'] as const
export const lunarStemStrings = ['庚', '辛', '壬', '癸', '甲', '乙', '丙', '丁', '戊', '己'] as const
export const lunarBranchStrings = ['申', '酉', '戌', '亥', '子', '丑', '寅', '卯', '辰', '巳', '午', '未'] as const
export const jieQiStrings = ['小寒', '大寒', '立春', '雨水', '惊蛰', '春分', '清明', '谷雨', '立夏', '小满', '芒种', '夏至', '小暑', '大暑', '立秋', '处暑', '白露', '秋分', '寒露', '霜降', '立冬', '小雪', '大雪', '冬至'] as const

const lunarMonthDays = [
  1997,
  0x0000b26d, 0x0000125c, 0x0000192c, 0x00009a95, 0x00001a94, 0x00001b4a, 0x00004b55, 0x00000ad4, 0x0000f55b,
  0x000004ba, 0x0000125a, 0x0000b92b, 0x0000152a, 0x00001694, 0x000096aa, 0x000015aa, 0x00012ab5, 0x00000974,
  0x000014b6, 0x0000ca57, 0x00000a56, 0x00001526, 0x00008e95, 0x00000d54, 0x000015aa, 0x000049b5, 0x0000096c,
  0x0000d4ae, 0x0000149c, 0x00001a4c, 0x0000bd26, 0x00001aa6, 0x00000b54, 0x00006d6a, 0x000012da, 0x0001695d,
  0x0000095a, 0x0000149a, 0x0000da4b, 0x00001a4a, 0x00001aa4, 0x0000bb54, 0x000016b4, 0x00000ada, 0x0000495b,
  0x00000936, 0x0000f497, 0x00001496, 0x0000154a, 0x0000b6a5, 0x00000da4, 0x000015b4, 0x00006ab6, 0x0000126e,
] as const

const solar11 = [
  1997,
  0x000f9c3c, 0x000f9e50, 0x000fa045, 0x000fa238, 0x000fa44c, 0x000fa641, 0x000fa836, 0x000faa49, 0x000fac3d,
  0x000fae52, 0x000fb047, 0x000fb23a, 0x000fb44e, 0x000fb643, 0x000fb837, 0x000fba4a, 0x000fbc3f, 0x000fbe53,
  0x000fc048, 0x000fc23c, 0x000fc450, 0x000fc645, 0x000fc839, 0x000fca4c, 0x000fcc41, 0x000fce36, 0x000fd04a,
  0x000fd23d, 0x000fd451, 0x000fd646, 0x000fd83a, 0x000fda4d, 0x000fdc43, 0x000fde37, 0x000fe04b, 0x000fe23f,
  0x000fe453, 0x000fe648, 0x000fe83c, 0x000fea4f, 0x000fec44, 0x000fee38, 0x000ff04c, 0x000ff241, 0x000ff436,
  0x000ff64a, 0x000ff83e, 0x000ffa51, 0x000ffc46, 0x000ffe3a, 0x0010004e, 0x00100242, 0x00100437, 0x0010064b,
] as const

const yearMonthBits = [
  0x4e, 0xa6, 0x99, 0x9c, 0xa2, 0x98, 0x80, 0x00, 0x18, 0x00, 0x10, 0x24, 0x4e, 0xa6, 0x99, 0x9c, 0xa2, 0x98, 0x80, 0x82, 0x18, 0x00, 0x10, 0x24, 0x4e, 0xa6, 0xd9, 0x9e, 0xa2, 0x98,
  0x80, 0x82, 0x18, 0x00, 0x10, 0x04, 0x4e, 0xe6, 0xd9, 0x9e, 0xa6, 0xa8, 0x80, 0x82, 0x18, 0x00, 0x10, 0x00, 0x0f, 0xe6, 0xd9, 0xbe, 0xa6, 0x98, 0x88, 0x82, 0x18, 0x80, 0x00, 0x00,
  0x0f, 0xef, 0xd9, 0xbe, 0xa6, 0x99, 0x8c, 0x82, 0x98, 0x80, 0x00, 0x00, 0x0f, 0xef, 0xdb, 0xbe, 0xa6, 0x99, 0x9c, 0xa2, 0x98, 0x80, 0x00, 0x18, 0x0f, 0xef, 0xdb, 0xbe, 0xa6, 0x99,
  0x9c, 0xa2, 0x98, 0x80, 0x00, 0x18, 0x0f, 0xef, 0xdb, 0xbe, 0xa2, 0x99, 0x8c, 0xa0, 0x98, 0x80, 0x82, 0x18, 0x0b, 0xef, 0xdb, 0xbe, 0xa6, 0x99, 0x8c, 0xa2, 0x98, 0x80, 0x82, 0x18,
  0x0f, 0xef, 0xdb, 0xbe, 0xe6, 0xd9, 0x9e, 0xa2, 0x98, 0x80, 0x82, 0x18, 0x0f, 0xef, 0xfb, 0xbf, 0xe6, 0xd9, 0x9e, 0xa6, 0x98, 0x80, 0x82, 0x18, 0x0f, 0xff, 0xff, 0xfc, 0xef, 0xd9, 0xbe, 0xa6, 0x18,
] as const

const jieQiBaseDays = [6, 20, 4, 19, 6, 21, 5, 20, 6, 21, 6, 21, 7, 23, 8, 23, 8, 23, 8, 24, 8, 22, 7, 22] as const
const monthDays = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31] as const

const div = (value: number, divisor: number) => Math.trunc(value / divisor)
const getBits = (value: number, length: number, shift: number) => (value >>> shift) & ((1 << length) - 1)

function solarToInt(year: number, month: number, day: number) {
  month = (month + 9) % 12
  year -= div(month, 10)
  return 365 * year + div(year, 4) - div(year, 100) + div(year, 400) + div(month * 306 + 5, 10) + day - 1
}

export function firmwareSolarToLunar(year: number, month: number, day: number): FirmwareLunarDate {
  let yearIndex = year - solar11[0]
  if (month < 1 || month > 12 || day < 1 || day > 31 || yearIndex < 3 || yearIndex > solar11.length - 2) {
    return { year: 0, month: 0, day: 0, isLeap: 0 }
  }
  const solarData = (year << 9) | (month << 5) | day
  if (solar11[yearIndex] > solarData) yearIndex -= 1
  const first = solar11[yearIndex]
  const firstYear = getBits(first, 12, 9)
  const firstMonth = getBits(first, 4, 5)
  const firstDay = getBits(first, 5, 0)
  let offset = solarToInt(year, month, day) - solarToInt(firstYear, firstMonth, firstDay) + 1
  const encodedDays = lunarMonthDays[yearIndex]
  const leap = getBits(encodedDays, 4, 13)
  let lunarMonth = 1
  for (let index = 0; index < 13; index += 1) {
    const days = getBits(encodedDays, 1, 12 - index) === 1 ? 30 : 29
    if (offset <= days) break
    lunarMonth += 1
    offset -= days
  }
  let isLeap = 0
  if (leap !== 0 && lunarMonth > leap) {
    if (lunarMonth === leap + 1) isLeap = 1
    lunarMonth -= 1
  }
  return { year: yearIndex + solar11[0], month: lunarMonth, day: offset, isLeap }
}

export function firmwareIsLeapYear(year: number) {
  return year % 100 === 0 ? year % 400 === 0 : year % 4 === 0
}

export function firmwareDayOfWeek(month: number, day: number, year: number) {
  const offsets = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4]
  if (month < 3) year -= 1
  return (year + div(year, 4) - div(year, 100) + div(year, 400) + offsets[month - 1] + day) % 7
}

function isoWeeksInYear(year: number) {
  const januaryFirst = firmwareDayOfWeek(1, 1, year)
  return januaryFirst === 4 || (januaryFirst === 3 && firmwareIsLeapYear(year)) ? 53 : 52
}

export function firmwareIsoWeek(year: number, month: number, day: number) {
  let dayOfYear = day
  for (let index = 1; index < month; index += 1) dayOfYear += monthDays[index - 1]
  if (month > 2 && firmwareIsLeapYear(year)) dayOfYear += 1
  const weekday = firmwareDayOfWeek(month, day, year)
  const isoWeekday = weekday === 0 ? 7 : weekday
  const week = div(dayOfYear - isoWeekday + 10, 7)
  if (week < 1) return isoWeeksInYear(year - 1)
  if (week > isoWeeksInYear(year)) return 1
  return week
}

export function firmwareMonthMaxDays(year: number, month: number) {
  return monthDays[month - 1] + (month === 2 && year % 4 === 0 ? 1 : 0)
}

export function firmwareTransformTime(unixTime: number): FirmwareTime {
  let remaining = Math.trunc(unixTime)
  let year = 1970
  while (true) {
    const seconds = firmwareIsLeapYear(year) ? 31622400 : 31536000
    if (remaining < seconds) break
    remaining -= seconds
    year += 1
  }
  let month = 1
  while (true) {
    const seconds = firmwareMonthMaxDays(year, month) * 86400
    if (remaining < seconds) break
    remaining -= seconds
    month += 1
  }
  const day = div(remaining, 86400) + 1
  remaining %= 86400
  const hour = div(remaining, 3600)
  remaining %= 3600
  const minute = div(remaining, 60)
  return { year, month, day, hour, minute, second: remaining % 60, weekday: firmwareDayOfWeek(month, day, year) }
}

export function firmwareJieQiDate(year: number, month: number, day: number) {
  if (year < 2000 || year > 2050 || month < 1 || month > 12) return undefined
  let index = (month - 1) * 2
  if (day >= 15) index += 1
  const byte = yearMonthBits[(year - 2000) * 3 + div(index, 8)]
  const adjusted = ((byte << (index % 8)) & 0x80) !== 0
  let date = jieQiBaseDays[index]
  if (adjusted) date += (index === 1 || index === 11 || index === 18 || index === 21) && year < 2044 ? 1 : -1
  return { index, date }
}

export function firmwareJieQiDistance(year: number, month: number, day: number) {
  const current = firmwareJieQiDate(year, month, day)
  if (!current) return undefined
  let index = current.index
  if (day === current.date) return { index, days: 0 }
  if (day < current.date) return { index, days: current.date - day }
  index += 1
  if (day < 15) {
    const next = firmwareJieQiDate(year, month, 15)!
    return { index, days: next.date - day }
  }
  const max = firmwareMonthMaxDays(year, month)
  const nextMonth = month === 12 ? 1 : month + 1
  const next = firmwareJieQiDate(year, nextMonth, 1)!
  return { index, days: max - day + next.date }
}
