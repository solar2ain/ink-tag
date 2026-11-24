import type { AuxIoSettings, DeviceCapabilities } from '../types'
import boardNames from './boardNames.json'

// Signed executable identity, not a product model or a numeric board selector.
export const TLSR_FIRMWARE_FAMILY = 'tlsr8359_hanshow'

// These exact identities select only a transport and signed package target.
// Never resolve them to per-board pins, LED/NFC fittings or qualification.
const firmwareFamilies: Record<string, { ota: DeviceCapabilities['ota']; packageTarget: string }> = {
  "tlsr8359_hanshow": { ota: "telink", packageTarget: "tlsr8359_hanshow" },
  "gr5331_zkel": { ota: "goodix", packageTarget: "gr5331_zkc21v" },
  "gr551x_zkel": { ota: "goodix", packageTarget: "gr551x_zkc42v" },
  "gr5513_zkel_internal": { ota: "goodix", packageTarget: "gr5513_zkel_internal" },
  "om6626_zkcxxb": { ota: "onmicro", packageTarget: "om6626_zkcxxb" },
  "om6626_hsela": { ota: "onmicro", packageTarget: "om6626_hsela" },
}

export function firmwareFamilyProfile(identity: string): { ota: DeviceCapabilities['ota']; packageTarget: string } | undefined {
  return Object.hasOwn(firmwareFamilies, canonicalBoardId(identity)) ? firmwareFamilies[canonicalBoardId(identity)] : undefined
}

export const screenPinNames = ['MOSI', 'SCLK', 'CS', 'DC', 'RESET', 'BUSY', 'BS', 'WAKE', 'LED', 'EN'] as const
export type ScreenPinName = typeof screenPinNames[number]

export interface AuxIoPinOption {
  gpio: number
  label: string
}

export interface BoardProfile {
  label: string
  platform: string
  capabilities: DeviceCapabilities
  screenPins?: Partial<Record<ScreenPinName, string>>
  screenPinValues?: Partial<Record<ScreenPinName, number>>
  auxIoDefaults?: AuxIoSettings
  auxIoPins?: {
    button: AuxIoPinOption[]
  }
}

const om6626ScreenPins: Record<ScreenPinName, string> = {
  MOSI: 'GPIO21', SCLK: 'GPIO15', CS: 'GPIO22', DC: 'GPIO20', RESET: 'GPIO16',
  BUSY: 'GPIO03', BS: '未连接', WAKE: '未连接', LED: '未连接', EN: 'GPIO07',
}

const om6626ScreenPinValues: Record<ScreenPinName, number> = {
  MOSI: 21, SCLK: 15, CS: 22, DC: 20, RESET: 16, BUSY: 3,
  BS: 255, WAKE: 255, LED: 255, EN: 7,
}

const runtimeScreenPins: Record<ScreenPinName, string> = {
  MOSI: '设备配置', SCLK: '设备配置', CS: '设备配置', DC: '设备配置',
  RESET: '设备配置', BUSY: '设备配置', BS: '设备配置', WAKE: '设备配置',
  LED: '设备配置', EN: '设备配置',
}

const da14585ScreenPins: Partial<Record<ScreenPinName, string>> = {
  MOSI: 'P2.0', SCLK: 'P0.1', CS: 'P2.1', DC: 'P0.7', RESET: 'P1.0',
  BUSY: 'P1.1', BS: 'P2.2', EN: 'P2.3',
}

export const boardProfiles: Record<string, BoardProfile> = {
  aesl22_nrf: { label: 'AESL 2.1 / 2.9', platform: 'Nordic nRF52811', capabilities: { imageStore: true, nfc: 'none', button: false, auxReserved: false, led: 'rgb', ota: 'nordic' }, screenPins: runtimeScreenPins },
  nrf52832_priceetag: { label: 'PriceETag', platform: 'Nordic nRF52832', capabilities: { imageStore: true, nfc: 'none', button: true, auxReserved: false, led: 'none', ota: 'nordic' }, screenPins: { MOSI: 'P0.08', SCLK: 'P0.09', CS: 'P0.10', DC: 'P0.11', RESET: 'P0.12', BUSY: 'P0.13' } },
  gr5331_zkc21v: { label: 'ZKC21V', platform: 'Goodix GR5331', capabilities: { imageStore: true, nfc: 'none', button: false, auxReserved: false, led: 'rgb', ota: 'goodix' }, screenPins: { MOSI: 'GPIO08', SCLK: 'GPIO09', CS: 'GPIO0A', DC: 'GPIO0B', RESET: 'GPIO0C', BUSY: 'GPIO0D', EN: 'AON GPIO06' } },
  gr551x_zkc42vm: {
    label: 'ZKC42VM', platform: 'Goodix GR5513 / GR5515',
    capabilities: { imageStore: true, nfc: 'none', button: false, auxReserved: false, led: 'none', ota: 'goodix' },
    screenPins: runtimeScreenPins,
    auxIoDefaults: {
      buttonGpio: 26, buttonActiveLow: true, buttonPull: 'up',
      reserved: [0, 255, 255, 208, 7],
    },
    auxIoPins: {
      button: [{ gpio: 26, label: 'GPIO26 · RXD / IO' }],
    },
  },
  gr551x_zkc42v: {
    label: 'ZKC42V-N v1.2', platform: 'Goodix GR5513',
    capabilities: { imageStore: true, nfc: 'hardware', button: false, auxReserved: false, led: 'rgb', ota: 'goodix' },
    screenPins: runtimeScreenPins,
  },
  om6626_zkc42b: { label: 'ZKC42B', platform: 'OnMicro OM6626', capabilities: { imageStore: true, nfc: 'hardware', button: false, auxReserved: false, led: 'rgb', ota: 'onmicro' }, screenPins: om6626ScreenPins, screenPinValues: om6626ScreenPinValues },
  om6626_zkc21b: { label: 'ZKC21B', platform: 'OnMicro OM6626', capabilities: { imageStore: true, nfc: 'hardware', button: false, auxReserved: false, led: 'rgb', ota: 'onmicro' }, screenPins: om6626ScreenPins, screenPinValues: om6626ScreenPinValues },
  da14585_aesl: { label: 'AESL', platform: 'Dialog DA14585', capabilities: { imageStore: true, nfc: 'none', button: false, auxReserved: false, led: 'none', ota: 'dialog' }, screenPins: da14585ScreenPins },
  aesl42_da: { label: 'AESL-4.2', platform: 'Dialog DA14585', capabilities: { imageStore: true, nfc: 'none', button: false, auxReserved: false, led: 'none', ota: 'dialog' }, screenPins: da14585ScreenPins },
  aesl29_da: { label: 'AESL-2.9', platform: 'Dialog DA14585', capabilities: { imageStore: true, nfc: 'none', button: false, auxReserved: false, led: 'none', ota: 'dialog' }, screenPins: da14585ScreenPins },
  hs_nowa154: { label: 'Hanshow Nowa-154R-N', platform: 'Telink TLSR8359', capabilities: { imageStore: true, nfc: 'tap', button: false, auxReserved: false, led: 'rgb', ota: 'telink' }, screenPins: { MOSI: 'PB6', SCLK: 'PB5', CS: 'PB4', DC: 'PD7', RESET: 'PD4', BUSY: 'PA1', EN: 'PB7（低有效）' } },
  hs_nebular420: { label: 'Hanshow Nebular-420R-N', platform: 'Telink TLSR8359', capabilities: { imageStore: true, nfc: 'tap', button: false, auxReserved: false, led: 'rgb', ota: 'telink' }, screenPins: { MOSI: 'PB6', SCLK: 'PD7', CS: 'PD2', DC: 'PB7', RESET: 'PD4', BUSY: 'PA1', EN: 'PB5（低有效）' } },
}

export function canonicalBoardId(boardId: string) {
  return boardId.trim().toLowerCase()
}

/** Presentation only: keep firmware/tool selectors and security checks unchanged. */
export function boardDisplayName(boardId: string) {
  const id = canonicalBoardId(boardId)
  return Object.hasOwn(boardNames, id) ? (boardNames as Record<string, string>)[id] : id
}

/** MCU presentation only. Never use this lossy label for OTA or GPIO selection. */
export function mcuDisplayName(firmwareIdentity: string) {
  const identity = boardDisplayName(firmwareIdentity)
  const chip = /^(tlsr8359|om6626|gr5331|gr551[35x]|nrf52832|nrf52811|da14585)(?:_|$)/.exec(identity)?.[1]
  return chip === 'gr551x' ? 'GR551x' : chip?.toUpperCase() || '--'
}

export function resolveBoardProfile(boardId: string) {
  const id = canonicalBoardId(boardId)
  if (Object.hasOwn(boardProfiles, id)) return boardProfiles[id]
  const legacy = Object.entries(boardNames).find(([legacy, name]) => name === id && boardProfiles[legacy])?.[0]
  if (legacy) return boardProfiles[legacy]
  // These exact GR parts share the qualified runtime board profile, not identity.
  const gr = /^gr551[35]_(zkc42v|zkc42vm)$/.exec(id)
  return gr ? boardProfiles[`gr551x_${gr[1]}`] : undefined
}

/**
 * Board profiles describe fitted hardware. The protocol indicator bit only
 * describes whether this firmware exposes the configurable indicator API, so
 * an older/shared image must not erase a known board's physical LED identity.
 */
export function resolveBoardLedCapability(boardId: string, firmwareAdvertisesIndicator: boolean): DeviceCapabilities['led'] {
  const board = resolveBoardProfile(boardId)
  return board ? board.capabilities.led : firmwareAdvertisesIndicator ? 'rgb' : 'none'
}
