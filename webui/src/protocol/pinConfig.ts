import type { ScreenConfig } from '../types'
import { validateScreenConfig } from './screen'
import { screenBusyDefaults } from './generatedBusyDefaults'
import { PinReportFlagV2 } from './generatedCommandsV2'
import { RELEASE_EDITABLE_ACTIVE_LOW_MASK, RELEASE_PIN_REPORT_VERSION } from './generatedReleaseV1'
import { boardDisplayName } from './boards'

export const pinRoles = ['mosi', 'clock', 'cs', 'dc', 'reset', 'busy', 'enable', 'red', 'green', 'blue', 'txd', 'rxd'] as const
export type PinRole = typeof pinRoles[number]
export type PinConfig = { activeLow: number; pins: Record<PinRole, number> }
export type PinReport = { overridePresent: boolean; defaultValid: boolean; recipeBusyDerived?: boolean; editableRoles: number; default: PinConfig | null; effective: PinConfig }
export const activeLowMask = RELEASE_EDITABLE_ACTIVE_LOW_MASK
export function gpioLabel(pin: number, family: string) {
  if (pin === 255) return '未连接'
  if (family.startsWith('tlsr8359_') && pin >= 0 && pin < 32) return `P${'ABCD'[pin >> 3]}${pin & 7} (${pin})`
  if (/^da14585(?:_|$)/.test(boardDisplayName(family)) && Number.isInteger(pin) && pin >= 0 && pin < 64) return `P${pin >> 4}.${pin & 15} (${pin})`
  return `GPIO ${pin}`
}
/** Controller recommendation only, before any board inverter; never an applied value. */
export function screenBusyActiveHigh(screen: ScreenConfig | null): boolean | null {
  if (!screen || !validateScreenConfig(screen)) return null
  return screenBusyDefaults[screen.driver * 256 + screen.recipe] ?? null
}
export class PinWriteUnconfirmedError extends Error {
  constructor() { super('引脚写入结果未确认；请读取同一设备的配置，不要重复写入。'); this.name = 'PinWriteUnconfirmedError' }
}

export function encodePinBlock(config: PinConfig) {
  const pins = pinRoles.map(role => config.pins[role])
  if (Object.keys(config.pins).length !== 12 || pins.some(pin => !Number.isInteger(pin) || pin < 0 || pin > 255)) throw new Error('完整填写 12 个引脚，设备 GPIO 编号为 0..255；255 表示未连接')
  if (pins[10] !== 255 || pins[11] !== 255) throw new Error('TXD 和 RXD 为保留位置，必须为255')
  if (!Number.isInteger(config.activeLow) || config.activeLow < 0 || config.activeLow > activeLowMask || (config.activeLow & ~activeLowMask)) throw new Error('引脚极性掩码无效')
  const active = pins.filter(pin => pin !== 255)
  if (new Set(active).size !== active.length) throw new Error('已连接的引脚不能重复')
  if (pins.some((pin, i) => pin === 255 && (config.activeLow & (1 << i)))) throw new Error('未连接的引脚不能设置极性')
  const count = pins.slice(0, 6).filter(pin => pin !== 255).length
  if (count !== 6 && (count !== 0 || pins[6] !== 255)) throw new Error('必须完整连接 6 个屏幕信号，或关闭整个屏幕和供电脚')
  const data = new Uint8Array(16)
  data[0] = 1
  new DataView(data.buffer).setUint16(2, config.activeLow, true)
  data.set(pins, 4)
  return data
}

export function decodePinBlock(data: Uint8Array): PinConfig {
  if (data.length !== 16 || data[0] !== 1 || data[1] !== 0) throw new Error('引脚数据版本、保留位或长度无效')
  const config: PinConfig = { activeLow: new DataView(data.buffer, data.byteOffset, data.byteLength).getUint16(2, true),
    pins: Object.fromEntries(pinRoles.map((role, i) => [role, data[4 + i]])) as Record<PinRole, number> }
  encodePinBlock(config)
  return config
}

export function decodePinReport(data: Uint8Array): PinReport {
  const knownFlags = PinReportFlagV2.OVERRIDE_PRESENT | PinReportFlagV2.DEFAULT_VALID | PinReportFlagV2.RECIPE_BUSY_DERIVED
  if (data.length !== 36 || data[0] !== RELEASE_PIN_REPORT_VERSION || (data[1] & ~knownFlags)) throw new Error('设备引脚配置响应无效')
  const overridePresent = Boolean(data[1] & 1), defaultValid = Boolean(data[1] & 2)
  const editableRoles = new DataView(data.buffer, data.byteOffset, data.byteLength).getUint16(2, true)
  if (editableRoles & ~0x3ff) throw new Error('设备可编辑引脚角色无效')
  const defaults = defaultValid ? decodePinBlock(data.subarray(4, 20)) : null
  if (!defaultValid && data.subarray(4, 20).some(byte => byte !== 0)) throw new Error('缺失默认值的编码无效')
  const effective = decodePinBlock(data.subarray(20))
  if (!overridePresent && (defaultValid ? !encodePinBlock(effective).every((byte, i) => byte === data[4 + i]) : pinRoles.some(role => effective.pins[role] !== 255))) throw new Error('设备有效引脚与来源标志不一致')
  return { overridePresent, defaultValid, recipeBusyDerived: Boolean(data[1] & PinReportFlagV2.RECIPE_BUSY_DERIVED), editableRoles, default: defaults, effective }
}

export function validatePinUpdate(config: PinConfig, current: PinReport) {
  encodePinBlock(config)
  for (const [index, role] of pinRoles.entries()) {
    if (!(current.editableRoles & (1 << index)) && (config.pins[role] !== current.effective.pins[role] || ((config.activeLow ^ current.effective.activeLow) & (1 << index)))) throw new Error(`固件不允许修改 ${role} 引脚`)
  }
}

export function pinSetRequest(config: PinConfig | null) {
  return config === null ? new Uint8Array([2, 0]) : new Uint8Array([2, 1, ...encodePinBlock(config)])
}
