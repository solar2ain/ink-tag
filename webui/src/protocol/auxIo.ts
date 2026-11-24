import type { AuxIoSettings, ButtonPull } from '../types'
import { RELEASE_AUX_VERSION, RELEASE_AUX_BYTES } from './generatedReleaseV1'

export type AuxButton = { gpio: number; activeLow: boolean; pull: ButtonPull }
export type AuxIoConfiguration = { version: 1; buttons: AuxButton[]; reserved: number[] }
const pulls = ['none', 'up', 'down'] as const
export const unusedButton = (): AuxButton => ({ gpio: 255, activeLow: false, pull: 'none' })
export function emptyAuxIoConfiguration(): AuxIoConfiguration {
  return { version: RELEASE_AUX_VERSION, buttons: [unusedButton(), unusedButton(), unusedButton()], reserved: [0, 255, 255, 208, 7] }
}
export function firstAuxIoSettings(config: AuxIoConfiguration): AuxIoSettings {
  const first = config.buttons[0]
  return { buttonGpio: first.gpio, buttonActiveLow: first.activeLow, buttonPull: first.pull, reserved: [...config.reserved] }
}
export function auxPins(config: AuxIoConfiguration): number[] {
  return config.buttons.map(button => button.gpio).filter(pin => pin !== 255)
}
export function encodeAuxIoConfiguration(config: AuxIoConfiguration): Uint8Array {
  if (!config || config.version !== RELEASE_AUX_VERSION || !Array.isArray(config.buttons) || config.buttons.length !== 3) throw Error('按键配置必须包含三个固定槽位')
  for (const button of config.buttons) {
    if (!Number.isInteger(button.gpio) || button.gpio < 0 || button.gpio > 255 || typeof button.activeLow !== 'boolean' || !pulls.includes(button.pull)) throw Error('按键 GPIO、有效电平或上下拉无效')
    if (button.gpio === 255 && (button.activeLow || button.pull !== 'none')) throw Error('未使用的按键槽不能配置有效电平或上下拉')
  }
  const pins = auxPins(config)
  if (new Set(pins).size !== pins.length) throw Error('按键引脚不能重复')
  if (!Array.isArray(config.reserved) || config.reserved.length !== 5 || config.reserved.some(value => !Number.isInteger(value) || value < 0 || value > 255)) throw Error('扩展配置的预留数据无效')
  const wire = new Uint8Array(RELEASE_AUX_BYTES)
  wire[0] = RELEASE_AUX_VERSION
  config.buttons.forEach((button, index) => { wire[1 + index * 2] = button.gpio; wire[2 + index * 2] = Number(button.activeLow) | (pulls.indexOf(button.pull) << 1) })
  wire.set(config.reserved, 7)
  return wire
}
export function decodeAuxIoConfiguration(wire: Uint8Array): AuxIoConfiguration {
  if (wire.length !== RELEASE_AUX_BYTES || wire[0] !== RELEASE_AUX_VERSION) throw Error('三按键 GPIO 版本或长度无效')
  const config: AuxIoConfiguration = { version: RELEASE_AUX_VERSION,
    buttons: [0, 1, 2].map(index => ({ gpio: wire[1 + index * 2], activeLow: Boolean(wire[2 + index * 2] & 1), pull: pulls[wire[2 + index * 2] >> 1] })),
    reserved: Array.from(wire.subarray(7)) }
  if (!encodeAuxIoConfiguration(config).every((value, index) => value === wire[index])) throw Error('三按键 GPIO 读回无效')
  return config
}
