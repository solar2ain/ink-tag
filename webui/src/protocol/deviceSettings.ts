import type {
  AuxIoSettings, ButtonActionSettings, ButtonBehaviorSettings, ButtonGesture, DeviceInputAction, DeviceSettingsCapabilities,
  LedPolicy, LedSettings, NfcTapSettings,
} from '../types'
import { SettingsSectionV2 } from './generatedCommandsV2'

export const DEVICE_SETTINGS_PROTOCOL_VERSION = 1
export const DISPLAY_SETTINGS_PROTOCOL_VERSION = 1
export const DISPLAY_SETTINGS_BOOT_REFRESH = 0x01
export const DISPLAY_SETTINGS_BRAND_HIDDEN = 0x10
export const DISPLAY_SETTINGS_BASE_SIZE = 9
export const DISPLAY_SETTINGS_NAME_MAX_BYTES = 12
export const DISPLAY_SETTINGS_NAME_MAX_CODEPOINTS = 4
export const BLUETOOTH_NAME_MAX_BYTES = 19
export const BLUETOOTH_NAME_PREFIX = 'IKT_'
export const BLUETOOTH_NAME_LEGACY_PREFIX = 'InkTag_'
export const BLUETOOTH_NAME_PREFIXES = [BLUETOOTH_NAME_PREFIX, BLUETOOTH_NAME_LEGACY_PREFIX] as const
export const DEVICE_LABEL_MAX_BYTES = 24

export interface DisplaySettingsV2 {
  version: number
  supported: boolean
  complete: boolean
  weekStart: 0 | 1
  bootRefreshEnabled: boolean
  brandVisibilitySupported: boolean
  brandVisible: boolean
  countdownTargetDay: number
  countdownName: string
  countdownDate: string
}

export function calendarDateToTargetDay(value: string) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value)
  if (!match) return 0
  const year = Number(match[1])
  const month = Number(match[2])
  const day = Number(match[3])
  const date = new Date(Date.UTC(year, month - 1, day))
  if (date.getUTCFullYear() !== year || date.getUTCMonth() !== month - 1 ||
      date.getUTCDate() !== day) return 0
  return Math.floor(date.getTime() / 86_400_000)
}

export function targetDayToCalendarDate(targetDay: number) {
  if (!Number.isInteger(targetDay) || targetDay <= 0 || targetDay > 0xffffffff) return ''
  const date = new Date(targetDay * 86_400_000)
  if (!Number.isFinite(date.getTime())) return ''
  return `${String(date.getUTCFullYear()).padStart(4, '0')}-${String(date.getUTCMonth() + 1).padStart(2, '0')}-${String(date.getUTCDate()).padStart(2, '0')}`
}

export function encodeDisplaySettings(settings: Pick<DisplaySettingsV2,
  'weekStart' | 'bootRefreshEnabled' | 'brandVisible' | 'countdownTargetDay' | 'countdownName'>) {
  if (settings.weekStart !== 0 && settings.weekStart !== 1) throw new RangeError('每周起始日必须为周日或周一')
  const name = settings.countdownName.trim()
  const encodedName = new TextEncoder().encode(name)
  if (Array.from(name).length > DISPLAY_SETTINGS_NAME_MAX_CODEPOINTS ||
      encodedName.byteLength > DISPLAY_SETTINGS_NAME_MAX_BYTES) {
    throw new RangeError('倒计时名称最多 4 个字、12 个 UTF-8 字节')
  }
  if (!Number.isInteger(settings.countdownTargetDay) || settings.countdownTargetDay < 0 ||
      settings.countdownTargetDay > 0xffffffff ||
      (settings.countdownTargetDay === 0) !== (encodedName.byteLength === 0)) {
    throw new RangeError('倒计时名称和目标日期必须同时填写或同时清空')
  }
  const payload = new Uint8Array(DISPLAY_SETTINGS_BASE_SIZE + encodedName.byteLength)
  payload.set([
    SettingsSectionV2.DISPLAY, DISPLAY_SETTINGS_PROTOCOL_VERSION, settings.weekStart,
    (settings.bootRefreshEnabled ? DISPLAY_SETTINGS_BOOT_REFRESH : 0) |
      (settings.brandVisible ? 0 : DISPLAY_SETTINGS_BRAND_HIDDEN),
  ])
  new DataView(payload.buffer).setUint32(4, settings.countdownTargetDay, true)
  payload[8] = encodedName.byteLength
  payload.set(encodedName, DISPLAY_SETTINGS_BASE_SIZE)
  return payload
}

export function decodeDisplaySettings(payload: Uint8Array): DisplaySettingsV2 | null {
  if (payload.length < 4 || payload[0] !== SettingsSectionV2.DISPLAY ||
      payload[1] !== DISPLAY_SETTINGS_PROTOCOL_VERSION || payload[2] > 1) return null
  const flags = payload[3]
  const complete = payload.length >= DISPLAY_SETTINGS_BASE_SIZE
  let countdownTargetDay = 0
  let countdownName = ''
  if (complete) {
    if (payload.length !== DISPLAY_SETTINGS_BASE_SIZE + payload[8]) return null
    countdownTargetDay = new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint32(4, true)
    try { countdownName = new TextDecoder('utf-8', { fatal: true }).decode(payload.subarray(DISPLAY_SETTINGS_BASE_SIZE)) } catch { return null }
    const encodedName = new TextEncoder().encode(countdownName)
    if (encodedName.byteLength !== payload[8] || encodedName.byteLength > DISPLAY_SETTINGS_NAME_MAX_BYTES ||
        Array.from(countdownName).length > DISPLAY_SETTINGS_NAME_MAX_CODEPOINTS ||
        (countdownTargetDay === 0) !== (encodedName.byteLength === 0)) return null
  }
  return {
    version: payload[1], supported: complete, complete,
    weekStart: payload[2] as 0 | 1,
    bootRefreshEnabled: (flags & DISPLAY_SETTINGS_BOOT_REFRESH) !== 0,
    brandVisibilitySupported: true,
    brandVisible: (flags & DISPLAY_SETTINGS_BRAND_HIDDEN) === 0,
    countdownTargetDay, countdownName,
    countdownDate: targetDayToCalendarDate(countdownTargetDay),
  }
}

export const deviceInputActionIds: Record<DeviceInputAction, number> = {
  disabled: 0,
  nextPage: 1,
  showSlot: 2,
  showDeviceInfo: 3,
  showCalendar: 4,
  showClock: 5,
  toggleSlideshow: 6,
  stop: 7,
  controlUrl: 8,
  toggleClearPrevious: 9,
  previousPage: 10,
}

export const deviceInputActions: Array<{ id: DeviceInputAction; label: string; description: string }> = [
  { id: 'disabled', label: '关闭', description: '不执行设备操作' },
  { id: 'nextPage', label: '下一页', description: '日历、时钟、设备信息、图槽轮播循环；轮播中再按则退出回日历，不逐张切图' },
  { id: 'previousPage', label: '前一页', description: '按相反顺序切换页面；轮播作为一个页面，不逐张切图' },
  { id: 'showSlot', label: '指定图槽', description: '显示固定的设备图槽' },
  { id: 'showDeviceInfo', label: '设备信息', description: '显示设备原生信息页' },
  { id: 'showCalendar', label: '日历', description: '切换到设备原生日历' },
  { id: 'showClock', label: '时钟', description: '切换到设备原生时钟' },
  { id: 'toggleSlideshow', label: '轮播开关', description: '开始或停止图片轮播' },
  { id: 'stop', label: '停止', description: '取消当前屏幕操作' },
  { id: 'controlUrl', label: '控制 URL', description: '手机读取 NDEF 入口；设备屏幕不刷新' },
  { id: 'toggleClearPrevious', label: '刷白 / 返回', description: '在清屏和上一个非清屏模式之间切换' },
]

export const emptyDeviceSettingsCapabilities: DeviceSettingsCapabilities = {
  protocolVersion: 0,
  screenDriverIds: null,
  bluetoothName: false,
  bluetoothNameMaxBytes: BLUETOOTH_NAME_MAX_BYTES,
  deviceLabel: false,
  deviceLabelMaxBytes: DEVICE_LABEL_MAX_BYTES,
  nfcActionMask: 0,
  nfcNdef: false,
  buttonActionMask: 0,
  buttonGestureMask: 0,
  displayActionMask: 0,
  nativeDeviceInfo: false,
  deviceInfoQr: false,
  guiAssetUpdate: false,
  deviceInfoLayoutMask: 0,
  customButtonGpio: false,
  reservedIo: false,
  statusLedSettings: false,
  runtimePinConfig: false,
  lifecycle: false,
  experimentalTimedSleep: false,
  experimentalFind: false,
}

export function inputActionMask(action: DeviceInputAction) {
  return 1 << deviceInputActionIds[action]
}

export function inputActionSupported(mask: number, action: DeviceInputAction) {
  return (mask & inputActionMask(action)) !== 0
}

export function bluetoothNameBytes(value: string) {
  return new TextEncoder().encode(value.trim()).byteLength
}

export function normalizeBluetoothName(value: string) {
  const normalized = value.trim()
  if (!normalized || BLUETOOTH_NAME_PREFIXES.some((prefix) => normalized.startsWith(prefix))) return normalized
  return `${BLUETOOTH_NAME_PREFIX}${normalized}`
}

export function validateBluetoothName(value: string, maxBytes = BLUETOOTH_NAME_MAX_BYTES) {
  const normalized = value.trim()
  if (!normalized) return '请输入蓝牙广播名'
  const prefix = BLUETOOTH_NAME_PREFIXES.find((candidate) => normalized.startsWith(candidate))
  if (!prefix) return `蓝牙广播名必须以 ${BLUETOOTH_NAME_PREFIX} 开头`
  if (normalized.length === prefix.length) return '请在蓝牙名前缀后填写名称'
  const suffix = normalized.slice(prefix.length)
  if (!/^[A-Za-z0-9_-]+$/.test(suffix)) return '蓝牙广播名只支持英文、数字、下划线和连字符'
  if (bluetoothNameBytes(normalized) > maxBytes) return `蓝牙广播名不能超过 ${maxBytes} 个 UTF-8 字节`
  return ''
}

export function deviceLabelBytes(value: string) {
  return new TextEncoder().encode(value.trim()).byteLength
}

export function validateDeviceLabel(value: string, maxBytes = DEVICE_LABEL_MAX_BYTES) {
  const normalized = value.trim()
  if (/[\u0000-\u001f\u007f]/.test(normalized)) return '设备名不能包含控制字符'
  if (/[^\u0020-\u007e]/.test(normalized)) return '设备名只支持英文、数字、空格和英文标点'
  if (deviceLabelBytes(normalized) > maxBytes) return `设备名不能超过 ${maxBytes} 个 UTF-8 字节`
  return ''
}

export function encodeDeviceLabel(value: string, maxBytes = DEVICE_LABEL_MAX_BYTES) {
  const error = validateDeviceLabel(value, maxBytes)
  if (error) throw new RangeError(error)
  const label = new TextEncoder().encode(value.trim())
  const payload = new Uint8Array(2 + label.byteLength)
  payload[0] = DEVICE_SETTINGS_PROTOCOL_VERSION
  payload[1] = label.byteLength
  payload.set(label, 2)
  return payload
}

export function encodeBluetoothName(value: string, maxBytes = BLUETOOTH_NAME_MAX_BYTES) {
  const error = validateBluetoothName(value, maxBytes)
  if (error) throw new RangeError(error)
  const name = new TextEncoder().encode(value.trim())
  const payload = new Uint8Array(2 + name.byteLength)
  payload[0] = DEVICE_SETTINGS_PROTOCOL_VERSION
  payload[1] = name.byteLength
  payload.set(name, 2)
  return payload
}

const inputSourceIds: Record<'nfc' | ButtonGesture, number> = { nfc: 1, single: 2, double: 3, long: 4 }

export function encodeInputAction(source: 'nfc' | ButtonGesture, settings: NfcTapSettings | ButtonActionSettings) {
  if (source !== 'nfc' && settings.action === 'controlUrl') {
    throw new RangeError('实体按键不支持控制 URL')
  }
  if (source === 'nfc' && settings.action === 'toggleClearPrevious') {
    throw new RangeError('NFC 不支持刷白 / 返回按键动作')
  }
  const controlUrl = source === 'nfc'
    ? (settings as NfcTapSettings).controlUrl.trim()
    : ''
  if (controlUrl || settings.action === 'controlUrl') {
    throw new RangeError('NFC 动作不支持自定义控制 URL')
  }
  // SET_INPUT_CONFIG is exactly four bytes on every current firmware port.
  // The phone-readable device URL belongs to the separate NDEF configuration.
  const payload = new Uint8Array(4)
  payload[0] = DEVICE_SETTINGS_PROTOCOL_VERSION
  payload[1] = inputSourceIds[source]
  payload[2] = deviceInputActionIds[settings.action]
  payload[3] = Math.max(0, Math.min(255, Math.round(settings.slotId)))
  return payload
}

function numericField(message: string, key: string) {
  const value = message.match(new RegExp(`(?:^|\\s)${key}=([^\\s]+)`))?.[1]
  return value ? Number.parseInt(value, 0) : 0
}

function hexListField(message: string, key: string) {
  const value = message.match(new RegExp(`(?:^|\\s)${key}=([^\\s]+)`))?.[1]
  if (!value) return null
  const items = value.split(',')
    .map((item) => Number.parseInt(item, 16))
    .filter((item) => Number.isInteger(item) && item >= 0 && item <= 0xff)
  return items.length ? [...new Set(items)] : null
}

export function inputActionFromId(value: number): DeviceInputAction | null {
  const entry = (Object.entries(deviceInputActionIds) as Array<[DeviceInputAction, number]>)
    .find(([, id]) => id === value)
  return entry?.[0] || null
}

export function decodeReportedButtonSettings(message: string): ButtonBehaviorSettings | null {
  const match = message.match(/^settings:button single=(\d+),(\d+) double=(\d+),(\d+) long=(\d+),(\d+)$/)
  if (!match) return null
  const binding = (actionIndex: number, slotIndex: number): ButtonActionSettings | null => {
    const action = inputActionFromId(Number(match[actionIndex]))
    if (!action || action === 'controlUrl') return null
    return { action, slotId: Number(match[slotIndex]) }
  }
  const single = binding(1, 2)
  const double = binding(3, 4)
  const long = binding(5, 6)
  return single && double && long ? { single, double, long } : null
}

export function decodeReportedNfcSettings(message: string, controlUrl = ''): NfcTapSettings | null {
  const actionName = message.match(/(?:^|\s)action=([^\s]+)/)?.[1]
  const actions: Record<string, NfcTapSettings['action']> = {
    disabled: 'disabled', next: 'nextPage', slot: 'showSlot', deviceInfo: 'showDeviceInfo',
    calendar: 'showCalendar', clock: 'showClock', slideshow: 'toggleSlideshow',
    stop: 'stop', url: 'controlUrl',
  }
  const action = actionName ? actions[actionName] : undefined
  if (!message.startsWith('nfc:status') || !action) return null
  return {
    action,
    slotId: numericField(message, 'slot'),
    controlUrl,
  }
}

export function decodeReportedNfcUrl(message: string): string | null {
  const prefix = 'settings:nfc-url='
  if (!message.startsWith(prefix)) return null
  const controlUrl = message.slice(prefix.length)
  return controlUrl && !validateNfcControlUrl(controlUrl) ? controlUrl : null
}

export function decodeReportedDeviceLabel(message: string): { label: string } | null {
  const prefix = 'settings:device name='
  if (!message.startsWith(prefix)) return null
  try {
    return { label: decodeURIComponent(message.slice(prefix.length)) }
  } catch {
    return null
  }
}

export function decodeDeviceSettingsCapabilities(message: string): DeviceSettingsCapabilities | null {
  if (!message.startsWith('caps:')) return null
  const features = numericField(message, 'features')
  const nfcActionMask = numericField(message, 'nfc')
  const explicitNdef = message.match(/(?:^|\s)ndef=(0|1)(?:\s|$)/)
  return {
    protocolVersion: numericField(message, 'v'),
    screenDriverIds: hexListField(message, 'drivers'),
    bluetoothName: (features & 0x01) !== 0,
    bluetoothNameMaxBytes: numericField(message, 'name') || BLUETOOTH_NAME_MAX_BYTES,
    deviceLabel: (features & 0x80) !== 0,
    deviceLabelMaxBytes: numericField(message, 'device') || DEVICE_LABEL_MAX_BYTES,
    nfcActionMask,
    nfcNdef: explicitNdef ? explicitNdef[1] === '1' : (nfcActionMask & inputActionMask('controlUrl')) !== 0,
    buttonActionMask: numericField(message, 'button'),
    buttonGestureMask: numericField(message, 'gestures') || (numericField(message, 'button') ? 0x01 : 0),
    displayActionMask: 0,
    nativeDeviceInfo: (features & 0x04) !== 0,
    deviceInfoQr: (features & 0x08) !== 0,
    deviceInfoLayoutMask: numericField(message, 'device-info'),
    customButtonGpio: (features & 0x10) !== 0,
    reservedIo: (features & 0x20) !== 0,
    statusLedSettings: (features & 0x40) !== 0,
    runtimePinConfig: false,
    lifecycle: false,
    experimentalTimedSleep: false,
    experimentalFind: false,
    guiAssetUpdate: false,
  }
}

export function defaultAuxIoSettings(): AuxIoSettings {
  return { buttonGpio: 255, buttonActiveLow: true, buttonPull: 'up', reserved: [0, 255, 255, 208, 7] }
}

export function validateAuxIoSettings(settings: AuxIoSettings) {
  if (!Number.isInteger(settings.buttonGpio) || settings.buttonGpio < 0 || settings.buttonGpio > 255) return 'GPIO 必须是 0..255，255 表示关闭'
  if (!Array.isArray(settings.reserved) || settings.reserved.length !== 5 || settings.reserved.some(value => !Number.isInteger(value) || value < 0 || value > 255)) return '扩展配置的预留数据无效'
  return ''
}

export function decodeButtonConfig(payload: Uint8Array) {
  if ((payload.length !== 10 && payload.length !== 14) ||
      payload[0] !== DEVICE_SETTINGS_PROTOCOL_VERSION) return null
  const binding = (offset: number): ButtonActionSettings | null => {
    const action = inputActionFromId(payload[offset])
    return action && action !== 'controlUrl' ? { action, slotId: payload[offset + 1] } : null
  }
  const single = binding(4)
  const double = binding(6)
  const long = binding(8)
  if (!single || !double || !long) return null
  const result = {
    gestureMask: payload[1],
    actionMask: new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint16(2, true),
    settings: { single, double, long } satisfies ButtonBehaviorSettings,
    nfcActionMask: 0,
    nfcSettings: null as NfcTapSettings | null,
  }
  if (payload.length === 14) {
    const nfcAction = inputActionFromId(payload[12])
    if (!nfcAction || nfcAction === 'toggleClearPrevious') return null
    result.nfcActionMask = new DataView(
      payload.buffer, payload.byteOffset, payload.byteLength,
    ).getUint16(10, true)
    result.nfcSettings = { action: nfcAction, slotId: payload[13], controlUrl: '' }
  }
  return result
}

// Fixed product limit for a bounded Flash record and NDEF payload; firmware
// must enforce the same UTF-8 byte limit when the write protocol is added.
export const NFC_CONTROL_URL_MAX_BYTES = 192

export function nfcControlUrlBytes(value: string) {
  return new TextEncoder().encode(value.trim()).byteLength
}

export function validateNfcControlUrl(value: string) {
  const normalized = value.trim()
  if (!normalized) return '请输入控制 URL'
  if (nfcControlUrlBytes(normalized) > NFC_CONTROL_URL_MAX_BYTES) {
    return `控制 URL 不能超过 ${NFC_CONTROL_URL_MAX_BYTES} 个 UTF-8 字节`
  }
  try {
    const url = new URL(normalized)
    const host = url.hostname.toLowerCase()
    const privateIpv4 = /^10\./.test(host)
      || /^192\.168\./.test(host)
      || /^169\.254\./.test(host)
      || (() => {
        const match = host.match(/^172\.(\d{1,3})\./)
        return match ? Number(match[1]) >= 16 && Number(match[1]) <= 31 : false
      })()
    const localHttp = url.protocol === 'http:' && (
      host.endsWith('.local')
      || privateIpv4
      || /^\[(?:fc|fd|fe8|fe9|fea|feb)/.test(host)
    )
    if (url.protocol !== 'https:' && !localHttp) return '只允许 HTTPS 或本地网络 HTTP 地址'
    if (url.username || url.password) return '控制 URL 不能包含用户名或密码'
  } catch {
    return '控制 URL 格式无效'
  }
  return ''
}

export function defaultLedSettings(): LedSettings {
  return { policy: 'all' }
}

const ledPolicyIds: Record<LedPolicy, number> = { off: 0, all: 1, critical: 3 }
const LED_SETTINGS_PROTOCOL_VERSION = 1

export function encodeLedSettings(settings: LedSettings) {
  const policy = ledPolicyIds[settings.policy]
  if (policy === undefined) throw new RangeError('状态灯策略无效')
  return new Uint8Array([LED_SETTINGS_PROTOCOL_VERSION, policy])
}

export function decodeReportedLedSettings(message: string): LedSettings | null {
  const match = message.match(/^settings:led policy=(\d+)$/i)
  if (!match) return null
  const policies: Record<number, LedPolicy> = { 0: 'off', 1: 'all', 3: 'critical' }
  const policy = policies[Number(match[1])]
  return policy ? { policy } : null
}

export function decodeLedSettingsPayload(payload: Uint8Array): LedSettings | null {
  if (payload.length !== 2 || payload[0] !== LED_SETTINGS_PROTOCOL_VERSION) return null
  const policies: Record<number, LedPolicy> = { 0: 'off', 1: 'all', 3: 'critical' }
  const policy = policies[payload[1]]
  return policy ? { policy } : null
}

export function defaultNfcTapSettings(): NfcTapSettings {
  return { action: 'disabled', slotId: 0, controlUrl: '' }
}

export function defaultButtonBehaviorSettings(): ButtonBehaviorSettings {
  return {
    single: { action: 'nextPage', slotId: 0 },
    double: { action: 'previousPage', slotId: 0 },
    long: { action: 'toggleClearPrevious', slotId: 0 },
  }
}

// Firmware commands are deliberately behind this boundary. The UI may persist a
// draft now; a later protocol revision can implement these methods without mixing
// device identity or LED policy into screen and board-pin configuration.
export interface DeviceSettingsWriter {
  readonly supportsBluetoothName: boolean
  readonly supportsLedSettings: boolean
  writeBluetoothName(name: string): Promise<void>
  writeLedSettings(settings: LedSettings): Promise<void>
}

export const pendingDeviceSettingsWriter: DeviceSettingsWriter = {
  supportsBluetoothName: false,
  supportsLedSettings: false,
  async writeBluetoothName() { throw new Error('当前固件尚未发布蓝牙名称写入协议') },
  async writeLedSettings() { throw new Error('当前固件尚未发布灯控写入协议') },
}
